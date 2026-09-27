
/* A friendly warning from bake.test
 * ----------------------------------------------------------------------------
 * This file is generated. To add/remove testcases modify the 'project.json' of
 * the test project. ANY CHANGE TO THIS FILE IS LOST AFTER (RE)BUILDING!
 * ----------------------------------------------------------------------------
 */

#include <test.h>

// Testsuite 'siformat'
void siformat_basic(void);
void siformat_empty(void);

// Testsuite 'request'
void request_parse_get_query(void);
void request_parse_post_body(void);
void request_headers_and_query(void);
void request_path_numbers(void);

// Testsuite 'route'
void route_exact(void);
void route_param(void);
void route_method_not_allowed(void);
void route_multiple_and_rollback(void);

// Testsuite 'response'
void response_default_status(void);
void response_custom_status(void);
void response_json_content_type(void);
void response_cors_headers(void);
void response_binary_keeps_null_bytes(void);
void response_helpers(void);
void response_status_and_cors(void);

// Testsuite 'server'
void server_config(void);
void server_socket_roundtrip(void);
void server_poll_idle(void);
void server_poll_roundtrip(void);
void server_not_found(void);
void server_cors_preflight(void);
void server_dispatch_exact(void);
void server_dispatch_param(void);
void server_dispatch_body(void);
void server_dispatch_bytes(void);
void server_dispatch_not_found(void);
void server_dispatch_method_not_allowed(void);
void server_dispatch_features(void);
void server_options_and_cors(void);
void server_network_body_limit(void);

bake_test_case siformat_testcases[] = {
    {
        "basic",
        siformat_basic
    },
    {
        "empty",
        siformat_empty
    }
};

bake_test_case request_testcases[] = {
    {
        "parse_get_query",
        request_parse_get_query
    },
    {
        "parse_post_body",
        request_parse_post_body
    },
    {
        "headers_and_query",
        request_headers_and_query
    },
    {
        "path_numbers",
        request_path_numbers
    }
};

bake_test_case route_testcases[] = {
    {
        "exact",
        route_exact
    },
    {
        "param",
        route_param
    },
    {
        "method_not_allowed",
        route_method_not_allowed
    },
    {
        "multiple_and_rollback",
        route_multiple_and_rollback
    }
};

bake_test_case response_testcases[] = {
    {
        "default_status",
        response_default_status
    },
    {
        "custom_status",
        response_custom_status
    },
    {
        "json_content_type",
        response_json_content_type
    },
    {
        "cors_headers",
        response_cors_headers
    },
    {
        "binary_keeps_null_bytes",
        response_binary_keeps_null_bytes
    },
    {
        "helpers",
        response_helpers
    },
    {
        "status_and_cors",
        response_status_and_cors
    }
};

bake_test_case server_testcases[] = {
    {
        "config",
        server_config
    },
    {
        "socket_roundtrip",
        server_socket_roundtrip
    },
    {
        "poll_idle",
        server_poll_idle
    },
    {
        "poll_roundtrip",
        server_poll_roundtrip
    },
    {
        "not_found",
        server_not_found
    },
    {
        "cors_preflight",
        server_cors_preflight
    },
    {
        "dispatch_exact",
        server_dispatch_exact
    },
    {
        "dispatch_param",
        server_dispatch_param
    },
    {
        "dispatch_body",
        server_dispatch_body
    },
    {
        "dispatch_bytes",
        server_dispatch_bytes
    },
    {
        "dispatch_not_found",
        server_dispatch_not_found
    },
    {
        "dispatch_method_not_allowed",
        server_dispatch_method_not_allowed
    },
    {
        "dispatch_features",
        server_dispatch_features
    },
    {
        "options_and_cors",
        server_options_and_cors
    },
    {
        "network_body_limit",
        server_network_body_limit
    }
};


static bake_test_suite suites[] = {
    {
        "siformat",
        NULL,
        NULL,
        2,
        siformat_testcases
    },
    {
        "request",
        NULL,
        NULL,
        4,
        request_testcases
    },
    {
        "route",
        NULL,
        NULL,
        4,
        route_testcases
    },
    {
        "response",
        NULL,
        NULL,
        7,
        response_testcases
    },
    {
        "server",
        NULL,
        NULL,
        15,
        server_testcases
    }
};

int main(int argc, char *argv[]) {
    return bake_test_run("sihttp.test", argc, argv, suites, 5);
}
