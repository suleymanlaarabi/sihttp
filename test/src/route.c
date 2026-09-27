#include "sihttp_route.h"
#include <test.h>

#include <string.h>

static sihttp_response_t route_noop(const sihttp_request_t *req) {
    return sihttp_response({ .body = siformat("%s", req->path) });
}

void route_exact(void) {
    sihttp_route_table_t routes;
    sihttp_route_table_init(&routes);
    test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_GET, "/hello", route_noop), 0);

    const char *raw = "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n";
    sihttp_request_internal_t req;
    test_int(sihttp_request_parse(&req, raw, strlen(raw), NULL), 200);

    int method_not_allowed = 0;
    sihttp_handler_t handler =
        sihttp_route_table_match(&routes, SIHTTP_METHOD_GET, req.public_req.path, &req, &method_not_allowed);
    test_not_null(handler);
    test_int(method_not_allowed, 0);

    sihttp_request_internal_fini(&req);
    sihttp_route_table_fini(&routes);
}

void route_param(void) {
    sihttp_route_table_t routes;
    sihttp_route_table_init(&routes);
    test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_GET, "/users/:id", route_noop), 0);

    const char *raw = "GET /users/42 HTTP/1.1\r\nHost: localhost\r\n\r\n";
    sihttp_request_internal_t req;
    test_int(sihttp_request_parse(&req, raw, strlen(raw), NULL), 200);

    int method_not_allowed = 0;
    sihttp_handler_t handler =
        sihttp_route_table_match(&routes, SIHTTP_METHOD_GET, req.public_req.path, &req, &method_not_allowed);
    test_not_null(handler);
    test_int(sihttp_param(&req.public_req, "id"), 42);

    sihttp_request_internal_fini(&req);
    sihttp_route_table_fini(&routes);
}

void route_method_not_allowed(void) {
    sihttp_route_table_t routes;
    sihttp_route_table_init(&routes);
    test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_POST, "/login", route_noop), 0);

    const char *raw = "GET /login HTTP/1.1\r\nHost: localhost\r\n\r\n";
    sihttp_request_internal_t req;
    test_int(sihttp_request_parse(&req, raw, strlen(raw), NULL), 200);

    int method_not_allowed = 0;
    sihttp_handler_t handler =
        sihttp_route_table_match(&routes, SIHTTP_METHOD_GET, req.public_req.path, &req, &method_not_allowed);
    test_null(handler);
    test_int(method_not_allowed, 1);

    sihttp_request_internal_fini(&req);
    sihttp_route_table_fini(&routes);
}

void route_multiple_and_rollback(void) {
    sihttp_route_table_t routes;
    sihttp_request_internal_t req;
    int method_not_allowed = 0;
    sihttp_route_table_init(&routes);
    test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_GET, "/:first/nope", route_noop), 0);
    test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_GET, "/:one/:second", route_noop), 0);
    sihttp_request_internal_init(&req);
    sihttp_handler_t handler = sihttp_route_table_match(&routes, SIHTTP_METHOD_GET,
        "/a/longer", &req, &method_not_allowed);
    test_not_null(handler);
    test_uint(req.param_count, 2);
    test_str(sihttp_path_param(&req.public_req, "one"), "a");
    test_str(sihttp_path_param(&req.public_req, "second"), "longer");
    test_assert(sihttp_path_param(&req.public_req, "first") == NULL);
    sihttp_request_internal_fini(&req);
    sihttp_route_table_fini(&routes);
}

void route_precedence_and_validation(void) {
    for (int reverse = 0; reverse < 2; reverse++) {
        sihttp_route_table_t routes;
        sihttp_request_internal_t req;
        int other = 0;
        sihttp_route_table_init(&routes);
        if (reverse) test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_GET, "/users/me", route_noop), 0);
        test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_GET, "/users/:id", route_noop), 0);
        if (!reverse) test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_GET, "/users/me", route_noop), 0);
        test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_POST, "/users/me", route_noop), 0);
        test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_GET, "/users/:id/details", route_noop), 0);
        test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_GET, "/users/me", route_noop), -1);
        test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_GET, "/:id/:id", route_noop), -1);
        test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_GET, "/users/:", route_noop), -1);
        test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_GET, "/users/:bad-name", route_noop), -1);
        test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_GET, "users", route_noop), -1);
        test_int(sihttp_route_table_add(&routes, SIHTTP_METHOD_GET, "/users//me", route_noop), -1);
        sihttp_request_internal_init(&req);
        test_not_null(sihttp_route_table_match(&routes, SIHTTP_METHOD_GET, "/users/me", &req, &other));
        test_uint(req.param_count, 0);
        test_not_null(sihttp_route_table_match(&routes, SIHTTP_METHOD_GET, "/users/a%2Fb", &req, &other));
        test_str(sihttp_path_param(&req.public_req, "id"), "a/b");
        req.param_count = 0;
        test_not_null(sihttp_route_table_match(&routes, SIHTTP_METHOD_GET, "/users/me/details", &req, &other));
        test_str(sihttp_path_param(&req.public_req, "id"), "me");
        req.param_count = 0;
        test_null(sihttp_route_table_match(&routes, SIHTTP_METHOD_GET, "/users", &req, &other));
        test_int(other, 0);
        sihttp_request_internal_fini(&req);
        sihttp_route_table_fini(&routes);
    }
    sihttp_route_table_t tied;
    sihttp_request_internal_t req;
    int other = 0;
    sihttp_route_table_init(&tied);
    test_int(sihttp_route_table_add(&tied, SIHTTP_METHOD_GET, "/:first/a", route_noop), 0);
    test_int(sihttp_route_table_add(&tied, SIHTTP_METHOD_GET, "/b/:second", route_noop), 0);
    sihttp_request_internal_init(&req);
    test_not_null(sihttp_route_table_match(&tied, SIHTTP_METHOD_GET, "/b/a", &req, &other));
    test_str(sihttp_path_param(&req.public_req, "first"), "b");
    test_assert(sihttp_path_param(&req.public_req, "second") == NULL);
    sihttp_request_internal_fini(&req);
    sihttp_route_table_fini(&tied);
}
