#include "sihttp_buffer.h"
#include "sihttp_internal.h"
#include "sihttp_route.h"

#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#define close closesocket
#define MSG_NOSIGNAL 0
#define ssize_t int
#define socklen_t int
#undef errno
#undef EINTR
#undef EAGAIN
#undef EWOULDBLOCK
#undef EBADF
#undef EINVAL
#define errno WSAGetLastError()
#define EINTR WSAEINTR
#define EAGAIN WSAEWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#define EBADF WSAEBADF
#define EINVAL WSAEINVAL
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static char sihttp_error_buffer[256];

static char *sihttp_trim_header_value(char *value) {
    char *end;
    while (*value && isspace((unsigned char)*value)) value++;
    end = value + strlen(value);
    while (end > value && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return value;
}

void sihttp_set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(sihttp_error_buffer, sizeof(sihttp_error_buffer), fmt, args);
    va_end(args);
}

SIHTTP_API const char *sihttp_error(void) {
    return sihttp_error_buffer[0] ? sihttp_error_buffer : NULL;
}

enum {
    SIHTTP_DEFAULT_BACKLOG = 128,
    SIHTTP_DEFAULT_MAX_REQUESTS_PER_POLL = 64,
};

static int sihttp_set_nonblocking(int fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket((SOCKET)fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1) {
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }

    return 0;
#endif
}

static sihttp_response_t sihttp_dispatch_request(
    sihttp_server_t *server,
    sihttp_method_t method,
    sihttp_request_internal_t *req
) {
    int method_not_allowed = 0;
    sihttp_handler_t handler;

    handler = sihttp_route_table_match(
        server->routes,
        method,
        req->public_req.path,
        req,
        &method_not_allowed
    );
    if (!handler) {
        if (method == SIHTTP_METHOD_OPTIONS && server->cors.enabled) {
            return sihttp_response_empty(204);
        }
        return sihttp_response_empty(method_not_allowed ? 405 : 404);
    }

    return handler(&req->public_req);
}

SIHTTP_API sihttp_server_t *sihttp_server_init(const sihttp_server_desc_t *desc) {
    sihttp_server_t *server;
    int port = 0;
    int backlog = SIHTTP_DEFAULT_BACKLOG;
    int max_requests_per_poll = SIHTTP_DEFAULT_MAX_REQUESTS_PER_POLL;
    size_t max_body_bytes = SIHTTP_MAX_BODY_BYTES;

#ifdef _WIN32
    {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            sihttp_set_error("WSAStartup failed");
            return NULL;
        }
    }
#endif

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
        if (desc->max_body_bytes > 0) {
            max_body_bytes = desc->max_body_bytes;
        }
    }

    server = calloc(1, sizeof(*server));
    if (!server) {
#ifdef _WIN32
        WSACleanup();
#endif
        sihttp_set_error("out of memory");
        return NULL;
    }

    server->routes = malloc(sizeof(*server->routes));
    if (!server->routes) {
        free(server);
#ifdef _WIN32
        WSACleanup();
#endif
        sihttp_set_error("out of memory");
        return NULL;
    }

    sihttp_route_table_init(server->routes);
    server->port = (uint16_t)port;
    server->backlog = backlog;
    server->max_requests_per_poll = max_requests_per_poll;
    server->max_body_bytes = max_body_bytes;
    if (desc) {
        server->state = desc->state;
        server->cors = desc->cors;
        if (desc->host) {
            size_t host_len = strlen(desc->host);
            server->host = malloc(host_len + 1);
            if (!server->host) {
                sihttp_route_table_fini(server->routes);
                free(server->routes);
                free(server);
                sihttp_set_error("out of memory");
                return NULL;
            }
            memcpy(server->host, desc->host, host_len + 1);
        }
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
    free(server->host);
    free(server);
#ifdef _WIN32
    WSACleanup();
#endif
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

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

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
#ifdef _WIN32
        shutdown(fd, SD_BOTH);
#else
        shutdown(fd, SHUT_RDWR);
#endif
        close(fd);
    }
}

int sihttp_server_handle_client(sihttp_server_t *server, int client_fd) {
    sihttp_buffer_t buffer;
    int status = 400;
    sihttp_request_internal_t req;
    int method_ok = 0;
    sihttp_method_t method;
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
            parse_state = sihttp_request_parse_state_with_limit(
                buffer.data, buffer.len, server->max_body_bytes
            );
            status = parse_state.code ? parse_state.code : 400;
            break;
        }

        if (sihttp_buffer_append(&buffer, chunk, (size_t)received) != 0) {
            status = 500;
            break;
        }

        parse_state = sihttp_request_parse_state_with_limit(
            buffer.data, buffer.len, server->max_body_bytes
        );
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
        sihttp_send_response(client_fd, sihttp_response_empty(status), &server->cors);
        sihttp_buffer_fini(&buffer);
        return -1;
    }

    status = sihttp_request_parse_with_limit(
        &req, buffer.data, buffer.len, server->state, server->max_body_bytes
    );
    if (status != 200) {
        sihttp_send_response(client_fd, sihttp_response_empty(status), &server->cors);
        sihttp_request_internal_fini(&req);
        sihttp_buffer_fini(&buffer);
        return -1;
    }

    method = sihttp_method_from_name(req.public_req.method, &method_ok);
    if (!method_ok) {
        sihttp_send_response(client_fd, sihttp_response_empty(405), &server->cors);
        sihttp_request_internal_fini(&req);
        sihttp_buffer_fini(&buffer);
        return -1;
    }

    response = sihttp_dispatch_request(server, method, &req);
    sihttp_response_normalize(&response);
    if (sihttp_send_response(client_fd, response, &server->cors) != 0) {
        status = 500;
    }

    sihttp_request_internal_fini(&req);
    sihttp_buffer_fini(&buffer);
    return status == 200 ? 0 : -1;
}

SIHTTP_API sihttp_response_t sihttp_server_dispatch(
    sihttp_server_t *server,
    sihttp_method_t method,
    const char *path,
    const char *body
) {
    return sihttp_server_dispatch_bytes(server, method, path, body, body ? strlen(body) : 0);
}

SIHTTP_API sihttp_response_t sihttp_server_dispatch_bytes(
    sihttp_server_t *server,
    sihttp_method_t method,
    const char *path,
    const void *data,
    size_t size
) {
    sihttp_dispatch_desc_t desc = {.method = method, .path = path, .body = data, .body_size = size};
    return sihttp_server_dispatch_ex(server, &desc);
}

SIHTTP_API sihttp_response_t
sihttp_server_dispatch_ex(sihttp_server_t *server, const sihttp_dispatch_desc_t *desc) {
    sihttp_request_internal_t req;
    sihttp_response_t response;
    size_t target_len;
    size_t header_bytes = 0;
    char *cursor;
    if (!server || !desc || !desc->path || (desc->body_size && !desc->body) ||
        (desc->header_count && !desc->headers) || desc->header_count > SIHTTP_MAX_HEADERS) {
        return sihttp_response_empty(400);
    }
    if (desc->body_size > server->max_body_bytes) return sihttp_response_empty(413);
    sihttp_request_internal_init(&req);
    target_len = strlen(desc->path);
    req.storage = malloc(target_len + 1);
    if (!req.storage) return sihttp_response_empty(500);
    memcpy(req.storage, desc->path, target_len + 1);
    if (sihttp_request_set_target(&req, req.storage) != 0) {
        sihttp_request_internal_fini(&req);
        return sihttp_response_empty(400);
    }
    for (size_t i = 0; i < desc->header_count; i++) {
        size_t name_len;
        size_t value_len;
        if (!desc->headers[i].name || !desc->headers[i].value) {
            sihttp_request_internal_fini(&req);
            return sihttp_response_empty(400);
        }
        name_len = strlen(desc->headers[i].name);
        value_len = strlen(desc->headers[i].value);
        if (header_bytes > SIZE_MAX - 2 || name_len > SIZE_MAX - header_bytes - 2 ||
            value_len > SIZE_MAX - header_bytes - name_len - 2) {
            sihttp_request_internal_fini(&req);
            return sihttp_response_empty(400);
        }
        header_bytes += name_len + value_len + 2;
    }
    if (header_bytes) {
        req.header_storage = malloc(header_bytes);
        if (!req.header_storage) {
            sihttp_request_internal_fini(&req);
            return sihttp_response_empty(500);
        }
        cursor = req.header_storage;
        for (size_t i = 0; i < desc->header_count; i++) {
            size_t n = strlen(desc->headers[i].name);
            memcpy(cursor, desc->headers[i].name, n + 1);
            req.headers[i].name = sihttp_trim_header_value(cursor);
            cursor += n + 1;
            n = strlen(desc->headers[i].value);
            memcpy(cursor, desc->headers[i].value, n + 1);
            req.headers[i].value = sihttp_trim_header_value(cursor);
            cursor += n + 1;
        }
        req.header_count = desc->header_count;
    }
    req.public_req.method = sihttp_method_name(desc->method);
    req.public_req.body = desc->body;
    req.public_req.body_size = desc->body_size;
    req.public_req.state = server->state;
    response = sihttp_dispatch_request(server, desc->method, &req);
    sihttp_response_normalize(&response);
    sihttp_request_internal_fini(&req);
    return response;
}

SIHTTP_API int sihttp_server_start(sihttp_server_t *server) {
    if (!server) {
        sihttp_set_error("server is NULL");
        return -1;
    }

    if (server->listen_fd == -1 && sihttp_server_listen(server, server->host, server->port) != 0) {
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

    if (server->listen_fd == -1 && sihttp_server_listen(server, server->host, server->port) != 0) {
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

SIHTTP_API void sihttp_options(sihttp_server_t *server, const char *path, sihttp_handler_t callback) {
    sihttp_route(server, path, { .method = SIHTTP_METHOD_OPTIONS, .callback = callback });
}
