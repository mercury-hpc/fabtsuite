/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdbool.h>
#include <stdlib.h>

#include <rdma/fi_domain.h>

#include "FStypes.h"

#include "FSseqsource.h"

void
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

loop_control_t
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


