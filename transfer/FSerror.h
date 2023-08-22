/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FSerror.h
 *
 * Error-handling functions used throughout the code
 */

#ifndef FSerror_H
#define FSerror_H

#include <stdarg.h>

#define bailout_for_ofi_ret(ret, ...)                                          \
    bailout_for_ofi_ret_impl(ret, __func__, __LINE__, __VA_ARGS__)

#define warn_about_ofi_ret(ret, ...)                                           \
    warn_about_ofi_ret_impl(ret, __func__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
extern "C" {
#endif

void
warnv_about_ofi_ret_impl(int ret, const char *fn, int lineno, const char *fmt,
                         va_list ap);
void
warn_about_ofi_ret_impl(int ret, const char *fn, int lineno, const char *fmt,
                        ...);
void
bailout_for_ofi_ret_impl(int ret, const char *fn, int lineno, const char *fmt,
                         ...);

#ifdef __cplusplus
}
#endif

#endif
