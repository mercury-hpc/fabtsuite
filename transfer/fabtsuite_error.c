/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <rdma/fi_errno.h>

#include "fabtsuite_error.h"

void
warnv_about_ofi_ret_impl(int ret, const char *fn, int lineno, const char *fmt,
                         va_list ap)
{
    fprintf(stderr, "%s.%d: ", fn, lineno);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, ": %s\n", fi_strerror(-ret));
}

void
warn_about_ofi_ret_impl(int ret, const char *fn, int lineno, const char *fmt,
                        ...)
{
    va_list ap;

    va_start(ap, fmt);
    warnv_about_ofi_ret_impl(ret, fn, lineno, fmt, ap);
    va_end(ap);
}

void
bailout_for_ofi_ret_impl(int ret, const char *fn, int lineno, const char *fmt,
                         ...)
{
    va_list ap;

    va_start(ap, fmt);
    warnv_about_ofi_ret_impl(ret, fn, lineno, fmt, ap);
    va_end(ap);

    exit(EXIT_FAILURE);
}
