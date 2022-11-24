/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <err.h>
#include <inttypes.h> /* PRIu32 */
#include <libgen.h>   /* basename(3) */
#include <limits.h>   /* INT_MAX */
#include <sched.h>    /* CPU_SET(3) */
#include <signal.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* strcmp(3), strdup(3) */
#include <unistd.h> /* getopt(3), sysconf(3) */

#include <sys/epoll.h>

#include <rdma/fabric.h>
#include <rdma/fi_cm.h> /* fi_listen, fi_getname */
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>    /* struct fi_msg_rma */
#include <rdma/fi_tagged.h> /* struct fi_msg_tagged */

#include "fabtsuite_buffer.h"
#include "fabtsuite_error.h"
#include "fabtsuite_fifo.h"
#include "fabtsuite_register.h"
#include "fabtsuite_seqsource.h"
#include "fabtsuite_types.h"
#include "fabtsuite_util.h"

static const unsigned split_progress_interval = 2047;
static const unsigned split_vector_interval = 15;
static const unsigned rotate_ready_interval = 3;

state_t global_state = {.domain = NULL,
                        .fabric = NULL,
                        .info = NULL,
                        .personality = NULL,
                        .local_sessions = 1,
                        .total_sessions = 1,
                        .processors = {.first = 0, .last = INT_MAX},
                        .cancelled = 0,
                        .peer_addr = NULL};

static pthread_mutex_t workers_mtx = PTHREAD_MUTEX_INITIALIZER;
static worker_t workers[WORKERS_MAX];
static _Atomic size_t nworkers_running;
static size_t nworkers_allocated;
static pthread_cond_t nworkers_cond = PTHREAD_COND_INITIALIZER;

static bool workers_assignment_suspended = false;

static struct {
    int signum;
    struct sigaction saved_action;
} siglist[] = {{.signum = SIGHUP},
               {.signum = SIGINT},
               {.signum = SIGQUIT},
               {.signum = SIGTERM}};

static struct sigaction saved_wakeup1_action;
static struct sigaction saved_wakeup2_action;

static const struct {
    uint64_t rx;
    uint64_t tx;
} payload_access = {.rx = FI_RECV | FI_REMOTE_WRITE, .tx = FI_SEND};

static int
get(void);

static const uint64_t desired_rx_flags = FI_RECV | FI_MSG;
static const uint64_t desired_tagged_rx_flags = FI_RECV | FI_TAGGED;
static const uint64_t desired_tagged_tx_flags = FI_SEND | FI_TAGGED;

static char txbuf[] = "If this message was received in error then please "
                      "print it out and shred it.";

static bool
session_init(session_t *s, cxn_t *c, terminal_t *t)
{
    memset(s, 0, sizeof(*s));

    s->waitable = false;
    s->cxn = c;
    s->terminal = t;

    if ((s->ready_for_cxn = fifo_create(64)) == NULL)
        return NULL;

    if ((s->ready_for_terminal = fifo_create(64)) == NULL) {
        fifo_destroy(s->ready_for_cxn);
        return NULL;
    }

    return s;
}

static bool
paybuflist_replenish(seqsource_t *keys, uint64_t access, buflist_t *bl)
{
    size_t i, paylen;
    int rc;

    if (bl->nfull >= bl->nallocated / 2)
        return true;

    size_t ntofill = bl->nallocated / 2 - bl->nfull;

    for (paylen = 0, i = bl->nfull; i < ntofill; i++) {
        bytebuf_t *buf;

        // paylen cycle: -> 23 -> 29 -> 31 -> 37 -> 23
        switch (paylen) {
            case 0:
            default:
                paylen = 23;
                break;
            case 23:
                paylen = 29;
                break;
            case 29:
                paylen = 31;
                break;
            case 31:
                paylen = 37;
                break;
            case 37:
                paylen = 23;
                break;
        }
        buf = bytebuf_alloc(paylen);
        if (buf == NULL)
            err(EXIT_FAILURE, "%s.%d: malloc", __func__, __LINE__);

        buf->hdr.xfc.type = xft_rdma_write;

        if (!global_state.reregister &&
            (rc = buf_mr_reg(global_state.domain, NULL, access,
                             seqsource_get(keys), &buf->hdr)) != 0) {
            warn_about_ofi_ret(rc, "buf_mr_reg");
            free(buf);
            break;
        }

        bl->buf[i] = &buf->hdr;
    }
    bl->nfull = i;

    return bl->nfull > 0;
}

static bytebuf_t *
worker_payload_txbuf_get(worker_t *w, struct fid_ep *ep)
{
    bytebuf_t *b;
    int rc;

    while ((b = (bytebuf_t *) buflist_get(w->paybufs.tx)) == NULL &&
           paybuflist_replenish(&w->keys, payload_access.tx, w->paybufs.tx))
        ; // do nothing

    if (!global_state.reregister && (rc = buf_mr_bind(&b->hdr, ep)) != 0) {
        buflist_put(w->paybufs.tx, &b->hdr);
        return NULL;
    }

    return b;
}

static bytebuf_t *
worker_payload_rxbuf_get(worker_t *w, struct fid_ep *ep)
{
    bytebuf_t *b;
    int rc;

    while ((b = (bytebuf_t *) buflist_get(w->paybufs.rx)) == NULL &&
           paybuflist_replenish(&w->keys, payload_access.rx, w->paybufs.rx))
        ; // do nothing

    if (!global_state.reregister && (rc = buf_mr_bind(&b->hdr, ep)) != 0) {
        buflist_put(w->paybufs.tx, &b->hdr);
        return NULL;
    }

    return b;
}

static size_t
fibonacci_iov_setup(void *_buf, size_t len, struct iovec *iov, size_t niovs)
{
    char *buf = _buf;
    size_t i;
    struct fibo {
        size_t prev, curr;
    } state = {.prev = 0, .curr = 1}; // Fibonacci sequence state

    if (niovs < 1 && len > 0)
        return -1;

    if (niovs > SSIZE_MAX)
        niovs = SSIZE_MAX;

    for (i = 0; len > 0 && i < niovs - 1; i++) {
        iov[i].iov_len = (state.curr < len) ? state.curr : len;
        iov[i].iov_base = buf;
        len -= iov[i].iov_len;
        buf += iov[i].iov_len;
        state =
            (struct fibo){.prev = state.curr, .curr = state.prev + state.curr};
    }
    if (len > 0) {
        iov[i].iov_len = len;
        iov[i].iov_base = buf;
        i++;
    }
    return i;
}

static inline bool
rxctl_idle(rxctl_t *ctl)
{
    return fifo_empty(ctl->posted);
}

static inline bool
rxctl_ready(rxctl_t *ctl)
{
    return !fifo_full(ctl->posted);
}

static bufhdr_t *
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

static void
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

static void
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

static inline bool
txctl_idle(txctl_t *ctl)
{
    return fifo_empty(ctl->posted);
}

static inline bool
txctl_ready(txctl_t *ctl)
{
    return !fifo_full(ctl->posted);
}

static bool
txctl_put(txctl_t *ctl, bufhdr_t *h)
{
    h->tag = seqsource_get(&ctl->tags);

    if (fifo_put(ctl->ready, h))
        return true;

    seqsource_unget(&ctl->tags, h->tag);
    return false;
}

static void
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

static void
rxctl_cancel(struct fid_ep *ep, rxctl_t *ctl)
{
    fifo_cancel(ep, ctl->posted);
}

static void
txctl_cancel(struct fid_ep *ep, txctl_t *ctl)
{
    fifo_cancel(ep, ctl->posted);
}

/* Process completed progress-message transmission.  Return 0 if no
 * completions occurred, 1 if any completion occurred, -1 on an
 * irrecoverable error.
 */
static int
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

static void
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

static loop_control_t
rcvr_start(worker_t *w, rcvr_t *r, fifo_t *ready_for_cxn)
{
    size_t nleftover, nloaded;

    r->cxn.started = true;

    while (rxctl_ready(&r->progress)) {
        progbuf_t *pb = progbuf_alloc();

        rxctl_post(&r->cxn, &r->progress, &pb->hdr);
    }

    for (nleftover = sizeof(txbuf), nloaded = 0; nleftover > 0;) {
        bytebuf_t *b = worker_payload_rxbuf_get(w, r->cxn.ep);

        if (b == NULL) {
            fprintf(stderr, "%s: could not get a buffer", __func__);
            return loop_error;
        }

        b->hdr.nused = minsize(nleftover, b->hdr.nallocated);
        nleftover -= b->hdr.nused;
        nloaded += b->hdr.nused;
        if (!fifo_put(ready_for_cxn, &b->hdr)) {
            fprintf(stderr, "%s: could not enqueue tx buffer", __func__);
            return loop_error;
        }
    }

    return loop_continue;
}

/* Return `loop_continue` if the source is producing more bytes, `loop_end` if
 * the source will produce no more bytes.
 */
static loop_control_t
source_trade(terminal_t *t, fifo_t *ready, fifo_t *completed)
{
    source_t *s = (source_t *) t;
    bufhdr_t *h;

    if (fifo_eoput(completed))
        return loop_end;

    while ((h = fifo_peek(ready)) != NULL && !fifo_full(completed)) {
        bytebuf_t *b = (bytebuf_t *) h;
        size_t len, ofs;

        if (s->idx == s->entirelen) {
            fifo_put_close(completed);
            break;
        }

        h->nused = minsize(s->entirelen - s->idx, h->nallocated);
        for (ofs = 0; ofs < h->nused; ofs += len) {
            size_t txbuf_ofs = (s->idx + ofs) % s->txbuflen;
            len = minsize(h->nused - ofs, s->txbuflen - txbuf_ofs);
            memcpy(&b->payload[ofs], &txbuf[txbuf_ofs], len);
        }

        (void) fifo_get(ready);
        (void) fifo_alt_put(completed, h);

        s->idx += h->nused;
    }

    if (s->idx != s->entirelen)
        return loop_continue;

    return loop_end;
}

/* Return `loop_continue` if the sink is accepting more bytes, `loop_error` if
 * unexpected bytes are on `ready`, `loop_end` if the sink expects no more
 * bytes.
 */
static loop_control_t
sink_trade(terminal_t *t, fifo_t *ready, fifo_t *completed)
{
    sink_t *s = (sink_t *) t;
    bufhdr_t *h;

    if (fifo_eoget(ready)) {
        if (!fifo_alt_empty(ready))
            goto fail;
        return loop_end;
    }

    while ((h = fifo_peek(ready)) != NULL && !fifo_full(completed)) {
        bytebuf_t *b = (bytebuf_t *) h;
        size_t len, ofs;

        if (h->nused + s->idx > s->entirelen)
            goto fail;

        for (ofs = 0; ofs < h->nused; ofs += len) {
            size_t txbuf_ofs = (s->idx + ofs) % s->txbuflen;
            len = minsize(h->nused - ofs, s->txbuflen - txbuf_ofs);
            if (memcmp(&b->payload[ofs], &txbuf[txbuf_ofs], len) != 0)
                goto fail;
        }

        (void) fifo_get(ready);
        (void) fifo_put(completed, h);
        s->idx += h->nused;
    }
    if (s->idx != s->entirelen)
        return loop_continue;

    fifo_get_close(ready);
    return loop_end;
fail:
    fprintf(stderr, "unexpected received payload");
    return loop_error;
}

/* Process completion vector-message reception.  Return 0 if no
 * completions occurred, 1 if any completion occurred, -1 on an
 * irrecoverable error.
 */
static int
rcvr_progress_rx_process(rcvr_t *r, bufhdr_t *h)
{
    progbuf_t *pb = (progbuf_t *) h;

    if (h->xfc.cancelled) {
        buf_free(h);
        return 0;
    }

    if (!progbuf_is_wellformed(pb)) {
        rxctl_post(&r->cxn, &r->progress, h);
        return 0;
    }

    r->nfull += pb->msg.nfilled;

    if (pb->msg.nleftover == 0) {
        r->cxn.eof.remote = true;
    }

    rxctl_post(&r->cxn, &r->progress, h);

    return 1;
}

/* Process completions.  Return 0 if no completions occurred, 1 if
 * any completion occurred, -1 on an irrecoverable error.
 */
static int
rcvr_cq_process(rcvr_t *r)
{
    struct fi_cq_msg_entry fcmpl;
    completion_t cmpl, *cmplp;
    bufhdr_t *h;
    size_t nprocessed;
    ssize_t ncompleted;

    if ((ncompleted = fi_cq_read(r->cxn.cq, &fcmpl, 1)) == -FI_EAGAIN)
        return 0;

    if (ncompleted == -FI_EAVAIL) {
        struct fi_cq_err_entry e;
        char errbuf[256];
        char flagsbuf[2][256];
        ssize_t nfailed = fi_cq_readerr(r->cxn.cq, &e, 0);

        cmpl = (completion_t){.xfc = e.op_context, .len = 0, .flags = 0};

        if (e.err != FI_ECANCELED || !cmpl.xfc->cancelled) {
            fprintf(stderr, "%s: read %zd errors, %s", __func__, nfailed,
                    fi_strerror(e.err));
            fprintf(stderr, "%s: context %p type %s", __func__,
                    (void *) cmpl.xfc, xfc_type_to_string(cmpl.xfc->type));
            fprintf(stderr, "%s: completion flags %" PRIx64 " symbolic %s",
                    __func__, e.flags,
                    completion_flags_to_string(e.flags, flagsbuf[0],
                                               sizeof(flagsbuf[0])));
            fprintf(stderr, "%s: provider error %s", __func__,
                    fi_cq_strerror(r->cxn.cq, e.prov_errno, e.err_data, errbuf,
                                   sizeof(errbuf)));
            return -1;
        }
    } else if (ncompleted < 0) {
        bailout_for_ofi_ret(ncompleted, "fi_cq_read");
        /* exit() is called in the above function, but the compiler doesn't
         * introspect at that level, producing a warning about cmpl.xfc
         * not being initialized. Adding an additional exit call suppresses
         * this while we refactor the problem out.
         */
        exit(EXIT_FAILURE);
    } else if (ncompleted != 1) {
        errx(EXIT_FAILURE, "%s: expected 1 completion, read %zd", __func__,
             ncompleted);
    } else {
        cmpl = (completion_t){
            .xfc = fcmpl.op_context, .len = fcmpl.len, .flags = fcmpl.flags};
        // fi_cancel races with completion, so it's not safe to
        // assert that the cancelled flag is false:
        // assert(!cmpl.xfc->cancelled);
    }

    switch (cmpl.xfc->type) {
        case xft_progress:
            for (nprocessed = 0, cmplp = &cmpl;
                 (h = rxctl_complete(&r->progress, cmplp)) != NULL;
                 cmplp = NULL) {
                switch (rcvr_progress_rx_process(r, h)) {
                    case 1:
                        nprocessed++;
                        break;
                    case 0:
                        break;
                    default:
                        return -1;
                }
            }
            return (nprocessed > 0) ? 1 : 0;
        case xft_vector:
            return txctl_complete(&r->vec, &cmpl);
        case xft_ack:
            return 1;
        default:
            fprintf(stderr, "%s: unexpected xfer context type", __func__);
            return -1;
    }
}

static void
rcvr_vector_update(fifo_t *ready_for_cxn, rcvr_t *r)
{
    bufhdr_t *h;
    vecbuf_t *vb;
    size_t i;
    int rc;

    /* Transmit vector. */

    if (r->cxn.eof.remote && !r->cxn.eof.local && !fifo_full(r->vec.ready) &&
        (vb = (vecbuf_t *) buflist_get(r->vec.pool)) != NULL) {
        memset(vb->msg.iov, 0, sizeof(vb->msg.iov));
        vb->msg.niovs = 0;
        vb->hdr.nused = (char *) &vb->msg.iov[0] - (char *) &vb->msg;
        (void) txctl_put(&r->vec, &vb->hdr);
        r->cxn.eof.local = true;
        return;
    } else if (r->cxn.eof.remote) {
        return; // send no more non-empty vectors after remote sends EOF
    }

    while (!fifo_full(r->vec.ready) && !fifo_empty(ready_for_cxn) &&
           (vb = (vecbuf_t *) buflist_get(r->vec.pool)) != NULL) {
        size_t maxniovs;

        if (r->split_vector_countdown == 0) {
            if (fifo_nfull(ready_for_cxn) > 1) {
                maxniovs = minsize(fifo_nfull(ready_for_cxn),
                                   arraycount(vb->msg.iov)) /
                           2;
                r->split_vector_countdown = split_vector_interval;
            } else {
                maxniovs = arraycount(vb->msg.iov);
            }
        } else {
            r->split_vector_countdown--;
            maxniovs = arraycount(vb->msg.iov);
        }

        for (i = 0; i < maxniovs && (h = fifo_get(ready_for_cxn)) != NULL;
             i++) {

            h->nused = 0;

            /* TBD rebind */
            if (global_state.reregister &&
                (rc = buf_mr_reg(global_state.domain, r->cxn.ep,
                                 payload_access.rx, seqsource_get(&r->cxn.keys),
                                 h)) < 0)
                bailout_for_ofi_ret(rc, "payload memory registration failed");

            (void) fifo_put(r->tgtposted, h);

            vb->msg.iov[i].addr = 0;
            vb->msg.iov[i].len = h->nallocated;
            vb->msg.iov[i].key = fi_mr_key(h->mr);
        }
        vb->msg.niovs = i;
        vb->hdr.nused = (char *) &vb->msg.iov[i] - (char *) &vb->msg;

        (void) txctl_put(&r->vec, &vb->hdr);
    }
}

static void
rcvr_targets_read(fifo_t *ready_for_terminal, rcvr_t *r)
{
    bufhdr_t *h;
    int rc;

    while (r->nfull > 0 && (h = fifo_peek(r->tgtposted)) != NULL &&
           !fifo_alt_full(ready_for_terminal)) {
        if (h->nused + r->nfull < h->nallocated) {
            h->nused += r->nfull;
            r->nfull = 0;
        } else {
            r->nfull -= (h->nallocated - h->nused);
            h->nused = h->nallocated;
            (void) fifo_get(r->tgtposted);

            if (global_state.reregister && (rc = buf_mr_dereg(h)) != 0)
                warn_about_ofi_ret(rc, "fi_close");

            (void) fifo_alt_put(ready_for_terminal, h);
        }
    }

    /* The remote does not necessarily indicate EOF on an RDMA target
     * buffer boundary.  On EOF, take any partially-full (looking on the
     * bright side) RDMA buffer off of the queue of RDMA target buffers.
     */
    if (r->cxn.eof.remote && (h = fifo_peek(r->tgtposted)) != NULL &&
        h->nused != 0) {
        (void) fifo_get(r->tgtposted);

        if (global_state.reregister && (rc = buf_mr_dereg(h)) != 0)
            warn_about_ofi_ret(rc, "fi_close");

        (void) fifo_alt_put(ready_for_terminal, h);
    }
}

static loop_control_t
rcvr_ack_send(rcvr_t *r)
{
    xfer_context_t *xfc = &r->ack.xfc;

    xfc->type = xft_ack;
    xfc->owner = xfo_nic;
    xfc->place = xfp_first | xfp_last;
    xfc->nchildren = 0;
    xfc->cancelled = 0;

    const int rc = fi_sendmsg(r->cxn.ep,
                              &(struct fi_msg){.msg_iov = r->ack.iov,
                                               .desc = r->ack.desc,
                                               .iov_count = r->ack.niovs,
                                               .addr = r->cxn.peer_addr,
                                               .context = xfc,
                                               .data = 0},
                              FI_COMPLETION);

    if (rc == -FI_EAGAIN) {
        return loop_continue;
    }

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_sendmsg");

    r->cxn.sent_first = true;
    return loop_end;
}

static void
rcvr_cancel(cxn_t *cxn)
{
    rcvr_t *r = (rcvr_t *) cxn;

    rxctl_cancel(r->cxn.ep, &r->progress);
    txctl_cancel(r->cxn.ep, &r->vec);
}

static bool
rcvr_cancellation_complete(cxn_t *cxn)
{
    rcvr_t *r = (rcvr_t *) cxn;

    return rxctl_idle(&r->progress) && txctl_idle(&r->vec);
}

static loop_control_t
rcvr_loop(worker_t *w, session_t *s)
{
    rcvr_t *r = (rcvr_t *) s->cxn;

    switch (r->cxn.sent_first ? loop_end : rcvr_ack_send(r)) {
        case loop_end:
            break;
        case loop_continue:
            if (rcvr_cq_process(r) == -1)
                return loop_error;
            return loop_continue;
        default:
            return loop_error;
    }

    if (!r->cxn.started)
        return rcvr_start(w, r, s->ready_for_cxn);

    if (rcvr_cq_process(r) == -1)
        return loop_error;

    rcvr_vector_update(s->ready_for_cxn, r);

    txctl_transmit(&r->cxn, &r->vec);

    rcvr_targets_read(s->ready_for_terminal, r);

    if (fifo_eoget(s->ready_for_terminal) && r->cxn.eof.remote &&
        r->cxn.eof.local && txctl_idle(&r->vec))
        return loop_end;

    return loop_continue;
}

static loop_control_t
xmtr_initial_send(xmtr_t *x)
{
    xfer_context_t *xfc = &x->initial.xfc;

    xfc->type = xft_initial;
    xfc->owner = xfo_nic;
    xfc->place = xfp_first | xfp_last;
    xfc->nchildren = 0;
    xfc->cancelled = 0;

    const int rc = fi_sendmsg(
        x->cxn.ep,
        &(struct fi_msg){.msg_iov =
                             &(struct iovec){.iov_base = &x->initial.msg,
                                             .iov_len = sizeof(x->initial.msg)},
                         .desc = &x->initial.desc,
                         .iov_count = 1,
                         .addr = x->cxn.peer_addr,
                         .context = xfc,
                         .data = 0},
        FI_COMPLETION);

    if (rc == -FI_EAGAIN) {
        return loop_continue;
    }

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_sendmsg");

    x->cxn.sent_first = true;
    return loop_continue;
}

static loop_control_t
xmtr_ack_rx_process(xmtr_t *x, completion_t *cmpl)
{
    int rc;

    if ((cmpl->flags & desired_rx_flags) != desired_rx_flags) {
        errx(EXIT_FAILURE,
             "%s: expected flags %" PRIu64 ", received flags %" PRIu64,
             __func__, desired_rx_flags, cmpl->flags & desired_rx_flags);
    }

    if (cmpl->len != sizeof(x->ack.msg))
        errx(EXIT_FAILURE, "%s: ack is incorrect size", __func__);

    rc =
        fi_av_insert(x->cxn.av, x->ack.msg.addr, 1, &x->cxn.peer_addr, 0, NULL);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_av_insert dest_addr %p", x->ack.msg.addr);
    else if (rc != 1) {
        errx(EXIT_FAILURE, "%s: inserted %d addresses, expected 1", __func__,
             rc);
    }

    while (rxctl_ready(&x->vec)) {
        vecbuf_t *vb = vecbuf_alloc();

        rc = buf_mr_reg(global_state.domain, x->cxn.ep, FI_RECV,
                        seqsource_get(&x->cxn.keys), &vb->hdr);

        if (rc < 0)
            bailout_for_ofi_ret(rc, "buffer memory registration failed");

        rxctl_post(&x->cxn, &x->vec, &vb->hdr);
    }

    x->rcvd_ack = true;

    return loop_continue;
}

static loop_control_t
xmtr_start(worker_t *w, xmtr_t *x, fifo_t *ready_for_terminal)
{
    x->cxn.started = true;

    while (!fifo_full(ready_for_terminal)) {
        bytebuf_t *b = worker_payload_txbuf_get(w, x->cxn.ep);

        if (b == NULL)
            errx(EXIT_FAILURE, "%s: could not get a buffer", __func__);

        b->hdr.nused = 0;
        if (!fifo_put(ready_for_terminal, &b->hdr))
            errx(EXIT_FAILURE, "%s: could not enqueue tx buffer", __func__);
    }

    return loop_continue;
}

typedef struct write_fully_params {
    struct fid_ep *ep;
    const struct iovec *iov_in;
    void **desc_in;
    struct iovec *iov_out;
    void **desc_out;
    size_t niovs;
    size_t *niovs_out;
    const struct fi_rma_iov *riov_in;
    struct fi_rma_iov *riov_out;
    size_t nriovs;
    size_t *nriovs_out;
    size_t len;
    size_t maxsegs;
    uint64_t flags;
    fi_addr_t addr;
    struct fi_context *context;
} write_fully_params_t;

static ssize_t
write_fully(const write_fully_params_t p)
{
    ssize_t rc;
    size_t i, j, nremaining;
    struct {
        size_t local;
        size_t remote;
    } maxsegs = {.local = minsize(p.maxsegs, p.niovs),
                 .remote = minsize(p.maxsegs, p.nriovs)},
      nsegs = {.local = 0, .remote = 0}, sumlen = {.local = 0, .remote = 0};

    for (i = 0; i < maxsegs.local; i++)
        sumlen.local += p.iov_in[i].iov_len;

    for (i = 0; i < maxsegs.remote; i++)
        sumlen.remote += p.riov_in[i].len;

    const size_t len = minsize(minsize(sumlen.local, sumlen.remote),
                               minsize(p.len, SSIZE_MAX));

    for (i = 0, nremaining = len; 0 < nremaining && i < maxsegs.local; i++) {
        p.iov_out[i] = p.iov_in[i];
        p.desc_out[i] = p.desc_in[i];
        if (p.iov_in[i].iov_len > nremaining) {
            p.iov_out[i].iov_len = nremaining;
            nremaining = 0;
        } else {
            nremaining -= p.iov_in[i].iov_len;
        }
    }

    nsegs.local = i;

    for (i = 0, nremaining = len; 0 < nremaining && i < maxsegs.remote; i++) {
        p.riov_out[i] = p.riov_in[i];
        if (p.riov_in[i].len > nremaining) {
            p.riov_out[i].len = nremaining;
            nremaining = 0;
        } else {
            nremaining -= p.riov_in[i].len;
        }
    }

    nsegs.remote = i;

    struct fi_msg_rma mrma = {.msg_iov = p.iov_out,
                              .desc = p.desc_out,
                              .iov_count = nsegs.local,
                              .addr = p.addr,
                              .rma_iov = p.riov_out,
                              .rma_iov_count = nsegs.remote,
                              .context = p.context,
                              .data = 0};

    rc = fi_writemsg(p.ep, &mrma, p.flags);

    if (rc != 0)
        return rc;

    for (i = j = 0, nremaining = len; i < p.niovs; i++) {
        if (nremaining >= p.iov_in[i].iov_len) {
            nremaining -= p.iov_in[i].iov_len;
            continue;
        }
        p.desc_out[j] = p.desc_in[i];
        p.iov_out[j] = p.iov_in[i];
        if (nremaining > 0) {
            p.iov_out[j].iov_len -= nremaining;
            p.iov_out[j].iov_base = (char *) p.iov_out[j].iov_base + nremaining;
            nremaining = 0;
        }
        j++;
    }
    *p.niovs_out = j;

    for (i = j = 0, nremaining = len; i < p.nriovs; i++) {
        if (nremaining >= p.riov_in[i].len) {
            nremaining -= p.riov_in[i].len;
            continue;
        }
        p.riov_out[j] = p.riov_in[i];
        if (nremaining > 0) {
            p.riov_out[j].len -= nremaining;
            p.riov_out[j].addr += nremaining;
            nremaining = 0;
        }
        j++;
    }

    *p.nriovs_out = j;
    return len;
}

static bool
xmtr_vecbuf_unload(xmtr_t *x)
{
    vecbuf_t *vb;
    struct fi_rma_iov *riov;
    size_t i;

    if ((vb = (vecbuf_t *) fifo_peek(x->vec.rcvd)) == NULL ||
        x->nriovs == arraycount(x->riov))
        return false;

    if (!x->cxn.eof.remote && vb->msg.niovs == 0) {
        x->cxn.eof.remote = true;
    }

    riov = (!x->phase) ? x->riov : x->riov2;

    for (i = x->next_riov; i < vb->msg.niovs && x->nriovs < arraycount(x->riov);
         i++) {

        riov[x->nriovs++] = (struct fi_rma_iov){.len = vb->msg.iov[i].len,
                                                .addr = vb->msg.iov[i].addr,
                                                .key = vb->msg.iov[i].key};
    }

    if (i == vb->msg.niovs) {
        (void) fifo_get(x->vec.rcvd);
        rxctl_post(&x->cxn, &x->vec, &vb->hdr);
        x->next_riov = 0;
    } else
        x->next_riov = i;

    return true;
}

/* Process completed vector-message reception.  Return 0 if no
 * completions occurred, 1 if any completion occurred, -1 on an
 * irrecoverable error.
 */
static int
xmtr_vector_rx_process(xmtr_t *x, bufhdr_t *h)
{
    vecbuf_t *vb = (vecbuf_t *) h;

    if (h->xfc.cancelled) {
        buf_free(h);
        return 0;
    }

    if (!vecbuf_is_wellformed(vb)) {
        fprintf(stderr, "%s: rx'd malformed vector message", __func__);
        rxctl_post(&x->cxn, &x->vec, h);
        return 0;
    }

    if (!fifo_put(x->vec.rcvd, h))
        errx(EXIT_FAILURE, "%s: received vectors FIFO was full", __func__);

    return 1;
}

/* Process completions.  Return 0 if no completions occurred, 1 if
 * any completion occurred, -1 on an irrecoverable error.
 */
static int
xmtr_cq_process(xmtr_t *x, fifo_t *ready_for_terminal, bool reregister)
{
    struct fi_cq_msg_entry fcmpl;
    completion_t cmpl, *cmplp;
    bufhdr_t *h;
    size_t nprocessed;
    ssize_t ncompleted;

    if ((ncompleted = fi_cq_read(x->cxn.cq, &fcmpl, 1)) == -FI_EAGAIN)
        return 0;

    if (ncompleted == -FI_EAVAIL) {
        struct fi_cq_err_entry e;
        char errbuf[256];
        char flagsbuf[2][256];
        ssize_t nfailed = fi_cq_readerr(x->cxn.cq, &e, 0);

        cmpl = (completion_t){.xfc = e.op_context, .flags = 0, .len = 0};

        if (e.err != FI_ECANCELED || !cmpl.xfc->cancelled) {
            fprintf(stderr, "%s: read %zd errors, %s", __func__, nfailed,
                    fi_strerror(e.err));
            fprintf(stderr, "%s: context %p type %s", __func__,
                    (void *) cmpl.xfc, xfc_type_to_string(cmpl.xfc->type));
            fprintf(stderr, "%s: completion flags %" PRIx64 " symbolic %s",
                    __func__, e.flags,
                    completion_flags_to_string(e.flags, flagsbuf[0],
                                               sizeof(flagsbuf[0])));
            fprintf(stderr, "%s: provider error %s", __func__,
                    fi_cq_strerror(x->cxn.cq, e.prov_errno, e.err_data, errbuf,
                                   sizeof(errbuf)));
            return -1;
        }
    } else if (ncompleted < 0) {
        bailout_for_ofi_ret(ncompleted, "fi_cq_read");
        /* exit() is called in the above function, but the compiler doesn't
         * introspect at that level, producing a warning about cmpl.xfc
         * not being initialized. Adding an additional exit call suppresses
         * this while we refactor the problem out.
         */
        exit(EXIT_FAILURE);
    } else if (ncompleted != 1) {
        errx(EXIT_FAILURE, "%s: expected 1 completion, read %zd", __func__,
             ncompleted);
    } else {
        cmpl = (completion_t){
            .xfc = fcmpl.op_context, .flags = fcmpl.flags, .len = fcmpl.len};
    }

    cmpl.xfc->owner = xfo_program;

    switch (cmpl.xfc->type) {
        case xft_vector:
            for (nprocessed = 0, cmplp = &cmpl;
                 (h = rxctl_complete(&x->vec, cmplp)) != NULL; cmplp = NULL) {
                switch (xmtr_vector_rx_process(x, h)) {
                    case 1:
                        nprocessed++;
                        break;
                    case 0:
                        break;
                    default:
                        return -1;
                }
            }
            return (nprocessed > 0) ? 1 : 0;
        case xft_fragment:
        case xft_rdma_write:
            /* If the head of `wrposted` is marked `xfo_program`, then dequeue
             * the txbuffers at the head of `wrposted` through the last one
             * marked `xfo_program`.
             */
            if ((h = fifo_peek(x->wrposted)) == NULL) {
                fprintf(stderr, "%s: no RDMA-write completions expected",
                        __func__);
                return -1;
            }
            /* XXX This can fail if `ready_for_terminal` ever fills
             * to capacity, in the loop below.  That should not happen
             * unless we accidentally put more buffers into circulation
             * than there are slots in `ready_for_terminal`.
             */
            if ((h->xfc.place & xfp_first) == 0) {
                fprintf(stderr, "%s: expected `first` context at head",
                        __func__);
                return -1;
            }
            while ((h = fifo_peek(x->wrposted)) != NULL &&
                   h->xfc.owner == xfo_program && h->xfc.type == xft_fragment) {
                fragment_t *f = (fragment_t *) h;
                (void) fifo_get(x->wrposted);

                assert(f->parent->xfc.nchildren > 0);
                f->parent->xfc.nchildren--;

                (void) buflist_put(x->fragment.pool, h);
            }
            while ((h = fifo_peek(x->wrposted)) != NULL &&
                   h->xfc.owner == xfo_program &&
                   h->xfc.type == xft_rdma_write && h->xfc.nchildren == 0 &&
                   !fifo_full(ready_for_terminal)) {
                int rc;

                (void) fifo_get(x->wrposted);

                if (reregister && (rc = buf_mr_dereg(h)) != 0)
                    warn_about_ofi_ret(rc, "fi_close");

                x->bytes_progress += h->nused;
                (void) fifo_alt_put(ready_for_terminal, h);
            }
            return 1;
        case xft_progress:
            return txctl_complete(&x->progress, &cmpl);
        case xft_ack:
            return xmtr_ack_rx_process(x, &cmpl);
        case xft_initial:
            return 1;
        default:
            fprintf(stderr, "%s: unexpected xfer context type", __func__);
            return -1;
    }
}

static bufhdr_t *
xmtr_buf_split(xmtr_t *x, bufhdr_t *parent, size_t len)
{
    bufhdr_t *h;
    fragment_t *f;

    assert(x->fragment.offset < parent->nused);
    assert(len < parent->nused - x->fragment.offset);

    if ((h = buflist_get(x->fragment.pool)) == NULL)
        errx(EXIT_FAILURE, "%s: out of fragment headers", __func__);

    f = (fragment_t *) h;

    h->raddr = x->fragment.offset;
    h->nused = len;
    h->nallocated = 0;
    h->mr = parent->mr;
    h->desc = parent->desc;
    f->parent = parent;

    parent->xfc.nchildren++;

    return h;
}

/* Take Tx buffers off of our queue while their cumulative length
 * is less than the sum length of RDMA targets that we can write
 * in one scatter-gather I/O, sum(0 <= i < maxriovs, riov[i].len).
 *
 * If the first Tx buffer is longer than the RDMA targets, and no more
 * RDMA buffers are expected to arrive, then fragment the Tx buffer
 * and write the fragments independently.
 *
 * Flag the first Tx buffer `xfp_first` and the last `xfp_last`
 * (first and last may be the same buffer).  Clear flags on the rest
 * of the Tx buffers.  Set the owner of the first to `xfo_nic`.
 *
 * Finally, perform one fi_writemsg using the context on the first
 * Tx buffer.
 */
static loop_control_t
xmtr_targets_write(fifo_t *ready_for_cxn, xmtr_t *x)
{
    bufhdr_t *first_h, *h, *head, *last_h = NULL;
    const size_t maxriovs = minsize(global_state.rma_maxsegs, x->nriovs);
    size_t i, len, maxbytes, niovs, niovs_out = 0, nriovs_out = 0, total;
    ssize_t nwritten, rc;

    for (maxbytes = 0, i = 0; i < maxriovs; i++)
        maxbytes += ((!x->phase) ? x->riov : x->riov2)[i].len;

    /* If x->nriovs < global_state.rma_maxsegs, then more RDMA vectors will
     * arrive, so there is no need to fragment.
     */
    const bool riovs_maxed_out = x->nriovs >= global_state.rma_maxsegs;

    for (i = 0, total = 0, first_h = last_h = NULL;
         i < maxriovs && (head = fifo_peek(ready_for_cxn)) != NULL &&
         total < maxbytes && !fifo_full(x->wrposted);
         i++, last_h = h, total += len) {
        const bool oversize_load =
            head->nused - x->fragment.offset > maxbytes - total;

        /* Fragment oversize loads unless more RDMA vectors will arrive. */
        if (oversize_load && !riovs_maxed_out)
            break;

        if (oversize_load)
            len = maxbytes - total;
        else
            len = head->nused - x->fragment.offset;

        if (x->fragment.offset == 0)
            head->xfc.nchildren = 0;

        if (global_state.reregister && x->fragment.offset == 0 &&
            (rc = buf_mr_reg(global_state.domain, x->cxn.ep, payload_access.tx,
                             seqsource_get(&x->cxn.keys), head)) < 0)
            bailout_for_ofi_ret(rc, "payload memory registration failed");

        if (oversize_load) {
            h = xmtr_buf_split(x, head, len);
        } else {
            (void) fifo_get(ready_for_cxn);
            h = head;
        }

        (void) fifo_put(x->wrposted, h);

        if (last_h == NULL)
            first_h = h;

        h->xfc.owner = xfo_program;
        h->xfc.place = 0;

        bytebuf_t *b = (bytebuf_t *) head;

        ((!x->phase) ? x->payload.iov : x->payload.iov2)[i] = (struct iovec){
            .iov_len = len, .iov_base = &b->payload[x->fragment.offset]};
        ((!x->phase) ? x->payload.desc : x->payload.desc2)[i] = h->desc;
        if (oversize_load) {
            x->fragment.offset += len;
            assert(x->fragment.offset < head->nused);
        } else {
            x->fragment.offset = 0;
        }
    }
    niovs = i;

    if (first_h != NULL) {
        first_h->xfc.owner = xfo_nic;
        first_h->xfc.place = xfp_first;
        last_h->xfc.place |= xfp_last;

        write_fully_params_t p = {
            .ep = x->cxn.ep,
            .iov_in = (!x->phase) ? x->payload.iov : x->payload.iov2,
            .desc_in = (!x->phase) ? x->payload.desc : x->payload.desc2,
            .iov_out = (!x->phase) ? x->payload.iov2 : x->payload.iov,
            .desc_out = (!x->phase) ? x->payload.desc2 : x->payload.desc,
            .niovs = niovs,
            .niovs_out = &niovs_out,
            .riov_in = (!x->phase) ? x->riov : x->riov2,
            .riov_out = (!x->phase) ? x->riov2 : x->riov,
            .nriovs = x->nriovs,
            .nriovs_out = &nriovs_out,
            .len = total,
            .maxsegs = maxriovs,
            .flags = FI_COMPLETION | FI_DELIVERY_COMPLETE,
            .context = &first_h->xfc.ctx,
            .addr = x->cxn.peer_addr};

        nwritten = write_fully(p);

        if (nwritten < 0)
            bailout_for_ofi_ret(nwritten, "write_fully");

        if ((size_t) nwritten != total || niovs_out != 0) {
            fprintf(stderr,
                    "%s: local I/O vectors were partially written, "
                    "nwritten %zu total %zu niovs_out %zu",
                    __func__, nwritten, total, niovs_out);
            return loop_error;
        }

        x->nriovs = nriovs_out;

        x->phase = !x->phase;
    }
    return loop_continue;
}

static void
xmtr_progress_update(fifo_t *ready_for_cxn, xmtr_t *x)
{
    progbuf_t *pb;
    size_t progress;

    /* If the terminal reached EOF, ready_for_cxn and wrposted
     * are empty, and nleftover == 0 has not previously been sent
     * (!x->cxn.eof.local), then send nleftover == 0; on a successful
     * transmission, set x->cxn.eof.local to true.
     */
    bool reached_eof = (fifo_eoget(ready_for_cxn) && fifo_empty(x->wrposted) &&
                        !x->cxn.eof.local);

    if (x->bytes_progress == 0 && !reached_eof)
        return;

    if (fifo_full(x->progress.ready))
        return;

    if ((pb = (progbuf_t *) buflist_get(x->progress.pool)) == NULL)
        return;

    if (x->split_progress_countdown == 0) {
        if (x->bytes_progress >= 2) {
            progress = x->bytes_progress / 2;
            x->split_progress_countdown = split_progress_interval;
        } else {
            progress = x->bytes_progress;
        }
    } else {
        x->split_progress_countdown--;
        progress = x->bytes_progress;
    }
    pb->hdr.xfc.owner = xfo_nic;
    pb->hdr.nused = pb->hdr.nallocated;

    pb->msg.nfilled = progress;
    pb->msg.nleftover = reached_eof ? 0 : 1;

    x->bytes_progress -= progress;

    (void) txctl_put(&x->progress, &pb->hdr);

    if (reached_eof) {
        x->cxn.eof.local = true;
    }

    if (x->bytes_progress > 0)
        xmtr_progress_update(ready_for_cxn, x);
}

static void
xmtr_cancel(cxn_t *cxn)
{
    xmtr_t *x = (xmtr_t *) cxn;

    txctl_cancel(x->cxn.ep, &x->progress);
    rxctl_cancel(x->cxn.ep, &x->vec);
    fifo_cancel(x->cxn.ep, x->wrposted);
}

static bool
xmtr_cancellation_complete(cxn_t *cxn)
{
    xmtr_t *x = (xmtr_t *) cxn;

    return txctl_idle(&x->progress) && rxctl_idle(&x->vec) &&
           fifo_empty(x->wrposted);
}

static loop_control_t
xmtr_loop(worker_t *w, session_t *s)
{
    vecbuf_t *vb;
    xmtr_t *x = (xmtr_t *) s->cxn;

    if (xmtr_cq_process(x, s->ready_for_terminal, global_state.reregister) ==
        -1)
        return loop_error;

    if (!x->cxn.sent_first)
        return xmtr_initial_send(x);

    if (!x->cxn.started)
        return xmtr_start(w, x, s->ready_for_terminal);

    if (!x->rcvd_ack)
        return loop_continue;

    while (xmtr_vecbuf_unload(x))
        ; // do nothing

    if (xmtr_targets_write(s->ready_for_cxn, x) == loop_error)
        return loop_error;

    xmtr_progress_update(s->ready_for_cxn, x);

    txctl_transmit(&x->cxn, &x->progress);

    if (!(fifo_eoget(s->ready_for_cxn) && fifo_empty(x->wrposted) &&
          x->bytes_progress == 0 && x->cxn.eof.local))
        return loop_continue;

    /* Hunt for remote EOF. */
    while (!x->cxn.eof.remote &&
           (vb = (vecbuf_t *) fifo_get(x->vec.rcvd)) != NULL) {
        if (vb->msg.niovs == 0)
            x->cxn.eof.remote = true;
        buf_mr_dereg(&vb->hdr);
        vecbuf_free(vb);
    }

    if (x->cxn.eof.remote && txctl_idle(&x->progress))
        return loop_end;

    return loop_continue;
}

static void
session_shutdown(session_t *s)
{
    bufhdr_t *h;
    cxn_t *cxn = s->cxn;
    int rc;

    if (cxn->shutdown != NULL)
        cxn->shutdown(cxn);

    assert(cxn->parent == s);
    cxn->parent = NULL;
    s->cxn = NULL;

    while ((h = fifo_alt_get(s->ready_for_cxn)) != NULL ||
           (h = fifo_alt_get(s->ready_for_terminal)) != NULL) {
        buf_mr_dereg(h);
        buf_free(h);
    }

    if ((rc = fi_close(&cxn->cq->fid)) < 0) {
        // TODO: Investigate this returning a negative error code. Normal?
        //        fprintf(stderr, "%s: could not fi_close CQ %p: %s", __func__,
        //                  (void *) &cxn->cq, fi_strerror(-rc));
    }

    if ((rc = fi_close(&cxn->ep->fid)) < 0) {
        fprintf(stderr, "%s: could not fi_close endpoint %p: %s", __func__,
                (void *) &cxn->ep, fi_strerror(-rc));
    }
}

static loop_control_t
cxn_loop(worker_t *w, session_t *s)
{
    cxn_t *cxn = s->cxn;
    const loop_control_t ctl = cxn->loop(w, s);

    switch (ctl) {
        case loop_end:
        case loop_error:
            cxn->cancel(cxn);
            cxn->ended = true;
            cxn->end_reason = ctl;
            break;
        case loop_continue:
            if (cxn->cancelled || cxn->ended) {
                if (cxn->cancellation_complete(cxn)) {
                    return cxn->cancelled ? loop_canceled : cxn->end_reason;
                }
            } else if (global_state.cancelled) {
                cxn->cancel(cxn);
                cxn->cancelled = true;
            }
            break;
        default:
            break;
    }
    return ctl;
}

static loop_control_t
session_loop(worker_t *w, session_t *s)
{
    terminal_t *t = s->terminal;

    if (t->trade(t, s->ready_for_terminal, s->ready_for_cxn) == loop_error)
        return loop_error;

    return cxn_loop(w, s);
}

static void
sessions_swap(session_t *r, session_t *s)
{
    session_t tmp;

    if (r == s)
        return;

    tmp = *r;
    *r = *s;
    if (r->cxn != NULL)
        r->cxn->parent = r;
    *s = tmp;
    if (s->cxn != NULL)
        s->cxn->parent = s;
}

static void
worker_update_load(load_t *load, int nready)
{
    if (nready > load->max_loop_contexts)
        load->max_loop_contexts = nready;

    if (nready < load->min_loop_contexts)
        load->min_loop_contexts = nready;

    load->ctxs_serviced_since_mark += nready;

    if (load->loops_since_mark < UINT16_MAX) {
        load->loops_since_mark++;
    } else {
        // MARK
        load->average = (load->average + 256 * load->ctxs_serviced_since_mark /
                                             (UINT16_MAX + 1)) /
                        2;
#if 0 /* uncomment to dump info */
        fprintf(stderr, "%s: average %" PRIuFAST16 "x%" PRIuFAST16, __func__,
                  load->average / (uint_fast16_t) 256,
                  load->average % (uint_fast16_t) 256);
        fprintf(stderr, "%s: %" PRIu32 " contexts in %" PRIuFAST16 " loops",
                  __func__, load->ctxs_serviced_since_mark,
                  load->loops_since_mark);
        fprintf(stderr, "%s: %d to %d contexts per loop", __func__,
                  load->min_loop_contexts, load->max_loop_contexts);
#endif
        load->loops_since_mark = 0;
        load->ctxs_serviced_since_mark = 0;
        load->max_loop_contexts = 0;
        load->min_loop_contexts = INT_MAX;
    }
}

static bool
worker_waitable(worker_t *self)
{
    struct fid *fid[WORKER_SESSIONS_MAX];
    size_t nfids = 0;
    size_t i;
    bool waitable;

    self->stats.epoll_loops.total++;

    if (global_state.cancelled)
        return false;

    if (self->nsessions[0] == 0 && self->nsessions[1] == 0)
        return false;

    for (i = 0; i < arraycount(self->session); i++) {
        session_t *s = &self->session[i];
        cxn_t *c = s->cxn;

        if (c == NULL)
            continue;

        if (!s->waitable)
            return false;

        fid[nfids++] = &c->cq->fid;
    }

    waitable = (fi_trywait(global_state.fabric, fid, nfids) == FI_SUCCESS);
    if (waitable)
        self->stats.epoll_loops.waitable++;
    return waitable;
}

static int
extract_contexts_for_half(const session_t *session_half,
                          const struct epoll_event *events, int nevents,
                          void **context, bool waitable)
{
    int i, ncontexts = 0;
    const session_t *from = session_half,
                    *upto = &session_half[WORKER_SESSIONS_MAX / 2];

    if (!waitable) {
        for (i = 0; i < upto - from; i++) {
            if (from[i].cxn == NULL)
                continue;
            context[ncontexts++] = from[i].cxn;
        }
        return ncontexts;
    }

    for (i = 0; i < nevents; i++) {
        cxn_t *c = events[i].data.ptr;

        assert(c->magic == 0xdeadbeef);

        if (c->parent == NULL)
            continue;

        if (c->parent < from || upto <= c->parent)
            continue;

        context[ncontexts++] = c;
    }

    return ncontexts;
}

static void
worker_run_loop(worker_t *self)
{
    size_t half;
    ptrdiff_t i;
    const ptrdiff_t nsessions = arraycount(self->session);
    struct epoll_event events[WORKER_SESSIONS_MAX];
    int nevents = 0;
    bool waitable;

    if ((waitable = global_state.waitfd && worker_waitable(self)) &&
        (nevents = epoll_pwait(self->epoll_fd, events, (int) arraycount(events),
                               -1, &self->epoll_sigset)) == -1 &&
        errno != EINTR)
        err(EXIT_FAILURE, "%s: epoll_pwait", __func__);

    for (half = 0; half < 2; half++) {
        void *context[WORKER_SESSIONS_MAX];
        pthread_mutex_t *mtx = &self->mtx[half];
        session_t *session_half = &self->session[half * nsessions / 2];
        int ncontexts, rc;

        if (pthread_mutex_trylock(mtx) == EBUSY)
            continue;

        if (global_state.waitfd) {
            ncontexts = extract_contexts_for_half(session_half, events, nevents,
                                                  context, waitable);
        } else if (self->pollable) {
            ncontexts =
                fi_poll(self->pollset[half], context, WORKER_SESSIONS_MAX);
            if (ncontexts < 0) {
                (void) pthread_mutex_unlock(mtx);
                bailout_for_ofi_ret(ncontexts, "fi_poll");
            }
        } else {
            ncontexts = extract_contexts_for_half(session_half, NULL, 0,
                                                  context, false);
        }

        /* TBD move this down, use the total number ready regardless of
         * I/O readiness.
         *
         * May need to take care about counting all sessions ready because
         * they are cancelled.
         */
        worker_update_load(&self->load, ncontexts);

        for (i = 0; i < ncontexts; i++) {
            cxn_t *c = context[i];
            assert(c != NULL);
            assert(c->magic == 0xdeadbeef);

            session_t *s = c->parent;
            assert(s != NULL);

            assert(0 <= s - session_half && s - session_half < nsessions / 2);

            sessions_swap(s, &session_half[i]);
        }

        session_t *io_ready_up_to = &session_half[ncontexts];
        session_t *ready_up_to = io_ready_up_to;

        for (i = ready_up_to - session_half; i < nsessions / 2; i++) {
            session_t *s = &session_half[i];
            cxn_t *c = s->cxn;

            // skip empty slots
            if (c == NULL)
                continue;

            if (c->sent_first && fifo_empty(s->ready_for_terminal) &&
                !global_state.cancelled)
                continue;

            sessions_swap(s, ready_up_to);
            ready_up_to++;
        }

        session_t *active_up_to = ready_up_to;

        self->stats.half_loops.total++;

        if (io_ready_up_to == session_half)
            self->stats.half_loops.no_io_ready++;

        if (ready_up_to == io_ready_up_to)
            self->stats.half_loops.no_session_ready++;

        /*
         * TBD change terminology to `occupied` and `empty` slots.
         */

        /* Move active session slots (i.e., s->cxn != NULL) to front
         * so that inactive slots are consecutive at the end of
         * the self->session[] half.  Point `active_up_to` at
         * the first inactive slot or, if there are no inactive
         * slots, point it one past the end of the session[] half.
         */
        for (i = active_up_to - session_half; i < nsessions / 2; i++) {
            session_t *s = &session_half[i];

            // skip empty (inactive) slots
            if (s->cxn == NULL)
                continue;

            sessions_swap(s, active_up_to);
            active_up_to++;
        }

#if 1
        session_t *ready_from = session_half;
#else
        session_t *ready_from;
        bool stole;

        /* TBD If
         * `ready_up_to == session_half`,
         * then choose a second worker at random and ask for it to
         * fill inactive session slots, if there are any.
         */

        if (ready_up_to == session_half &&
            active_up_to - session_half < arraycount(self->session) / 2) {
            worker_steal(self, active_up_to,
                         arraycount(self->session) / 2 -
                             (active_up_to - session_half),
                         &ready_up_to);
            stole = true;
        } else {
            ready_from = session_half;
            stole = false;
        }
#endif

        /* Service ready session slots. */
        for (i = ready_from - session_half; i < ready_up_to - session_half;
             i++) {
            session_t *s;
            cxn_t *c;
            struct {
                bool cxn_empty;
                bool terminal_full;
                eof_state_t eof;
            } after;

            s = &session_half[i];

            if (s == ready_up_to) {
                assert(i >= ncontexts);
                break;
            }

            c = s->cxn;
            assert(c != NULL);

            assert(/* stole || */ i < ncontexts || !c->sent_first ||
                   !fifo_empty(s->ready_for_terminal) ||
                   global_state.cancelled);

            loop_control_t ctl = session_loop(self, s);

            after.cxn_empty = fifo_empty(s->ready_for_cxn);
            after.terminal_full = fifo_full(s->ready_for_terminal);
            after.eof = c->eof;

            if (!fifo_empty(s->ready_for_terminal))
                s->waitable = false;
            else if (after.eof.remote && !after.eof.local)
                s->waitable = false;
            else if (!c->sent_first)
                s->waitable = false;
            else
                s->waitable = true;

            // continue at next cxn_t if `c` did not exit
            switch (ctl) {
                case loop_continue:
                    continue;
                case loop_end:
                    break;
                case loop_canceled:
                    self->canceled = true;
                    break;
                case loop_error:
                    self->failed = true;
                    break;
            }

            if (self->pollable &&
                (rc = fi_poll_del(self->pollset[half], &c->cq->fid, 0)) != 0)
                bailout_for_ofi_ret(rc, "fi_poll_del");

            if (!global_state.waitfd)
                ;
            else if (epoll_ctl(self->epoll_fd, EPOLL_CTL_DEL, c->cq_wait_fd,
                               NULL) == -1) {
                err(EXIT_FAILURE, "%s.%d: epoll_ctl(,EPOLL_CTL_DEL,)", __func__,
                    __LINE__);
            }

            session_shutdown(s);

            atomic_fetch_add_explicit(&self->nsessions[half], -1,
                                      memory_order_relaxed);
        }

        (void) pthread_mutex_unlock(mtx);
    }
}

static bool
worker_is_idle(worker_t *self)
{
    const ptrdiff_t self_idx = self - &workers[0];
    size_t half, nlocked;

    if (self->nsessions[0] != 0 || self->nsessions[1] != 0)
        return false;

    if (self_idx + (size_t) 1 !=
        atomic_load_explicit(&nworkers_running, memory_order_relaxed))
        return false;

    if (pthread_mutex_trylock(&workers_mtx) == EBUSY)
        return false;

    for (nlocked = 0; nlocked < 2; nlocked++) {
        if (pthread_mutex_trylock(&self->mtx[nlocked]) == EBUSY)
            break;
    }

    bool idle =
        (nlocked == 2 && self->nsessions[0] == 0 && self->nsessions[1] == 0 &&
         self_idx + (size_t) 1 == nworkers_running);

    if (idle) {
        nworkers_running--;
        pthread_cond_signal(&nworkers_cond);
    }

    for (half = 0; half < nlocked; half++)
        (void) pthread_mutex_unlock(&self->mtx[half]);

    (void) pthread_mutex_unlock(&workers_mtx);

    return idle;
}

static void
worker_idle_loop(worker_t *self)
{
    const ptrdiff_t self_idx = self - &workers[0];

    (void) pthread_mutex_lock(&workers_mtx);
    while (nworkers_running <= (size_t) self_idx && !self->shutting_down &&
           !self->canceled)
        pthread_cond_wait(&self->sleep, &workers_mtx);
    (void) pthread_mutex_unlock(&workers_mtx);
}

#if 0 /* uncomment to dump stats */
static void
worker_stats_log(worker_t *self)
{
    fprintf(stderr, "worker %p %" PRIu64 " epoll loops waitable",
              (void *) self, self->stats.epoll_loops.waitable);
    fprintf(stderr, "worker %p %" PRIu64 " epoll loops total",
              (void *) self, self->stats.epoll_loops.total);
    fprintf(stderr, "worker %p %" PRIu64 " half loops no I/O ready",
              (void *) self, self->stats.half_loops.no_io_ready);
    fprintf(stderr, "worker %p %" PRIu64 " half loops no session ready",
              (void *) self, self->stats.half_loops.no_session_ready);
    fprintf(stderr, "worker %p %" PRIu64 " half loops total",
              (void *) self, self->stats.half_loops.total);
}
#endif

static void *
worker_outer_loop(void *arg)
{
    worker_t *self = arg;

    while (!self->shutting_down) {
        worker_idle_loop(self);
        do {
            worker_run_loop(self);
        } while (!worker_is_idle(self) && !self->shutting_down);
    }
    return NULL;
}

static void
paybuflist_destroy(buflist_t *bl)
{
    size_t i;
    int rc;

    for (i = 0; i < bl->nfull; i++) {
        bufhdr_t *h = bl->buf[i];

        if (!global_state.reregister && (rc = buf_mr_dereg(h)) != 0)
            warn_about_ofi_ret(rc, "fi_close");

        free(h);
    }
    bl->nfull = bl->nallocated = 0;
    free(bl);
}

static buflist_t *
worker_paybuflist_create(worker_t *w, uint64_t access)
{
    buflist_t *bl = buflist_create(16);

    if (bl == NULL)
        return NULL;

    if (!paybuflist_replenish(&w->keys, access, bl)) {
        paybuflist_destroy(bl);
        return NULL;
    }

    return bl;
}

static void
worker_init(worker_t *w)
{
    struct fi_poll_attr attr = {.flags = 0};
    int rc;
    size_t i;

    w->shutting_down = false;
    w->failed = false;
    seqsource_init(&w->keys);

    if ((rc = pthread_cond_init(&w->sleep, NULL)) != 0) {
        errx(EXIT_FAILURE, "%s.%d: pthread_cond_init: %s", __func__, __LINE__,
             strerror(rc));
    }

    if (!global_state.waitfd)
        w->epoll_fd = -1;
    else if ((w->epoll_fd = epoll_create(1)) == -1)
        err(EXIT_FAILURE, "%s.%d: epoll_create", __func__, __LINE__);

    w->pollable = true;
    for (i = 0; i < arraycount(w->mtx); i++) {
        if ((rc = pthread_mutex_init(&w->mtx[i], NULL)) != 0) {
            errx(EXIT_FAILURE, "%s.%d: pthread_mutex_init: %s", __func__,
                 __LINE__, strerror(rc));
        }
        if ((rc = fi_poll_open(global_state.domain, &attr, &w->pollset[i])) ==
            -FI_ENOSYS) {
            w->pollable = false;
            break;
        } else if (rc != 0) {
            bailout_for_ofi_ret(rc, "fi_poll_open");
        }
    }
    for (i = 0; i < arraycount(w->session); i++)
        w->session[i] = (session_t){.cxn = NULL, .terminal = NULL};

    w->paybufs.rx = worker_paybuflist_create(w, payload_access.rx);
    w->paybufs.tx = worker_paybuflist_create(w, payload_access.tx);

    w->load = (load_t){.max_loop_contexts = 0,
                       .min_loop_contexts = INT_MAX,
                       .average = 0,
                       .loops_since_mark = 0,
                       .ctxs_serviced_since_mark = 0};
    w->stats = (worker_stats_t){
        .epoll_loops = {.waitable = 0, .total = 0},
        .half_loops = {.no_io_ready = 0, .no_session_ready = 0, .total = 0}};
}

static bool
worker_launch(worker_t *w)
{
    pthread_attr_t attr;
    size_t i;
    int rc, create_rc;
    sigset_t oldset, blockset;
    cpu_set_t cpuset;

    if ((rc = sigemptyset(&blockset)) == -1) {
        err(EXIT_FAILURE, "%s.%d: sigfillset", __func__, __LINE__);
    }

    for (i = 0; i < arraycount(siglist); i++) {
        if (sigaddset(&blockset, siglist[i].signum) == -1)
            err(EXIT_FAILURE, "%s.%d: sigaddset", __func__, __LINE__);
    }

    w->epoll_sigset = blockset;

    if (sigaddset(&blockset, SIGUSR1) == -1)
        err(EXIT_FAILURE, "%s.%d: sigaddset", __func__, __LINE__);

    if (sigaddset(&blockset, SIGUSR2) == -1)
        err(EXIT_FAILURE, "%s.%d: sigaddset", __func__, __LINE__);

    CPU_ZERO(&cpuset);
    CPU_SET(global_state.nextcpu, &cpuset);

    if ((rc = pthread_attr_init(&attr)) != 0) {
        errx(EXIT_FAILURE, "%s.%d: pthread_attr_init: %s", __func__, __LINE__,
             strerror(rc));
    }

    if (global_state.personality == get &&
        (rc = pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset)) !=
            0) {
        errx(EXIT_FAILURE, "%s.%d: pthread_attr_setaffinity_cp: %s", __func__,
             __LINE__, strerror(rc));
    }

    if ((rc = pthread_sigmask(SIG_BLOCK, &blockset, &oldset)) != 0) {
        errx(EXIT_FAILURE, "%s.%d: pthread_sigmask: %s", __func__, __LINE__,
             strerror(rc));
    }

    create_rc = pthread_create(&w->thd, &attr, worker_outer_loop, w);

    if ((rc = pthread_sigmask(SIG_SETMASK, &oldset, NULL)) != 0) {
        errx(EXIT_FAILURE, "%s.%d: pthread_sigmask: %s", __func__, __LINE__,
             strerror(rc));
    }

    if (create_rc != 0) {
        errx(EXIT_FAILURE, "%s.%d: pthread_create: %s", __func__, __LINE__,
             strerror(create_rc));
    }

    if (global_state.nextcpu == (int) global_state.processors.last)
        global_state.nextcpu = (int) global_state.processors.first;
    else
        global_state.nextcpu++;

    return true;
}

#if 0
static void
worker_teardown(worker_t *w)
{
    int rc;
    size_t i;

    for (i = 0; i < arraycount(w->mtx); i++) {
        if ((rc = pthread_mutex_destroy(&w->mtx[i])) != 0) {
            errx(EXIT_FAILURE, "%s.%d: pthread_mutex_destroy: %s",
                __func__, __LINE__, strerror(rc));
        }
        if ((rc = fi_close(&w->pollset[i]->fid)) != 0)
            bailout_for_ofi_ret(rc, "fi_close");
    }
    for (i = 0; i < arraycount(w->session); i++)
        assert(w->session[i].cxn == NULL && w->session[i].terminal == NULL);

    if ((rc = pthread_cond_destroy(&w->sleep)) != 0) {
        errx(EXIT_FAILURE, "%s.%d: pthread_cond_destroy: %s",
            __func__, __LINE__, strerror(rc));
    }
    // TBD release buffers and free-buffer reservior.
}
#endif

static worker_t *
worker_create(void)
{
    worker_t *w;

    (void) pthread_mutex_lock(&workers_mtx);
    w = (nworkers_allocated < arraycount(workers))
            ? &workers[nworkers_allocated++]
            : NULL;
    if (w != NULL)
        worker_init(w);
    (void) pthread_mutex_unlock(&workers_mtx);

    if (w == NULL)
        return NULL;

    if (!worker_launch(w)) {
        (void) pthread_mutex_lock(&workers_mtx);

        if ((w - &workers[0]) + (size_t) 1 != nworkers_allocated) {
            (void) pthread_mutex_unlock(&workers_mtx);
            errx(EXIT_FAILURE, "%s: worker launch failed irrecoverably",
                 __func__);
        }

        nworkers_allocated--;

        (void) pthread_mutex_unlock(&workers_mtx);
        return NULL;
    }

    return w;
}

static void
workers_initialize(void)
{
}

static bool
worker_assign_session(worker_t *w, session_t s)
{
    size_t half, i;
    int rc;

    for (half = 0; half < 2; half++) {
        pthread_mutex_t *mtx = &w->mtx[half];

        if (pthread_mutex_trylock(mtx) == EBUSY)
            continue;

        // find an empty receiver slot
        for (i = 0; i < arraycount(w->session) / 2; i++) {
            session_t *slot =
                &w->session[half * arraycount(w->session) / 2 + i];

            if (slot->cxn != NULL)
                continue;

            *slot = s;
            slot->cxn->parent = slot;

            rc = w->pollable ? fi_poll_add(w->pollset[half], &s.cxn->cq->fid, 0)
                             : 0;

            if (rc != 0)
                bailout_for_ofi_ret(rc, "fi_poll_add");

            if (!global_state.waitfd)
                ;
            else if (epoll_ctl(w->epoll_fd, EPOLL_CTL_ADD, s.cxn->cq_wait_fd,
                               &(struct epoll_event){
                                   .events = EPOLLIN,
                                   .data = {.ptr = slot->cxn}}) == -1) {
                err(EXIT_FAILURE, "%s.%d: epoll_ctl(,EPOLL_CTL_ADD,)", __func__,
                    __LINE__);
            }

            atomic_fetch_add_explicit(&w->nsessions[half], 1,
                                      memory_order_relaxed);

            (void) pthread_mutex_unlock(mtx);
            return true;
        }
        (void) pthread_mutex_unlock(mtx);
    }
    if (global_state.waitfd && (rc = pthread_kill(w->thd, SIGUSR1)) != 0) {
        errx(EXIT_FAILURE, "%s: could not signal thread for worker %p: %s",
             __func__, (void *) w, strerror(rc));
    }
    return false;
}

/* Try to allocate `c` to an active worker, least active, first.
 * Caller must hold `workers_mtx`.
 */
static worker_t *
workers_assign_session_to_running(session_t s)
{
    size_t iplus1;

    for (iplus1 = nworkers_running; 0 < iplus1; iplus1--) {
        size_t i = iplus1 - 1;
        worker_t *w = &workers[i];
        if (worker_assign_session(w, s))
            return w;
    }
    return NULL;
}

/* Try to assign `c` to the next idle worker servicing `dom`.
 * Caller must hold `workers_mtx`.
 */
static worker_t *
workers_assign_session_to_idle(session_t s)
{
    size_t i;

    if ((i = nworkers_running) < nworkers_allocated) {
        worker_t *w = &workers[i];
        if (worker_assign_session(w, s))
            return w;
    }
    return NULL;
}

/* Try to wake the first idle worker.
 *
 * Caller must hold `workers_mtx`.
 */
static void
workers_wake(worker_t *w)
{
    assert(&workers[nworkers_running] == w);
    nworkers_running++;
    pthread_cond_signal(&w->sleep);
}

static worker_t *
workers_assign_session(session_t s)
{
    worker_t *w;

    do {
        (void) pthread_mutex_lock(&workers_mtx);

        if (workers_assignment_suspended) {
            (void) pthread_mutex_unlock(&workers_mtx);
            return NULL;
        }

        if ((w = workers_assign_session_to_running(s)) != NULL)
            ;
        else if ((w = workers_assign_session_to_idle(s)) != NULL)
            workers_wake(w);
        (void) pthread_mutex_unlock(&workers_mtx);
    } while (w == NULL && (w = worker_create()) != NULL);

    return w;
}

static int
workers_join_all(void)
{
    int code = EXIT_SUCCESS;
    size_t i;

    (void) pthread_mutex_lock(&workers_mtx);

    workers_assignment_suspended = true;

    while (nworkers_running > 0) {
        pthread_cond_wait(&nworkers_cond, &workers_mtx);
    }

    for (i = 0; i < nworkers_allocated; i++) {
        worker_t *w = &workers[i];
        w->shutting_down = true;
        pthread_cond_signal(&w->sleep);
    }

    (void) pthread_mutex_unlock(&workers_mtx);

    for (i = 0; i < nworkers_allocated; i++) {
        worker_t *w = &workers[i];
        int rc;

        if ((rc = pthread_join(w->thd, NULL)) != 0) {
            errx(EXIT_FAILURE, "%s.%d: pthread_join: %s", __func__, __LINE__,
                 strerror(rc));
        }
        if (w->failed || w->canceled != global_state.expect_cancellation)
            code = EXIT_FAILURE;
    }

#if 0 /* Uncomment to dump worker stats */
    for (i = 0; i < nworkers_allocated; i++) {
        worker_t *w = &workers[i];

        worker_stats_log(w);
    }
#endif

    return code;
}

static void
cxn_init(cxn_t *c, struct fid_av *av,
         loop_control_t (*loop)(worker_t *, session_t *),
         void (*cancel)(cxn_t *), bool (*cancellation_complete)(cxn_t *),
         void (*shutdown)(cxn_t *))
{
    memset(c, 0, sizeof(*c));
    c->magic = 0xdeadbeef;
    c->cancel = cancel;
    c->cancellation_complete = cancellation_complete;
    c->shutdown = shutdown;
    c->loop = loop;
    c->av = av;
    c->sent_first = false;
    c->started = false;
    c->cancelled = false;
    c->eof.local = c->eof.remote = false;
    seqsource_init(&c->keys);
}

void
xmtr_shutdown(cxn_t *c)
{
    xmtr_t *x = (xmtr_t *) c;
    if (fi_close(&x->initial.mr->fid) < 0)
        fprintf(stderr, "%s: could not close initial MR", __func__);
    if (fi_close(&x->ack.mr->fid) < 0)
        fprintf(stderr, "%s: could not close ack MR", __func__);
    if (fi_close(&x->payload.mr->fid) < 0)
        fprintf(stderr, "%s: could not close payload MR", __func__);
}

static void
xmtr_memory_init(xmtr_t *x)
{
    const size_t txbuflen = strlen(txbuf);
    int rc;

    rc = fi_mr_reg(global_state.domain, &x->initial.msg, sizeof(x->initial.msg),
                   FI_SEND, 0, seqsource_get(&global_state.keys), 0,
                   &x->initial.mr, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_mr_reg");

    rc =
        fi_mr_reg(global_state.domain, &x->ack.msg, sizeof(x->ack.msg), FI_RECV,
                  0, seqsource_get(&global_state.keys), 0, &x->ack.mr, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_mr_reg");

    rc = fi_mr_reg(global_state.domain, txbuf, txbuflen, FI_WRITE, 0,
                   seqsource_get(&global_state.keys), 0, &x->payload.mr, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_mr_reg");
}

/* First stage of initialization, no endpoint (x->cxn.ep) necessary. */
static void
xmtr_init(xmtr_t *x, struct fid_av *av)
{
    const size_t maxposted = 64;

    memset(x, 0, sizeof(*x));

    x->next_riov = 0;
    x->fragment.offset = 0;
    x->phase = false;
    x->bytes_progress = 0;

    cxn_init(&x->cxn, av, xmtr_loop, xmtr_cancel, xmtr_cancellation_complete,
             xmtr_shutdown);
    xmtr_memory_init(x);
    if ((x->wrposted = fifo_create(maxposted)) == NULL) {
        errx(EXIT_FAILURE, "%s: could not create posted RDMA writes FIFO",
             __func__);
    }
    rxctl_init(&x->vec, 64);
}

/* Second stage initialization needs an endpoint (x->cxn.ep). */
static void
xmtr_buffers_init(xmtr_t *x)
{
    const size_t maxposted = 64;
    size_t i;

    txctl_init(&x->progress, 64, 16, progbuf_create_and_register, x->cxn.ep);

    if ((x->fragment.pool = buflist_create(maxposted)) == NULL) {
        errx(EXIT_FAILURE, "%s: could not create fragment header pool",
             __func__);
    }

    for (i = 0; i < maxposted; i++) {
        fragment_t *f = fragment_alloc();

        if (!buflist_put(x->fragment.pool, &f->hdr))
            errx(EXIT_FAILURE, "%s: fragment pool full", __func__);
    }
}

static void
terminal_init(terminal_t *t,
              loop_control_t (*trade)(terminal_t *, fifo_t *, fifo_t *))
{
    t->trade = trade;
}

static void
sink_init(sink_t *s)
{
    memset(s, 0, sizeof(*s));
    terminal_init(&s->terminal, sink_trade);
    s->txbuflen = strlen(txbuf);
    s->entirelen = s->txbuflen * (size_t) 100000;
    s->idx = 0;
}

static void
source_init(source_t *s)
{
    memset(s, 0, sizeof(*s));
    terminal_init(&s->terminal, source_trade);
    s->txbuflen = strlen(txbuf);
    s->entirelen = s->txbuflen * (size_t) 100000;
    s->idx = 0;
}

static void
rcvr_initial_msg_init(rcvr_t *r, struct fid_ep *listen_ep)
{
    int rc;

    r->initial.niovs =
        fibonacci_iov_setup(&r->initial.msg, sizeof(r->initial.msg),
                            r->initial.iov, global_state.rx_maxsegs);

    if (r->initial.niovs < 1) {
        errx(EXIT_FAILURE, "%s: unexpected I/O vector length %zd", __func__,
             r->initial.niovs);
    }

    rc = mr_regv_all(global_state.domain, listen_ep, r->initial.iov,
                     r->initial.niovs, minsize(2, global_state.mr_maxsegs),
                     FI_RECV, 0, &global_state.keys, 0, r->initial.mr,
                     r->initial.desc, r->initial.raddr, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "mr_regv_all");
}

static void
rcvr_shutdown(cxn_t *c)
{
    bufhdr_t *h;
    rcvr_t *r = (rcvr_t *) c;
    session_t *s = c->parent;
    int rc;

    if (mr_deregv_all(r->initial.niovs, minsize(2, global_state.mr_maxsegs),
                      r->initial.mr) < 0) {
        fprintf(stderr, "%s: could not close initial MR", __func__);
    }
    if (mr_deregv_all(r->ack.niovs, minsize(2, global_state.mr_maxsegs),
                      r->ack.mr) < 0) {
        fprintf(stderr, "%s: could not close ack MR", __func__);
    }
    while ((h = fifo_get(r->tgtposted)) != NULL) {

        if ((rc = buf_mr_dereg(h)) != 0)
            warn_about_ofi_ret(rc, "buf_mr_dereg");

        (void) fifo_alt_put(s->ready_for_terminal, h);
    }
}

static void
rcvr_ack_msg_init(rcvr_t *r, struct fid_ep *ep)
{
    int rc;

    r->ack.niovs = fibonacci_iov_setup(&r->ack.msg, sizeof(r->ack.msg),
                                       r->ack.iov, global_state.rx_maxsegs);

    if (r->ack.niovs < 1) {
        errx(EXIT_FAILURE, "%s: unexpected I/O vector length %zd", __func__,
             r->ack.niovs);
    }

    rc = mr_regv_all(global_state.domain, ep, r->ack.iov, r->ack.niovs,
                     minsize(2, global_state.mr_maxsegs), FI_RECV, 0,
                     &global_state.keys, 0, r->ack.mr, r->ack.desc,
                     r->ack.raddr, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "mr_regv_all");
}

static void
rcvr_init(rcvr_t *r, struct fid_ep *listen_ep, struct fid_av *av)
{
    memset(r, 0, sizeof(*r));

    cxn_init(&r->cxn, av, rcvr_loop, rcvr_cancel, rcvr_cancellation_complete,
             rcvr_shutdown);
    rcvr_initial_msg_init(r, listen_ep);

    if ((r->tgtposted = fifo_create(64)) == NULL) {
        errx(EXIT_FAILURE, "%s: could not create RDMA targets FIFO", __func__);
    }

    rxctl_init(&r->progress, 64);
}

static void
rcvr_buffers_init(rcvr_t *r)
{
    txctl_init(&r->vec, 64, 16, vecbuf_create_and_register, r->cxn.ep);
}

/* Post a receive for the initial message for session `gs`
 * on endpoint `ep`.
 */
static void
post_initial_rx(struct fid_ep *ep, get_session_t *gs)
{
    int rc;

    rcvr_t *r = &gs->rcvr;

    rc = fi_recvmsg(ep,
                    &(struct fi_msg){.msg_iov = r->initial.iov,
                                     .desc = r->initial.desc,
                                     .iov_count = r->initial.niovs,
                                     .addr = r->cxn.peer_addr,
                                     .context = &gs->ctx,
                                     .data = 0},
                    FI_COMPLETION);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_recvmsg");
}

static get_session_t *
get_session_accept(get_state_t *gst)
{
    struct fi_cq_attr cq_attr = {.size = 128,
                                 .flags = 0,
                                 .format = FI_CQ_FORMAT_MSG,
                                 .wait_obj = global_state.waitfd ? FI_WAIT_FD
                                                                 : FI_WAIT_NONE,
                                 .signaling_vector = 0,
                                 .wait_cond = FI_CQ_COND_NONE,
                                 .wait_set = NULL};
    struct fi_cq_msg_entry completion;
    get_session_t *gs;
    rcvr_t *r;
    ssize_t ncompleted;
    int rc;

    /* Await initial message. */
    do {
        ncompleted = global_state.waitfd
                         ? fi_cq_sread(gst->listen_cq, &completion, 1, NULL, -1)
                         : fi_cq_read(gst->listen_cq, &completion, 1);
        //        if (ncompleted == -FI_EINTR)
        //            fprintf(stderr, "%s: fi_cq_{,s}read interrupted",
        //            __func__);
    } while ((ncompleted == -FI_EAGAIN || ncompleted == -FI_EINTR) &&
             !global_state.cancelled);

    if (global_state.cancelled)
        errx(EXIT_FAILURE, "caught a signal, exiting.");

    if (ncompleted < 0)
        bailout_for_ofi_ret(ncompleted, "fi_cq_{,s}read");

    if (ncompleted != 1) {
        errx(EXIT_FAILURE, "%s: expected 1 completion, read %zd", __func__,
             ncompleted);
    }

    if ((completion.flags & desired_rx_flags) != desired_rx_flags) {
        errx(EXIT_FAILURE,
             "%s: expected flags %" PRIu64 ", received flags %" PRIu64,
             __func__, desired_rx_flags, completion.flags & desired_rx_flags);
    }

    gs = completion.op_context;
    r = &gs->rcvr;

    if (completion.len != sizeof(r->initial.msg)) {
        errx(EXIT_FAILURE, "initially received %zu bytes, expected %zu",
             completion.len, sizeof(r->initial.msg));
    }

    if (r->initial.msg.nsources != global_state.total_sessions ||
        r->initial.msg.id > global_state.total_sessions) {
        errx(EXIT_FAILURE,
             "received nsources %" PRIu32 ", id %" PRIu32 ", expected %zu, 0",
             r->initial.msg.nsources, r->initial.msg.id,
             global_state.total_sessions);
    }

    rc = fi_av_insert(r->cxn.av, r->initial.msg.addr, 1, &r->cxn.peer_addr, 0,
                      NULL);

    if (rc < 0) {
        bailout_for_ofi_ret(rc, "fi_av_insert initial.msg.addr %p",
                            r->initial.msg.addr);
    } else if (rc != 1) {
        errx(EXIT_FAILURE, "%s: inserted %d addresses, expected 1", __func__,
             rc);
    }

    struct fi_info *ep_info, *hints = fi_dupinfo(global_state.info);

    hints->dest_addr = r->initial.msg.addr;
    hints->dest_addrlen = r->initial.msg.addrlen;
    hints->src_addr = NULL;
    hints->src_addrlen = 0;

    rc = fi_getinfo(FI_VERSION(1, 13), NULL, NULL, 0, hints, &ep_info);

    if ((rc = fi_endpoint(global_state.domain, ep_info, &r->cxn.ep, NULL)) < 0)
        bailout_for_ofi_ret(rc, "fi_endpoint");

    hints->dest_addr = NULL; // fi_freeinfo wants to free(3) dest_addr
    hints->dest_addrlen = 0;
    fi_freeinfo(hints);

    fi_freeinfo(ep_info);

    if ((rc = fi_cq_open(global_state.domain, &cq_attr, &r->cxn.cq, &r->cxn)) !=
        0)
        bailout_for_ofi_ret(rc, "fi_cq_open");

    if (global_state.waitfd) {
        int fd;

        rc = fi_control(&r->cxn.cq->fid, FI_GETWAIT, &fd);

        if (rc != 0)
            bailout_for_ofi_ret(rc, "fi_control(,FI_GETWAIT,)");

        r->cxn.cq_wait_fd = fd;
    }

    if ((rc = fi_ep_bind(r->cxn.ep, &r->cxn.cq->fid,
                         FI_SELECTIVE_COMPLETION | FI_RECV | FI_TRANSMIT)) != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind");

    if ((rc = fi_ep_bind(r->cxn.ep, &r->cxn.av->fid, 0)) != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind (address vector)");

    if ((rc = fi_enable(r->cxn.ep)) != 0)
        bailout_for_ofi_ret(rc, "fi_enable");

    size_t addrlen = sizeof(r->ack.msg.addr);

    if ((rc = fi_getname(&r->cxn.ep->fid, r->ack.msg.addr, &addrlen)) != 0)
        bailout_for_ofi_ret(rc, "fi_getname");

    r->ack.msg.addrlen = (uint32_t) addrlen;

    return gs;
}

static put_state_t *
put_state_open(void)
{
    struct fi_av_attr av_attr = {.type = FI_AV_UNSPEC,
                                 .rx_ctx_bits = 0,
                                 .count = 0,
                                 .ep_per_node = 0,
                                 .name = NULL,
                                 .map_addr = NULL,
                                 .flags = 0};
    put_state_t *pst;
    int rc;

    if ((pst = calloc(1, sizeof(*pst))) == NULL)
        errx(EXIT_FAILURE, "%s: failed to allocate put state", __func__);

    pst->session = calloc(global_state.local_sessions, sizeof(*pst->session));

    if (pst->session == NULL)
        errx(EXIT_FAILURE, "%s: failed to allocate sessions", __func__);

    rc = fi_av_open(global_state.domain, &av_attr, &pst->av, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_av_open");

    assert(global_state.peer_addr != NULL);

    struct fi_info *addr_info, *hints = fi_dupinfo(global_state.info);

    size_t nbytes;
    uint8_t *addrbytes = hex_string_to_bytes(global_state.peer_addr, &nbytes);
    if (addrbytes == NULL) {
        errx(EXIT_FAILURE, "%s: could not decode hex address '%s'", __func__,
             global_state.peer_addr);
    }
    hints->dest_addr = addrbytes;
    hints->dest_addrlen = nbytes;
    hints->addr_format = FI_FORMAT_UNSPEC;

    rc = fi_getinfo(FI_VERSION(1, 13), NULL, NULL, 0, hints, &addr_info);

    if (rc < 0) {
        bailout_for_ofi_ret(rc, "fi_getinfo for peer_addr %s",
                            global_state.peer_addr);
    }

    hints->dest_addr = NULL;
    hints->dest_addrlen = 0;
    fi_freeinfo(hints);

    rc = fi_av_insert(pst->av, addr_info->dest_addr, 1, &pst->peer_addr, 0,
                      NULL);

    if (rc < 0) {
        bailout_for_ofi_ret(rc, "fi_av_insert dest_addr %p",
                            global_state.info->dest_addr);
    } else if (rc != 1) {
        errx(EXIT_FAILURE, "%s: inserted %d addresses, expected 1 (%s)",
             __func__, rc, global_state.peer_addr);
    }

    return pst;
}

static void
emit_address(uint8_t *buf, size_t len)
{
    char *hexstr = bytes_to_hex_string(buf, len);
    int fd;

    FILE *file;
    const char *address_filename = global_state.address_filename;
    char template[] = "fabtget.XXXXXX";

    if (address_filename == NULL) {
        file = NULL;
    } else if ((fd = mkstemp(template)) == -1) {
        err(EXIT_FAILURE, "%s: mkstemp", __func__);
    } else if ((file = fdopen(fd, "w")) == NULL) // does not `dup(fd)`
        err(EXIT_FAILURE, "%s: fdopen", __func__);

    fprintf((file == NULL) ? stdout : file, "%s\n", hexstr);

    free(hexstr);

    if (file == NULL)
        return;

    fclose(file); // closes `fd`
    if (link(template, address_filename) == 0)
        ;
    else if (errno != EEXIST) {
        (void) unlink(template);
        err(EXIT_FAILURE, "%s: failed to move temporary file `%s` to `%s`",
            __func__, template, address_filename);
    } else if (unlink(address_filename) == -1) {
        (void) unlink(template);
        err(EXIT_FAILURE, "%s: failed to remove address file `%s`", __func__,
            address_filename);
    } else if (link(template, address_filename) == -1) {
        (void) unlink(template);
        err(EXIT_FAILURE, "%s: failed to move temporary file `%s` to `%s`",
            __func__, template, address_filename);
    }
    if (unlink(template) == -1) {
        warn("%s: failed to unlink temporary file `%s`", __func__, template);
    }
}

static get_state_t *
get_state_open(void)
{
    struct fi_av_attr av_attr = {.type = FI_AV_UNSPEC,
                                 .rx_ctx_bits = 0,
                                 .count = 0,
                                 .ep_per_node = 0,
                                 .name = NULL,
                                 .map_addr = NULL,
                                 .flags = 0};
    struct fi_cq_attr cq_attr = {.size = 128,
                                 .flags = 0,
                                 .format = FI_CQ_FORMAT_MSG,
                                 .wait_obj = global_state.waitfd ? FI_WAIT_FD
                                                                 : FI_WAIT_NONE,
                                 .signaling_vector = 0,
                                 .wait_cond = FI_CQ_COND_NONE,
                                 .wait_set = NULL};
    get_state_t *gst;
    int rc;

    if ((gst = calloc(1, sizeof(*gst))) == NULL)
        errx(EXIT_FAILURE, "%s: failed to allocate get state", __func__);

    gst->session = calloc(global_state.total_sessions, sizeof(*gst->session));

    if (gst->session == NULL)
        errx(EXIT_FAILURE, "%s: failed to allocate sessions", __func__);

    rc = fi_av_open(global_state.domain, &av_attr, &gst->av, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_av_open");

    if ((rc = fi_endpoint(global_state.domain, global_state.info,
                          &gst->listen_ep, NULL)) != 0)
        bailout_for_ofi_ret(rc, "fi_endpoint");

    if ((rc = fi_cq_open(global_state.domain, &cq_attr, &gst->listen_cq,
                         NULL)) != 0)
        bailout_for_ofi_ret(rc, "fi_cq_open");

    if ((rc = fi_ep_bind(gst->listen_ep, &gst->listen_cq->fid,
                         FI_SELECTIVE_COMPLETION | FI_RECV | FI_TRANSMIT)) != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind (completion queue)");

    if ((rc = fi_ep_bind(gst->listen_ep, &gst->av->fid, 0)) != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind (address vector)");

    if ((rc = fi_enable(gst->listen_ep)) != 0)
        bailout_for_ofi_ret(rc, "fi_enable");

    struct {
        uint8_t buf[512];
        size_t len;
    } raw;
    raw.len = sizeof(raw.buf);

    if ((rc = fi_getname(&gst->listen_ep->fid, raw.buf, &raw.len)) != 0)
        bailout_for_ofi_ret(rc, "fi_getname");

    if (raw.len > sizeof(raw.buf))
        errx(EXIT_FAILURE, "%s: raw-address buffer too small", __func__);

    emit_address(raw.buf, raw.len);

    return gst;
}

static int
get(void)
{
    get_state_t *gst;
    rcvr_t *r;
    sink_t *s;
    get_session_t *gs;
    worker_t *w;
    size_t i;

    gst = get_state_open();

    for (i = 0; i < global_state.total_sessions; i++) {
        gs = &gst->session[i];

        r = &gs->rcvr;
        s = &gs->sink;

        rcvr_init(r, gst->listen_ep, gst->av);
        sink_init(s);

        post_initial_rx(gst->listen_ep, gs);
    }

    for (i = 0; i < global_state.total_sessions; i++) {
        gs = get_session_accept(gst);

        r = &gs->rcvr;
        s = &gs->sink;

        rcvr_ack_msg_init(r, r->cxn.ep);
        rcvr_buffers_init(r);
        if (!session_init(&gs->sess, &r->cxn, &s->terminal))
            errx(EXIT_FAILURE, "%s: failed to initialize session", __func__);
    }

    for (i = 0; i < global_state.total_sessions; i++) {
        gs = &gst->session[i];

        if ((w = workers_assign_session(gs->sess)) == NULL) {
            errx(EXIT_FAILURE,
                 "%s: could not assign a new receiver to a worker", __func__);
        }
    }

    return workers_join_all();
}

static void
put_session_setup(put_state_t *pst, put_session_t *ps)
{
    struct fi_cq_attr cq_attr = {.size = 128,
                                 .flags = 0,
                                 .format = FI_CQ_FORMAT_MSG,
                                 .wait_obj = global_state.waitfd ? FI_WAIT_FD
                                                                 : FI_WAIT_NONE,
                                 .signaling_vector = 0,
                                 .wait_cond = FI_CQ_COND_NONE,
                                 .wait_set = NULL};
    xmtr_t *x = &ps->xmtr;
    int rc;

    if ((rc = fi_endpoint(global_state.domain, global_state.info, &x->cxn.ep,
                          NULL)) != 0)
        bailout_for_ofi_ret(rc, "fi_endpoint");

    if ((rc = fi_cq_open(global_state.domain, &cq_attr, &x->cxn.cq, &x->cxn)) !=
        0)
        bailout_for_ofi_ret(rc, "fi_cq_open");

    if (global_state.waitfd) {
        int fd;

        rc = fi_control(&x->cxn.cq->fid, FI_GETWAIT, &fd);

        if (rc != 0)
            bailout_for_ofi_ret(rc, "fi_control(,FI_GETWAIT,)");

        x->cxn.cq_wait_fd = fd;
    }

    if ((rc = fi_ep_bind(x->cxn.ep, &x->cxn.cq->fid,
                         FI_SELECTIVE_COMPLETION | FI_RECV | FI_TRANSMIT)) != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind");

    if ((rc = fi_ep_bind(x->cxn.ep, &pst->av->fid, 0)) != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind (address vector)");

    if ((rc = fi_enable(x->cxn.ep)) != 0)
        bailout_for_ofi_ret(rc, "fi_enable");

    x->cxn.peer_addr = pst->peer_addr;

    /* Setup initial message. */
    memset(&x->initial.msg, 0, sizeof(x->initial.msg));
    x->initial.msg.nsources = global_state.total_sessions;
    x->initial.msg.id = 0;

    x->initial.desc = fi_mr_desc(x->initial.mr);

    size_t addrlen = sizeof(x->initial.msg.addr);

    if ((rc = fi_getname(&x->cxn.ep->fid, x->initial.msg.addr, &addrlen)) != 0)
        bailout_for_ofi_ret(rc, "fi_getname");

    assert(addrlen <= sizeof(x->initial.msg.addr));
    x->initial.msg.addrlen = (uint32_t) addrlen;

    /* Post receive for connection acknowledgement. */
    x->ack.desc = fi_mr_desc(x->ack.mr);

    xfer_context_t *xfc = &x->ack.xfc;

    xfc->type = xft_ack;
    xfc->owner = xfo_nic;
    xfc->place = xfp_first | xfp_last;
    xfc->nchildren = 0;
    xfc->cancelled = 0;

    rc = fi_recvmsg(
        x->cxn.ep,
        &(struct fi_msg){.msg_iov =
                             &(struct iovec){.iov_base = &x->ack.msg,
                                             .iov_len = sizeof(x->ack.msg)},
                         .desc = &x->ack.desc,
                         .iov_count = 1,
                         .addr = x->cxn.peer_addr,
                         .context = xfc,
                         .data = 0},
        FI_COMPLETION);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_recvmsg");
}

static int
put(void)
{
    put_state_t *pst;
    put_session_t *ps;
    worker_t *w;
    size_t i;

    pst = put_state_open();

    for (i = 0; i < global_state.local_sessions; i++) {
        ps = &pst->session[i];
        xmtr_t *x = &ps->xmtr;
        source_t *s = &ps->source;
        xmtr_init(x, pst->av);
        source_init(s);

        if (!session_init(&ps->sess, &x->cxn, &s->terminal))
            errx(EXIT_FAILURE, "%s: failed to initialize session", __func__);

        xmtr_buffers_init(x);
        put_session_setup(pst, ps);
    }

    for (i = 0; i < global_state.local_sessions; i++) {
        ps = &pst->session[i];

        if ((w = workers_assign_session(ps->sess)) == NULL) {
            errx(EXIT_FAILURE,
                 "%s: could not assign a new transmitter to a worker",
                 __func__);
        }
    }

    return workers_join_all();
}

static int
count_info(const struct fi_info *first)
{
    int count;
    const struct fi_info *info;

    for (info = first, count = 0; info != NULL; count++, info = info->next) {
        fprintf(stderr, "%s: info %d provider \"%s\"", __func__, count,
                info->fabric_attr->prov_name);
    }

    return count;
}

static const char *
personality_to_name(personality_t p)
{
    if (p == get)
        return "fabtget";
    else if (p == put)
        return "fabtput";
    else
        return "unknown";
}

/* Note that there is a doc/usage.md file that mirrors this, so please keep
 * that file in sync with this one.
 */
static void
usage(personality_t personality, const char *progname)
{
    const char *common1 = "[-c]";
    const char *common2 = "[-n <n>] [-p '<i> - <j>' ] [-r] [-w]";

    fprintf(stderr, "\n");
    fprintf(stderr, "USAGE:\n");
    fprintf(stderr, "\n");

    if (personality == put) {
        fprintf(stderr, "    %s %s [-g] [-h] [-k <k>] %s <remote_address>\n",
                progname, common1, common2);
    } else {
        fprintf(stderr, "    %s [-a <address-file>] [-h] %s %s\n", progname,
                common1, common2);
    }
    fprintf(stderr, "\n");

    if (personality == get) {
        fprintf(stderr, "    -a <address-file>\n");
        fprintf(stderr, "        dump address to file <address-file> "
                        "(otherwise goes to stdout)\n");
        fprintf(stderr, "\n");
    }

    fprintf(stderr, "    -c\n");
    fprintf(stderr, "        Expect cancellation by a signal. Use exit code 0 "
                    "(success) if the\n");
    fprintf(stderr, "        program is cancelled by a signal (SIGHUP, -INT, "
                    "-QUIT, -TERM). Use\n");
    fprintf(stderr, "        exit code 1 (failure), otherwise.\n");
    fprintf(stderr, "\n");

    if (personality == put) {
        fprintf(stderr, "    -g\n");
        fprintf(stderr, "        RDMA-write only from contiguous buffers "
                        "(default is scatter-gather RDMA)\n");
        fprintf(stderr, "\n");
    }

    fprintf(stderr, "    -h\n");
    fprintf(stderr, "        print this help message\n");
    fprintf(stderr, "\n");

    if (personality == put) {
        fprintf(stderr, "    -k <k>\n");
        fprintf(stderr, "        Start only k transmit sessions. Use this "
                        "option with -n n. k may\n");
        fprintf(stderr, "        not exceed n.\n");
        fprintf(stderr, "\n");
    }

    fprintf(stderr, "    -n\n");
    fprintf(stderr, "        Tell the peer to expect that between this process "
                    "and the other fabtput\n");
    fprintf(stderr, "        processes will establish n transmit sessions with "
                    "the peer. Unless a\n");
    fprintf(stderr, "        -k k argument (fabtput only) says otherwise, the "
                    "new fabtput process\n");
    fprintf(stderr, "        will start all n sessions.\n");
    fprintf(stderr, "\n");

    fprintf(stderr, "    -p '<i> - <j>'\n");
    fprintf(stderr, "        pin worker threads to processors i through j\n");
    fprintf(stderr, "\n");

    fprintf(stderr, "    -r\n");
    fprintf(stderr,
            "        deregister/(r)eregister each RDMA buffer before reuse\n");
    fprintf(stderr, "\n");

    fprintf(stderr, "    -v\n");
    fprintf(stderr, "        verbose mode (dumps extra info to stderr)\n");
    fprintf(stderr, "\n");

    fprintf(stderr, "    -w\n");
    fprintf(stderr, "        wait for I/O using epoll_pwait(2) instead of "
                    "polling in a busy loop\n");
    fprintf(stderr, "        with fi_poll(3)\n");
    fprintf(stderr, "\n");

    if (personality == put) {
        fprintf(stderr, "    <remote_address>\n");
        fprintf(stderr,
                "        tells the host where the peer fabtget process runs\n");
        fprintf(stderr, "\n");
    }

    fprintf(stderr, "NOTES:\n");
    fprintf(stderr, "\n");

    fprintf(stderr, "    To run in 'cacheless' mode, set the "
                    "FI_MR_CACHE_MAX_SIZE environment\n");
    fprintf(stderr,
            "    variable to 0 to disable the memory registration cache.\n");
    fprintf(stderr, "\n");
}

/* Handler for SIGUSR1, used to wake a thread from epoll(2) so that
 * it can pick up new sessions.
 */
static void
handle_wakeup(transfer_unused int signum, transfer_unused siginfo_t *info,
              transfer_unused void *ucontext)
{
}

/* Handler for SIG{HUP,INT,QUIT,TERM}, used to cancel the program. */
static void
handle_cancel(transfer_unused int signum, transfer_unused siginfo_t *info,
              transfer_unused void *ucontext)
{
}

static void *
await_cancellation(void transfer_unused *arg)
{
    sigset_t cancelset;
    size_t i;

    if (sigemptyset(&cancelset) == -1)
        err(EXIT_FAILURE, "%s.%d: sigemptyset", __func__, __LINE__);

    for (i = 0; i < arraycount(siglist); i++) {
        if (sigaddset(&cancelset, siglist[i].signum) == -1)
            err(EXIT_FAILURE, "%s.%d: sigaddset", __func__, __LINE__);
    }

    if (sigaddset(&cancelset, SIGUSR1) == -1)
        err(EXIT_FAILURE, "%s.%d: sigaddset", __func__, __LINE__);

    for (;;) {
        int rc, sig;

        if ((rc = sigwait(&cancelset, &sig)) != 0) {
            errx(EXIT_FAILURE, "%s.%d: sigwait: %s", __func__, __LINE__,
                 strerror(rc));
        }

        /* Join the main thread when SIGUSR1 arrives. */
        if (sig == SIGUSR1)
            break;

        /* Ignore repeat cancellations. */
        if (global_state.cancelled)
            continue;

        global_state.cancelled = true;

        if ((rc = pthread_kill(global_state.main_thd, SIGUSR2)) != 0) {
            errx(EXIT_FAILURE,
                 "%s: could not signal thread for main thread: %s", __func__,
                 strerror(rc));
        }

        (void) pthread_mutex_lock(&workers_mtx);

        for (i = 0; i < nworkers_running; i++) {
            worker_t *w = &workers[i];

            (void) pthread_cond_signal(&w->sleep);

            /*
             * Wake each worker with SIGUSR1 if blocking in epoll_pwait(2)
             * is allowed.
             */
            if (global_state.waitfd &&
                (rc = pthread_kill(w->thd, SIGUSR1)) != 0) {
                errx(EXIT_FAILURE,
                     "%s: could not signal thread for worker %p: %s", __func__,
                     (void *) w, strerror(rc));
            }
        }

        (void) pthread_cond_signal(&nworkers_cond);

        (void) pthread_mutex_unlock(&workers_mtx);
    }
    return NULL;
}

static size_t
parse_nsessions(const char *s, char flagname)
{
    char *end;
    uintmax_t n;

    errno = 0;
    n = strtoumax(s, &end, 0);
    if (n < 1 || SIZE_MAX < n) {
        errx(EXIT_FAILURE, "`-%c` parameter `%s` is out of range", flagname, s);
    }
    if (end == s) {
        errx(EXIT_FAILURE, "could not parse `-%c` parameter `%s`", flagname, s);
    }
    return (size_t) n;
}

int
main(int argc, char **argv)
{
    sigset_t oldset, blockset, usr2set;
    struct fi_info *hints;
    char *progname, *tmp;
    size_t i;
    int ecode, opt, ninput, rc;
    struct {
        bool k, n;
    } set = {.k = false, .n = false};
    bool verbose = false;

    if ((tmp = strdup(argv[0])) == NULL)
        err(EXIT_FAILURE, "%s: strdup", __func__);

    progname = basename(tmp);

    if (strcmp(progname, "fabtget") == 0) {
        global_state.personality = get;
    } else if (strcmp(progname, "fabtput") == 0) {
        global_state.personality = put;
    } else {
        errx(EXIT_FAILURE, "program personality '%s' is not implemented",
             progname);
    }

    const char *optstring =
        (global_state.personality == get) ? "a:chn:p:rw" : "cghk:n:p:rw";

    while ((opt = getopt(argc, argv, optstring)) != -1) {
        switch (opt) {
            case 'a':
                if ((global_state.address_filename = strdup(optarg)) == NULL) {
                    err(EXIT_FAILURE, "%s: could not set address filename",
                        __func__);
                }
                break;
            case 'c':
                global_state.expect_cancellation = true;
                break;
            case 'g':
                global_state.contiguous = true;
                break;
            case 'h':
                usage(global_state.personality, progname);
                exit(EXIT_SUCCESS);
            case 'k':
                set.k = true;
                global_state.local_sessions = parse_nsessions(optarg, 'k');
                break;
            case 'n':
                set.n = true;
                global_state.total_sessions = parse_nsessions(optarg, 'n');
                break;
            case 'p':
                ninput = 0;
                (void) sscanf(optarg, "%u - %u%n",
                              &global_state.processors.first,
                              &global_state.processors.last, &ninput);
                if (optarg[ninput] != '\0')
                    errx(EXIT_FAILURE, "unexpected `-p` parameter `%s`",
                         optarg);
                if (INT_MAX < global_state.processors.first ||
                    INT_MAX < global_state.processors.last)
                    errx(EXIT_FAILURE, "unexpected `-p` parameter `%s`",
                         optarg);
                break;
            case 'r':
                global_state.reregister = true;
                break;
            case 'v':
                verbose = true;
                break;
            case 'w':
                global_state.waitfd = true;
                break;
            default:
                usage(global_state.personality, progname);
                exit(EXIT_FAILURE);
        }
    }

    argc -= optind;
    argv += optind;

    global_state.nextcpu = (int) global_state.processors.first;

    if (global_state.personality == put) {
        if (argc != 1) {
            usage(global_state.personality, progname);
            exit(EXIT_FAILURE);
        }
        if (strlen(argv[0]) >= sizeof(global_state.peer_addr_buf))
            errx(EXIT_FAILURE, "destination address too long");
        strcpy(global_state.peer_addr_buf, argv[0]);
        global_state.peer_addr = global_state.peer_addr_buf;
    } else if (argc != 0) {
        usage(global_state.personality, progname);
        exit(EXIT_FAILURE);
    }

    if (!set.k && set.n) {
        global_state.local_sessions = global_state.total_sessions;
    } else if (set.k && !set.n) {
        global_state.total_sessions = global_state.local_sessions;
    } else if (set.k && set.n) {
        if (global_state.total_sessions < global_state.local_sessions) {
            warnx("-k argument must not exceed -n argument");
            usage(global_state.personality, progname);
            exit(EXIT_FAILURE);
        }
    }

    workers_initialize();

    seqsource_init(&global_state.keys);

    if (verbose) {
        fprintf(stderr, "%ld POSIX I/O vector items maximum",
                sysconf(_SC_IOV_MAX));
    }

    if ((hints = fi_allocinfo()) == NULL)
        errx(EXIT_FAILURE, "%s: fi_allocinfo", __func__);

    hints->ep_attr->type = FI_EP_RDM;
    hints->caps |= FI_MSG | FI_TAGGED;
    hints->caps |= FI_RMA;
    hints->caps |= FI_REMOTE_READ | FI_READ | FI_REMOTE_WRITE | FI_WRITE;
    hints->mode = FI_CONTEXT;
    /* FI_MR_ENDPOINT is *required* by cxi; `FI_MR_UNSPEC` will not do. */
    hints->domain_attr->mr_mode = FI_MR_ENDPOINT | FI_MR_PROV_KEY;

    if (verbose) {
        fprintf(stderr, "hints:\n%s", fi_tostr(hints, FI_TYPE_INFO));
    }

    global_state.info = NULL;
    rc =
        fi_getinfo(FI_VERSION(1, 13), NULL, NULL, 0, hints, &global_state.info);

    fi_freeinfo(hints);

    switch (-rc) {
        case FI_ENODATA:
            fprintf(stderr, "libfabric capabilities not available?");
            break;
        case FI_ENOSYS:
            fprintf(stderr, "available libfabric version < 1.13?");
            break;
        default:
            break;
    }

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_getinfo");

    if (verbose) {
        fprintf(stderr, "%d infos found", count_info(global_state.info));
    }

    if (verbose) {
        fprintf(stderr, "global_state.info: %s",
                fi_tostr(global_state.info, FI_TYPE_INFO));
    }

    if ((global_state.info->domain_attr->mr_mode & FI_MR_ENDPOINT) != 0)
        global_state.mr_endpoint = true;

    if (verbose && (global_state.info->mode & FI_CONTEXT) != 0) {
        fprintf(stderr,
                "contexts must embed fi_context; good thing %s does that.",
                progname);
    }

    if (sigemptyset(&blockset) == -1)
        err(EXIT_FAILURE, "%s.%d: sigemptyset", __func__, __LINE__);

    if (sigemptyset(&usr2set) == -1)
        err(EXIT_FAILURE, "%s.%d: sigemptyset", __func__, __LINE__);

    for (i = 0; i < arraycount(siglist); i++) {
        if (sigaddset(&blockset, siglist[i].signum) == -1)
            err(EXIT_FAILURE, "%s.%d: sigaddset", __func__, __LINE__);
    }

    if (sigaddset(&blockset, SIGUSR1) == -1)
        err(EXIT_FAILURE, "%s.%d: sigaddset", __func__, __LINE__);

    if (sigaddset(&blockset, SIGUSR2) == -1)
        err(EXIT_FAILURE, "%s.%d: sigaddset", __func__, __LINE__);

    if (sigaddset(&usr2set, SIGUSR2) == -1)
        err(EXIT_FAILURE, "%s.%d: sigaddset", __func__, __LINE__);

    if ((rc = pthread_sigmask(SIG_BLOCK, &blockset, &oldset)) != 0) {
        errx(EXIT_FAILURE, "%s.%d: pthread_sigmask: %s", __func__, __LINE__,
             strerror(rc));
    }

    struct sigaction cancel_action = {.sa_sigaction = handle_cancel,
                                      .sa_flags = SA_SIGINFO};

    if (sigemptyset(&cancel_action.sa_mask) == -1)
        err(EXIT_FAILURE, "%s.%d: sigaddset", __func__, __LINE__);

    for (i = 0; i < arraycount(siglist); i++) {
        if (sigaction(siglist[i].signum, &cancel_action,
                      &siglist[i].saved_action) == -1)
            err(EXIT_FAILURE, "%s.%d: sigaddset", __func__, __LINE__);
    }

    struct sigaction wakeup_action = {.sa_sigaction = handle_wakeup,
                                      .sa_flags = SA_SIGINFO};

    if (sigemptyset(&wakeup_action.sa_mask) == -1)
        err(EXIT_FAILURE, "%s.%d: sigaddset", __func__, __LINE__);

    if (sigaction(SIGUSR1, &wakeup_action, &saved_wakeup1_action) == -1)
        err(EXIT_FAILURE, "%s.%d: sigaddset", __func__, __LINE__);

    if (sigaction(SIGUSR2, &wakeup_action, &saved_wakeup2_action) == -1)
        err(EXIT_FAILURE, "%s.%d: sigaddset", __func__, __LINE__);

    if ((rc = pthread_create(&global_state.cancel_thd, NULL, await_cancellation,
                             NULL)) != 0) {
        warnx("%s.%d: pthread_create: %s", __func__, __LINE__, strerror(rc));
        ecode = EXIT_FAILURE;
        goto out;
    }

    rc = fi_fabric(global_state.info->fabric_attr, &global_state.fabric,
                   NULL /* app context */);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_fabric");

    rc = fi_domain(global_state.fabric, global_state.info, &global_state.domain,
                   NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_domain");

    if (verbose) {
        fprintf(stderr, "provider %s, memory-registration I/O vector limit %zu",
                global_state.info->fabric_attr->prov_name,
                global_state.info->domain_attr->mr_iov_limit);

        fprintf(
            stderr,
            "provider %s %s application-requested memory-registration keys",
            global_state.info->fabric_attr->prov_name,
            ((global_state.info->domain_attr->mr_mode & FI_MR_PROV_KEY) != 0)
                ? "does not support"
                : "supports");
    }

    if ((global_state.info->domain_attr->mr_mode & FI_MR_VIRT_ADDR) != 0) {
        fprintf(stderr,
                "provider %s RDMA uses virtual addresses instead of offsets, "
                "quitting.",
                global_state.info->fabric_attr->prov_name);
        exit(EXIT_FAILURE);
    }

    if (verbose) {
        fprintf(stderr, "Rx/Tx I/O vector limits %zu/%zu",
                global_state.info->rx_attr->iov_limit,
                global_state.info->tx_attr->iov_limit);

        fprintf(stderr, "RMA I/O vector limit %zu",
                global_state.info->tx_attr->rma_iov_limit);
    }

    /* Always use 1 because there are problems with using mr_maxsegs > 1,
     * i.e., global_state.info->domain_attr->mr_iov_limit.
     */
    global_state.mr_maxsegs = 1;
    global_state.rx_maxsegs = 1;
    global_state.tx_maxsegs = 1;
    global_state.rma_maxsegs =
        global_state.contiguous ? 1 : global_state.info->tx_attr->rma_iov_limit;

    if (verbose) {
        fprintf(stderr, "maximum endpoint message size (RMA limit) 0x%zx",
                global_state.info->ep_attr->max_msg_size);

        fprintf(stderr, "starting personality '%s'",
                personality_to_name(global_state.personality));
    }

    global_state.main_thd = pthread_self();

    if ((rc = pthread_sigmask(SIG_UNBLOCK, &usr2set, NULL)) != 0) {
        warnx("%s.%d: pthread_sigmask: %s", __func__, __LINE__, strerror(rc));
        ecode = EXIT_FAILURE;
        goto out;
    }

    ecode = (*global_state.personality)();

    if ((rc = pthread_kill(global_state.cancel_thd, SIGUSR1)) != 0) {
        warnx("%s.%d: pthread_kill: %s", __func__, __LINE__, strerror(rc));
    }

    if ((rc = pthread_join(global_state.cancel_thd, NULL)) != 0) {
        warnx("%s.%d: pthread_join: %s", __func__, __LINE__, strerror(rc));
    }

out:

    if ((rc = pthread_sigmask(SIG_SETMASK, &oldset, NULL)) != 0) {
        errx(EXIT_FAILURE, "%s.%d: pthread_sigmask: %s", __func__, __LINE__,
             strerror(rc));
    }

    return ecode;
}
