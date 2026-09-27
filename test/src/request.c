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
    test_str(sihttp_query(&req.public_req, "page"), "12");
    test_assert(sihttp_path_param(&req.public_req, "page") == NULL);
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
    test_uint(req.public_req.body_size, 15);

    sihttp_request_internal_fini(&req);
}

void request_headers_and_query(void) {
    const char *raw = "GET /items/42?id=7&flag=true HTTP/1.1\r\nHost: localhost\r\nContent-Type:  application/json  \r\nAuthorization: token\r\n\r\n";
    sihttp_request_internal_t req;
    test_int(sihttp_request_parse(&req, raw, strlen(raw), NULL), 200);
    test_str(req.public_req.path, "/items/42");
    test_str(sihttp_header(&req.public_req, "content-type"), "application/json");
    test_str(sihttp_header(&req.public_req, "AUTHORIZATION"), "token");
    test_assert(sihttp_header(&req.public_req, "missing") == NULL);
    test_str(sihttp_query(&req.public_req, "id"), "7");
    test_assert(sihttp_query(&req.public_req, "missing") == NULL);
    uint32_t number = 0;
    bool flag = false;
    test_assert(sihttp_query_u32(&req.public_req, "id", &number));
    test_uint(number, 7);
    test_assert(sihttp_query_bool(&req.public_req, "flag", &flag));
    test_assert(flag);
    sihttp_request_internal_fini(&req);
}

void request_path_numbers(void) {
    sihttp_request_internal_t req;
    sihttp_request_internal_init(&req);
    uint16_t small = 99;
    uint32_t large = 99;
    test_int(sihttp_request_add_param(&req, "id", "0"), 0);
    test_int(sihttp_request_add_param(&req, "max16", "65535"), 0);
    test_int(sihttp_request_add_param(&req, "max32", "4294967295"), 0);
    test_int(sihttp_request_add_param(&req, "overflow16", "65536"), 0);
    test_int(sihttp_request_add_param(&req, "overflow32", "4294967296"), 0);
    test_int(sihttp_request_add_param(&req, "suffix", "1x"), 0);
    test_int(sihttp_request_add_param(&req, "minus", "-1"), 0);
    test_int(sihttp_request_add_param(&req, "plus", "+1"), 0);
    test_int(sihttp_request_add_param(&req, "leading", " 1"), 0);
    test_int(sihttp_request_add_param(&req, "trailing", "1 "), 0);
    test_int(sihttp_request_add_param(&req, "empty", ""), 0);
    test_str(sihttp_path_param(&req.public_req, "id"), "0");
    test_assert(sihttp_path_param(&req.public_req, "absent") == NULL);
    test_assert(sihttp_path_param_u16(&req.public_req, "id", &small));
    test_uint(small, 0);
    test_assert(sihttp_path_param_u16(&req.public_req, "max16", &small));
    test_uint(small, 65535);
    test_assert(sihttp_path_param_u32(&req.public_req, "max32", &large));
    test_uint(large, 4294967295u);
    test_assert(!sihttp_path_param_u16(&req.public_req, "overflow16", &small));
    test_assert(!sihttp_path_param_u32(&req.public_req, "overflow32", &large));
    const char *bad[] = {"suffix", "minus", "plus", "leading", "trailing", "empty", "absent"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        test_assert(!sihttp_path_param_u32(&req.public_req, bad[i], &large));
    }
    test_uint(large, 4294967295u);
    test_int(sihttp_param(&req.public_req, "suffix"), 0);
    sihttp_request_internal_fini(&req);
}
