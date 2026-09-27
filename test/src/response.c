#include "sihttp_internal.h"
#include <test.h>

#include <stdlib.h>
#include <math.h>
#include <string.h>

void response_default_status(void) {
    size_t len = 0;
    char *message = sihttp_build_response(sihttp_response({ .body = siformat("%s", "OK") }), &(sihttp_cors_desc_t){.enabled = true}, &len);

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
        NULL, &len
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
    char *message = sihttp_build_response(sihttp_response({ .status = 404, .body = NULL }), NULL, &len);

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
        NULL, &len
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
    char *message = sihttp_build_response(sihttp_response({ .body = siformat("%s", "OK") }), &(sihttp_cors_desc_t){.enabled = true}, &len);

    test_not_null(message);
    test_assert(strstr(message, "Access-Control-Allow-Origin: *\r\n") != NULL);
    test_assert(strstr(message, "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n") != NULL);
    test_assert(strstr(message, "Access-Control-Allow-Headers: Content-Type, Authorization\r\n") != NULL);
    test_assert(len > 0);

    free(message);
}

void response_helpers(void) {
    sihttp_response_t r = sihttp_response_empty(204);
    test_int(r.status, 204);
    test_null(r.body);
    sihttp_response_fini(&r);
    r = sihttp_response_text(201, "hello");
    test_str(r.body, "hello");
    test_uint(r.body_size, 5);
    test_int(r.content_type, SIHTTP_CONTENT_TEXT);
    sihttp_response_fini(&r);
    sijson_value_t object = sijson_make_object();
    test_assert(sijson_object_set(object, "ok", sijson_make_bool(true)));
    r = sihttp_response_json(200, object);
    test_str(r.body, "{\"ok\":true}");
    test_int(r.content_type, SIHTTP_CONTENT_JSON);
    sihttp_response_fini(&r);
    r = sihttp_response_json_error(422, "bad input");
    test_str(r.body, "{\"error\":\"bad input\"}");
    sihttp_response_fini(&r);
    r = sihttp_response_json(200, sijson_make_number(INFINITY));
    test_int(r.status, 500);
    sihttp_response_fini(&r);
    char *bytes = malloc(3);
    bytes[0] = 'A'; bytes[1] = 0; bytes[2] = 'B';
    r = sihttp_response_take_binary(200, bytes, 3);
    sihttp_response_normalize(&r);
    test_uint(r.body_size, 3);
    test_int(r.body[1], 0);
    sihttp_response_fini(&r);
    r = sihttp_response_take_binary(200, malloc(1), 0);
    test_not_null(r.body);
    sihttp_response_normalize(&r);
    test_uint(r.body_size, 0);
    sihttp_response_fini(&r);
}

void response_status_and_cors(void) {
    const int statuses[] = {409, 415, 422, 501, 503};
    const char *reasons[] = {"Conflict", "Unsupported Media Type", "Unprocessable Content", "Not Implemented", "Service Unavailable"};
    for (size_t i = 0; i < 5; i++) {
        size_t len = 0;
        char *message = sihttp_build_response(sihttp_response_empty(statuses[i]), NULL, &len);
        test_not_null(message);
        test_assert(strstr(message, reasons[i]) != NULL);
        test_assert(strstr(message, "Access-Control-Allow-Origin") == NULL);
        free(message);
    }
    size_t len = 0;
    sihttp_cors_desc_t cors = {.enabled = true, .allow_origin = "https://example.test", .allow_methods = "GET", .allow_headers = "Authorization"};
    char *message = sihttp_build_response(sihttp_response_empty(0), &cors, &len);
    test_assert(strstr(message, "HTTP/1.1 200 OK") == message);
    test_assert(strstr(message, "Access-Control-Allow-Origin: https://example.test") != NULL);
    test_assert(strstr(message, "Access-Control-Allow-Methods: GET") != NULL);
    free(message);
}
