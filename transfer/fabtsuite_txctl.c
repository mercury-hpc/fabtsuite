/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <rdma/fi_domain.h>
#include <rdma/fi_tagged.h>

#include "fabtsuite_types.h"

#include "fabtsuite_buffer.h"
#include "fabtsuite_error.h"
#include "fabtsuite_fifo.h"
#include "fabtsuite_txctl.h"
#include "fabtsuite_seqsource.h"
#include "fabtsuite_util.h"

bool
txctl_idle(txctl_t *ctl)
{
    return fifo_empty(ctl->posted);
}

bool
txctl_ready(txctl_t *ctl)
{
    return !fifo_full(ctl->posted);
}

bool
txctl_put(txctl_t *ctl, bufhdr_t *h)
{
    h->tag = seqsource_get(&ctl->tags);

    if (fifo_put(ctl->ready, h))
        return true;

    seqsource_unget(&ctl->tags, h->tag);
    return false;
}

void
txctl_init(txctl_t *ctl, size_t len, size_t nbufs,
           bufhdr_t *(*create_and_register)(struct fid_ep *), struct fid_ep *ep)
{
    size_t i;

    assert(size_is_power_of_2(len));

    seqsource_init(&ctl->tags);
    ctl->ignore = ~(uint64_t) (len - 1);

    if ((ctl->ready = fifo_create(len)) == NULL) {
        errx(EXIT_FAILURE, "%s: could not create ready messages FIFO",
             __func__);
    }

    if ((ctl->posted = fifo_create(len)) == NULL) {
        errx(EXIT_FAILURE, "%s: could not create posted messages FIFO",
             __func__);
    }

    if ((ctl->pool = buflist_create(nbufs)) == NULL) {
        errx(EXIT_FAILURE, "%s: could not create tx buffer pool", __func__);
    }

    for (i = 0; i < nbufs; i++) {
        bufhdr_t *h = create_and_register(ep);

        if (!buflist_put(ctl->pool, h))
            errx(EXIT_FAILURE, "%s: vector buffer pool full", __func__);
    }
}

void
txctl_cancel(struct fid_ep *ep, txctl_t *ctl)
{
    fifo_cancel(ep, ctl->posted);
}

/* Process completed progress-message transmission.  Return 0 if no
 * completions occurred, 1 if any completion occurred, -1 on an
 * irrecoverable error.
 */
int
txctl_complete(txctl_t *tc, const completion_t *cmpl)
{
    bufhdr_t *h;

    if (cmpl->xfc->cancelled) {
        cmpl->xfc->cancelled = 0;
    } else if ((cmpl->flags & desired_tagged_tx_flags) !=
               desired_tagged_tx_flags) {
        char difference[128];

        errx(EXIT_FAILURE,
             "%s: expected flags %" PRIu64
             " differs from received flags %" PRIu64 " at %s",
             __func__, desired_tagged_tx_flags,
             cmpl->flags & desired_tagged_tx_flags,
             completion_flags_to_string(
                 desired_tagged_tx_flags ^
                     (cmpl->flags & desired_tagged_tx_flags),
                 difference, sizeof(difference)));
    }

    if ((h = fifo_get(tc->posted)) == NULL) {
        fprintf(stderr, "%s: message Tx completed, but no Tx was posted",
                __func__);
        return -1;
    }

    if (cmpl->xfc != &h->xfc) {
        errx(EXIT_FAILURE, "%s: expected context %p received %p", __func__,
             (void *) &h->xfc.ctx, (void *) cmpl->xfc);
    }

    if (!buflist_put(tc->pool, h))
        errx(EXIT_FAILURE, "%s: buffer pool full", __func__);

    return 1;
}

void
txctl_transmit(cxn_t *c, txctl_t *tc)
{
    bufhdr_t *h;
    size_t nsent = 0;

    /* Periodically induce an out-of-order message transmission by
     * "rotating" the tx-ready FIFO: if more than one buffer is on the
     * FIFO, then move the buffer at the front of the FIFO to the back.
     */
    if (tc->rotate_ready_countdown == 0) {
        if (fifo_nfull(tc->ready) > 1 && fifo_nempty(tc->posted) > 1) {
            (void) fifo_put(tc->ready, fifo_get(tc->ready));
            tc->rotate_ready_countdown = rotate_ready_interval;
        }
    } else {
        tc->rotate_ready_countdown--;
    }

    while ((h = fifo_peek(tc->ready)) != NULL && txctl_ready(tc)) {
        const int rc = fi_tsendmsg(
            c->ep,
            &(struct fi_msg_tagged){
                .msg_iov =
                    &(struct iovec){.iov_base = &((bytebuf_t *) h)->payload[0],
                                    .iov_len = h->nused},
                .desc = h->desc,
                .iov_count = 1,
                .addr = c->peer_addr,
                .tag = h->tag & ~tc->ignore,
                .ignore = 0,
                .context = &h->xfc.ctx,
                .data = 0},
            FI_COMPLETION);

        if (rc == 0) {
            (void) fifo_get(tc->ready);
            (void) fifo_put(tc->posted, h);
            nsent++;
        } else if (rc == -FI_EAGAIN) {
            break;
        } else if (rc < 0) {
            bailout_for_ofi_ret(rc, "fi_tsendmsg");
        }
    }
}
