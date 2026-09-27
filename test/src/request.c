#include "sihttp_internal.h"
#include <test.h>

#include <string.h>
#include <stdlib.h>

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

void request_uri_decoding(void) {
    const char *raw = "GET /files/a%2Fb+z?q=hello%20world&value=hello+world&utf=%C3%A9&empty&same=first&same=second HTTP/1.1\r\nHost: localhost\r\n\r\n";
    sihttp_request_internal_t req;
    test_int(sihttp_request_parse(&req, raw, strlen(raw), NULL), 200);
    test_str(req.public_req.path, "/files/a%2Fb+z");
    test_str(sihttp_query(&req.public_req, "q"), "hello world");
    test_str(sihttp_query(&req.public_req, "value"), "hello world");
    test_str(sihttp_query(&req.public_req, "utf"), "é");
    test_str(sihttp_query(&req.public_req, "empty"), "");
    test_str(sihttp_query(&req.public_req, "same"), "first");
    test_assert(sihttp_query(&req.public_req, "missing") == NULL);
    sihttp_request_internal_fini(&req);
    const char *bad[] = {"/bad%", "/bad%2", "/bad%Q0", "/ok?x=%", "/ok?x=%XY", "/bad%00"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char *request = siformat("GET %s HTTP/1.1\r\nHost: localhost\r\n\r\n", bad[i]);
        test_int(sihttp_request_parse(&req, request, strlen(request), NULL), 400);
        sihttp_request_internal_fini(&req);
        free(request);
    }
}

void request_malformed_http(void) {
    const char *bad[] = {
        "GET / HTTP/1.1\r\n\r\n",
        "GET / HTTP/2.0\r\nHost: x\r\n\r\n",
        " / HTTP/1.1\r\nHost: x\r\n\r\n",
        "GET  HTTP/1.1\r\nHost: x\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: x\r\nBad\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: x\r\n Bad: x\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: x\r\nBad Name: x\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: x\r\nBad: x\v\r\n\r\n",
        "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: +1\r\n\r\n",
        "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 1 0\r\n\r\n",
        "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 184467440737095516160\r\n\r\n",
        "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n",
        "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 02\r\nContent-Length: 2\r\n\r\n",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        sihttp_parse_result_t result = sihttp_request_parse_state(bad[i], strlen(bad[i]));
        test_int(result.code, 400);
    }
    const char *chunked = "POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n";
    test_int(sihttp_request_parse_state(chunked, strlen(chunked)).code, 501);
    const char *duplicate = "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\nab";
    test_int(sihttp_request_parse_state(duplicate, strlen(duplicate)).code, 200);
    const char *http10 = "GET / HTTP/1.0\r\n\r\n";
    test_int(sihttp_request_parse_state(http10, strlen(http10)).code, 200);
    char many[1024] = "GET / HTTP/1.1\r\nHost: x\r\n";
    for (int i = 0; i < 64; i++) strcat(many, "X: y\r\n");
    strcat(many, "\r\n");
    test_int(sihttp_request_parse_state(many, strlen(many)).code, 400);
    char *large = malloc(SIHTTP_MAX_HEADER_BYTES + 100);
    test_not_null(large);
    const char *prefix = "GET / HTTP/1.1\r\nHost: x\r\nX: ";
    size_t prefix_len = strlen(prefix);
    memcpy(large, prefix, prefix_len);
    memset(large + prefix_len, 'a', SIHTTP_MAX_HEADER_BYTES);
    memcpy(large + prefix_len + SIHTTP_MAX_HEADER_BYTES, "\r\n\r\n", 5);
    test_int(sihttp_request_parse_state(large, strlen(large)).code, 413);
    free(large);
}

void request_signed_numbers(void) {
    sihttp_request_internal_t req;
    sihttp_request_internal_init(&req);
    int64_t signed_value = 5;
    uint64_t unsigned_value = 5;
    test_int(sihttp_request_add_param(&req, "min", "-9223372036854775808"), 0);
    test_assert(sihttp_path_param_i64(&req.public_req, "min", &signed_value));
    test_assert(signed_value == INT64_MIN);
    test_assert(!sihttp_path_param_i64(&req.public_req, "min", NULL));
    char query[] = "/?max=18446744073709551615&bad=18446744073709551616&signed=%2B42&partial=7x";
    test_int(sihttp_request_set_target(&req, query), 0);
    test_assert(sihttp_query_u64(&req.public_req, "max", &unsigned_value));
    test_assert(unsigned_value == UINT64_MAX);
    test_assert(!sihttp_query_u64(&req.public_req, "bad", &unsigned_value));
    test_assert(!sihttp_query_u64(&req.public_req, "max", NULL));
    test_assert(sihttp_query_i64(&req.public_req, "signed", &signed_value));
    test_int(signed_value, 42);
    test_assert(!sihttp_query_i64(&req.public_req, "partial", &signed_value));
    sihttp_request_internal_fini(&req);
}
