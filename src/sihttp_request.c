#include "sihttp_internal.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const char *sihttp_find_header_end_const(const char *data, size_t len) {
    if (len < 4) {
        return NULL;
    }

    for (size_t i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
            return data + i;
        }
    }

    return NULL;
}

static char *sihttp_find_header_end(char *data, size_t len) {
    if (len < 4) {
        return NULL;
    }

    for (size_t i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
            return data + i;
        }
    }

    return NULL;
}

static int sihttp_streq_icase(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static char *sihttp_trim(char *str) {
    char *end;

    while (*str == ' ' || *str == '\t') {
        str++;
    }

    end = str + strlen(str);
    while (end > str && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    *end = '\0';

    return str;
}

static int sihttp_parse_size(const char *value, size_t *out) {
    size_t parsed = 0;
    if (!*value) return -1;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p < '0' || *p > '9' || parsed > (SIZE_MAX - (*p - '0')) / 10) return -1;
        parsed = parsed * 10 + (*p - '0');
    }
    *out = parsed;
    return 0;
}

int sihttp_request_check_header(sihttp_header_state_t *state, const char *name, const char *value) {
    if (!sihttp_header_valid(name, value)) return 400;
    if (sihttp_streq_icase(name, "Content-Length")) {
        size_t parsed;
        if (sihttp_parse_size(value, &parsed) != 0 ||
            (state->has_length && strcmp(value, state->length_text) != 0)) return 400;
        state->content_length = parsed;
        state->length_text = value;
        state->has_length = 1;
    }
    if (sihttp_streq_icase(name, "Transfer-Encoding")) return 501;
    if (sihttp_streq_icase(name, "Host")) {
        if (state->has_host || !*value) return 400;
        state->has_host = 1;
    }
    return 200;
}

static int sihttp_tchar(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || strchr("!#$%&'*+-.^_`|~", c) != NULL;
}

int sihttp_header_valid(const char *name, const char *value) {
    if (!name || !*name || !value) return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (!sihttp_tchar(*p)) return 0;
    }
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if ((*p < 32 && *p != '\t') || *p == 127) return 0;
    }
    return 1;
}

static int sihttp_hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int sihttp_url_decode(char *value, int query) {
    char *out = value;
    for (char *p = value; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '%') {
            if (!p[1] || !p[2]) return -1;
            int hi = sihttp_hex((unsigned char)p[1]);
            int lo = sihttp_hex((unsigned char)p[2]);
            if (hi < 0 || lo < 0) return -1;
            c = (unsigned char)((hi << 4) | lo);
            p += 2;
        } else if (c == '+' && query) {
            c = ' ';
        }
        if (c == 0 || c < 32 || c == 127) return -1;
        *out++ = (char)c;
    }
    *out = '\0';
    return 0;
}

static int sihttp_add_pair(sihttp_pair_t *pairs, size_t *count, const char *name, const char *value) {
    if (*count >= SIHTTP_MAX_PARAMS) {
        return -1;
    }

    pairs[*count].name = name;
    pairs[*count].value = value;
    (*count)++;
    return 0;
}

static int sihttp_parse_query(sihttp_request_internal_t *req, char *query) {
    char *cursor = query;
    while (cursor && *cursor) {
        char *next = strchr(cursor, '&');
        char *eq;

        if (next) {
            *next = '\0';
            next++;
        }

        eq = strchr(cursor, '=');
        if (eq) *eq++ = '\0';
        else eq = cursor + strlen(cursor);
        if (sihttp_url_decode(cursor, 1) != 0 || sihttp_url_decode(eq, 1) != 0 ||
            sihttp_add_pair(req->query, &req->query_count, cursor, eq) != 0) return -1;

        cursor = next;
    }
    return 0;
}

void sihttp_request_internal_init(sihttp_request_internal_t *req) {
    memset(req, 0, sizeof(*req));
}

void sihttp_request_internal_fini(sihttp_request_internal_t *req) {
    free(req->storage);
    free(req->header_storage);
    sihttp_request_internal_init(req);
}

int sihttp_request_set_target(sihttp_request_internal_t *req, char *target) {
    char *query;
    if (!target || target[0] != '/' || strchr(target, '#')) {
        return -1;
    }
    req->public_req.path = target;
    query = strchr(target, '?');
    if (query) {
        *query++ = '\0';
        if (sihttp_parse_query(req, query) != 0) return -1;
    }
    if (strchr(target, '#')) return -1;
    for (const unsigned char *p = (const unsigned char *)target; *p; p++) {
        if (*p <= 32 || *p == 127) return -1;
        if (*p == '%' && (!p[1] || !p[2] || sihttp_hex(p[1]) < 0 || sihttp_hex(p[2]) < 0 ||
                          (unsigned)((sihttp_hex(p[1]) << 4) | sihttp_hex(p[2])) < 32 ||
                          (unsigned)((sihttp_hex(p[1]) << 4) | sihttp_hex(p[2])) == 127)) return -1;
        if (*p == '%') p += 2;
    }
    return 0;
}

int sihttp_request_add_param(sihttp_request_internal_t *req, const char *name, const char *value) {
    size_t name_len;
    size_t value_len;

    if (req->param_count >= SIHTTP_MAX_PARAMS) {
        return -1;
    }

    name_len = strlen(name);
    value_len = strlen(value);
    if (name_len >= sizeof(req->param_names[0]) || value_len >= sizeof(req->param_values[0])) {
        return -1;
    }

    memcpy(req->param_names[req->param_count], name, name_len + 1);
    memcpy(req->param_values[req->param_count], value, value_len + 1);
    if (sihttp_url_decode(req->param_values[req->param_count], 0) != 0) return -1;
    req->params[req->param_count].name = req->param_names[req->param_count];
    req->params[req->param_count].value = req->param_values[req->param_count];
    req->param_count++;
    return 0;
}

const char *sihttp_method_name(sihttp_method_t method) {
    switch (method) {
    case SIHTTP_METHOD_GET:
        return "GET";
    case SIHTTP_METHOD_POST:
        return "POST";
    case SIHTTP_METHOD_PUT:
        return "PUT";
    case SIHTTP_METHOD_DELETE:
        return "DELETE";
    case SIHTTP_METHOD_OPTIONS:
        return "OPTIONS";
    case SIHTTP_METHOD_PATCH: return "PATCH";
    case SIHTTP_METHOD_HEAD: return "HEAD";
    }
    return NULL;
}

sihttp_method_t sihttp_method_from_name(const char *method, int *ok) {
    if (strcmp(method, "GET") == 0) {
        *ok = 1;
        return SIHTTP_METHOD_GET;
    }
    if (strcmp(method, "POST") == 0) {
        *ok = 1;
        return SIHTTP_METHOD_POST;
    }
    if (strcmp(method, "PUT") == 0) {
        *ok = 1;
        return SIHTTP_METHOD_PUT;
    }
    if (strcmp(method, "DELETE") == 0) {
        *ok = 1;
        return SIHTTP_METHOD_DELETE;
    }
    if (strcmp(method, "OPTIONS") == 0) {
        *ok = 1;
        return SIHTTP_METHOD_OPTIONS;
    }
    if (strcmp(method, "PATCH") == 0) { *ok = 1; return SIHTTP_METHOD_PATCH; }
    if (strcmp(method, "HEAD") == 0) { *ok = 1; return SIHTTP_METHOD_HEAD; }

    *ok = 0;
    return SIHTTP_METHOD_GET;
}

sihttp_parse_result_t sihttp_request_parse_state_with_limit(
    const char *data,
    size_t len,
    size_t max_body_bytes
) {
    sihttp_parse_result_t result = { .code = 0, .expected_len = 0 };
    const char *headers_end;
    size_t header_len;
    sihttp_header_state_t header_state = {0};
    int http11 = 0;
    size_t header_count = 0;
    char *copy;
    char *line;

    headers_end = sihttp_find_header_end_const(data, len);
    if (!headers_end) {
        if (len > SIHTTP_MAX_HEADER_BYTES) {
            result.code = 413;
        }
        return result;
    }

    header_len = (size_t)(headers_end - data) + 4;
    if (header_len > SIHTTP_MAX_HEADER_BYTES) {
        result.code = 413;
        return result;
    }

    copy = malloc(header_len + 1);
    if (!copy) {
        result.code = 500;
        return result;
    }
    memcpy(copy, data, header_len);
    copy[header_len] = '\0';

    if (memchr(copy, '\0', header_len) || !strstr(copy, "\r\n")) {
        result.code = 400;
        free(copy);
        return result;
    }
    line = strstr(copy, "\r\n");
    *line = '\0';
    {
        char *method = copy;
        char *target = strchr(method, ' ');
        char *version;
        if (!target || target == method || !(version = strchr(target + 1, ' ')) ||
            version == target + 1 || strchr(version + 1, ' ') ||
            (strcmp(version + 1, "HTTP/1.1") != 0 && strcmp(version + 1, "HTTP/1.0") != 0)) {
            result.code = 400;
            free(copy);
            return result;
        }
        http11 = strcmp(version + 1, "HTTP/1.1") == 0;
        *target++ = '\0';
        *version = '\0';
        for (const unsigned char *p = (const unsigned char *)method; *p; p++) {
            if (!sihttp_tchar(*p)) { result.code = 400; free(copy); return result; }
        }
        if (*target != '/') { result.code = 400; free(copy); return result; }
        for (const unsigned char *p = (const unsigned char *)target; *p; p++) {
            if (*p <= 32 || *p == 127) { result.code = 400; free(copy); return result; }
        }
    }
    while (line) {
        char *line_end;
        char *colon;

        line += 2;
        if (*line == '\r' && line[1] == '\n') {
            break;
        }

        line_end = strstr(line, "\r\n");
        if (!line_end) { result.code = 400; break; }
        *line_end = '\0';

        colon = strchr(line, ':');
        if (!colon || ++header_count > SIHTTP_MAX_HEADERS) { result.code = 400; break; }
        {
            char *name;
            char *value;

            *colon = '\0';
            name = line;
            value = sihttp_trim(colon + 1);
            result.code = sihttp_request_check_header(&header_state, name, value);
            if (result.code != 200) break;
            result.code = 0;
        }

        line = line_end;
    }

    free(copy);
    if (result.code) return result;
    if (http11 && !header_state.has_host) { result.code = 400; return result; }

    if (header_state.content_length > max_body_bytes) {
        result.code = 413;
        return result;
    }

    if (header_state.content_length > SIZE_MAX - header_len) {
        result.code = 413;
        return result;
    }
    result.expected_len = header_len + header_state.content_length;
    if (len >= result.expected_len) {
        result.code = 200;
    }
    return result;
}

sihttp_parse_result_t sihttp_request_parse_state(const char *data, size_t len) {
    return sihttp_request_parse_state_with_limit(data, len, SIHTTP_MAX_BODY_BYTES);
}

int sihttp_request_parse_with_limit(
    sihttp_request_internal_t *req,
    const char *data,
    size_t len,
    sihttp_app_state_t *state,
    size_t max_body_bytes
) {
    sihttp_parse_result_t state_result;
    char *headers_end;
    char *body;
    char *request_line_end;
    char *method;
    char *target;
    char *version;
    char *line;
    int method_ok = 0;

    sihttp_request_internal_init(req);

    state_result = sihttp_request_parse_state_with_limit(data, len, max_body_bytes);
    if (state_result.code != 200) {
        return state_result.code ? state_result.code : 400;
    }

    req->storage = malloc(state_result.expected_len + 1);
    if (!req->storage) {
        return 500;
    }

    memcpy(req->storage, data, state_result.expected_len);
    req->storage[state_result.expected_len] = '\0';
    req->storage_len = state_result.expected_len;

    headers_end = sihttp_find_header_end(req->storage, req->storage_len);
    if (!headers_end) {
        return 400;
    }

    body = headers_end + 4;

    request_line_end = strstr(req->storage, "\r\n");
    if (!request_line_end) {
        return 400;
    }
    *request_line_end = '\0';

    method = req->storage;
    target = strchr(method, ' ');
    if (!target) {
        return 400;
    }
    *target++ = '\0';

    version = strchr(target, ' ');
    if (!version) {
        return 400;
    }
    *version++ = '\0';

    if (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) {
        return 400;
    }

    if (sihttp_request_set_target(req, target) != 0) {
        return 400;
    }

    line = request_line_end + 2;
    while (line < headers_end) {
        char *line_end = strstr(line, "\r\n");
        char *colon;
        char *name;
        char *value;
        if (!line_end) {
            break;
        }
        *line_end = '\0';
        if (req->header_count >= SIHTTP_MAX_HEADERS) {
            return 400;
        }
        colon = strchr(line, ':');
        if (!colon) {
            return 400;
        }
        *colon++ = '\0';
        name = sihttp_trim(line);
        value = sihttp_trim(colon);
        if (!*name) {
            return 400;
        }
        req->headers[req->header_count++] = (sihttp_pair_t){name, value};
        line = line_end + 2;
    }
    *headers_end = '\0';

    sihttp_method_from_name(method, &method_ok);
    req->public_req.method = method;
    if (!method_ok) {
        return 405;
    }

    req->public_req.body = body;
    req->public_req.body_size = state_result.expected_len - (size_t)(body - req->storage);
    req->public_req.state = state;
    return 200;
}

int sihttp_request_parse(
    sihttp_request_internal_t *req,
    const char *data,
    size_t len,
    sihttp_app_state_t *state
) {
    return sihttp_request_parse_with_limit(req, data, len, state, SIHTTP_MAX_BODY_BYTES);
}

static const char *sihttp_pair_get(const sihttp_pair_t *pairs, size_t count, const char *name, bool icase) {
    if (!name) return NULL;
    for (size_t i = 0; i < count; i++) {
        if (icase ? sihttp_streq_icase(pairs[i].name, name) : strcmp(pairs[i].name, name) == 0) {
            return pairs[i].value;
        }
    }
    return NULL;
}

SIHTTP_API const char *sihttp_path_param(const sihttp_request_t *public_req, const char *name) {
    if (!public_req) return NULL;
    const sihttp_request_internal_t *req = (const sihttp_request_internal_t *)public_req;
    return sihttp_pair_get(req->params, req->param_count, name, false);
}

SIHTTP_API const char *sihttp_query(const sihttp_request_t *public_req, const char *name) {
    if (!public_req) return NULL;
    const sihttp_request_internal_t *req = (const sihttp_request_internal_t *)public_req;
    return sihttp_pair_get(req->query, req->query_count, name, false);
}

SIHTTP_API const char *sihttp_header(const sihttp_request_t *public_req, const char *name) {
    if (!public_req) return NULL;
    const sihttp_request_internal_t *req = (const sihttp_request_internal_t *)public_req;
    return sihttp_pair_get(req->headers, req->header_count, name, true);
}

static bool sihttp_parse_u64_strict(const char *value, uint64_t max, uint64_t *out) {
    uint64_t parsed = 0;
    if (!value || !*value || !out) return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        unsigned digit = *p - '0';
        if (parsed > (max - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    *out = parsed;
    return true;
}

SIHTTP_API bool sihttp_path_param_u32(const sihttp_request_t *req, const char *name, uint32_t *out) {
    uint64_t parsed;
    if (!sihttp_parse_u64_strict(sihttp_path_param(req, name), UINT32_MAX, &parsed) || !out) return false;
    *out = (uint32_t)parsed;
    return true;
}

SIHTTP_API bool sihttp_path_param_u16(const sihttp_request_t *req, const char *name, uint16_t *out) {
    uint64_t parsed;
    if (!sihttp_parse_u64_strict(sihttp_path_param(req, name), UINT16_MAX, &parsed) || !out) return false;
    *out = (uint16_t)parsed;
    return true;
}

SIHTTP_API bool sihttp_query_u32(const sihttp_request_t *req, const char *name, uint32_t *out) {
    uint64_t parsed;
    if (!sihttp_parse_u64_strict(sihttp_query(req, name), UINT32_MAX, &parsed) || !out) return false;
    *out = (uint32_t)parsed;
    return true;
}

SIHTTP_API bool sihttp_query_u64(const sihttp_request_t *req, const char *name, uint64_t *out) {
    return sihttp_parse_u64_strict(sihttp_query(req, name), UINT64_MAX, out);
}

static bool sihttp_parse_i64_strict(const char *value, int64_t *out) {
    uint64_t magnitude;
    bool negative;
    if (!value || !*value || !out) return false;
    negative = *value == '-';
    if (*value == '-' || *value == '+') value++;
    if (!sihttp_parse_u64_strict(value, negative ? (uint64_t)INT64_MAX + 1 : INT64_MAX, &magnitude)) return false;
    *out = negative && magnitude == (uint64_t)INT64_MAX + 1 ? INT64_MIN :
           negative ? -(int64_t)magnitude : (int64_t)magnitude;
    return true;
}

SIHTTP_API bool sihttp_path_param_i64(const sihttp_request_t *req, const char *name, int64_t *out) {
    return sihttp_parse_i64_strict(sihttp_path_param(req, name), out);
}

SIHTTP_API bool sihttp_query_i64(const sihttp_request_t *req, const char *name, int64_t *out) {
    return sihttp_parse_i64_strict(sihttp_query(req, name), out);
}

SIHTTP_API bool sihttp_query_bool(const sihttp_request_t *req, const char *name, bool *out) {
    const char *value = sihttp_query(req, name);
    if (!value || !out) return false;
    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) { *out = true; return true; }
    if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) { *out = false; return true; }
    return false;
}

SIHTTP_API int64_t sihttp_param(const sihttp_request_t *public_req, const char *name) {
    const char *value = sihttp_path_param(public_req, name);
    uint64_t parsed;
    if (!sihttp_parse_u64_strict(value, INT64_MAX, &parsed)) return 0;
    return (int64_t)parsed;
}
