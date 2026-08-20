#ifndef SIHTTP_ROUTE_H
#define SIHTTP_ROUTE_H

#include "sihttp_internal.h"
#include <sicore.h>

typedef struct {
    sihttp_method_t method;
    char *path;
    sihttp_handler_t callback;
} sihttp_route_entry_t;

struct sihttp_route_table_s {
    sicore_vec_t entries;
};

#endif
