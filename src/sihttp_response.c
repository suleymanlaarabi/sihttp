#include "sihttp_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static const char *sihttp_status_reason(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 413:
        return "Payload Too Large";
    case 500:
        return "Internal Server Error";
    }

    return status >= 200 && status < 300 ? "OK" : "Error";
}

static int sihttp_send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t written = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (written <= 0) {
            return -1;
        }
        sent += (size_t)written;
    }

    return 0;
}

char *sihttp_build_response(sihttp_response_t response, size_t *out_len) {
    int status = response.status == 0 ? 200 : response.status;
    const char *reason = sihttp_status_reason(status);
    const char *body = response.body ? response.body : "";
    size_t body_len = response.body ? strlen(response.body) : 0;
    int header_len;
    size_t total;
    char *message;

    header_len = snprintf(
        NULL,
        0,
        "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        status,
        reason,
        body_len
    );
    if (header_len < 0) {
        return NULL;
    }

    total = (size_t)header_len + body_len;
    message = malloc(total + 1);
    if (!message) {
        return NULL;
    }

    snprintf(
        message,
        (size_t)header_len + 1,
        "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        status,
        reason,
        body_len
    );
    memcpy(message + header_len, body, body_len);
    message[total] = '\0';

    if (out_len) {
        *out_len = total;
    }
    return message;
}

int sihttp_send_response(int fd, sihttp_response_t response) {
    size_t len = 0;
    char *message = sihttp_build_response(response, &len);
    int result;

    if (!message) {
        free(response.body);
        return -1;
    }

    result = sihttp_send_all(fd, message, len);
    free(message);
    free(response.body);
    return result;
}
