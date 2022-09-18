#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <rdma/fabric.h>

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
