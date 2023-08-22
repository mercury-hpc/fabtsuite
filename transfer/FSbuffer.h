/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FSbuffer.h
 *
 * Buffer routines
 */

#ifndef FSbuffer_H
#define FSbuffer_H

#include <inttypes.h>
#include <stdlib.h>

#include <rdma/fi_domain.h>

#include <sys/epoll.h>

#include "FStypes.h"

struct bufhdr {
    xfer_context_t xfc;
    uint64_t raddr;
    size_t nused;
    size_t nallocated;
    struct fid_mr *mr;
    void *desc;
    uint64_t tag;
    struct fid_ep *ep;
    max_align_t pad;
};

struct buflist {
    uint64_t access;
    size_t nfull;
    size_t nallocated;
    bufhdr_t *buf[];
};

struct bytebuf {
    bufhdr_t hdr;
    char payload[];
};

struct fragment {
    bufhdr_t hdr;
    bufhdr_t *parent;
};

struct progbuf {
    bufhdr_t hdr;
    progress_msg_t msg;
};

struct vecbuf {
    bufhdr_t hdr;
    vector_msg_t msg;
};

#ifdef __cplusplus
extern "C" {
#endif

bufhdr_t *
buflist_get(buflist_t *bl);
bool
buflist_put(buflist_t *bl, bufhdr_t *h);
buflist_t *
buflist_create(size_t n);

bufhdr_t *
buf_alloc(size_t paylen);
void
buf_free(bufhdr_t *h);

bytebuf_t *
bytebuf_alloc(size_t paylen);

fragment_t *
fragment_alloc(void);

progbuf_t *
progbuf_alloc(void);
bool
progbuf_is_wellformed(progbuf_t *pb);

vecbuf_t *
vecbuf_alloc(void);
void
vecbuf_free(vecbuf_t *vb);
bool
vecbuf_is_wellformed(vecbuf_t *vb);

/* Buffer registration functions
 *
 * TODO: Poor separation of concerns here - should separate creation and
 *       registration.
 */

int
buf_mr_bind(bufhdr_t *h, struct fid_ep *ep);
int
buf_mr_dereg(bufhdr_t *h);
int
buf_mr_reg(struct fid_domain *dom, struct fid_ep *ep, uint64_t access,
           uint64_t key, bufhdr_t *h);
bufhdr_t *
progbuf_create_and_register(struct fid_ep *ep);
bufhdr_t *
vecbuf_create_and_register(struct fid_ep *ep);

#ifdef __cplusplus
}
#endif

#endif
