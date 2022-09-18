#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "fabtsuite_buffer.h"
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
