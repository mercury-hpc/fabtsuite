/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FSsession.h
 *
 * Session routines
 */

#ifndef FSsession_H
#define FSsession_H

#include <inttypes.h>
#include <stdlib.h>

#include <rdma/fi_domain.h>

#include "FStypes.h"

#ifdef __cplusplus
extern "C" {
#endif

bool
session_init(session_t *s, cxn_t *c, terminal_t *t);
void
session_shutdown(session_t *s);
loop_control_t
session_loop(worker_t *w, session_t *s);

void
sessions_swap(session_t *r, session_t *s);

#ifdef __cplusplus
}
#endif

#endif
