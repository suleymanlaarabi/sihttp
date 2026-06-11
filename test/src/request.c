#include "sihttp_internal.h"
#include <test.h>

#include <string.h>

struct sihttp_app_state_s {
    int value;
};

void request_parse_get_query(void) {
    struct sihttp_app_state_s state = { .value = 42 };
    const char *raw = "GET /page?page=12 HTTP/1.1\r\nHost: localhost\r\n\r\n";

    sihttp_request_internal_t req;
    int status = sihttp_request_parse(&req, raw, strlen(raw), &state);

    test_int(status, 200);
    test_str(req.public_req.method, "GET");
    test_str(req.public_req.path, "/page");
    test_int(sihttp_param(&req.public_req, "page"), 12);
    test_int(req.public_req.state->value, 42);

    sihttp_request_internal_fini(&req);
}

void request_parse_post_body(void) {
    const char *raw =
        "POST /login HTTP/1.1\r\nHost: localhost\r\nContent-Length: 15\r\n\r\n{\"user\":\"root\"}";

    sihttp_request_internal_t req;
    int status = sihttp_request_parse(&req, raw, strlen(raw), NULL);

    test_int(status, 200);
    test_str(req.public_req.method, "POST");
    test_str(req.public_req.path, "/login");
    test_str(req.public_req.body, "{\"user\":\"root\"}");

    sihttp_request_internal_fini(&req);
}
