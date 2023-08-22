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
#include <stdlib.h>

#include <rdma/fi_domain.h>
#include <rdma/fi_tagged.h>

#include "FStypes.h"

#include "FSbuffer.h"
#include "FSerror.h"
#include "FSfifo.h"
#include "FSrxctl.h"
#include "FSseqsource.h"
#include "FSutil.h"

bool
rxctl_idle(rxctl_t *ctl)
{
    return fifo_empty(ctl->posted);
}

bool
rxctl_ready(rxctl_t *ctl)
{
    return !fifo_full(ctl->posted);
}

bufhdr_t *
rxctl_complete(rxctl_t *rc, const completion_t *cmpl)
{
    bufhdr_t *h, *head;

    head = fifo_peek(rc->posted);

    if (cmpl == NULL) {
        goto out;
    } else if (head == NULL) {
        errx(EXIT_FAILURE, "%s: received a completion, but no Rx was posted",
             __func__);
    }

    cmpl->xfc->owner = xfo_program;

    if (cmpl->xfc->cancelled) {
        ; /* do nothing; leave cancelled flag set, it's needed by
           * higher-level processing---e.g., in xmtr_vector_rx_process
           */
    } else if ((cmpl->flags & desired_tagged_rx_flags) !=
               desired_tagged_rx_flags) {
        char difference[128];

        errx(EXIT_FAILURE,
             "%s: expected flags %" PRIu64
             " differs from received flags %" PRIu64 " at %s",
             __func__, desired_tagged_rx_flags,
             cmpl->flags & desired_tagged_rx_flags,
             completion_flags_to_string(
                 desired_tagged_rx_flags ^
                     (cmpl->flags & desired_tagged_rx_flags),
                 difference, sizeof(difference)));
    }

    h = (bufhdr_t *) ((char *) cmpl->xfc - offsetof(bufhdr_t, xfc));
    h->nused = cmpl->len;

out:
    if (head == NULL || head->xfc.owner != xfo_program)
        return NULL;

    return fifo_get(rc->posted);
}

void
rxctl_post(cxn_t *c, rxctl_t *ctl, bufhdr_t *h)
{
    int rc;

    h->xfc.cancelled = 0;
    h->xfc.owner = xfo_nic;

    const uint64_t tag = seqsource_get(&ctl->tags);

    rc = fi_trecvmsg(
        c->ep,
        &(struct fi_msg_tagged){
            .msg_iov =
                &(struct iovec){.iov_base = &((bytebuf_t *) h)->payload[0],
                                .iov_len = h->nallocated},
            .desc = &h->desc,
            .iov_count = 1,
            .addr = c->peer_addr,
            .tag = tag & ~ctl->ignore,
            .ignore = 0,
            .context = &h->xfc.ctx,
            .data = 0},
        FI_COMPLETION);

    if (rc < 0) {
        seqsource_unget(&ctl->tags, tag);
        bailout_for_ofi_ret(rc, "fi_trecvmsg");
    }

    (void) fifo_put(ctl->posted, h);
}

void
rxctl_init(rxctl_t *ctl, size_t len)
{
    assert(size_is_power_of_2(len));

    seqsource_init(&ctl->tags);
    ctl->ignore = ~(uint64_t) (len - 1);

    if ((ctl->posted = fifo_create(len)) == NULL) {
        errx(EXIT_FAILURE, "%s: could not create posted messages FIFO",
             __func__);
    }
    if ((ctl->rcvd = fifo_create(len)) == NULL) {
        errx(EXIT_FAILURE, "%s: could not create received messages FIFO",
             __func__);
    }
}

void
rxctl_cancel(struct fid_ep *ep, rxctl_t *ctl)
{
    fifo_cancel(ep, ctl->posted);
}
