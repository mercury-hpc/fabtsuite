/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FStxctl.h
 */

#ifndef FStxctl_H
#define FStxctl_H

#include <inttypes.h>
#include <stdlib.h>

#include <rdma/fi_domain.h>

#include "FStypes.h"

#include "FSbuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

bool
txctl_idle(txctl_t *ctl);
bool
txctl_ready(txctl_t *ctl);
bool
txctl_put(txctl_t *ctl, bufhdr_t *h);
void
txctl_init(txctl_t *ctl, size_t len, size_t nbufs,
           bufhdr_t *(*create_and_register)(struct fid_ep *), struct fid_ep *ep);
void
txctl_cancel(struct fid_ep *ep, txctl_t *ctl);
int
txctl_complete(txctl_t *tc, const completion_t *cmpl);
void
txctl_transmit(cxn_t *c, txctl_t *tc);

#ifdef __cplusplus
}
#endif

#endif
