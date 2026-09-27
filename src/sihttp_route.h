#ifndef SIHTTP_ROUTE_H
#define SIHTTP_ROUTE_H

#include "sihttp_internal.h"
#include <sicore.h>

typedef struct {
    const char *text;
    size_t len;
    bool parameter;
} sihttp_route_segment_t;

typedef struct {
    sihttp_method_t method;
    char *path;
    sihttp_handler_t callback;
    sihttp_route_segment_t *segments;
    size_t segment_count;
    size_t literal_count;
} sihttp_route_entry_t;

struct sihttp_route_table_s {
    sicore_vec_t entries;
};

#endif
