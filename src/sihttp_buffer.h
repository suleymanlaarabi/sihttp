#ifndef SIHTTP_BUFFER_H
#define SIHTTP_BUFFER_H

#include <stddef.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} sihttp_buffer_t;

void sihttp_buffer_init(sihttp_buffer_t *buffer);
void sihttp_buffer_fini(sihttp_buffer_t *buffer);
int sihttp_buffer_append(sihttp_buffer_t *buffer, const char *data, size_t len);

#endif
