#include "sihttp_route.h"

#include <stdlib.h>
#include <string.h>

static char *sihttp_strdup(const char *str) {
    size_t len = strlen(str);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, str, len + 1);
    return copy;
}

static int
sihttp_route_path_matches(const char *pattern, const char *path, sihttp_request_internal_t *req) {
    const char *p = pattern;
    const char *s = path;

    while (*p && *s) {
        if (*p == ':') {
            const char *name_start;
            const char *value_start;
            size_t name_len;
            size_t value_len;
            char name[32];
            char value[64];
            int ok;

            p++;
            name_start = p;
            while (*p && *p != '/') {
                p++;
            }

            value_start = s;
            while (*s && *s != '/') {
                s++;
            }

            name_len = (size_t)(p - name_start);
            value_len = (size_t)(s - value_start);
            if (name_len == 0 || value_len == 0 || req->param_count >= SIHTTP_MAX_PARAMS) {
                return 0;
            }

            if (name_len >= sizeof(name) || value_len >= sizeof(value)) {
                return 0;
            }
            memcpy(name, name_start, name_len);
            name[name_len] = '\0';
            memcpy(value, value_start, value_len);
            value[value_len] = '\0';

            ok = sihttp_request_add_param(req, name, value) == 0;
            if (!ok) {
                return 0;
            }
        } else if (*p == *s) {
            p++;
            s++;
        } else {
            return 0;
        }
    }

    return *p == '\0' && *s == '\0';
}

int sihttp_route_table_init(sihttp_route_table_t *table) {
    table->entries = NULL;
    table->count = 0;
    table->capacity = 0;
    return 0;
}

void sihttp_route_table_fini(sihttp_route_table_t *table) {
    for (size_t i = 0; i < table->count; i++) {
        free(table->entries[i].path);
    }
    free(table->entries);
    table->entries = NULL;
    table->count = 0;
    table->capacity = 0;
}

int sihttp_route_table_add(
    sihttp_route_table_t *table,
    sihttp_method_t method,
    const char *path,
    sihttp_handler_t callback
) {
    char *path_copy;

    if (!path || path[0] != '/' || !callback) {
        return -1;
    }

    if (table->count == table->capacity) {
        size_t next = table->capacity ? table->capacity * 2 : 8;
        sihttp_route_entry_t *entries = realloc(table->entries, next * sizeof(*entries));
        if (!entries) {
            return -1;
        }
        table->entries = entries;
        table->capacity = next;
    }

    path_copy = sihttp_strdup(path);
    if (!path_copy) {
        return -1;
    }

    table->entries[table->count++] = (sihttp_route_entry_t){
        .method = method,
        .path = path_copy,
        .callback = callback,
    };
    return 0;
}

sihttp_handler_t sihttp_route_table_match(
    const sihttp_route_table_t *table,
    sihttp_method_t method,
    const char *path,
    sihttp_request_internal_t *req,
    int *method_not_allowed
) {
    *method_not_allowed = 0;

    for (size_t i = 0; i < table->count; i++) {
        const sihttp_route_entry_t *entry = &table->entries[i];
        size_t saved_count = req->param_count;
        if (!sihttp_route_path_matches(entry->path, path, req)) {
            req->param_count = saved_count;
            continue;
        }

        if (entry->method == method) {
            return entry->callback;
        }

        req->param_count = saved_count;
        *method_not_allowed = 1;
    }

    return NULL;
}
