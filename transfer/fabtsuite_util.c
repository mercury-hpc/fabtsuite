#include <stdbool.h>
#include <stdlib.h>

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
