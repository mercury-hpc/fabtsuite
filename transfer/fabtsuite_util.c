#include <stdbool.h>
#include <stdlib.h>

bool
size_is_power_of_2(size_t size)
{
    return ((size - 1) & size) == 0;
}


