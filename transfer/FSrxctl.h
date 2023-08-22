/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FSrxctl.h
 */

#ifndef FSrxctl_H
#define FSrxctl_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#include <rdma/fi_domain.h>

#include "FStypes.h"

#include "FSbuffer.h"
#include "FSfifo.h"

#ifdef __cplusplus
extern "C" {
#endif

bool
rxctl_idle(rxctl_t *ctl);
bool
rxctl_ready(rxctl_t *ctl);
bufhdr_t *
rxctl_complete(rxctl_t *rc, const completion_t *cmpl);
void
rxctl_post(cxn_t *c, rxctl_t *ctl, bufhdr_t *h);
void
rxctl_init(rxctl_t *ctl, size_t len);
void
rxctl_cancel(struct fid_ep *ep, rxctl_t *ctl);

#ifdef __cplusplus
}
#endif

#endif
