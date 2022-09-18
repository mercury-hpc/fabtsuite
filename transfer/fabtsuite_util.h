/* fabtsuite_util.h
 *
 * Utility functions used throughout the code
 */

#ifndef fabtsuite_util_H
#define fabtsuite_util_H

#include <stdbool.h>
#include <stdlib.h>

#define arraycount(a) (sizeof(a) / sizeof(a[0]))

#ifndef transfer_unused
#define transfer_unused __attribute__((unused))
#endif

#ifdef __cplusplus
extern "C" {
#endif

bool size_is_power_of_2(size_t size);

#ifdef __cplusplus
}
#endif

#endif
