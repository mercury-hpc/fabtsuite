#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include <sys/epoll.h>

#include <rdma/fabric.h>
#include <rdma/fi_endpoint.h>

#include "fabtsuite_error.h"
#include "fabtsuite_fifo.h"
#include "fabtsuite_types.h"
#include "fabtsuite_util.h"

fifo_t *
fifo_create(size_t size)
{
    if (!size_is_power_of_2(size))
        return NULL;

    fifo_t *f = malloc(offsetof(fifo_t, hdr[0]) + sizeof(f->hdr[0]) * size);

    if (f == NULL)
        return NULL;

    f->insertions = f->removals = 0;
    f->index_mask = size - 1;
    f->closed = UINT64_MAX;

    return f;
}

/* Return true if the head of the FIFO (the removal point) is at or past
 * the close position.  Otherwise, return false.
 */
bool
fifo_eoget(const fifo_t *f)
{
    return f->closed <= f->removals;
}

/* Return true if the tail of the FIFO (the insertion point) is at or
 * past the close position.  Otherwise, return false.
 */
bool
fifo_eoput(const fifo_t *f)
{
    return f->closed <= f->insertions;
}

/* Set the close position to the current head of the FIFO (the removal
 * point).  Every `fifo_get` that follows will fail, and `fifo_eoget`
 * will be true.
 */
void
fifo_get_close(fifo_t *f)
{
    assert(!fifo_eoget(f));
    f->closed = f->removals;
}

/* Set the close position to the current tail of the FIFO (the insertion
 * point).  Every `fifo_put` that follows will fail, and `fifo_eoput`
 * will be true.
 */
void
fifo_put_close(fifo_t *f)
{
    assert(!fifo_eoput(f));
    f->closed = f->insertions;
}

void
fifo_destroy(fifo_t *f)
{
    free(f);
}

/* See `fifo_get`: this is a variant that does not respect the close
 * position.
 */
bufhdr_t *
fifo_alt_get(fifo_t *f)
{
    assert(f->insertions >= f->removals);

    if (f->insertions == f->removals)
        return NULL;

    bufhdr_t *h = f->hdr[f->removals & (uint64_t) f->index_mask];
    f->removals++;

    return h;
}

/* Return NULL if the FIFO is empty or if the FIFO has been read up to
 * the close position.  Otherwise, remove and return the next item on the
 * FIFO.
 */
bufhdr_t *
fifo_get(fifo_t *f)
{
    if (fifo_eoget(f))
        return NULL;

    return fifo_alt_get(f);
}

/* See `fifo_empty`: this is a variant that does not respect the close
 * position.
 */
bool
fifo_alt_empty(fifo_t *f)
{
    return f->insertions == f->removals;
}

/* Return true if the FIFO is empty or if the FIFO has been read up
 * to the close position.  Otherwise, return false.
 */
bool
fifo_empty(fifo_t *f)
{
    return fifo_eoget(f) || fifo_alt_empty(f);
}

size_t
fifo_nfull(fifo_t *f)
{
    return f->insertions - f->removals;
}

size_t
fifo_nempty(fifo_t *f)
{
    return f->index_mask + 1 - fifo_nfull(f);
}

/* Return NULL if the FIFO is empty or if the FIFO has been read up to
 * the close position.  Otherwise, return the next item on the FIFO without
 * removing it.
 */
bufhdr_t *
fifo_peek(fifo_t *f)
{
    assert(f->insertions >= f->removals);

    if (fifo_empty(f))
        return NULL;

    return f->hdr[f->removals & (uint64_t) f->index_mask];
}

/* See `fifo_full`: this is a variant that does not respect the close
 * position.
 */
bool
fifo_alt_full(fifo_t *f)
{
    return f->insertions - f->removals == f->index_mask + 1;
}

/* Return true if the FIFO is full or if the FIFO has been written up
 * to the close position.  Otherwise, return false.
 */
bool
fifo_full(fifo_t *f)
{
    return fifo_eoput(f) || fifo_alt_full(f);
}

/* See `fifo_put`: this is a variant that does not respect the close
 * position.
 */
bool
fifo_alt_put(fifo_t *f, bufhdr_t *h)
{
    assert(f->insertions - f->removals <= f->index_mask + 1);

    if (f->insertions - f->removals > f->index_mask)
        return false;

    f->hdr[f->insertions & (uint64_t) f->index_mask] = h;
    f->insertions++;

    return true;
}

/* If the FIFO is full or if it has been written up to the close
 * position, then return false without changing the FIFO.
 * Otherwise, add item `h` to the tail of the FIFO.
 */
bool
fifo_put(fifo_t *f, bufhdr_t *h)
{
    if (fifo_eoput(f))
        return false;

    return fifo_alt_put(f, h);
}

void
fifo_cancel(struct fid_ep *ep, fifo_t *posted)
{
    bufhdr_t *first = NULL, *h;
    int rc;

    while ((h = fifo_peek(posted)) != NULL) {
        if (h == first)
            break;
        (void) fifo_get(posted);
        if (first == NULL)
            first = h;
        h->xfc.cancelled = 1;
        if ((rc = fi_cancel(&ep->fid, &h->xfc.ctx)) != 0)
            bailout_for_ofi_ret(rc, "fi_cancel");
        (void) fifo_put(posted, h);
    }
}
