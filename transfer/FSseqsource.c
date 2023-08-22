/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "FSseqsource.h"

static uint64_t _Atomic next_key_pool = 512;

void
seqsource_init(seqsource_t *s)
{
    memset(s, 0, sizeof(*s));
}

uint64_t
seqsource_get(seqsource_t *s)
{
    if (s->next_key % 256 == 0) {
        s->next_key = atomic_fetch_add_explicit(&next_key_pool, 256,
                                                memory_order_relaxed);
    }

    return s->next_key++;
}

bool
seqsource_unget(seqsource_t *s, uint64_t got)
{
    if (got + 1 != s->next_key)
        return false;

    s->next_key--;
    return true;
}
