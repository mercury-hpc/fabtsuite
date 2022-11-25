/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <rdma/fi_domain.h>
#include <rdma/fi_rma.h>

#include "FStypes.h"

#include "FSregister.h"
#include "FSseqsource.h"
#include "FSutil.h"

/* Register the `niovs`-segment I/O vector `iov` using up to `niovs`
 * of the registrations, descriptors, and remote addresses in the
 * vectors `mrp`, `descp`, and `raddrp`, respectively.  Register no
 * more than `maxsegs` segments in a single `fi_mr_regv` call.
 */
int
mr_regv_all(struct fid_domain *domain, struct fid_ep *ep,
            const struct iovec *iov, size_t niovs, size_t maxsegs,
            uint64_t access, uint64_t offset, seqsource_t *keys, uint64_t flags,
            struct fid_mr **mrp, void **descp, uint64_t *raddrp, void *context)
{
    int rc;
    size_t i, j, nregs = (niovs + maxsegs - 1) / maxsegs;
    size_t nleftover;

    for (nleftover = niovs, i = 0; i < nregs;
         iov += maxsegs, nleftover -= maxsegs, i++) {
        struct fid_mr *mr;
        uint64_t raddr = 0;

        size_t nsegs = minsize(nleftover, maxsegs);

        rc = fi_mr_regv(domain, iov, nsegs, access, offset, seqsource_get(keys),
                        flags, &mr, context);

        if (rc != 0)
            goto err;

        if (global_state.mr_endpoint &&
            (rc = fi_mr_bind(mr, &ep->fid, 0)) != 0) {
            fprintf(stderr, "%s: fi_mr_bind: %s", __func__, fi_strerror(-rc));
            goto err;
        }

        if (global_state.mr_endpoint && (rc = fi_mr_enable(mr)) != 0) {
            fprintf(stderr, "%s: fi_mr_enable: %s", __func__, fi_strerror(-rc));
            goto err;
        }

        for (j = 0; j < nsegs; j++) {
            mrp[i * maxsegs + j] = mr;
            descp[i * maxsegs + j] = fi_mr_desc(mr);
            raddrp[i * maxsegs + j] = raddr;
            raddr += iov[j].iov_len;
        }
    }

    return 0;

err:
    for (j = 0; j < i; j++)
        (void) fi_close(&mrp[j]->fid);

    return rc;
}

int
mr_deregv_all(size_t niovs, size_t maxsegs, struct fid_mr **mrp)
{
    size_t i, nregs = (niovs + maxsegs - 1) / maxsegs;
    int rc;

    for (i = 0; i < nregs; i++) {
        if ((rc = fi_close(&mrp[i]->fid)) < 0)
            return rc;
    }
    return 0;
}


