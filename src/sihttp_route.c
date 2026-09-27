#include "sihttp_route.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int sihttp_param_name_valid(const char *name, size_t len) {
    if (!len || len >= 32 || !(isalpha((unsigned char)name[0]) || name[0] == '_')) return 0;
    for (size_t i = 1; i < len; i++) {
        if (!(isalnum((unsigned char)name[i]) || name[i] == '_')) return 0;
    }
    return 1;
}

static int sihttp_route_compile(sihttp_route_entry_t *entry, const char *path) {
    size_t len = strlen(path);
    size_t count = 0;
    if (!len || path[0] != '/' || strchr(path, '?') || strchr(path, '#')) return -1;
    if (len > 1) {
        count = 1;
        for (size_t i = 1; i < len; i++) {
            if (path[i] == '/') {
                if (path[i - 1] == '/' || i == len - 1) return -1;
                count++;
            } else if ((unsigned char)path[i] <= 32 || (unsigned char)path[i] == 127) return -1;
        }
    }
    entry->path = malloc(len + 1);
    entry->segments = count ? calloc(count, sizeof(*entry->segments)) : NULL;
    if (!entry->path || (count && !entry->segments)) return -1;
    memcpy(entry->path, path, len + 1);
    entry->segment_count = count;
    const char *cursor = entry->path + 1;
    size_t param_count = 0;
    for (size_t i = 0; i < count; i++) {
        const char *end = strchr(cursor, '/');
        if (!end) end = entry->path + len;
        sihttp_route_segment_t *segment = &entry->segments[i];
        segment->parameter = cursor[0] == ':';
        segment->text = cursor + segment->parameter;
        segment->len = (size_t)(end - segment->text);
        if (segment->parameter) {
            if (++param_count > SIHTTP_MAX_PARAMS) return -1;
            if (!sihttp_param_name_valid(segment->text, segment->len)) return -1;
            for (size_t j = 0; j < i; j++) {
                sihttp_route_segment_t *prior = &entry->segments[j];
                if (prior->parameter && prior->len == segment->len &&
                    memcmp(prior->text, segment->text, segment->len) == 0) return -1;
            }
        } else {
            if (memchr(segment->text, ':', segment->len)) return -1;
            entry->literal_count++;
        }
        cursor = end + 1;
    }
    return 0;
}

static int sihttp_route_path_matches(const sihttp_route_entry_t *entry, const char *path) {
    if (!path || path[0] != '/') return 0;
    if (entry->segment_count == 0) return path[1] == '\0';
    const char *cursor = path + 1;
    for (size_t i = 0; i < entry->segment_count; i++) {
        const char *end = strchr(cursor, '/');
        if (!end) end = cursor + strlen(cursor);
        size_t len = (size_t)(end - cursor);
        const sihttp_route_segment_t *segment = &entry->segments[i];
        if (!len || (!segment->parameter &&
            (len != segment->len || memcmp(cursor, segment->text, len) != 0))) return 0;
        if (i + 1 == entry->segment_count) return *end == '\0';
        if (*end != '/') return 0;
        cursor = end + 1;
    }
    return 0;
}

int sihttp_route_table_init(sihttp_route_table_t *table) {
    sicore_vec_init(&table->entries, sizeof(sihttp_route_entry_t));
    return table->entries.data ? 0 : -1;
}

void sihttp_route_table_fini(sihttp_route_table_t *table) {
    sihttp_route_entry_t *entries = sicore_vec_data(&table->entries, sihttp_route_entry_t);
    for (uint32_t i = 0; i < table->entries.size; i++) {
        free(entries[i].path);
        free(entries[i].segments);
    }
    sicore_vec_fini(&table->entries);
}

int sihttp_route_table_add(sihttp_route_table_t *table, sihttp_method_t method,
                           const char *path, sihttp_handler_t callback) {
    sihttp_route_entry_t entry = {.method = method, .callback = callback};
    if (!table || !path || !callback || !sihttp_method_name(method)) return -1;
    const sihttp_route_entry_t *entries = sicore_vec_data(&table->entries, sihttp_route_entry_t);
    for (uint32_t i = 0; i < table->entries.size; i++) {
        if (entries[i].method == method && strcmp(entries[i].path, path) == 0) return -1;
    }
    if (sihttp_route_compile(&entry, path) != 0) {
        free(entry.path);
        free(entry.segments);
        return -1;
    }
    if (table->entries.size == UINT32_MAX) {
        free(entry.path); free(entry.segments); return -1;
    }
    if (table->entries.size == table->entries.capacity) {
        uint32_t capacity = table->entries.capacity;
        if (capacity > UINT32_MAX / 2) {
            free(entry.path); free(entry.segments); return -1;
        }
#if SIZE_MAX <= UINT32_MAX
        if (capacity > SIZE_MAX / (2 * sizeof(entry))) {
            free(entry.path); free(entry.segments); return -1;
        }
#endif
        void *grown = realloc(table->entries.data, (size_t)capacity * 2 * sizeof(entry));
        if (!grown) { free(entry.path); free(entry.segments); return -1; }
        table->entries.data = grown;
        table->entries.capacity = capacity * 2;
    }
    ((sihttp_route_entry_t *)table->entries.data)[table->entries.size++] = entry;
    return 0;
}

sihttp_handler_t sihttp_route_table_match_ex(const sihttp_route_table_t *table,
    sihttp_method_t method, const char *path, sihttp_request_internal_t *req, unsigned *allow_mask) {
    const sihttp_route_entry_t *entries = sicore_vec_data(&table->entries, sihttp_route_entry_t);
    const sihttp_route_entry_t *best = NULL;
    int best_rank = -1;
    *allow_mask = 0;
    for (uint32_t i = 0; i < table->entries.size; i++) {
        const sihttp_route_entry_t *entry = &entries[i];
        if (!sihttp_route_path_matches(entry, path)) continue;
        *allow_mask |= 1u << entry->method;
        if (entry->method == SIHTTP_METHOD_GET) *allow_mask |= 1u << SIHTTP_METHOD_HEAD;
        int rank = entry->method == method ? 2 :
                   method == SIHTTP_METHOD_HEAD && entry->method == SIHTTP_METHOD_GET ? 1 : 0;
        if (rank && (rank > best_rank || (rank == best_rank &&
            entry->literal_count > best->literal_count))) {
            best = entry;
            best_rank = rank;
        }
    }
    if (!best) return NULL;
    const char *cursor = path + 1;
    for (size_t i = 0; i < best->segment_count; i++) {
        const char *end = strchr(cursor, '/');
        if (!end) end = cursor + strlen(cursor);
        if (best->segments[i].parameter) {
            char name[32];
            size_t name_len = best->segments[i].len;
            size_t value_len = (size_t)(end - cursor);
            if (value_len >= sizeof(req->param_values[0])) {
                req->param_error = 1;
                return NULL;
            }
            char value[sizeof(req->param_values[0])];
            memcpy(name, best->segments[i].text, name_len);
            name[name_len] = '\0';
            memcpy(value, cursor, value_len);
            value[value_len] = '\0';
            if (sihttp_request_add_param(req, name, value) != 0) {
                req->param_error = 1;
                return NULL;
            }
        }
        cursor = *end ? end + 1 : end;
    }
    return best->callback;
}

sihttp_handler_t sihttp_route_table_match(const sihttp_route_table_t *table,
    sihttp_method_t method, const char *path, sihttp_request_internal_t *req,
    int *method_not_allowed) {
    unsigned mask = 0;
    sihttp_handler_t handler = sihttp_route_table_match_ex(table, method, path, req, &mask);
    *method_not_allowed = !handler && mask != 0;
    return handler;
}
