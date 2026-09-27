#include "sihttp_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#define MSG_NOSIGNAL 0
#else
#include <sys/socket.h>
#endif

SIHTTP_API void sihttp_response_fini(sihttp_response_t *response) {
    if (!response) return;
    free(response->body);
    *response = (sihttp_response_t){0};
}

void sihttp_response_normalize(sihttp_response_t *response) {
    if (response->status == 0) response->status = 200;
    if (response->body && response->body_size == 0 && response->content_type != SIHTTP_CONTENT_BINARY) {
        response->body_size = strlen(response->body);
    }
}

SIHTTP_API sihttp_response_t sihttp_response_empty(int status) {
    return (sihttp_response_t){.status = status};
}

SIHTTP_API sihttp_response_t sihttp_response_text(int status, const char *text) {
    size_t size = text ? strlen(text) : 0;
    char *body = malloc(size + 1);
    if (!body) return sihttp_response_empty(500);
    if (size) memcpy(body, text, size);
    body[size] = '\0';
    return (sihttp_response_t){.status = status, .body = body, .body_size = size,
                               .content_type = SIHTTP_CONTENT_TEXT};
}

SIHTTP_API sihttp_response_t sihttp_response_json(int status, sijson_value_t value) {
    char *body = sijson_stringify(value);
    if (!body) return sihttp_response_empty(500);
    return (sihttp_response_t){.status = status, .body = body, .body_size = strlen(body),
                               .content_type = SIHTTP_CONTENT_JSON};
}

SIHTTP_API sihttp_response_t sihttp_response_json_error(int status, const char *message) {
    sijson_value_t object = sijson_make_object();
    sijson_value_t string = sijson_make_string(message ? message : "");
    if (!object || !string || !sijson_object_set(object, "error", string)) {
        return sihttp_response_empty(500);
    }
    return sihttp_response_json(status, object);
}

SIHTTP_API sihttp_response_t sihttp_response_take_binary(int status, void *data, size_t size) {
    return (sihttp_response_t){.status = status, .body = data, .body_size = size,
                               .content_type = SIHTTP_CONTENT_BINARY};
}

static const char *sihttp_status_reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 422: return "Unprocessable Content";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    }
    return status >= 200 && status < 300 ? "OK" : "Error";
}

static const char *sihttp_content_type_name(sihttp_content_type_t type) {
    switch (type) {
    case SIHTTP_CONTENT_JSON: return "application/json";
    case SIHTTP_CONTENT_HTML: return "text/html; charset=utf-8";
    case SIHTTP_CONTENT_BINARY: return "application/octet-stream";
    default: return "text/plain; charset=utf-8";
    }
}

static int sihttp_send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t written = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (written <= 0) return -1;
        sent += (size_t)written;
    }
    return 0;
}

char *sihttp_build_response(sihttp_response_t response, const sihttp_cors_desc_t *cors, size_t *out_len) {
    const char *format = "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\nContent-Type: %s\r\n%sConnection: close\r\n\r\n";
    const char *cors_format = "Access-Control-Allow-Origin: %s\r\nAccess-Control-Allow-Methods: %s\r\nAccess-Control-Allow-Headers: %s\r\n";
    char *cors_headers = NULL;
    const char *cors_text = "";
    int header_len;
    char *message;
    size_t total;
    sihttp_response_normalize(&response);
    if (cors && cors->enabled) {
        const char *origin = cors->allow_origin ? cors->allow_origin : "*";
        const char *methods = cors->allow_methods ? cors->allow_methods : "GET, POST, PUT, DELETE, OPTIONS";
        const char *headers = cors->allow_headers ? cors->allow_headers : "Content-Type, Authorization";
        int n = snprintf(NULL, 0, cors_format, origin, methods, headers);
        if (n < 0) return NULL;
        cors_headers = malloc((size_t)n + 1);
        if (!cors_headers) return NULL;
        snprintf(cors_headers, (size_t)n + 1, cors_format, origin, methods, headers);
        cors_text = cors_headers;
    }
    header_len = snprintf(NULL, 0, format, response.status, sihttp_status_reason(response.status),
                          response.body ? response.body_size : 0,
                          sihttp_content_type_name(response.content_type), cors_text);
    if (header_len < 0 || (response.body && response.body_size > SIZE_MAX - (size_t)header_len - 1)) {
        free(cors_headers);
        return NULL;
    }
    total = (size_t)header_len + (response.body ? response.body_size : 0);
    message = malloc(total + 1);
    if (message) {
        snprintf(message, (size_t)header_len + 1, format, response.status,
                 sihttp_status_reason(response.status), response.body ? response.body_size : 0,
                 sihttp_content_type_name(response.content_type), cors_text);
        if (response.body && response.body_size) memcpy(message + header_len, response.body, response.body_size);
        message[total] = '\0';
        if (out_len) *out_len = total;
    }
    free(cors_headers);
    return message;
}

int sihttp_send_response(int fd, sihttp_response_t response, const sihttp_cors_desc_t *cors) {
    size_t len = 0;
    char *message = sihttp_build_response(response, cors, &len);
    int result = message ? sihttp_send_all(fd, message, len) : -1;
    free(message);
    sihttp_response_fini(&response);
    return result;
}
