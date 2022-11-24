/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* fabtsuite_fifo.h
 *
 * fifo data structure
 */

#ifndef fabtsuite_fifo_H
#define fabtsuite_fifo_H

#include <stdbool.h>
#include <stdlib.h>

#include <sys/epoll.h>

#include "fabtsuite_buffer.h"
#include "fabtsuite_types.h"

struct fifo {
    uint64_t insertions;
    uint64_t removals;
    size_t index_mask; // for some integer n > 0, 2^n - 1 == index_mask
    uint64_t closed;   /* close position: no insertions or removals may
                        * take place at or after this position.
                        */
    bufhdr_t *hdr[];
};

#ifdef __cplusplus
extern "C" {
#endif

fifo_t *
fifo_create(size_t size);

/* Return true if the head of the FIFO (the removal point) is at or past
 * the close position.  Otherwise, return false.
 */
bool
fifo_eoget(const fifo_t *f);

/* Return true if the tail of the FIFO (the insertion point) is at or
 * past the close position.  Otherwise, return false.
 */
bool
fifo_eoput(const fifo_t *f);

/* Set the close position to the current head of the FIFO (the removal
 * point).  Every `fifo_get` that follows will fail, and `fifo_eoget`
 * will be true.
 */
void
fifo_get_close(fifo_t *f);

/* Set the close position to the current tail of the FIFO (the insertion
 * point).  Every `fifo_put` that follows will fail, and `fifo_eoput`
 * will be true.
 */
void
fifo_put_close(fifo_t *f);

void
fifo_destroy(fifo_t *f);

/* See `fifo_get`: this is a variant that does not respect the close
 * position.
 */
bufhdr_t *
fifo_alt_get(fifo_t *f);

/* Return NULL if the FIFO is empty or if the FIFO has been read up to
 * the close position.  Otherwise, remove and return the next item on the
 * FIFO.
 */
bufhdr_t *
fifo_get(fifo_t *f);

/* See `fifo_empty`: this is a variant that does not respect the close
 * position.
 */
bool
fifo_alt_empty(fifo_t *f);

/* Return true if the FIFO is empty or if the FIFO has been read up
 * to the close position.  Otherwise, return false.
 */
bool
fifo_empty(fifo_t *f);

size_t
fifo_nfull(fifo_t *f);

size_t
fifo_nempty(fifo_t *f);

/* Return NULL if the FIFO is empty or if the FIFO has been read up to
 * the close position.  Otherwise, return the next item on the FIFO without
 * removing it.
 */
bufhdr_t *
fifo_peek(fifo_t *f);

/* See `fifo_full`: this is a variant that does not respect the close
 * position.
 */
bool
fifo_alt_full(fifo_t *f);

/* Return true if the FIFO is full or if the FIFO has been written up
 * to the close position.  Otherwise, return false.
 */
bool
fifo_full(fifo_t *f);

/* See `fifo_put`: this is a variant that does not respect the close
 * position.
 */
bool
fifo_alt_put(fifo_t *f, bufhdr_t *h);

/* If the FIFO is full or if it has been written up to the close
 * position, then return false without changing the FIFO.
 * Otherwise, add item `h` to the tail of the FIFO.
 */
bool
fifo_put(fifo_t *f, bufhdr_t *h);

void
fifo_cancel(struct fid_ep *ep, fifo_t *posted);

#ifdef __cplusplus
}
#endif

#endif
