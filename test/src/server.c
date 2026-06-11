#include "sihttp_internal.h"
#include <test.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct sihttp_app_state_s {
    int base;
};

static sihttp_response_t server_user(const sihttp_request_t *req) {
    return sihttp_response(
        { .body = siformat("user=%ld base=%d", sihttp_param(req, "id"), req->state->base) }
    );
}

static char *server_request(sihttp_server_t *server, const char *request) {
    int fds[2];
    test_int(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    ssize_t written = send(fds[0], request, strlen(request), 0);
    test_int(written, (int)strlen(request));
    shutdown(fds[0], SHUT_WR);

    sihttp_server_handle_client(server, fds[1]);
    close(fds[1]);

    char buffer[4096];
    ssize_t received = recv(fds[0], buffer, sizeof(buffer) - 1, 0);
    test_assert(received > 0);
    buffer[received] = '\0';
    close(fds[0]);

    return siformat("%s", buffer);
}

static int server_tcp_client(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;

    test_assert(fd != -1);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    test_int(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr), 1);
    test_int(connect(fd, (const struct sockaddr *)&addr, sizeof(addr)), 0);

    return fd;
}

static char *server_read_client(int fd) {
    char buffer[4096];
    ssize_t received = recv(fd, buffer, sizeof(buffer) - 1, 0);

    test_assert(received > 0);
    buffer[received] = '\0';
    close(fd);

    return siformat("%s", buffer);
}

void server_config(void) {
    struct sihttp_app_state_s state = { .base = 8 };
    sihttp_server_t *server = sihttp_server({
        .port = 4040,
        .state = &state,
    });

    test_not_null(server);
    test_int(sihttp_server_port(server), 4040);
    sihttp_server_fini(server);

    server = sihttp_server({ .port = 70000 });
    test_null(server);
    test_assert(strstr(sihttp_error(), "invalid server port") != NULL);
}

void server_socket_roundtrip(void) {
    struct sihttp_app_state_s state = { .base = 8 };
    sihttp_server_t *server = sihttp_server({ .state = &state });
    test_not_null(server);
    sihttp_get(server, "/users/:id", server_user);

    char *response = server_request(server, "GET /users/34 HTTP/1.1\r\nHost: localhost\r\n\r\n");
    test_assert(strstr(response, "HTTP/1.1 200 OK\r\n") == response);
    test_assert(strstr(response, "Content-Length: 14\r\n") != NULL);
    test_assert(strstr(response, "Content-Type: text/plain; charset=utf-8\r\n") != NULL);
    test_assert(strstr(response, "\r\n\r\nuser=34 base=8") != NULL);

    free(response);
    sihttp_server_fini(server);
}

void server_poll_idle(void) {
    sihttp_server_t *server = sihttp_server({ .port = 0 });
    test_not_null(server);

    test_int(sihttp_server_start(server), 0);
    test_assert(sihttp_server_port(server) != 0);
    test_int(sihttp_server_poll(server), 0);

    sihttp_server_fini(server);
}

void server_poll_roundtrip(void) {
    struct sihttp_app_state_s state = { .base = 8 };
    sihttp_server_t *server = sihttp_server({ .port = 0, .state = &state });
    test_not_null(server);
    sihttp_get(server, "/users/:id", server_user);

    test_int(sihttp_server_start(server), 0);

    int client_fd = server_tcp_client(sihttp_server_port(server));
    const char *request = "GET /users/34 HTTP/1.1\r\nHost: localhost\r\n\r\n";
    ssize_t written = send(client_fd, request, strlen(request), 0);
    test_int(written, (int)strlen(request));
    shutdown(client_fd, SHUT_WR);

    test_int(sihttp_server_poll(server), 1);

    char *response = server_read_client(client_fd);
    test_assert(strstr(response, "HTTP/1.1 200 OK\r\n") == response);
    test_assert(strstr(response, "Content-Length: 14\r\n") != NULL);
    test_assert(strstr(response, "\r\n\r\nuser=34 base=8") != NULL);

    free(response);
    sihttp_server_fini(server);
}

void server_not_found(void) {
    sihttp_server_t *server = sihttp_server({});
    test_not_null(server);

    char *response = server_request(server, "GET /missing HTTP/1.1\r\nHost: localhost\r\n\r\n");
    test_assert(strstr(response, "HTTP/1.1 404 Not Found\r\n") == response);
    test_assert(strstr(response, "Content-Length: 0\r\n") != NULL);
    test_assert(strstr(response, "Content-Type: text/plain; charset=utf-8\r\n") != NULL);

    free(response);
    sihttp_server_fini(server);
}
