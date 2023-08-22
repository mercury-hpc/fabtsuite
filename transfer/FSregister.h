/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FSregister.h
 *
 * Routines that register things with libfabric
 */

#ifndef FSregister_H
#define FSregister_H

#include <inttypes.h>
#include <stdlib.h>

#include <rdma/fi_domain.h>
#include <rdma/fi_rma.h>

#include "FStypes.h"

#ifdef __cplusplus
extern "C" {
#endif

int
mr_regv_all(struct fid_domain *domain, struct fid_ep *ep,
            const struct iovec *iov, size_t niovs, size_t maxsegs,
            uint64_t access, uint64_t offset, seqsource_t *keys, uint64_t flags,
            struct fid_mr **mrp, void **descp, uint64_t *raddrp, void *context);
int
mr_deregv_all(size_t niovs, size_t maxsegs, struct fid_mr **mrp);

#ifdef __cplusplus
}
#endif

#endif
