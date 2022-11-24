/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <rdma/fi_domain.h>

#include "fabtsuite_buffer.h"
#include "fabtsuite_error.h"
#include "fabtsuite_types.h"
#include "fabtsuite_util.h"

bufhdr_t *
buflist_get(buflist_t *bl)
{
    if (bl->nfull == 0)
        return NULL;

    return bl->buf[--bl->nfull];
}

bool
buflist_put(buflist_t *bl, bufhdr_t *h)
{
    if (bl->nfull == bl->nallocated)
        return false;

    bl->buf[bl->nfull++] = h;
    return true;
}

buflist_t *
buflist_create(size_t n)
{
    buflist_t *bl = malloc(offsetof(buflist_t, buf) + sizeof(bl->buf[0]) * n);

    if (bl == NULL)
        return NULL;

    bl->nallocated = n;
    bl->nfull = 0;

    return bl;
}

bufhdr_t *
buf_alloc(size_t paylen)
{
    bufhdr_t *h;

    if ((h = calloc(1, offsetof(bytebuf_t, payload[0]) + paylen)) == NULL)
        return NULL;

    h->nallocated = paylen;

    return h;
}

void
buf_free(bufhdr_t *h)
{
    free(h);
}

bytebuf_t *
bytebuf_alloc(size_t paylen)
{
    return (bytebuf_t *) buf_alloc(paylen);
}

fragment_t *
fragment_alloc(void)
{
    bufhdr_t *h;
    fragment_t *f;

    if ((h = buf_alloc(sizeof(*f) - sizeof(bufhdr_t))) == NULL)
        return NULL;

    f = (fragment_t *) h;
    h->xfc.type = xft_fragment;

    return f;
}

progbuf_t *
progbuf_alloc(void)
{
    bufhdr_t *h;
    progbuf_t *pb;

    if ((h = buf_alloc(sizeof(*pb) - sizeof(bufhdr_t))) == NULL)
        return NULL;

    pb = (progbuf_t *) h;
    h->xfc.type = xft_progress;

    return pb;
}

bool
progbuf_is_wellformed(progbuf_t *pb)
{
    return pb->hdr.nused == sizeof(pb->msg);
}

vecbuf_t *
vecbuf_alloc(void)
{
    bufhdr_t *h;
    vecbuf_t *vb;

    if ((h = buf_alloc(sizeof(*vb) - sizeof(bufhdr_t))) == NULL)
        return NULL;

    vb = (vecbuf_t *) h;
    vb->msg.pad = 0;
    h->xfc.type = xft_vector;

    return vb;
}

void
vecbuf_free(vecbuf_t *vb)
{
    buf_free(&vb->hdr);
}

bool
vecbuf_is_wellformed(vecbuf_t *vb)
{
    size_t len = vb->hdr.nused;
    static const size_t least_vector_msglen = offsetof(vector_msg_t, iov[0]);

    const size_t niovs_space =
        (len - least_vector_msglen) / sizeof(vb->msg.iov[0]);

    if (len < least_vector_msglen) {
        fprintf(stderr, "%s: expected >= %zu bytes, received %zu", __func__,
                least_vector_msglen, len);
    } else if ((len - least_vector_msglen) % sizeof(vb->msg.iov[0]) != 0) {
        fprintf(stderr,
                "%s: %zu-byte vector message did not end on vector boundary, "
                "disconnecting...",
                __func__, len);
    } else if (niovs_space < vb->msg.niovs) {
        fprintf(stderr, "%s: peer sent truncated vectors, disconnecting...",
                __func__);
    } else if (vb->msg.niovs > arraycount(vb->msg.iov)) {
        fprintf(stderr, "%s: peer sent too many vectors, disconnecting...",
                __func__);
    } else
        return true;

    return false;
}

/*
 * Registration functions
 */

int
buf_mr_bind(bufhdr_t *h, struct fid_ep *ep)
{
    int rc;

    if (ep == NULL)
        h->ep = NULL;
    else if (global_state.mr_endpoint &&
             (rc = fi_mr_bind(h->mr, &ep->fid, 0)) != 0)
        return rc;
    else if (global_state.mr_endpoint && (rc = fi_mr_enable(h->mr)) != 0)
        return rc;
    else
        h->ep = ep;

    h->desc = fi_mr_desc(h->mr);

    return 0;
}

int
buf_mr_dereg(bufhdr_t *h)
{
    struct fid_mr *mr = h->mr;

    if (mr == NULL)
        return 0;

    h->mr = NULL;
    return fi_close(&mr->fid);
}

int
buf_mr_reg(struct fid_domain *dom, struct fid_ep *ep, uint64_t access,
           uint64_t key, bufhdr_t *h)
{
    int rc;
    bytebuf_t *b = (bytebuf_t *) h;

    rc = fi_mr_reg(dom, &b->payload[0], h->nallocated, access, 0, key, 0,
                   &h->mr, NULL);

    if (rc != 0) {
        h->mr = NULL;
        return rc;
    }

    if ((rc = buf_mr_bind(h, ep)) != 0) {
        (void) buf_mr_dereg(h);
        return rc;
    }

    return 0;
}

bufhdr_t *
progbuf_create_and_register(struct fid_ep *ep)
{
    progbuf_t *pb = progbuf_alloc();
    int rc;

    rc = buf_mr_reg(global_state.domain, ep, FI_SEND,
                    seqsource_get(&global_state.keys), &pb->hdr);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "buf_mr_reg");

    return &pb->hdr;
}

bufhdr_t *
vecbuf_create_and_register(struct fid_ep *ep)
{
    vecbuf_t *vb = vecbuf_alloc();
    int rc;

    rc = buf_mr_reg(global_state.domain, ep, FI_SEND,
                    seqsource_get(&global_state.keys), &vb->hdr);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "buf_mr_reg");

    return &vb->hdr;
}
