/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rdma/fabric.h>

#include "fabtsuite_types.h"
#include "fabtsuite_util.h"

char *
completion_flags_to_string(const uint64_t flags, char *const buf,
                           const size_t bufsize)
{
    char *next = buf;
    static const struct {
        uint64_t flag;
        const char *name;
    } flag_to_name[] = {
        {.flag = FI_RECV, .name = "recv"},
        {.flag = FI_SEND, .name = "send"},
        {.flag = FI_MSG, .name = "msg"},
        {.flag = FI_RMA, .name = "rma"},
        {.flag = FI_WRITE, .name = "write"},
        {.flag = FI_TAGGED, .name = "tagged"},
        {.flag = FI_COMPLETION, .name = "completion"},
        {.flag = FI_DELIVERY_COMPLETE, .name = "delivery complete"}};
    size_t i;
    size_t bufleft = bufsize;
    uint64_t found = 0, residue;
    const char *delim = "<";

    if (bufsize < 1)
        return NULL;
    buf[0] = '\0';

    for (i = 0; i < arraycount(flag_to_name); i++) {
        uint64_t curflag = flag_to_name[i].flag;
        const char *name = flag_to_name[i].name;
        if ((flags & curflag) == 0)
            continue;
        found |= curflag;
        int nprinted = snprintf(next, bufleft, "%s%s", delim, name);
        delim = ",";
        if (nprinted < 0 || (size_t) nprinted >= bufleft)
            continue;
        next += nprinted;
        bufleft -= (size_t) nprinted;
    }
    residue = flags & ~found;
    while (residue != 0) {
        uint64_t oresidue = residue;
        residue = residue & (residue - 1);
        uint64_t lsb = oresidue ^ residue;
        int nprinted = snprintf(next, bufleft, "%s0x%" PRIx64, delim, lsb);
        delim = ",";
        if (nprinted < 0 || (size_t) nprinted >= bufleft)
            continue;
        next += nprinted;
        bufleft -= (size_t) nprinted;
    }
    if (next != buf)
        (void) snprintf(next, bufleft, ">");
    return buf;
}

int
minsize(size_t l, size_t r)
{
    return (l < r) ? l : r;
}

bool
size_is_power_of_2(size_t size)
{
    return ((size - 1) & size) == 0;
}

uint8_t *
hex_string_to_bytes(const char *inbuf, size_t *nbytesp)
{
    uint8_t *outbuf;
    const char *p;
    size_t i, inbuflen = strlen(inbuf), nbytes;
    int nconverted;

    if (inbuflen == 0) {
        if (nbytesp != NULL)
            *nbytesp = 0;
        /* Avoid returning NULL here, NULL is reserved to indicate an error. */
        return malloc(1);
    }

    if ((inbuflen + 1) % 3 != 0)
        return NULL;

    nbytes = (inbuflen + 1) / 3;
    if ((outbuf = malloc(nbytes)) == NULL)
        return NULL;

    if (sscanf(inbuf, "%02" SCNx8 "%n", &outbuf[0], &nconverted) != 1 ||
        nconverted != 2)
        goto fail;

    for (p = &inbuf[nconverted], i = 1; i < nbytes; i++, p += nconverted) {
        if (sscanf(p, ":%02" SCNx8 "%n", &outbuf[i], &nconverted) != 1 ||
            nconverted != 3)
            goto fail;
    }

    if (nbytesp != NULL)
        *nbytesp = nbytes;

    return outbuf;
fail:
    free(outbuf);
    return NULL;
}

char *
bytes_to_hex_string(const uint8_t *inbuf, size_t inbuflen)
{
    char *outbuf, *p;
    const char *delim;
    size_t i, nempty, outbuflen;
    int nprinted;

    if (inbuflen == 0)
        return strdup("");

    /* sizeof("ff") includes the terminating NUL, thus `outbuflen`
     * accounts for the colon delimiters between hex octets and
     * the NUL at the end of string.
     */
    outbuflen = inbuflen * sizeof("ff");

    if ((outbuf = malloc(outbuflen)) == NULL)
        return NULL;

    for (p = outbuf, nempty = outbuflen, delim = "", i = 0; i < inbuflen;
         i++, delim = ":", p += nprinted, nempty -= (size_t) nprinted) {

        nprinted = snprintf(p, nempty, "%s%02" PRIx8, delim, inbuf[i]);

        if (nprinted < 0 || (size_t) nprinted >= nempty) {
            free(outbuf);
            return NULL;
        }
    }
    return outbuf;
}

const char *
xfc_type_to_string(xfc_type_t t)
{
    switch (t) {
        case xft_ack:
            return "ack";
        case xft_fragment:
            return "fragment";
        case xft_initial:
            return "initial";
        case xft_progress:
            return "progress";
        case xft_rdma_write:
            return "rdma_write";
        case xft_vector:
            return "vector";
        default:
            return "<unknown>";
    }
}
