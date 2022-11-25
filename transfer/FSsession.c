/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <rdma/fi_domain.h>

#include "FStypes.h"

#include "FScxn.h"
#include "FSbuffer.h"
#include "FSerror.h"
#include "FSfifo.h"
#include "FSregister.h"
#include "FSsession.h"
#include "FSutil.h"

bool
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

void
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

loop_control_t
session_loop(worker_t *w, session_t *s)
{
    terminal_t *t = s->terminal;

    if (t->trade(t, s->ready_for_terminal, s->ready_for_cxn) == loop_error)
        return loop_error;

    return cxn_loop(w, s);
}

void
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


