/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdbool.h>
#include <stdlib.h>

#include <rdma/fi_domain.h>

#include "fabtsuite_types.h"

#include "fabtsuite_seqsource.h"

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
