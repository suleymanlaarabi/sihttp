#include "sihttp_internal.h"
#include <test.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#define SHUT_WR SD_SEND
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <stdlib.h>
#include <string.h>

struct sihttp_app_state_s {
    int base;
};

static sihttp_response_t server_user(const sihttp_request_t *req) {
    return sihttp_response(
        { .body = siformat("user=%ld base=%d", sihttp_param(req, "id"), req->state->base) }
    );
}

 static int server_socketpair(int fds[2]) {
#ifdef _WIN32
    SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
    SOCKET client = INVALID_SOCKET;
    SOCKET server = INVALID_SOCKET;
    struct sockaddr_in address;
    int address_len = sizeof(address);
    int yes = 1;

    if (listener == INVALID_SOCKET) {
        return -1;
    }
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);
    if (bind(listener, (const struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR ||
        listen(listener, 1) == SOCKET_ERROR ||
        getsockname(listener, (struct sockaddr *)&address, &address_len) == SOCKET_ERROR) {
        closesocket(listener);
        return -1;
    }

    client = socket(AF_INET, SOCK_STREAM, 0);
    if (client == INVALID_SOCKET ||
        connect(client, (const struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        if (client != INVALID_SOCKET) {
            closesocket(client);
        }
        closesocket(listener);
        return -1;
    }

    server = accept(listener, NULL, NULL);
    closesocket(listener);
    if (server == INVALID_SOCKET) {
        closesocket(client);
        return -1;
    }

    fds[0] = (int)client;
    fds[1] = (int)server;
    return 0;
#else
    return socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
#endif
}

static sihttp_response_t server_dispatch_handler(const sihttp_request_t *req) {
    (void)req;
    return sihttp_response({ .status = 200, .body = siformat("dispatch-ok") });
}

static sihttp_response_t server_dispatch_param_handler(const sihttp_request_t *req) {
    return sihttp_response({
        .status = 200,
        .body = siformat("%ld", sihttp_param(req, "index"))
    });
}

static sihttp_response_t server_dispatch_body_handler(const sihttp_request_t *req) {
    return sihttp_response({ .status = 200, .body = siformat("%s", req->body) });
}

static sihttp_response_t server_dispatch_binary_handler(const sihttp_request_t *req) {
    char *body = malloc(req->body_size);
    test_not_null(body);
    memcpy(body, req->body, req->body_size);
    return sihttp_response({
        .status = 200,
        .body = body,
        .body_size = req->body_size,
        .content_type = SIHTTP_CONTENT_BINARY,
    });
}

static char *server_request(sihttp_server_t *server, const char *request) {
    int fds[2];
    test_int(server_socketpair(fds), 0);

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

void server_cors_preflight(void) {
    sihttp_server_t *server = sihttp_server({ .cors = {.enabled = true} });
    test_not_null(server);

    char *response = server_request(
        server,
        "OPTIONS /entities HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Origin: http://localhost:5173\r\n"
        "Access-Control-Request-Method: POST\r\n"
        "\r\n"
    );
    test_assert(strstr(response, "HTTP/1.1 204 No Content\r\n") == response);
    test_assert(strstr(response, "Content-Length: 0\r\n") != NULL);
    test_assert(strstr(response, "Access-Control-Allow-Origin: *\r\n") != NULL);
    test_assert(strstr(response, "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n") != NULL);

    free(response);
    sihttp_server_fini(server);
}

void server_dispatch_exact(void) {
    sihttp_server_t *server = sihttp_server({});
    test_not_null(server);
    sihttp_get(server, "/dispatch", server_dispatch_handler);

    sihttp_response_t response = sihttp_server_dispatch(
        server,
        SIHTTP_METHOD_GET,
        "/dispatch",
        NULL
    );
    test_int(response.status, 200);
    test_str(response.body, "dispatch-ok");
    sihttp_response_fini(&response);
    sihttp_server_fini(server);
}

void server_dispatch_param(void) {
    sihttp_server_t *server = sihttp_server({});
    test_not_null(server);
    sihttp_get(server, "/entities/:index", server_dispatch_param_handler);

    sihttp_response_t response = sihttp_server_dispatch(
        server,
        SIHTTP_METHOD_GET,
        "/entities/42",
        NULL
    );
    test_int(response.status, 200);
    test_str(response.body, "42");
    sihttp_response_fini(&response);
    sihttp_server_fini(server);
}

void server_dispatch_body(void) {
    sihttp_server_t *server = sihttp_server({});
    test_not_null(server);
    sihttp_post(server, "/dispatch-body", server_dispatch_body_handler);

    sihttp_response_t response = sihttp_server_dispatch(
        server,
        SIHTTP_METHOD_POST,
        "/dispatch-body",
        "{\"value\":123}"
    );
    test_int(response.status, 200);
    test_str(response.body, "{\"value\":123}");
    sihttp_response_fini(&response);
    sihttp_server_fini(server);
}

void server_dispatch_bytes(void) {
    sihttp_server_t *server = sihttp_server({});
    test_not_null(server);
    sihttp_post(server, "/dispatch-bytes", server_dispatch_binary_handler);

    const unsigned char body[] = { 'A', 0, 'B', 'C' };
    sihttp_response_t response = sihttp_server_dispatch_bytes(
        server, SIHTTP_METHOD_POST, "/dispatch-bytes", body, sizeof(body)
    );
    test_int(response.status, 200);
    test_int(response.body_size, sizeof(body));
    test_assert(memcmp(response.body, body, sizeof(body)) == 0);
    sihttp_response_fini(&response);
    sihttp_server_fini(server);
}

void server_dispatch_not_found(void) {
    sihttp_server_t *server = sihttp_server({});
    test_not_null(server);

    sihttp_response_t response = sihttp_server_dispatch(
        server,
        SIHTTP_METHOD_GET,
        "/missing",
        NULL
    );
    test_int(response.status, 404);
    sihttp_response_fini(&response);
    sihttp_server_fini(server);
}

void server_dispatch_method_not_allowed(void) {
    sihttp_server_t *server = sihttp_server({});
    test_not_null(server);
    sihttp_get(server, "/dispatch", server_dispatch_handler);

    sihttp_response_t response = sihttp_server_dispatch(
        server,
        SIHTTP_METHOD_POST,
        "/dispatch",
        NULL
    );
    test_int(response.status, 405);
    sihttp_response_fini(&response);
    sihttp_server_fini(server);
}

static int server_calls;
static sihttp_response_t server_inspect(const sihttp_request_t *req) {
    server_calls++;
    const char *id = sihttp_path_param(req, "id");
    const char *query = sihttp_query(req, "id");
    const char *auth = sihttp_header(req, "authorization");
    return sihttp_response({.body = siformat("%s|%s|%s|%s", req->path, id ? id : "", query ? query : "", auth ? auth : "")});
}

void server_dispatch_features(void) {
    sihttp_server_t *server = sihttp_server({.max_body_bytes = 3, .host = "127.0.0.1"});
    test_not_null(server);
    sihttp_get(server, "/items/:id", server_inspect);
    server_calls = 0;
    sihttp_header_t headers[] = {{"Authorization", " token "}};
    sihttp_dispatch_desc_t desc = {.method = SIHTTP_METHOD_GET, .path = "/items/42?id=7",
        .body = "abc", .body_size = 3, .headers = headers, .header_count = 1};
    sihttp_response_t response = sihttp_server_dispatch_ex(server, &desc);
    test_int(response.status, 200);
    test_str(response.body, "/items/42|42|7|token");
    test_int(server_calls, 1);
    sihttp_response_fini(&response);
    desc.body_size = 4;
    response = sihttp_server_dispatch_ex(server, &desc);
    test_int(response.status, 413);
    test_int(server_calls, 1);
    sihttp_response_fini(&response);
    desc.body_size = 2;
    response = sihttp_server_dispatch_ex(server, &desc);
    test_int(response.status, 200);
    test_int(server_calls, 2);
    sihttp_response_fini(&response);
    char *wire = server_request(server, "GET /items/42?id=7 HTTP/1.1\r\nHost: localhost\r\nAuthorization: token\r\n\r\n");
    test_assert(strstr(wire, "HTTP/1.1 200 OK") == wire);
    test_assert(strstr(wire, "\r\n\r\n/items/42|42|7|token") != NULL);
    free(wire);
    test_int(sihttp_server_start(server), 0);
    test_assert(server->host && strcmp(server->host, "127.0.0.1") == 0);
    sihttp_server_fini(server);
}

static sihttp_response_t server_options_handler(const sihttp_request_t *req) {
    (void)req;
    return sihttp_response_text(202, "explicit");
}

void server_options_and_cors(void) {
    sihttp_server_t *server = sihttp_server({.cors = {.enabled = true, .allow_origin = "https://example.test"}});
    test_not_null(server);
    sihttp_options(server, "/explicit", server_options_handler);
    sihttp_response_t response = sihttp_server_dispatch(server, SIHTTP_METHOD_OPTIONS, "/explicit", NULL);
    test_int(response.status, 202);
    test_str(response.body, "explicit");
    sihttp_response_fini(&response);
    response = sihttp_server_dispatch(server, SIHTTP_METHOD_OPTIONS, "/auto", NULL);
    test_int(response.status, 204);
    sihttp_response_fini(&response);
    char *wire = server_request(server, "OPTIONS /explicit HTTP/1.1\r\nHost: localhost\r\n\r\n");
    test_assert(strstr(wire, "HTTP/1.1 202 Accepted") == wire);
    free(wire);
    wire = server_request(server, "OPTIONS /auto HTTP/1.1\r\nHost: localhost\r\n\r\n");
    test_assert(strstr(wire, "Access-Control-Allow-Origin: https://example.test") != NULL);
    free(wire);
    sihttp_server_fini(server);
    server = sihttp_server({});
    response = sihttp_server_dispatch(server, SIHTTP_METHOD_OPTIONS, "/auto", NULL);
    test_int(response.status, 404);
    sihttp_response_fini(&response);
    wire = server_request(server, "GET /missing HTTP/1.1\r\nHost: localhost\r\n\r\n");
    test_assert(strstr(wire, "Access-Control-Allow-Origin") == NULL);
    free(wire);
    sihttp_server_fini(server);
}

void server_network_body_limit(void) {
    sihttp_server_t *server = sihttp_server({.max_body_bytes = 3});
    sihttp_post(server, "/body", server_dispatch_handler);
    char *wire = server_request(server, "POST /body HTTP/1.1\r\nHost: localhost\r\nContent-Length: 3\r\n\r\nabc");
    test_assert(strstr(wire, "HTTP/1.1 200 OK") == wire);
    free(wire);
    wire = server_request(server, "POST /body HTTP/1.1\r\nHost: localhost\r\nContent-Length: 2\r\n\r\nab");
    test_assert(strstr(wire, "HTTP/1.1 200 OK") == wire);
    free(wire);
    wire = server_request(server, "POST /body HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\nabcd");
    test_assert(strstr(wire, "HTTP/1.1 413 Payload Too Large") == wire);
    free(wire);
    sihttp_server_fini(server);
}
