/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* fabtsuite_seqsource.h
 *
 */

#ifndef fabtsuite_seqsource_H
#define fabtsuite_seqsource_H

#include <inttypes.h>
#include <stdbool.h>

#include "fabtsuite_types.h"

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
