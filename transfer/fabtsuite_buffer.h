/* fabtsuite_buffer.h
 *
 * Buffer routines
 */

#ifndef fabtsuite_buffer_H
#define fabtsuite_buffer_H

#include <inttypes.h>
#include <stdlib.h>

#include <sys/epoll.h>

#include "fabtsuite_types.h"

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

/* TODO: Move create_and_register functions over. Or, even better, split them
 *       into create and register functions and just move the create functions
 *       here.
 */

bufhdr_t *buflist_get(buflist_t *bl);
bool buflist_put(buflist_t *bl, bufhdr_t *h);
buflist_t *buflist_create(size_t n);

bufhdr_t *buf_alloc(size_t paylen);
void buf_free(bufhdr_t *h);

bytebuf_t *bytebuf_alloc(size_t paylen);

fragment_t *fragment_alloc(void);

progbuf_t *progbuf_alloc(void);

vecbuf_t *vecbuf_alloc(void);
void vecbuf_free(vecbuf_t *vb);
bool vecbuf_is_wellformed(vecbuf_t *vb);

#ifdef __cplusplus
}
#endif

#endif
