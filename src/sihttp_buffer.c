#include "sihttp_buffer.h"

#include <stdlib.h>
#include <string.h>

void sihttp_buffer_init(sihttp_buffer_t *buffer) {
    buffer->data = NULL;
    buffer->len = 0;
    buffer->cap = 0;
}

void sihttp_buffer_fini(sihttp_buffer_t *buffer) {
    free(buffer->data);
    buffer->data = NULL;
    buffer->len = 0;
    buffer->cap = 0;
}

int sihttp_buffer_append(sihttp_buffer_t *buffer, const char *data, size_t len) {
    size_t required;

    if (len == 0) {
        return 0;
    }

    required = buffer->len + len;
    if (required < buffer->len) {
        return -1;
    }

    if (required > buffer->cap) {
        size_t next = buffer->cap ? buffer->cap : 1024;
        char *new_data;

        while (next < required) {
            size_t doubled = next * 2;
            if (doubled < next) {
                return -1;
            }
            next = doubled;
        }

        new_data = realloc(buffer->data, next);
        if (!new_data) {
            return -1;
        }

        buffer->data = new_data;
        buffer->cap = next;
    }

    memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;
    return 0;
}
