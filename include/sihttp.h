#ifndef SIHTTP_H
#define SIHTTP_H

#include "sihttp/bake_config.h"
#include "sijson.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define SIHTTP_PRINTF_FORMAT(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define SIHTTP_PRINTF_FORMAT(fmt, args)
#endif

/* Opaque HTTP server handle. */
typedef struct sihttp_server_s sihttp_server_t;

/* User-defined application state.
 * Define struct sihttp_app_state_s in your application before accessing req->state fields.
 * The state is owned by the user and must outlive the server.
 */
typedef struct sihttp_app_state_s sihttp_app_state_t;

/* Response content type. AUTO defaults to text/plain for string bodies. */
typedef enum {
    SIHTTP_CONTENT_AUTO = 0,
    SIHTTP_CONTENT_TEXT,
    SIHTTP_CONTENT_JSON,
    SIHTTP_CONTENT_HTML,
    SIHTTP_CONTENT_BINARY,
} sihttp_content_type_t;

typedef struct {
    const char *name;
    const char *value;
} sihttp_header_t;

/* Handler response.
 * status defaults to 200 when set to 0.
 * body must be heap-allocated; the server takes ownership and frees it.
 * Add application headers with the helpers below; they copy names and values.
 * Release an in-process result with sihttp_response_fini().
 */
typedef struct {
    int status;
    char *body;
    size_t body_size;
    sihttp_content_type_t content_type;
    sihttp_header_t *headers;
    size_t header_count;
    bool suppress_body; /* Library-managed HEAD serialization state. */
} sihttp_response_t;

/* Incoming HTTP request passed to route handlers. */
typedef struct {
    const char *method;
    const char *path;
    const char *body;
    size_t body_size;
    sihttp_app_state_t *state;
} sihttp_request_t;

typedef sihttp_response_t (*sihttp_handler_t)(const sihttp_request_t *);

/* HTTP methods. GET is the default method when the value is 0. */
typedef enum {
    SIHTTP_METHOD_GET = 0,
    SIHTTP_METHOD_POST,
    SIHTTP_METHOD_PUT,
    SIHTTP_METHOD_DELETE,
    SIHTTP_METHOD_OPTIONS,
    SIHTTP_METHOD_PATCH,
    SIHTTP_METHOD_HEAD,
} sihttp_method_t;

/* Route descriptor used by sihttp_route. */
typedef struct {
    sihttp_method_t method;
    sihttp_handler_t callback;
} sihttp_handler_desc_t;

typedef struct {
    bool enabled;
    const char *allow_origin;
    const char *allow_methods;
    const char *allow_headers;
} sihttp_cors_desc_t;

/* Server configuration.
 * state is owned by the user and must outlive the server.
 * port 0 lets the OS choose a port; backlog uses a library default when set to 0.
 * max_requests_per_poll limits per-frame work; 0 uses a library default.
 * host is copied and defaults to all IPv4 interfaces. CORS is disabled unless
 * cors.enabled is true; omitted allow_* values use the library defaults.
 */
typedef struct {
    const char *host;
    int port;
    sihttp_app_state_t *state;
    int backlog;
    int max_requests_per_poll;
    size_t max_body_bytes;
    sihttp_cors_desc_t cors;
} sihttp_server_desc_t;

/* Server lifecycle. */
#define sihttp_server(...) sihttp_server_init(&(sihttp_server_desc_t)__VA_ARGS__)

SIHTTP_API sihttp_server_t *sihttp_server_init(const sihttp_server_desc_t *desc);
SIHTTP_API void sihttp_server_fini(sihttp_server_t *server);

/* Server runtime. host may be NULL to bind all IPv4 interfaces. */
SIHTTP_API int sihttp_server_listen(sihttp_server_t *server, const char *host, uint16_t port);
SIHTTP_API int sihttp_server_start(sihttp_server_t *server);
SIHTTP_API int sihttp_server_poll(sihttp_server_t *server);
SIHTTP_API int sihttp_server_run(sihttp_server_t *server);
SIHTTP_API void sihttp_server_stop(sihttp_server_t *server);
SIHTTP_API uint16_t sihttp_server_port(const sihttp_server_t *server);

/*
 * Dispatch a registered route synchronously without using the network.
 * path contains only the path and its optional query string. body is NULL
 * when there is no body. No socket, listen, poll, TCP stream parsing, or HTTP
 * header serialization is performed. The returned response belongs to the
 * caller; release it with sihttp_response_fini(). The :name route
 * parameters and query values behave as they do over the
 * network. path and body only need to remain valid until this function returns.
 */
SIHTTP_API sihttp_response_t sihttp_server_dispatch(
    sihttp_server_t *server,
    sihttp_method_t method,
    const char *path,
    const char *body
);

/* Binary-safe variant of sihttp_server_dispatch. */
SIHTTP_API sihttp_response_t sihttp_server_dispatch_bytes(
    sihttp_server_t *server,
    sihttp_method_t method,
    const char *path,
    const void *data,
    size_t size
);

typedef struct {
    sihttp_method_t method;
    const char *path;
    const void *body;
    size_t body_size;
    const sihttp_header_t *headers;
    size_t header_count;
} sihttp_dispatch_desc_t;

SIHTTP_API sihttp_response_t
sihttp_server_dispatch_ex(sihttp_server_t *server, const sihttp_dispatch_desc_t *desc);

SIHTTP_API void sihttp_response_fini(sihttp_response_t *response);
/* Names are case-insensitive. Managed wire headers (Content-Length,
 * Content-Type, Connection and CORS headers) cannot be set here.
 * CR/LF and invalid header names are rejected. Empty values are allowed.
 */
SIHTTP_API bool sihttp_response_set_header(sihttp_response_t *response, const char *name, const char *value);
SIHTTP_API bool sihttp_response_add_header(sihttp_response_t *response, const char *name, const char *value);
SIHTTP_API const char *sihttp_response_header(const sihttp_response_t *response, const char *name);
SIHTTP_API sihttp_response_t sihttp_response_empty(int status);
SIHTTP_API sihttp_response_t sihttp_response_text(int status, const char *text);
SIHTTP_API sihttp_response_t sihttp_response_json(int status, sijson_value_t value);
SIHTTP_API sihttp_response_t sihttp_response_json_error(int status, const char *message);
/* Takes ownership of data, which must be free-compatible. */
SIHTTP_API sihttp_response_t sihttp_response_take_binary(int status, void *data, size_t size);

/* Route registration. */
#define sihttp_route(server, path, ...)                                                            \
    sihttp_route_impl(server, path, &(sihttp_handler_desc_t)__VA_ARGS__)

SIHTTP_API void
sihttp_route_impl(sihttp_server_t *server, const char *path, const sihttp_handler_desc_t *desc);
SIHTTP_API bool sihttp_try_route(sihttp_server_t *server, const char *path, const sihttp_handler_desc_t *desc);

SIHTTP_API void sihttp_get(sihttp_server_t *server, const char *path, sihttp_handler_t callback);
SIHTTP_API void sihttp_post(sihttp_server_t *server, const char *path, sihttp_handler_t callback);
SIHTTP_API void sihttp_put(sihttp_server_t *server, const char *path, sihttp_handler_t callback);
SIHTTP_API void sihttp_delete(sihttp_server_t *server, const char *path, sihttp_handler_t callback);
SIHTTP_API void sihttp_options(sihttp_server_t *server, const char *path, sihttp_handler_t callback);
SIHTTP_API void sihttp_patch(sihttp_server_t *server, const char *path, sihttp_handler_t callback);
SIHTTP_API void sihttp_head(sihttp_server_t *server, const char *path, sihttp_handler_t callback);

/* Returned values remain valid during the handler call. URI decoding is strict:
 * query '+' means space, path parameter '+' stays '+'. Repeated query names
 * return the first value; a present empty value is "", absence is NULL.
 */
SIHTTP_API const char *sihttp_path_param(const sihttp_request_t *req, const char *name);
SIHTTP_API bool sihttp_path_param_u32(const sihttp_request_t *req, const char *name, uint32_t *out);
SIHTTP_API bool sihttp_path_param_u16(const sihttp_request_t *req, const char *name, uint16_t *out);
SIHTTP_API bool sihttp_path_param_i64(const sihttp_request_t *req, const char *name, int64_t *out);
SIHTTP_API const char *sihttp_query(const sihttp_request_t *req, const char *name);
SIHTTP_API bool sihttp_query_u32(const sihttp_request_t *req, const char *name, uint32_t *out);
SIHTTP_API bool sihttp_query_u64(const sihttp_request_t *req, const char *name, uint64_t *out);
SIHTTP_API bool sihttp_query_i64(const sihttp_request_t *req, const char *name, int64_t *out);
SIHTTP_API bool sihttp_query_bool(const sihttp_request_t *req, const char *name, bool *out);
SIHTTP_API const char *sihttp_header(const sihttp_request_t *req, const char *name);
/* Legacy numeric path parameter accessor; returns 0 when absent or invalid. */
SIHTTP_API int64_t sihttp_param(const sihttp_request_t *req, const char *name);

/* Last library error for the current process. */
SIHTTP_API const char *sihttp_error(void);

/* printf-like formatter. Returns a heap-allocated string. */
SIHTTP_API char *siformat(const char *fmt, ...) SIHTTP_PRINTF_FORMAT(1, 2);

/* Build a response value with designated initializers. */
#define sihttp_response(...) ((sihttp_response_t)__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
