/* fabtsuite_util.h
 *
 * Utility functions used throughout the code
 */

#ifndef fabtsuite_util_H
#define fabtsuite_util_H

#define arraycount(a) (sizeof(a) / sizeof(a[0]))

#ifndef transfer_unused
#define transfer_unused __attribute__((unused))
#endif

#endif
