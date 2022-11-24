/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* fabtsuite_cxn.h
 */

#ifndef fabtsuite_cxn_H
#define fabtsuite_cxn_H

#include <stdbool.h>

#include <rdma/fi_domain.h>

#include "fabtsuite_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void
cxn_init(cxn_t *c, struct fid_av *av,
         loop_control_t (*loop)(worker_t *, session_t *),
         void (*cancel)(cxn_t *), bool (*cancellation_complete)(cxn_t *),
         void (*shutdown)(cxn_t *));

#ifdef __cplusplus
}
#endif

#endif
