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
    test_assert(len > 0);

    free(message);
}

void response_custom_status(void) {
    size_t len = 0;
    char *message = sihttp_build_response(sihttp_response({ .status = 404, .body = NULL }), &len);

    test_not_null(message);
    test_assert(strstr(message, "HTTP/1.1 404 Not Found\r\n") == message);
    test_assert(strstr(message, "Content-Length: 0\r\n") != NULL);
    test_assert(len > 0);

    free(message);
}
