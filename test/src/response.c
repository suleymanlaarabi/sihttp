#include "sihttp_internal.h"
#include <test.h>

#include <stdlib.h>
#include <string.h>

void response_default_status(void) {
    size_t len = 0;
    char *message = sihttp_build_response(sihttp_response({ .body = siformat("%s", "OK") }), &len);

    test_not_null(message);
    test_assert(strstr(message, "HTTP/1.1 200 OK\r\n") == message);
    test_assert(strstr(message, "Content-Length: 2\r\n") != NULL);
    test_assert(strstr(message, "Content-Type: text/plain; charset=utf-8\r\n") != NULL);
    test_assert(strstr(message, "Access-Control-Allow-Origin: *\r\n") != NULL);
    test_assert(len > 0);

    free(message);
}

void response_binary_keeps_null_bytes(void) {
    size_t len = 0;
    char *body = malloc(4);
    body[0] = 'S';
    body[1] = 0;
    body[2] = 'N';
    body[3] = '!';
    char *message = sihttp_build_response(
        sihttp_response({
            .body = body,
            .body_size = 4,
            .content_type = SIHTTP_CONTENT_BINARY,
        }),
        &len
    );

    test_not_null(message);
    test_assert(strstr(message, "Content-Length: 4\r\n") != NULL);
    test_int(0, (unsigned char)message[len - 3]);
    test_int('N', message[len - 2]);
    test_int('!', message[len - 1]);
    free(message);
    free(body);
}

void response_custom_status(void) {
    size_t len = 0;
    char *message = sihttp_build_response(sihttp_response({ .status = 404, .body = NULL }), &len);

    test_not_null(message);
    test_assert(strstr(message, "HTTP/1.1 404 Not Found\r\n") == message);
    test_assert(strstr(message, "Content-Length: 0\r\n") != NULL);
    test_assert(strstr(message, "Content-Type: text/plain; charset=utf-8\r\n") != NULL);
    test_assert(len > 0);

    free(message);
}

void response_json_content_type(void) {
    size_t len = 0;
    char *message = sihttp_build_response(
        sihttp_response({ .body = siformat("%s", "{\"ok\":true}"), .content_type = SIHTTP_CONTENT_JSON }),
        &len
    );

    test_not_null(message);
    test_assert(strstr(message, "HTTP/1.1 200 OK\r\n") == message);
    test_assert(strstr(message, "Content-Length: 11\r\n") != NULL);
    test_assert(strstr(message, "Content-Type: application/json\r\n") != NULL);
    test_assert(strstr(message, "\r\n\r\n{\"ok\":true}") != NULL);
    test_assert(len > 0);

    free(message);
}

void response_cors_headers(void) {
    size_t len = 0;
    char *message = sihttp_build_response(sihttp_response({ .body = siformat("%s", "OK") }), &len);

    test_not_null(message);
    test_assert(strstr(message, "Access-Control-Allow-Origin: *\r\n") != NULL);
    test_assert(strstr(message, "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n") != NULL);
    test_assert(strstr(message, "Access-Control-Allow-Headers: Content-Type, Authorization\r\n") != NULL);
    test_assert(len > 0);

    free(message);
}
