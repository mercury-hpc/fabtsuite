/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FSseqsource.h
 *
 */

#ifndef FSseqsource_H
#define FSseqsource_H

#include <inttypes.h>
#include <stdbool.h>

#include "FStypes.h"

#ifdef __cplusplus
extern "C" {
#endif

void
seqsource_init(seqsource_t *s);
uint64_t
seqsource_get(seqsource_t *s);
bool
seqsource_unget(seqsource_t *s, uint64_t got);

#ifdef __cplusplus
}
#endif

#endif
