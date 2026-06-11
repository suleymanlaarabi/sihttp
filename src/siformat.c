#include <sihttp.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

SIHTTP_API char *siformat(const char *fmt, ...) {
    va_list args;
    va_list copy;
    int size;
    char *str;

    va_start(args, fmt);
    va_copy(copy, args);

    size = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);

    if (size < 0) {
        va_end(args);
        return NULL;
    }

    str = malloc((size_t)size + 1);
    if (!str) {
        va_end(args);
        return NULL;
    }

    vsnprintf(str, (size_t)size + 1, fmt, args);
    va_end(args);

    return str;
}
