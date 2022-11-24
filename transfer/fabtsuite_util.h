/* fabtsuite_util.h
 *
 * Utility functions used throughout the code
 */

#ifndef fabtsuite_util_H
#define fabtsuite_util_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#include "fabtsuite_types.h"

#define arraycount(a) (sizeof(a) / sizeof(a[0]))

#ifndef transfer_unused
#define transfer_unused __attribute__((unused))
#endif

#ifdef __cplusplus
extern "C" {
#endif

char *
completion_flags_to_string(const uint64_t flags, char *const buf,
                           const size_t bufsize);
int
minsize(size_t l, size_t r);
bool
size_is_power_of_2(size_t size);

uint8_t *
hex_string_to_bytes(const char *inbuf, size_t *nbytesp);
char *
bytes_to_hex_string(const uint8_t *inbuf, size_t inbuflen);

const char *
xfc_type_to_string(xfc_type_t t);

#ifdef __cplusplus
}
#endif

#endif
