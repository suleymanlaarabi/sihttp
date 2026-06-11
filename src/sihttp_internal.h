#ifndef SIHTTP_INTERNAL_H
#define SIHTTP_INTERNAL_H

#include <sihttp.h>

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
    int running;
};

int sihttp_server_handle_client(sihttp_server_t *server, int client_fd);

#endif
