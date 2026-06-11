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
