#include "sihttp.h"
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

#ifndef SIHTTP_INTERNAL_H
#define SIHTTP_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#define SIHTTP_MAX_HEADER_BYTES (16u * 1024u)
#define SIHTTP_MAX_BODY_BYTES (1024u * 1024u)
#define SIHTTP_MAX_HEADERS 64u
#define SIHTTP_MAX_PARAMS 16u

typedef struct {
    const char *name;
    const char *value;
} sihttp_pair_t;

typedef struct {
    sihttp_request_t public_req;
    char param_names[SIHTTP_MAX_PARAMS][32];
    char param_values[SIHTTP_MAX_PARAMS][64];
    sihttp_pair_t params[SIHTTP_MAX_PARAMS];
    size_t param_count;
    sihttp_pair_t query[SIHTTP_MAX_PARAMS];
    size_t query_count;
    char *storage;
    size_t storage_len;
} sihttp_request_internal_t;

typedef struct {
    int code;
    size_t expected_len;
} sihttp_parse_result_t;

void sihttp_set_error(const char *fmt, ...) SIHTTP_PRINTF_FORMAT(1, 2);

const char *sihttp_method_name(sihttp_method_t method);
sihttp_method_t sihttp_method_from_name(const char *method, int *ok);

void sihttp_request_internal_init(sihttp_request_internal_t *req);
void sihttp_request_internal_fini(sihttp_request_internal_t *req);
int sihttp_request_add_param(sihttp_request_internal_t *req, const char *name, const char *value);
int sihttp_request_parse(
    sihttp_request_internal_t *req,
    const char *data,
    size_t len,
    sihttp_app_state_t *state
);
sihttp_parse_result_t sihttp_request_parse_state(const char *data, size_t len);

char *sihttp_build_response(sihttp_response_t response, size_t *out_len);
int sihttp_send_response(int fd, sihttp_response_t response);

typedef struct sihttp_route_table_s sihttp_route_table_t;

int sihttp_route_table_init(sihttp_route_table_t *table);
void sihttp_route_table_fini(sihttp_route_table_t *table);
int sihttp_route_table_add(
    sihttp_route_table_t *table,
    sihttp_method_t method,
    const char *path,
    sihttp_handler_t callback
);
sihttp_handler_t sihttp_route_table_match(
    const sihttp_route_table_t *table,
    sihttp_method_t method,
    const char *path,
    sihttp_request_internal_t *req,
    int *method_not_allowed
);

struct sihttp_server_s {
    sihttp_app_state_t *state;
    sihttp_route_table_t *routes;
    int listen_fd;
    uint16_t port;
    int backlog;
    int max_requests_per_poll;
    int running;
};

int sihttp_server_handle_client(sihttp_server_t *server, int client_fd);

#endif

#ifndef SIHTTP_ROUTE_H
#define SIHTTP_ROUTE_H

typedef struct {
    sihttp_method_t method;
    char *path;
    sihttp_handler_t callback;
} sihttp_route_entry_t;

struct sihttp_route_table_s {
    sihttp_route_entry_t *entries;
    size_t count;
    size_t capacity;
};

#endif

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static char sihttp_error_buffer[256];

void sihttp_set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(sihttp_error_buffer, sizeof(sihttp_error_buffer), fmt, args);
    va_end(args);
}

SIHTTP_API const char *sihttp_error(void) {
    return sihttp_error_buffer[0] ? sihttp_error_buffer : NULL;
}

static char *sihttp_static_body(const char *body) {
    size_t len = strlen(body);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, body, len + 1);
    return copy;
}

enum {
    SIHTTP_DEFAULT_BACKLOG = 128,
    SIHTTP_DEFAULT_MAX_REQUESTS_PER_POLL = 64,
};

static int sihttp_set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1) {
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }

    return 0;
}

static sihttp_response_t sihttp_error_response(int status, const char *body) {
    return (sihttp_response_t){ .status = status, .body = sihttp_static_body(body) };
}

static sihttp_response_t sihttp_preflight_response(void) {
    return (sihttp_response_t){ .status = 204, .body = sihttp_static_body("") };
}

SIHTTP_API sihttp_server_t *sihttp_server_init(const sihttp_server_desc_t *desc) {
    sihttp_server_t *server;
    int port = 0;
    int backlog = SIHTTP_DEFAULT_BACKLOG;
    int max_requests_per_poll = SIHTTP_DEFAULT_MAX_REQUESTS_PER_POLL;

    if (desc) {
        if (desc->port < 0 || desc->port > UINT16_MAX) {
            sihttp_set_error("invalid server port: %d", desc->port);
            return NULL;
        }
        port = desc->port;
        backlog = desc->backlog > 0 ? desc->backlog : SIHTTP_DEFAULT_BACKLOG;
        max_requests_per_poll = desc->max_requests_per_poll > 0
            ? desc->max_requests_per_poll
            : SIHTTP_DEFAULT_MAX_REQUESTS_PER_POLL;
    }

    server = calloc(1, sizeof(*server));
    if (!server) {
        sihttp_set_error("out of memory");
        return NULL;
    }

    server->routes = malloc(sizeof(*server->routes));
    if (!server->routes) {
        free(server);
        sihttp_set_error("out of memory");
        return NULL;
    }

    sihttp_route_table_init(server->routes);
    server->port = (uint16_t)port;
    server->backlog = backlog;
    server->max_requests_per_poll = max_requests_per_poll;
    if (desc) {
        server->state = desc->state;
    }
    server->listen_fd = -1;
    return server;
}

SIHTTP_API void sihttp_server_fini(sihttp_server_t *server) {
    if (!server) {
        return;
    }

    sihttp_server_stop(server);
    sihttp_route_table_fini(server->routes);
    free(server->routes);
    free(server);
}

SIHTTP_API int sihttp_server_listen(sihttp_server_t *server, const char *host, uint16_t port) {
    int fd;
    int yes = 1;
    struct sockaddr_in addr;
    socklen_t addr_len;

    if (!server) {
        sihttp_set_error("server is NULL");
        return -1;
    }

    if (server->listen_fd != -1) {
        sihttp_set_error("server is already listening");
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        sihttp_set_error("socket failed: %s", strerror(errno));
        return -1;
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (!host || strcmp(host, "") == 0 || strcmp(host, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        sihttp_set_error("invalid IPv4 host: %s", host);
        return -1;
    }

    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        sihttp_set_error("bind failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, server->backlog) != 0) {
        sihttp_set_error("listen failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    addr_len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &addr_len) == 0) {
        server->port = ntohs(addr.sin_port);
    } else {
        server->port = port;
    }

    server->listen_fd = fd;
    return 0;
}

SIHTTP_API uint16_t sihttp_server_port(const sihttp_server_t *server) {
    return server ? server->port : 0;
}

SIHTTP_API void sihttp_server_stop(sihttp_server_t *server) {
    if (!server) {
        return;
    }

    server->running = 0;
    if (server->listen_fd != -1) {
        int fd = server->listen_fd;
        server->listen_fd = -1;
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
}

int sihttp_server_handle_client(sihttp_server_t *server, int client_fd) {
    sihttp_buffer_t buffer;
    int status = 400;
    sihttp_request_internal_t req;
    int method_ok = 0;
    sihttp_method_t method;
    int method_not_allowed = 0;
    sihttp_handler_t handler;
    sihttp_response_t response;

    sihttp_buffer_init(&buffer);

    for (;;) {
        char chunk[4096];
        sihttp_parse_result_t parse_state;
        ssize_t received = recv(client_fd, chunk, sizeof(chunk), 0);

        if (received < 0) {
            status = 400;
            break;
        }
        if (received == 0) {
            parse_state = sihttp_request_parse_state(buffer.data, buffer.len);
            status = parse_state.code == 200 ? 200 : 400;
            break;
        }

        if (sihttp_buffer_append(&buffer, chunk, (size_t)received) != 0) {
            status = 500;
            break;
        }

        parse_state = sihttp_request_parse_state(buffer.data, buffer.len);
        if (parse_state.code == 200) {
            status = 200;
            break;
        }
        if (parse_state.code != 0) {
            status = parse_state.code;
            break;
        }
    }

    if (status != 200) {
        sihttp_send_response(client_fd, sihttp_error_response(status, ""));
        sihttp_buffer_fini(&buffer);
        return -1;
    }

    status = sihttp_request_parse(&req, buffer.data, buffer.len, server->state);
    if (status != 200) {
        sihttp_send_response(client_fd, sihttp_error_response(status, ""));
        sihttp_buffer_fini(&buffer);
        return -1;
    }

    method = sihttp_method_from_name(req.public_req.method, &method_ok);
    if (!method_ok) {
        sihttp_send_response(client_fd, sihttp_error_response(405, ""));
        sihttp_request_internal_fini(&req);
        sihttp_buffer_fini(&buffer);
        return -1;
    }

    if (method == SIHTTP_METHOD_OPTIONS) {
        sihttp_send_response(client_fd, sihttp_preflight_response());
        sihttp_request_internal_fini(&req);
        sihttp_buffer_fini(&buffer);
        return 0;
    }

    handler = sihttp_route_table_match(
        server->routes,
        method,
        req.public_req.path,
        &req,
        &method_not_allowed
    );

    if (!handler) {
        sihttp_send_response(client_fd, sihttp_error_response(method_not_allowed ? 405 : 404, ""));
        sihttp_request_internal_fini(&req);
        sihttp_buffer_fini(&buffer);
        return -1;
    }

    response = handler(&req.public_req);
    if (sihttp_send_response(client_fd, response) != 0) {
        status = 500;
    }

    sihttp_request_internal_fini(&req);
    sihttp_buffer_fini(&buffer);
    return status == 200 ? 0 : -1;
}

SIHTTP_API int sihttp_server_start(sihttp_server_t *server) {
    if (!server) {
        sihttp_set_error("server is NULL");
        return -1;
    }

    if (server->listen_fd == -1 && sihttp_server_listen(server, NULL, server->port) != 0) {
        return -1;
    }

    if (sihttp_set_nonblocking(server->listen_fd) != 0) {
        sihttp_set_error("could not make server socket non-blocking: %s", strerror(errno));
        return -1;
    }

    server->running = 1;
    return 0;
}

SIHTTP_API int sihttp_server_poll(sihttp_server_t *server) {
    int handled = 0;

    if (!server) {
        sihttp_set_error("server is NULL");
        return -1;
    }

    if (!server->running && sihttp_server_start(server) != 0) {
        return -1;
    }

    while (handled < server->max_requests_per_poll) {
        int client_fd = accept(server->listen_fd, NULL, NULL);

        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return handled;
            }
            if (!server->running || server->listen_fd == -1 || errno == EBADF || errno == EINVAL) {
                return handled;
            }

            sihttp_set_error("accept failed: %s", strerror(errno));
            return -1;
        }

        sihttp_server_handle_client(server, client_fd);
        close(client_fd);
        handled++;
    }

    return handled;
}

SIHTTP_API int sihttp_server_run(sihttp_server_t *server) {
    if (!server) {
        sihttp_set_error("server is NULL");
        return -1;
    }

    if (server->listen_fd == -1 && sihttp_server_listen(server, NULL, server->port) != 0) {
        return -1;
    }

    server->running = 1;
    while (server->running) {
        int client_fd = accept(server->listen_fd, NULL, NULL);
        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            if (!server->running || server->listen_fd == -1 || errno == EBADF || errno == EINVAL) {
                break;
            }
            sihttp_set_error("accept failed: %s", strerror(errno));
            return -1;
        }

        sihttp_server_handle_client(server, client_fd);
        close(client_fd);
    }

    return 0;
}

SIHTTP_API void
sihttp_route_impl(sihttp_server_t *server, const char *path, const sihttp_handler_desc_t *desc) {
    if (!server || !desc) {
        sihttp_set_error("invalid route descriptor");
        return;
    }

    if (sihttp_route_table_add(server->routes, desc->method, path, desc->callback) != 0) {
        sihttp_set_error("could not add route: %s", path ? path : "(null)");
    }
}

SIHTTP_API void sihttp_get(sihttp_server_t *server, const char *path, sihttp_handler_t callback) {
    sihttp_route(server, path, { .method = SIHTTP_METHOD_GET, .callback = callback });
}

SIHTTP_API void sihttp_post(sihttp_server_t *server, const char *path, sihttp_handler_t callback) {
    sihttp_route(server, path, { .method = SIHTTP_METHOD_POST, .callback = callback });
}

SIHTTP_API void sihttp_put(sihttp_server_t *server, const char *path, sihttp_handler_t callback) {
    sihttp_route(server, path, { .method = SIHTTP_METHOD_PUT, .callback = callback });
}

SIHTTP_API void
sihttp_delete(sihttp_server_t *server, const char *path, sihttp_handler_t callback) {
    sihttp_route(server, path, { .method = SIHTTP_METHOD_DELETE, .callback = callback });
}

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

    while (*str && isspace((unsigned char)*str)) {
        str++;
    }

    end = str + strlen(str);
    while (end > str && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';

    return str;
}

static int sihttp_parse_size(const char *value, size_t *out) {
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno || end == value || *sihttp_trim(end) != '\0' || parsed > SIZE_MAX) {
        return -1;
    }

    *out = (size_t)parsed;
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

static void sihttp_parse_query(sihttp_request_internal_t *req, char *query) {
    char *cursor = query;
    while (cursor && *cursor && req->query_count < SIHTTP_MAX_PARAMS) {
        char *next = strchr(cursor, '&');
        char *eq;

        if (next) {
            *next = '\0';
            next++;
        }

        eq = strchr(cursor, '=');
        if (eq) {
            *eq = '\0';
            sihttp_add_pair(req->query, &req->query_count, cursor, eq + 1);
        }

        cursor = next;
    }
}

void sihttp_request_internal_init(sihttp_request_internal_t *req) {
    memset(req, 0, sizeof(*req));
}

void sihttp_request_internal_fini(sihttp_request_internal_t *req) {
    free(req->storage);
    sihttp_request_internal_init(req);
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
    }

    return "GET";
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

    *ok = 0;
    return SIHTTP_METHOD_GET;
}

sihttp_parse_result_t sihttp_request_parse_state(const char *data, size_t len) {
    sihttp_parse_result_t result = { .code = 0, .expected_len = 0 };
    const char *headers_end;
    size_t header_len;
    size_t content_length = 0;
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

    line = strstr(copy, "\r\n");
    while (line) {
        char *line_end;
        char *colon;

        line += 2;
        if (*line == '\r' && line[1] == '\n') {
            break;
        }

        line_end = strstr(line, "\r\n");
        if (!line_end) {
            break;
        }
        *line_end = '\0';

        colon = strchr(line, ':');
        if (colon) {
            char *name;
            char *value;

            *colon = '\0';
            name = sihttp_trim(line);
            value = sihttp_trim(colon + 1);
            if (sihttp_streq_icase(name, "Content-Length") && sihttp_parse_size(value, &content_length) != 0) {
                free(copy);
                result.code = 400;
                return result;
            }
        }

        line = line_end;
    }

    free(copy);

    if (content_length > SIHTTP_MAX_BODY_BYTES) {
        result.code = 413;
        return result;
    }

    result.expected_len = header_len + content_length;
    if (len >= result.expected_len) {
        result.code = 200;
    }
    return result;
}

int sihttp_request_parse(
    sihttp_request_internal_t *req,
    const char *data,
    size_t len,
    sihttp_app_state_t *state
) {
    sihttp_parse_result_t state_result;
    char *headers_end;
    char *body;
    char *request_line_end;
    char *method;
    char *target;
    char *version;
    char *query;
    int method_ok = 0;

    sihttp_request_internal_init(req);

    state_result = sihttp_request_parse_state(data, len);
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
    *headers_end = '\0';

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

    query = strchr(target, '?');
    if (query) {
        *query++ = '\0';
        sihttp_parse_query(req, query);
    }

    sihttp_method_from_name(method, &method_ok);
    if (!method_ok) {
        return 405;
    }

    req->public_req.method = method;
    req->public_req.path = target;
    req->public_req.body = body;
    req->public_req.state = state;
    return 200;
}

SIHTTP_API int64_t sihttp_param(const sihttp_request_t *public_req, const char *name) {
    const sihttp_request_internal_t *req = (const sihttp_request_internal_t *)public_req;

    for (size_t i = 0; i < req->param_count; i++) {
        if (strcmp(req->params[i].name, name) == 0) {
            return strtoll(req->params[i].value, NULL, 10);
        }
    }

    for (size_t i = 0; i < req->query_count; i++) {
        if (strcmp(req->query[i].name, name) == 0) {
            return strtoll(req->query[i].value, NULL, 10);
        }
    }

    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static const char *sihttp_status_reason(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 413:
        return "Payload Too Large";
    case 500:
        return "Internal Server Error";
    }

    return status >= 200 && status < 300 ? "OK" : "Error";
}

static const char *sihttp_content_type_name(sihttp_content_type_t content_type) {
    switch (content_type) {
    case SIHTTP_CONTENT_AUTO:
    case SIHTTP_CONTENT_TEXT:
        return "text/plain; charset=utf-8";
    case SIHTTP_CONTENT_JSON:
        return "application/json";
    case SIHTTP_CONTENT_HTML:
        return "text/html; charset=utf-8";
    case SIHTTP_CONTENT_BINARY:
        return "application/octet-stream";
    }

    return "text/plain; charset=utf-8";
}

static int sihttp_send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t written = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (written <= 0) {
            return -1;
        }
        sent += (size_t)written;
    }

    return 0;
}

char *sihttp_build_response(sihttp_response_t response, size_t *out_len) {
    int status = response.status == 0 ? 200 : response.status;
    const char *reason = sihttp_status_reason(status);
    const char *content_type = sihttp_content_type_name(response.content_type);
    const char *body = response.body ? response.body : "";
    size_t body_len = response.body ? strlen(response.body) : 0;
    int header_len;
    size_t total;
    char *message;

    header_len = snprintf(
        NULL,
        0,
        "HTTP/1.1 %d %s\r\n"
        "Content-Length: %zu\r\n"
        "Content-Type: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "Connection: close\r\n"
        "\r\n",
        status,
        reason,
        body_len,
        content_type
    );
    if (header_len < 0) {
        return NULL;
    }

    total = (size_t)header_len + body_len;
    message = malloc(total + 1);
    if (!message) {
        return NULL;
    }

    snprintf(
        message,
        (size_t)header_len + 1,
        "HTTP/1.1 %d %s\r\n"
        "Content-Length: %zu\r\n"
        "Content-Type: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "Connection: close\r\n"
        "\r\n",
        status,
        reason,
        body_len,
        content_type
    );
    memcpy(message + header_len, body, body_len);
    message[total] = '\0';

    if (out_len) {
        *out_len = total;
    }
    return message;
}

int sihttp_send_response(int fd, sihttp_response_t response) {
    size_t len = 0;
    char *message = sihttp_build_response(response, &len);
    int result;

    if (!message) {
        free(response.body);
        return -1;
    }

    result = sihttp_send_all(fd, message, len);
    free(message);
    free(response.body);
    return result;
}

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

