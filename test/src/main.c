
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

// Testsuite 'route'
void route_exact(void);
void route_param(void);
void route_method_not_allowed(void);

// Testsuite 'response'
void response_default_status(void);
void response_custom_status(void);
void response_json_content_type(void);

// Testsuite 'server'
void server_config(void);
void server_socket_roundtrip(void);
void server_poll_idle(void);
void server_poll_roundtrip(void);
void server_not_found(void);

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
        2,
        request_testcases
    },
    {
        "route",
        NULL,
        NULL,
        3,
        route_testcases
    },
    {
        "response",
        NULL,
        NULL,
        3,
        response_testcases
    },
    {
        "server",
        NULL,
        NULL,
        5,
        server_testcases
    }
};

int main(int argc, char *argv[]) {
    return bake_test_run("sihttp.test", argc, argv, suites, 5);
}
