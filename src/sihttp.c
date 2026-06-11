#include "sihttp_buffer.h"
#include "sihttp_internal.h"
#include "sihttp_route.h"

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

        if (sihttp_set_nonblocking(client_fd) != 0) {
            sihttp_set_error("could not make client socket non-blocking: %s", strerror(errno));
            close(client_fd);
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
