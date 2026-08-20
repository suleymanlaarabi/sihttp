#include "sihttp.h"

#ifndef NDEBUG
#include <stdio.h>
#include <stdlib.h>

void sireflect_assert_fail(
    const char *condition,
    const char *message,
    const char *file,
    int line,
    const char *function
) {
    fprintf(stderr, "sireflect assertion failed: %s\n", message != NULL ? message : condition);
    fprintf(stderr, "  condition: %s\n", condition != NULL ? condition : "(unknown)");
    fprintf(stderr, "  location: %s:%d\n", file != NULL ? file : "(unknown)", line);
    fprintf(stderr, "  function: %s\n", function != NULL ? function : "(unknown)");
    abort();
}
#endif

#ifndef SIREFLECT_ERROR_H
#define SIREFLECT_ERROR_H

void sireflect_error_clear(void);
void sireflect_error_set(const char *message);

#endif

#include <stdlib.h>
#include <string.h>

static char *sireflect_current_error = NULL;

static char *sireflect_error_dup(const char *message) {
    sireflect_assert(message != NULL, "error message must not be NULL");

    const size_t len = strlen(message);
    char *copy = malloc(len + 1);
    sireflect_assert(copy != NULL, "failed to allocate error message");

    memcpy(copy, message, len + 1);
    return copy;
}

void sireflect_error_clear(void) {
    free(sireflect_current_error);
    sireflect_current_error = NULL;
}

void sireflect_error_set(const char *message) {
    sireflect_error_clear();

    if (message == NULL) {
        return;
    }

    sireflect_current_error = sireflect_error_dup(message);
}

const char *sireflect_error(void) {
    return sireflect_current_error;
}

const sireflect_field_info_t *
sireflect_field_info(sireflect_handle_t type, const char *field) {
    sireflect_error_clear();

    sireflect_assert(field != NULL, "field name must not be NULL");

    const sireflect_fields_t *fields = sireflect_type_fields(type);
    for (size_t i = 0; i < fields->field_count; i++) {
        if (strcmp(fields->fields[i].name, field) == 0) {
            return &fields->fields[i];
        }
    }

    return NULL;
}

sireflect_handle_t
sireflect_field_type(sireflect_handle_t type, const char *field) {
    sireflect_error_clear();

    const sireflect_field_info_t *info = sireflect_field_info(type, field);
    sireflect_assert(info != NULL, "field must exist");
    return info->type;
}

size_t
sireflect_field_size(sireflect_handle_t ref, const char *field) {
    sireflect_error_clear();

    const sireflect_field_info_t *info = sireflect_field_info(ref, field);
    sireflect_assert(info != NULL, "field must exist");
    return info->size;
}

const void *sireflect_field_ptr(
    sireflect_handle_t type,
    const void *obj,
    const char *field
) {
    sireflect_error_clear();

    sireflect_assert(obj != NULL, "object pointer must not be NULL");

    const sireflect_field_info_t *info = sireflect_field_info(type, field);
    sireflect_assert(info != NULL, "field must exist");

    return (const unsigned char *)obj + info->offset;
}

void *sireflect_field_mut_ptr(
    sireflect_handle_t type,
    void *obj,
    const char *field
) {
    sireflect_error_clear();

    sireflect_assert(obj != NULL, "object pointer must not be NULL");

    const sireflect_field_info_t *info = sireflect_field_info(type, field);
    sireflect_assert(info != NULL, "field must exist");

    return (unsigned char *)obj + info->offset;
}

int sireflect_field_copy(
    sireflect_handle_t type,
    void *obj,
    const char *field,
    const void *value
) {
    sireflect_error_clear();

    sireflect_assert(value != NULL, "source value pointer must not be NULL");

    const sireflect_field_info_t *info = sireflect_field_info(type, field);
    if (info == NULL) {
        return -1;
    }

    memcpy(sireflect_field_mut_ptr(type, obj, field), value, info->size);
    return 0;
}

#ifndef SIREFLECT_PARSER_H
#define SIREFLECT_PARSER_H

bool sireflect_parse_struct_fields(
    const char *struct_name,
    const char *fields_src,
    sireflect_field_info_t **out_fields,
    size_t *out_field_count,
    size_t struct_size,
    size_t struct_align,
    size_t *out_struct_size,
    size_t *out_struct_align,
    bool validate_layout,
    bool fail_fast
);

#endif

#ifndef SIREFLECT_REGISTRY_H
#define SIREFLECT_REGISTRY_H

typedef struct sireflect_registry_t sireflect_registry_t;

struct sireflect_registry_t {
    sireflect_type_info_t *types;
    size_t type_count;
    size_t type_cap;
};

sireflect_registry_t *sireflect_registry_current(void);
bool sireflect_registry_is_initialized(void);

sireflect_handle_t sireflect_registry_add_type(
    const char *name,
    sireflect_kind_t kind,
    size_t size,
    size_t align,
    sireflect_field_info_t *fields,
    size_t field_count
);

sireflect_handle_t sireflect_registry_get_or_add_array_type(
    sireflect_handle_t element_type,
    size_t element_count
);

sireflect_handle_t
sireflect_registry_get_or_add_pointer_type(sireflect_handle_t pointee_type);

sireflect_handle_t sireflect_registry_get_or_add_function_pointer_type(
    sireflect_handle_t return_type
);

sireflect_type_info_t *sireflect_registry_type_at(sireflect_handle_t handle);

const sireflect_type_info_t *sireflect_registry_const_type_at(sireflect_handle_t handle);

#endif

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

#define SIREFLECT_MAX_ARRAY_DIMS 16

typedef enum {
    sireflect_token_ident,
    sireflect_token_integer,
    sireflect_token_lbrace,
    sireflect_token_rbrace,
    sireflect_token_lbracket,
    sireflect_token_rbracket,
    sireflect_token_lparen,
    sireflect_token_rparen,
    sireflect_token_star,
    sireflect_token_comma,
    sireflect_token_semicolon,
    sireflect_token_unknown,
    sireflect_token_end
} sireflect_token_kind_t;

typedef struct {
    sireflect_token_kind_t kind;
    const char *start;
    size_t len;
    size_t offset;
    size_t line;
    size_t column;
} sireflect_token_t;

typedef struct {
    const char *src;
    const char *struct_name;
    const char *field_start;
    size_t field_len;
    size_t pos;
    size_t line;
    size_t column;
    sireflect_token_t current;
    char message[512];
    bool failed;
    bool fail_fast;
} sireflect_parser_t;

typedef struct {
    const char *start;
    size_t len;
    char name[64];
    int has_name;
    size_t line;
    size_t column;
} sireflect_type_spec_t;

static inline int sireflect_is_ident_start(char c) { return isalpha((unsigned char)c) || c == '_'; }

static inline int sireflect_is_ident_char(char c) { return isalnum((unsigned char)c) || c == '_'; }

static inline int sireflect_token_is_ident(sireflect_token_t token, const char *text) {
    return token.kind == sireflect_token_ident && strlen(text) == token.len &&
           strncmp(token.start, text, token.len) == 0;
}

static inline int sireflect_token_is_qualifier(sireflect_token_t token) {
    return sireflect_token_is_ident(token, "const") || sireflect_token_is_ident(token, "volatile");
}

static inline void
sireflect_type_spec_set(sireflect_type_spec_t *type, sireflect_token_t token) {
    type->start = token.start;
    type->len = token.len;
    type->name[0] = '\0';
    type->has_name = 0;
    type->line = token.line;
    type->column = token.column;
}

static inline void sireflect_type_spec_set2(
    sireflect_type_spec_t *type,
    sireflect_token_t first,
    sireflect_token_t second
) {
    const int len = snprintf(
        type->name,
        sizeof(type->name),
        "%.*s %.*s",
        (int)first.len,
        first.start,
        (int)second.len,
        second.start
    );
    (void)len;
    sireflect_indebug(
        sireflect_assert(len > 0 && (size_t)len < sizeof(type->name), "type specifier is too long");
    )
    type->start = NULL;
    type->len = 0;
    type->has_name = 1;
    type->line = first.line;
    type->column = first.column;
}

static inline void sireflect_type_spec_set3(
    sireflect_type_spec_t *type,
    sireflect_token_t first,
    sireflect_token_t second,
    sireflect_token_t third
) {
    const int len = snprintf(
        type->name,
        sizeof(type->name),
        "%.*s %.*s %.*s",
        (int)first.len,
        first.start,
        (int)second.len,
        second.start,
        (int)third.len,
        third.start
    );
    (void)len;
    sireflect_indebug(
        sireflect_assert(len > 0 && (size_t)len < sizeof(type->name), "type specifier is too long");
    );
    type->start = NULL;
    type->len = 0;
    type->has_name = 1;
    type->line = first.line;
    type->column = first.column;
}

static inline const char *sireflect_token_kind_name(sireflect_token_kind_t kind) {
    switch (kind) {
    case sireflect_token_ident:
        return "identifier";
    case sireflect_token_integer:
        return "integer";
    case sireflect_token_lbrace:
        return "'{'";
    case sireflect_token_rbrace:
        return "'}'";
    case sireflect_token_lbracket:
        return "'['";
    case sireflect_token_rbracket:
        return "']'";
    case sireflect_token_lparen:
        return "'('";
    case sireflect_token_rparen:
        return "')'";
    case sireflect_token_star:
        return "'*'";
    case sireflect_token_comma:
        return "','";
    case sireflect_token_semicolon:
        return "';'";
    case sireflect_token_unknown:
        return "unsupported token";
    case sireflect_token_end:
        return "end of input";
    }

    return "unknown token";
}

static inline void
sireflect_token_display(sireflect_token_t token, char *buffer, size_t buffer_size) {
    if (token.kind == sireflect_token_end) {
        snprintf(buffer, buffer_size, "end of input");
        return;
    }

    if (token.len == 0) {
        snprintf(buffer, buffer_size, "%s", sireflect_token_kind_name(token.kind));
        return;
    }

    snprintf(
        buffer,
        buffer_size,
        "%s '%.*s'",
        sireflect_token_kind_name(token.kind),
        (int)token.len,
        token.start
    );
}

static inline void
sireflect_parser_context(sireflect_parser_t *parser, char *buffer, size_t buffer_size) {
    if (parser->field_start != NULL) {
        snprintf(
            buffer,
            buffer_size,
            "struct '%s', field '%.*s'",
            parser->struct_name != NULL ? parser->struct_name : "<unknown>",
            (int)parser->field_len,
            parser->field_start
        );
        return;
    }

    snprintf(
        buffer,
        buffer_size,
        "struct '%s'",
        parser->struct_name != NULL ? parser->struct_name : "<unknown>"
    );
}

static inline void
sireflect_parser_fail_at(sireflect_parser_t *parser, sireflect_token_t token, const char *message) {
    char actual[96];
    char context[160];

    sireflect_token_display(token, actual, sizeof(actual));
    sireflect_parser_context(parser, context, sizeof(context));

    snprintf(
        parser->message,
        sizeof(parser->message),
        "%s in %s at line %zu, column %zu: actual %s",
        message,
        context,
        token.line,
        token.column,
        actual
    );

    parser->failed = true;
    if (parser->fail_fast) {
        sireflect_assert(false, parser->message);
    }
    sireflect_error_set(parser->message);
}

static inline void sireflect_parser_unexpected(
    sireflect_parser_t *parser,
    sireflect_token_kind_t expected,
    const char *context
) {
    char actual[96];
    char parser_context[160];

    sireflect_token_display(parser->current, actual, sizeof(actual));
    sireflect_parser_context(parser, parser_context, sizeof(parser_context));

    snprintf(
        parser->message,
        sizeof(parser->message),
        "unexpected token while parsing %s in %s at line %zu, column %zu: expected %s, actual %s",
        context,
        parser_context,
        parser->current.line,
        parser->current.column,
        sireflect_token_kind_name(expected),
        actual
    );

    parser->failed = true;
    if (parser->fail_fast) {
        sireflect_assert(false, parser->message);
    }
    sireflect_error_set(parser->message);
}

static inline void sireflect_parser_advance(sireflect_parser_t *parser) {
    if (parser->src[parser->pos] == '\n') {
        parser->line++;
        parser->column = 1;
    } else {
        parser->column++;
    }

    parser->pos++;
}

static inline void sireflect_parser_next(sireflect_parser_t *parser) {
    const char *src = parser->src;

    while (isspace((unsigned char)src[parser->pos])) {
        sireflect_parser_advance(parser);
    }

    const size_t start = parser->pos;
    const size_t line = parser->line;
    const size_t column = parser->column;
    const char c = src[start];

    if (c == '\0') {
        parser->current = (sireflect_token_t){ sireflect_token_end, &src[start], 0, start, line, column };
        return;
    }

    if (sireflect_is_ident_start(c)) {
        sireflect_parser_advance(parser);
        while (sireflect_is_ident_char(src[parser->pos])) {
            sireflect_parser_advance(parser);
        }

        parser->current = (sireflect_token_t){
            sireflect_token_ident,
            &src[start],
            parser->pos - start,
            start,
            line,
            column,
        };
        return;
    }

    if (isdigit((unsigned char)c)) {
        sireflect_parser_advance(parser);
        while (isdigit((unsigned char)src[parser->pos])) {
            sireflect_parser_advance(parser);
        }

        parser->current = (sireflect_token_t){
            sireflect_token_integer,
            &src[start],
            parser->pos - start,
            start,
            line,
            column,
        };
        return;
    }

    sireflect_parser_advance(parser);

    switch (c) {
    case '{':
        parser->current = (sireflect_token_t){ sireflect_token_lbrace, &src[start], 1, start, line, column };
        return;
    case '}':
        parser->current = (sireflect_token_t){ sireflect_token_rbrace, &src[start], 1, start, line, column };
        return;
    case '[':
        parser->current =
            (sireflect_token_t){ sireflect_token_lbracket, &src[start], 1, start, line, column };
        return;
    case ']':
        parser->current =
            (sireflect_token_t){ sireflect_token_rbracket, &src[start], 1, start, line, column };
        return;
    case '(':
        parser->current =
            (sireflect_token_t){ sireflect_token_lparen, &src[start], 1, start, line, column };
        return;
    case ')':
        parser->current =
            (sireflect_token_t){ sireflect_token_rparen, &src[start], 1, start, line, column };
        return;
    case '*':
        parser->current = (sireflect_token_t){ sireflect_token_star, &src[start], 1, start, line, column };
        return;
    case ',':
        parser->current = (sireflect_token_t){ sireflect_token_comma, &src[start], 1, start, line, column };
        return;
    case ';':
        parser->current =
            (sireflect_token_t){ sireflect_token_semicolon, &src[start], 1, start, line, column };
        return;
    default:
        parser->current = (sireflect_token_t){ sireflect_token_unknown, &src[start], 1, start, line, column };
        sireflect_parser_fail_at(
            parser,
            parser->current,
            "unsupported syntax in reflected struct; supported fields are '<type> <name>;', '<type> <name>, <name>;', '<type> *<name>;', '<type> (*<name>)();', '<type> <name>[N][M];', and '<type> *<name>[N];'"
        );
    }
}

static inline void sireflect_parser_init(
    sireflect_parser_t *parser,
    const char *struct_name,
    const char *src,
    bool fail_fast
) {
    sireflect_assert(parser != NULL, "parser must not be NULL");
    sireflect_assert(struct_name != NULL, "parser struct name must not be NULL");
    sireflect_assert(src != NULL, "parser source must not be NULL");

    parser->src = src;
    parser->struct_name = struct_name;
    parser->field_start = NULL;
    parser->field_len = 0;
    parser->pos = 0;
    parser->line = 1;
    parser->column = 1;
    parser->message[0] = '\0';
    parser->failed = false;
    parser->fail_fast = fail_fast;
    sireflect_parser_next(parser);
}

static inline sireflect_token_t
sireflect_expect(sireflect_parser_t *parser, sireflect_token_kind_t kind, const char *context) {
    sireflect_token_t token = parser->current;
    if (token.kind != kind) {
        sireflect_parser_unexpected(parser, kind, context);
        return token;
    }
    sireflect_parser_next(parser);
    return token;
}

static inline sireflect_token_t sireflect_expect_field_name(sireflect_parser_t *parser) {
    sireflect_token_t token = parser->current;
    if (token.kind != sireflect_token_ident || sireflect_token_is_qualifier(token)) {
        sireflect_parser_unexpected(parser, sireflect_token_ident, "field name");
        return token;
    }

    parser->field_start = token.start;
    parser->field_len = token.len;
    sireflect_parser_next(parser);
    return token;
}

static inline uint32_t sireflect_parse_qualifiers(sireflect_parser_t *parser) {
    uint32_t qualifiers = 0;

    for (;;) {
        if (sireflect_token_is_ident(parser->current, "const")) {
            qualifiers |= SIREFLECT_QUAL_CONST;
            sireflect_parser_next(parser);
            continue;
        }

        if (sireflect_token_is_ident(parser->current, "volatile")) {
            qualifiers |= SIREFLECT_QUAL_VOLATILE;
            sireflect_parser_next(parser);
            continue;
        }

        return qualifiers;
    }
}

static inline void
sireflect_fail_unsupported_type_specifier(sireflect_parser_t *parser, sireflect_token_t token) {
    sireflect_parser_fail_at(
        parser,
        token,
        "unsupported type specifier sequence; supported multi-token types are 'signed char', 'unsigned char', 'unsigned short', 'unsigned int', 'unsigned long', 'long long', and 'unsigned long long'"
    );
}

static inline sireflect_type_spec_t sireflect_parse_type_specifier(sireflect_parser_t *parser) {
    sireflect_token_t first = sireflect_expect(parser, sireflect_token_ident, "field type");
    sireflect_type_spec_t type;
    sireflect_type_spec_set(&type, first);
    if (parser->failed) {
        return type;
    }

    if (sireflect_token_is_ident(first, "signed")) {
        if (!sireflect_token_is_ident(parser->current, "char")) {
            sireflect_fail_unsupported_type_specifier(parser, parser->current);
            return type;
        }

        sireflect_token_t second = parser->current;
        sireflect_parser_next(parser);
        sireflect_type_spec_set2(&type, first, second);
        return type;
    }

    if (sireflect_token_is_ident(first, "long")) {
        if (!sireflect_token_is_ident(parser->current, "long")) {
            return type;
        }

        sireflect_token_t second = parser->current;
        sireflect_parser_next(parser);
        sireflect_type_spec_set2(&type, first, second);
        return type;
    }

    if (sireflect_token_is_ident(first, "unsigned")) {
        sireflect_token_t second = parser->current;

        if (sireflect_token_is_ident(second, "char") || sireflect_token_is_ident(second, "short") ||
            sireflect_token_is_ident(second, "int")) {
            sireflect_parser_next(parser);
            sireflect_type_spec_set2(&type, first, second);
            return type;
        }

        if (sireflect_token_is_ident(second, "long")) {
            sireflect_parser_next(parser);

            if (sireflect_token_is_ident(parser->current, "long")) {
                sireflect_token_t third = parser->current;
                sireflect_parser_next(parser);
                sireflect_type_spec_set3(&type, first, second, third);
                return type;
            }

            sireflect_type_spec_set2(&type, first, second);
            return type;
        }

        sireflect_fail_unsupported_type_specifier(parser, second);
        return type;
    }

    return type;
}

static inline char *sireflect_dup_range(const char *start, size_t len) {
    char *result = malloc(len + 1);
    sireflect_assert(result != NULL, "failed to allocate parser string");

    memcpy(result, start, len);
    result[len] = '\0';

    return result;
}

static inline size_t
sireflect_parse_array_count(sireflect_parser_t *parser, sireflect_token_t token) {
    size_t count = 0;

    for (size_t i = 0; i < token.len; i++) {
        const unsigned int digit = (unsigned int)(token.start[i] - '0');
        if (count > (SIZE_MAX - digit) / 10) {
            sireflect_parser_fail_at(parser, token, "array element count overflows size_t");
            return 0;
        }
        count = count * 10 + digit;
    }

    if (count == 0) {
        sireflect_parser_fail_at(parser, token, "array element count must be greater than zero");
        return 0;
    }

    return count;
}

static inline size_t
sireflect_parse_array_dimensions(sireflect_parser_t *parser, size_t *counts, size_t max_count) {
    size_t count = 0;

    while (parser->current.kind == sireflect_token_lbracket) {
        sireflect_parser_next(parser);

        if (parser->current.kind == sireflect_token_rbracket) {
            sireflect_parser_fail_at(parser, parser->current, "array element count is required");
            return count;
        }

        if (parser->current.kind != sireflect_token_integer) {
            sireflect_parser_fail_at(
                parser,
                parser->current,
                "array element count must be a positive integer literal"
            );
            return count;
        }

        sireflect_token_t count_token = parser->current;
        sireflect_parser_next(parser);

        if (parser->current.kind != sireflect_token_rbracket) {
            sireflect_parser_fail_at(parser, parser->current, "expected ']' after array element count");
            return count;
        }

        if (count >= max_count) {
            sireflect_parser_fail_at(parser, count_token, "too many array dimensions");
            return count;
        }

        counts[count++] = sireflect_parse_array_count(parser, count_token);
        if (parser->failed) {
            return count;
        }
        sireflect_parser_next(parser);
    }

    return count;
}

static inline void sireflect_parse_declarator_shape(sireflect_parser_t *parser) {
    size_t counts[SIREFLECT_MAX_ARRAY_DIMS];

    parser->field_start = NULL;
    parser->field_len = 0;

    if (parser->current.kind == sireflect_token_lparen) {
        sireflect_expect(parser, sireflect_token_lparen, "function pointer declarator");
        sireflect_expect(parser, sireflect_token_star, "function pointer declarator");
        (void)sireflect_expect_field_name(parser);
        sireflect_expect(parser, sireflect_token_rparen, "function pointer declarator");
        sireflect_expect(parser, sireflect_token_lparen, "function pointer parameters");
        sireflect_expect(parser, sireflect_token_rparen, "function pointer parameters");
    } else {
        if (parser->current.kind == sireflect_token_star) {
            sireflect_parser_next(parser);
        }

        sireflect_expect_field_name(parser);
    }

    (void)sireflect_parse_array_dimensions(parser, counts, SIREFLECT_MAX_ARRAY_DIMS);
}

static inline size_t sireflect_parse_declaration_shape(sireflect_parser_t *parser) {
    size_t count = 0;

    (void)sireflect_parse_qualifiers(parser);
    (void)sireflect_parse_type_specifier(parser);
    if (parser->failed) {
        return 0;
    }

    for (;;) {
        sireflect_parse_declarator_shape(parser);
        if (parser->failed) {
            return 0;
        }
        count++;

        if (parser->current.kind != sireflect_token_comma) {
            break;
        }

        sireflect_parser_next(parser);
    }

    sireflect_expect(parser, sireflect_token_semicolon, "field terminator");
    if (parser->failed) {
        return 0;
    }
    return count;
}

static inline bool sireflect_count_fields(
    const char *struct_name,
    const char *fields_src,
    bool fail_fast,
    size_t *out_count
) {
    sireflect_parser_t parser;
    size_t count = 0;

    sireflect_parser_init(&parser, struct_name, fields_src, fail_fast);
    sireflect_expect(&parser, sireflect_token_lbrace, "struct field list start");
    if (parser.failed) {
        return false;
    }

    while (parser.current.kind != sireflect_token_rbrace) {
        count += sireflect_parse_declaration_shape(&parser);
        if (parser.failed) {
            return false;
        }
    }

    sireflect_expect(&parser, sireflect_token_rbrace, "struct field list end");
    if (parser.failed) {
        return false;
    }
    sireflect_expect(&parser, sireflect_token_end, "trailing input after struct field list");
    if (parser.failed) {
        return false;
    }

    *out_count = count;
    return true;
}

static inline size_t sireflect_align_up(size_t value, size_t align) {
    sireflect_assert(align != 0, "alignment must not be zero");

    const size_t remainder = value % align;
    if (remainder == 0) {
        return value;
    }

    return value + align - remainder;
}

static inline sireflect_handle_t sireflect_resolve_field_type(
    sireflect_parser_t *parser,
    sireflect_type_spec_t type
) {
    char *owned_name = NULL;
    const char *type_name = type.name;

    if (!type.has_name) {
        owned_name = sireflect_dup_range(type.start, type.len);
        type_name = owned_name;
    }

    sireflect_handle_t field_type = sireflect_type_by_name(type_name);
    if (field_type == SIREFLECT_INVALID_HANDLE) {
        char context[160];

        sireflect_parser_context(parser, context, sizeof(context));
        snprintf(
            parser->message,
            sizeof(parser->message),
            "unknown field type '%s' in %s at line %zu, column %zu; register the type before this struct or use a supported primitive alias",
            type_name,
            context,
            type.line,
            type.column
        );
        free(owned_name);
        parser->failed = true;
        if (parser->fail_fast) {
            sireflect_assert(false, parser->message);
        }
        sireflect_error_set(parser->message);
        return SIREFLECT_INVALID_HANDLE;
    }

    free(owned_name);
    return field_type;
}

static inline void sireflect_parse_declarator(
    sireflect_parser_t *parser,
    sireflect_field_info_t *field,
    sireflect_type_spec_t type,
    uint32_t qualifiers,
    size_t *offset,
    size_t *max_align
) {
    parser->field_start = NULL;
    parser->field_len = 0;

    int is_pointer = 0;
    int is_function_pointer = 0;
    size_t array_counts[SIREFLECT_MAX_ARRAY_DIMS];
    size_t array_dim_count = 0;
    sireflect_token_t name_token;

    if (parser->current.kind == sireflect_token_lparen) {
        is_function_pointer = 1;
        sireflect_expect(parser, sireflect_token_lparen, "function pointer declarator");
        sireflect_expect(parser, sireflect_token_star, "function pointer declarator");
        name_token = sireflect_expect_field_name(parser);
        sireflect_expect(parser, sireflect_token_rparen, "function pointer declarator");
        sireflect_expect(parser, sireflect_token_lparen, "function pointer parameters");
        sireflect_expect(parser, sireflect_token_rparen, "function pointer parameters");
    } else {
        if (parser->current.kind == sireflect_token_star) {
            is_pointer = 1;
            sireflect_parser_next(parser);
        }

        name_token = sireflect_expect_field_name(parser);
    }
    if (parser->failed) {
        return;
    }

    array_dim_count =
        sireflect_parse_array_dimensions(parser, array_counts, SIREFLECT_MAX_ARRAY_DIMS);
    if (parser->failed) {
        return;
    }

    sireflect_handle_t field_type = sireflect_resolve_field_type(parser, type);
    if (parser->failed) {
        return;
    }

    if (is_function_pointer) {
        field_type = sireflect_registry_get_or_add_function_pointer_type(field_type);
    } else if (is_pointer) {
        field_type = sireflect_registry_get_or_add_pointer_type(field_type);
    }

    for (size_t i = array_dim_count; i > 0; i--) {
        field_type = sireflect_registry_get_or_add_array_type(field_type, array_counts[i - 1]);
    }

    const sireflect_type_info_t *type_info = sireflect_type_info(field_type);
    sireflect_assert(type_info != NULL, "field type metadata must exist");

    field->name = sireflect_dup_range(name_token.start, name_token.len);
    field->type = field_type;
    field->size = type_info->size;
    field->align = type_info->align;
    field->offset = sireflect_align_up(*offset, field->align);
    field->qualifiers = qualifiers;

    *offset = field->offset + field->size;
    if (field->align > *max_align) {
        *max_align = field->align;
    }
}

static inline size_t sireflect_parse_declaration(
    sireflect_parser_t *parser,
    sireflect_field_info_t *fields,
    size_t *offset,
    size_t *max_align
) {
    size_t count = 0;
    uint32_t qualifiers = sireflect_parse_qualifiers(parser);
    sireflect_type_spec_t type = sireflect_parse_type_specifier(parser);
    if (parser->failed) {
        return 0;
    }

    for (;;) {
        sireflect_parse_declarator(
            parser,
            &fields[count],
            type,
            qualifiers,
            offset,
            max_align
        );
        if (parser->failed) {
            return 0;
        }
        count++;

        if (parser->current.kind != sireflect_token_comma) {
            break;
        }

        sireflect_parser_next(parser);
    }

    sireflect_expect(parser, sireflect_token_semicolon, "field terminator");
    if (parser->failed) {
        return 0;
    }
    return count;
}

static inline void sireflect_free_parsed_fields(sireflect_field_info_t *fields, size_t field_count) {
    if (fields == NULL) {
        return;
    }

    for (size_t i = 0; i < field_count; i++) {
        free((char *)fields[i].name);
    }

    free(fields);
}

bool sireflect_parse_struct_fields(
    const char *struct_name,
    const char *fields_src,
    sireflect_field_info_t **out_fields,
    size_t *out_field_count,
    size_t struct_size,
    size_t struct_align,
    size_t *out_struct_size,
    size_t *out_struct_align,
    bool validate_layout,
    bool fail_fast
) {
    sireflect_assert(struct_name != NULL, "struct name must not be NULL");
    sireflect_assert(fields_src != NULL, "field source must not be NULL");
    sireflect_assert(out_fields != NULL, "output field pointer must not be NULL");
    sireflect_assert(out_field_count != NULL, "output field count pointer must not be NULL");
    sireflect_assert(out_struct_size != NULL, "output struct size must not be NULL");
    sireflect_assert(out_struct_align != NULL, "output struct alignment must not be NULL");

    size_t field_count = 0;
    if (!sireflect_count_fields(struct_name, fields_src, fail_fast, &field_count)) {
        *out_fields = NULL;
        *out_field_count = 0;
        return false;
    }

    sireflect_field_info_t *fields = NULL;

    if (field_count != 0) {
        fields = calloc(field_count, sizeof(*fields));
        sireflect_assert(fields != NULL, "failed to allocate field metadata");
    }

    sireflect_parser_t parser;
    sireflect_parser_init(&parser, struct_name, fields_src, fail_fast);
    sireflect_expect(&parser, sireflect_token_lbrace, "struct field list start");
    if (parser.failed) {
        sireflect_free_parsed_fields(fields, field_count);
        *out_fields = NULL;
        *out_field_count = 0;
        return false;
    }

    size_t offset = 0;
    size_t max_align = 1;

    for (size_t i = 0; i < field_count;) {
        const size_t parsed_count =
            sireflect_parse_declaration(&parser, &fields[i], &offset, &max_align);
        if (parser.failed) {
            sireflect_free_parsed_fields(fields, field_count);
            *out_fields = NULL;
            *out_field_count = 0;
            return false;
        }
        i += parsed_count;
    }

    sireflect_expect(&parser, sireflect_token_rbrace, "struct field list end");
    if (parser.failed) {
        sireflect_free_parsed_fields(fields, field_count);
        *out_fields = NULL;
        *out_field_count = 0;
        return false;
    }
    sireflect_expect(&parser, sireflect_token_end, "trailing input after struct field list");
    if (parser.failed) {
        sireflect_free_parsed_fields(fields, field_count);
        *out_fields = NULL;
        *out_field_count = 0;
        return false;
    }

#ifndef NDEBUG
    if (validate_layout) {
        const size_t computed_size = sireflect_align_up(offset, struct_align);
        if (computed_size != struct_size) {
            if (fail_fast) {
                sireflect_assert(false, "computed struct size does not match C layout");
            }
            sireflect_error_set("computed struct size does not match C layout");
            sireflect_free_parsed_fields(fields, field_count);
            *out_fields = NULL;
            *out_field_count = 0;
            return false;
        }

        if (max_align > struct_align) {
            if (fail_fast) {
                sireflect_assert(false, "computed field alignment exceeds struct alignment");
            }
            sireflect_error_set("computed field alignment exceeds struct alignment");
            sireflect_free_parsed_fields(fields, field_count);
            *out_fields = NULL;
            *out_field_count = 0;
            return false;
        }
    }
#else
    (void)struct_size;
    (void)struct_align;
    (void)validate_layout;
#endif

    *out_fields = fields;
    *out_field_count = field_count;
    *out_struct_align = max_align;
    *out_struct_size = sireflect_align_up(offset, max_align);
    return true;
}

static sireflect_registry_t sireflect_global_registry;
static size_t sireflect_global_references;

bool sireflect_registry_is_initialized(void) {
    return sireflect_global_references != 0;
}

sireflect_registry_t *sireflect_registry_current(void) {
    sireflect_assert(sireflect_registry_is_initialized(), "sireflect must be initialized");
    return &sireflect_global_registry;
}

static char *sireflect_dup_cstr(const char *str) {
    sireflect_assert(str != NULL, "string must not be NULL");

    const size_t len = strlen(str);
    char *result = malloc(len + 1);
    sireflect_assert(result != NULL, "failed to allocate string");

    memcpy(result, str, len + 1);
    return result;
}

static char *
sireflect_format_array_type_name(const sireflect_type_info_t *element, size_t element_count) {
    sireflect_assert(element != NULL, "array element metadata must exist");

    const char *suffix = strchr(element->name, '[');
    if (element->kind != sireflect_kind_array || suffix == NULL) {
        const int name_len = snprintf(NULL, 0, "%s[%zu]", element->name, element_count);
        sireflect_assert(name_len > 0, "failed to format array type name");

        char *name = malloc((size_t)name_len + 1);
        sireflect_assert(name != NULL, "failed to allocate array type name");
        snprintf(name, (size_t)name_len + 1, "%s[%zu]", element->name, element_count);
        return name;
    }

    const size_t prefix_len = (size_t)(suffix - element->name);
    const int count_len = snprintf(NULL, 0, "[%zu]", element_count);
    sireflect_assert(count_len > 0, "failed to format array dimension");

    const size_t suffix_len = strlen(suffix);
    char *name = malloc(prefix_len + (size_t)count_len + suffix_len + 1);
    sireflect_assert(name != NULL, "failed to allocate array type name");

    memcpy(name, element->name, prefix_len);
    snprintf(name + prefix_len, (size_t)count_len + 1, "[%zu]", element_count);
    memcpy(name + prefix_len + (size_t)count_len, suffix, suffix_len + 1);

    return name;
}

static sireflect_handle_t sireflect_handle_from_index(size_t index) {
    return (sireflect_handle_t)(index + 1);
}

static size_t sireflect_index_from_handle(sireflect_handle_t handle) {
    sireflect_assert(handle != SIREFLECT_INVALID_HANDLE, "type handle must be valid");
    return (size_t)(handle - 1);
}

static void sireflect_registry_reserve(size_t min_cap) {
    sireflect_registry_t *reg = sireflect_registry_current();

    if (reg->type_cap >= min_cap) {
        return;
    }

    size_t new_cap = reg->type_cap == 0 ? 16 : reg->type_cap * 2;
    while (new_cap < min_cap) {
        new_cap *= 2;
    }

    sireflect_type_info_t *types = realloc(reg->types, new_cap * sizeof(*types));
    sireflect_assert(types != NULL, "failed to allocate type metadata");

    reg->types = types;
    reg->type_cap = new_cap;
}

sireflect_handle_t sireflect_registry_add_type(
    const char *name,
    sireflect_kind_t kind,
    size_t size,
    size_t align,
    sireflect_field_info_t *fields,
    size_t field_count
) {
    sireflect_registry_t *reg = sireflect_registry_current();

    sireflect_assert(name != NULL, "type name must not be NULL");
    sireflect_assert(size != 0 || kind == sireflect_kind_struct, "non-struct type size must not be zero");
    sireflect_assert(align != 0, "type alignment must not be zero");

    sireflect_registry_reserve(reg->type_count + 1);

    const size_t index = reg->type_count++;
    reg->types[index] = (sireflect_type_info_t){
        .name = sireflect_dup_cstr(name),
        .kind = kind,
        .size = size,
        .align = align,
        .fields =
            {
                .fields = fields,
                .field_count = field_count,
            },
        .element_type = SIREFLECT_INVALID_HANDLE,
        .element_count = 0,
    };

    return sireflect_handle_from_index(index);
}

sireflect_handle_t
sireflect_registry_get_or_add_pointer_type(sireflect_handle_t pointee_type) {
    sireflect_registry_t *reg = sireflect_registry_current();

    sireflect_assert(pointee_type != SIREFLECT_INVALID_HANDLE, "pointer pointee type must be valid");

    for (size_t i = 0; i < reg->type_count; i++) {
        const sireflect_type_info_t *type = &reg->types[i];
        if (type->kind == sireflect_kind_pointer && type->element_type == pointee_type) {
            return sireflect_handle_from_index(i);
        }
    }

    const sireflect_type_info_t *pointee = sireflect_registry_const_type_at(pointee_type);
    sireflect_assert(pointee != NULL, "pointer pointee metadata must exist");

    const int name_len = snprintf(NULL, 0, "%s*", pointee->name);
    sireflect_assert(name_len > 0, "failed to format pointer type name");

    char *name = malloc((size_t)name_len + 1);
    sireflect_assert(name != NULL, "failed to allocate pointer type name");
    snprintf(name, (size_t)name_len + 1, "%s*", pointee->name);

    sireflect_handle_t pointer_type = sireflect_registry_add_type(
        name,
        sireflect_kind_pointer,
        sizeof(ptr),
        _Alignof(ptr),
        NULL,
        0
    );
    free(name);

    sireflect_type_info_t *pointer_info = sireflect_registry_type_at(pointer_type);
    pointer_info->element_type = pointee_type;

    return pointer_type;
}

sireflect_handle_t sireflect_registry_get_or_add_function_pointer_type(
    sireflect_handle_t return_type
) {
    sireflect_registry_t *reg = sireflect_registry_current();

    sireflect_assert(return_type != SIREFLECT_INVALID_HANDLE, "function return type must be valid");

    for (size_t i = 0; i < reg->type_count; i++) {
        const sireflect_type_info_t *type = &reg->types[i];
        if (type->kind == sireflect_kind_function_pointer && type->element_type == return_type) {
            return sireflect_handle_from_index(i);
        }
    }

    const sireflect_type_info_t *return_info = sireflect_registry_const_type_at(return_type);
    sireflect_assert(return_info != NULL, "function return type metadata must exist");

    const int name_len = snprintf(NULL, 0, "%s(*)()", return_info->name);
    sireflect_assert(name_len > 0, "failed to format function pointer type name");

    char *name = malloc((size_t)name_len + 1);
    sireflect_assert(name != NULL, "failed to allocate function pointer type name");
    snprintf(name, (size_t)name_len + 1, "%s(*)()", return_info->name);

    sireflect_handle_t function_pointer_type = sireflect_registry_add_type(
        name,
        sireflect_kind_function_pointer,
        sizeof(ptr),
        _Alignof(ptr),
        NULL,
        0
    );
    free(name);

    sireflect_type_info_t *function_pointer_info =
        sireflect_registry_type_at(function_pointer_type);
    function_pointer_info->element_type = return_type;

    return function_pointer_type;
}

sireflect_handle_t sireflect_registry_get_or_add_array_type(
    sireflect_handle_t element_type,
    size_t element_count
) {
    sireflect_registry_t *reg = sireflect_registry_current();

    sireflect_assert(element_type != SIREFLECT_INVALID_HANDLE, "array element type must be valid");
    sireflect_assert(element_count != 0, "array element count must not be zero");

    for (size_t i = 0; i < reg->type_count; i++) {
        const sireflect_type_info_t *type = &reg->types[i];
        if (type->kind == sireflect_kind_array && type->element_type == element_type &&
            type->element_count == element_count) {
            return sireflect_handle_from_index(i);
        }
    }

    const sireflect_type_info_t *element = sireflect_registry_const_type_at(element_type);
    sireflect_assert(element != NULL, "array element metadata must exist");
    sireflect_assert(element->size <= SIZE_MAX / element_count, "array type size overflows size_t");

    char *name = sireflect_format_array_type_name(element, element_count);

    sireflect_handle_t array_type = sireflect_registry_add_type(
        name,
        sireflect_kind_array,
        element->size * element_count,
        element->align,
        NULL,
        0
    );
    free(name);

    sireflect_type_info_t *array_info = sireflect_registry_type_at(array_type);
    array_info->element_type = element_type;
    array_info->element_count = element_count;

    return array_type;
}

#define add_type(name, kind) \
    sireflect_registry_add_type(#name, kind, sizeof(name), _Alignof(name), NULL, 0)

#define add_named_type(c_type, reflected_name, kind) \
    sireflect_registry_add_type(reflected_name, kind, sizeof(c_type), _Alignof(c_type), NULL, 0)

static inline void sireflect_register_builtin_types(void) {
    add_type(u8, sireflect_kind_u8);
    add_type(u16, sireflect_kind_u16);
    add_type(u32, sireflect_kind_u32);
    add_type(u64, sireflect_kind_u64);
    add_type(i8, sireflect_kind_i8);
    add_type(i16, sireflect_kind_i16);
    add_type(i32, sireflect_kind_i32);
    add_type(i64, sireflect_kind_i64);
    add_type(f32, sireflect_kind_f32);
    add_type(f64, sireflect_kind_f64);
    add_type(bool, sireflect_kind_bool);
    add_type(char, sireflect_kind_char);
    add_type(ptr, sireflect_kind_ptr);

    add_type(uint8_t, sireflect_kind_u8);
    add_type(uint16_t, sireflect_kind_u16);
    add_type(uint32_t, sireflect_kind_u32);
    add_type(uint64_t, sireflect_kind_u64);
    add_type(int8_t, sireflect_kind_i8);
    add_type(int16_t, sireflect_kind_i16);
    add_type(int32_t, sireflect_kind_i32);
    add_type(int64_t, sireflect_kind_i64);

    add_type(float, sireflect_kind_f32);
    add_type(double, sireflect_kind_f64);
    add_type(short, sireflect_kind_short);
    add_type(int, sireflect_kind_int);
    add_type(long, sireflect_kind_long);

    add_named_type(signed char, "signed char", sireflect_kind_signed_char);
    add_named_type(unsigned char, "unsigned char", sireflect_kind_unsigned_char);
    add_named_type(unsigned short, "unsigned short", sireflect_kind_unsigned_short);
    add_named_type(unsigned int, "unsigned int", sireflect_kind_unsigned_int);
    add_named_type(unsigned long, "unsigned long", sireflect_kind_unsigned_long);
    add_named_type(long long, "long long", sireflect_kind_long_long);
    add_named_type(unsigned long long, "unsigned long long", sireflect_kind_unsigned_long_long);
}

void sireflect_init(void) {
    sireflect_error_clear();

    if (sireflect_global_references == 0) {
        sireflect_global_references = 1;
        sireflect_register_builtin_types();
        return;
    }

    sireflect_assert(sireflect_global_references != SIZE_MAX, "sireflect reference count overflow");
    sireflect_global_references++;
}

static void sireflect_registry_clear(void) {
    sireflect_registry_t *reg = &sireflect_global_registry;

    for (size_t i = 0; i < reg->type_count; i++) {
        sireflect_type_info_t *type = &reg->types[i];

        free((char *)type->name);

        for (size_t f = 0; f < type->fields.field_count; f++) {
            free((char *)type->fields.fields[f].name);
        }

        free(type->fields.fields);
    }

    free(reg->types);
    memset(reg, 0, sizeof(*reg));
}

void sireflect_fini(void) {
    sireflect_error_clear();

    sireflect_assert(sireflect_global_references != 0, "sireflect is not initialized");
    if (sireflect_global_references == 0) {
        return;
    }

    sireflect_global_references--;
    if (sireflect_global_references == 0) {
        sireflect_registry_clear();
    }
}

sireflect_handle_t sireflect_type_by_name(const char *name) {
    sireflect_error_clear();

    sireflect_registry_t *reg = sireflect_registry_current();
    sireflect_assert(name != NULL, "type name must not be NULL");

    for (size_t i = 0; i < reg->type_count; i++) {
        if (strcmp(reg->types[i].name, name) == 0) {
            return sireflect_handle_from_index(i);
        }
    }

    return SIREFLECT_INVALID_HANDLE;
}

const sireflect_type_info_t *sireflect_registry_const_type_at(sireflect_handle_t handle) {
    const sireflect_registry_t *reg = sireflect_registry_current();

    const size_t index = sireflect_index_from_handle(handle);
    sireflect_assert(index < reg->type_count, "type handle is out of range");

    return &reg->types[index];
}

sireflect_type_info_t *sireflect_registry_type_at(sireflect_handle_t handle) {
    return (sireflect_type_info_t *)sireflect_registry_const_type_at(handle);
}

sireflect_handle_t
sireflect_try_register_struct(const sireflect_struct_desc_t *desc) {
    sireflect_error_clear();

    if (!sireflect_registry_is_initialized() || desc == NULL || desc->name == NULL || desc->fields == NULL ||
        desc->align == 0) {
        sireflect_error_set(
            sireflect_registry_is_initialized() ? "invalid struct descriptor"
                                                 : "sireflect is not initialized"
        );
        return SIREFLECT_INVALID_HANDLE;
    }

    sireflect_handle_t existing = sireflect_type_by_name(desc->name);
    if (existing != SIREFLECT_INVALID_HANDLE) {
        const sireflect_type_info_t *type = sireflect_type_info(existing);
        if (type->kind != sireflect_kind_struct || type->size != desc->size ||
            type->align != desc->align) {
            sireflect_error_set("existing type is incompatible with struct descriptor");
            return SIREFLECT_INVALID_HANDLE;
        }
        return existing;
    }

    sireflect_field_info_t *parsed_fields = NULL;
    size_t field_count = 0;
    size_t parsed_size = 0;
    size_t parsed_align = 0;

    if (!sireflect_parse_struct_fields(
        desc->name,
        desc->fields,
        &parsed_fields,
        &field_count,
        desc->size,
        desc->align,
        &parsed_size,
        &parsed_align,
        true,
        false
    )) {
        return SIREFLECT_INVALID_HANDLE;
    }

    return sireflect_registry_add_type(
        desc->name,
        sireflect_kind_struct,
        desc->size,
        desc->align,
        parsed_fields,
        field_count
    );
}

sireflect_handle_t
sireflect_register_struct(const sireflect_struct_desc_t *desc) {
    sireflect_error_clear();

    sireflect_assert(desc != NULL, "struct descriptor must not be NULL");
    sireflect_assert(desc->name != NULL, "struct descriptor name must not be NULL");
    sireflect_assert(desc->fields != NULL, "struct descriptor fields must not be NULL");
    sireflect_assert(desc->align != 0, "struct descriptor alignment must not be zero");

    sireflect_handle_t handle = SIREFLECT_INVALID_HANDLE;

    if (sireflect_registry_is_initialized() && desc != NULL && desc->name != NULL && desc->fields != NULL &&
        desc->align != 0) {
        sireflect_handle_t existing = sireflect_type_by_name(desc->name);
        if (existing != SIREFLECT_INVALID_HANDLE) {
            const sireflect_type_info_t *type = sireflect_type_info(existing);
            if (type->kind != sireflect_kind_struct || type->size != desc->size ||
                type->align != desc->align) {
                sireflect_assert(type->kind == sireflect_kind_struct, "existing type must be a struct");
                sireflect_assert(
                    type->size == desc->size,
                    "existing struct size must match descriptor"
                );
                sireflect_assert(
                    type->align == desc->align,
                    "existing struct alignment must match descriptor"
                );
                return SIREFLECT_INVALID_HANDLE;
            }
            return existing;
        }

        sireflect_field_info_t *parsed_fields = NULL;
        size_t field_count = 0;
        size_t parsed_size = 0;
        size_t parsed_align = 0;

        if (sireflect_parse_struct_fields(
                desc->name,
                desc->fields,
                &parsed_fields,
                &field_count,
                desc->size,
                desc->align,
                &parsed_size,
                &parsed_align,
                true,
                true
            )) {
            handle = sireflect_registry_add_type(
                desc->name,
                sireflect_kind_struct,
                desc->size,
                desc->align,
                parsed_fields,
                field_count
            );
        }
    }

    sireflect_assert(handle != SIREFLECT_INVALID_HANDLE, "failed to register struct");
    return handle;
}

sireflect_handle_t sireflect_try_register_dynamic_struct(
    const char *name,
    const char *fields
) {
    sireflect_error_clear();

    if (!sireflect_registry_is_initialized() || name == NULL || fields == NULL) {
        sireflect_error_set(
            sireflect_registry_is_initialized() ? "invalid dynamic struct descriptor"
                                                 : "sireflect is not initialized"
        );
        return SIREFLECT_INVALID_HANDLE;
    }

    sireflect_handle_t existing = sireflect_type_by_name(name);
    if (existing != SIREFLECT_INVALID_HANDLE) {
        if (!sireflect_type_is_struct(sireflect_type_info(existing))) {
            sireflect_error_set("existing type is not a struct");
            return SIREFLECT_INVALID_HANDLE;
        }
        return existing;
    }

    sireflect_field_info_t *parsed_fields = NULL;
    size_t field_count = 0;
    size_t size = 0;
    size_t align = 0;

    if (!sireflect_parse_struct_fields(
            name,
            fields,
            &parsed_fields,
            &field_count,
            0,
            1,
            &size,
            &align,
            false,
            false
        )) {
        return SIREFLECT_INVALID_HANDLE;
    }

    return sireflect_registry_add_type(
        name,
        sireflect_kind_struct,
        size,
        align,
        parsed_fields,
        field_count
    );
}

const char *sireflect_kind_name(sireflect_kind_t kind) {
    sireflect_error_clear();

    switch (kind) {
    case sireflect_kind_u8:
        return "u8";
    case sireflect_kind_u16:
        return "u16";
    case sireflect_kind_u32:
        return "u32";
    case sireflect_kind_u64:
        return "u64";
    case sireflect_kind_i8:
        return "i8";
    case sireflect_kind_i16:
        return "i16";
    case sireflect_kind_i32:
        return "i32";
    case sireflect_kind_i64:
        return "i64";
    case sireflect_kind_f32:
        return "f32";
    case sireflect_kind_f64:
        return "f64";
    case sireflect_kind_bool:
        return "bool";
    case sireflect_kind_char:
        return "char";
    case sireflect_kind_short:
        return "short";
    case sireflect_kind_int:
        return "int";
    case sireflect_kind_long:
        return "long";
    case sireflect_kind_ptr:
        return "ptr";
    case sireflect_kind_pointer:
        return "pointer";
    case sireflect_kind_struct:
        return "struct";
    case sireflect_kind_array:
        return "array";
    case sireflect_kind_signed_char:
        return "signed char";
    case sireflect_kind_unsigned_char:
        return "unsigned char";
    case sireflect_kind_unsigned_short:
        return "unsigned short";
    case sireflect_kind_unsigned_int:
        return "unsigned int";
    case sireflect_kind_unsigned_long:
        return "unsigned long";
    case sireflect_kind_long_long:
        return "long long";
    case sireflect_kind_unsigned_long_long:
        return "unsigned long long";
    case sireflect_kind_function_pointer:
        return "function pointer";
    }

    return "unknown";
}

bool sireflect_is_numeric(sireflect_kind_t kind) {
    sireflect_error_clear();

    switch (kind) {
    case sireflect_kind_u8:
    case sireflect_kind_u16:
    case sireflect_kind_u32:
    case sireflect_kind_u64:
    case sireflect_kind_i8:
    case sireflect_kind_i16:
    case sireflect_kind_i32:
    case sireflect_kind_i64:
    case sireflect_kind_f32:
    case sireflect_kind_f64:
    case sireflect_kind_char:
    case sireflect_kind_short:
    case sireflect_kind_int:
    case sireflect_kind_long:
    case sireflect_kind_signed_char:
    case sireflect_kind_unsigned_char:
    case sireflect_kind_unsigned_short:
    case sireflect_kind_unsigned_int:
    case sireflect_kind_unsigned_long:
    case sireflect_kind_long_long:
    case sireflect_kind_unsigned_long_long:
        return true;
    case sireflect_kind_bool:
    case sireflect_kind_ptr:
    case sireflect_kind_pointer:
    case sireflect_kind_struct:
    case sireflect_kind_array:
    case sireflect_kind_function_pointer:
        return false;
    }

    return false;
}

const sireflect_type_info_t *
sireflect_type_info(sireflect_handle_t ref) {
    sireflect_error_clear();

    return sireflect_registry_const_type_at(ref);
}

const sireflect_fields_t *
sireflect_type_fields(sireflect_handle_t ref) {
    sireflect_error_clear();

    const sireflect_type_info_t *type = sireflect_type_info(ref);
    return &type->fields;
}

size_t sireflect_type_size(sireflect_handle_t ref) {
    sireflect_error_clear();

    return sireflect_type_info(ref)->size;
}

const char *sireflect_type_name(sireflect_handle_t ref) {
    sireflect_error_clear();

    return sireflect_type_info(ref)->name;
}

bool sireflect_type_is_struct(const sireflect_type_info_t *info) {
    sireflect_error_clear();

    sireflect_assert(info != NULL, "type metadata must not be NULL");
    return info->kind == sireflect_kind_struct;
}

bool sireflect_type_is_array(const sireflect_type_info_t *info) {
    sireflect_error_clear();

    sireflect_assert(info != NULL, "type metadata must not be NULL");
    return info->kind == sireflect_kind_array;
}

bool sireflect_type_is_pointer(const sireflect_type_info_t *info) {
    sireflect_error_clear();

    sireflect_assert(info != NULL, "type metadata must not be NULL");
    return info->kind == sireflect_kind_pointer || info->kind == sireflect_kind_function_pointer;
}

sireflect_handle_t
sireflect_type_element(sireflect_handle_t ref) {
    sireflect_error_clear();

    const sireflect_type_info_t *type = sireflect_type_info(ref);
    sireflect_assert(type->kind == sireflect_kind_array, "type must be an array");
    return type->element_type;
}

size_t
sireflect_type_element_count(sireflect_handle_t ref) {
    sireflect_error_clear();

    const sireflect_type_info_t *type = sireflect_type_info(ref);
    sireflect_assert(type->kind == sireflect_kind_array, "type must be an array");
    return type->element_count;
}

sireflect_handle_t
sireflect_type_pointee(sireflect_handle_t ref) {
    sireflect_error_clear();

    const sireflect_type_info_t *type = sireflect_type_info(ref);
    sireflect_assert(
        type->kind == sireflect_kind_pointer || type->kind == sireflect_kind_function_pointer,
        "type must be a typed pointer"
    );
    return type->element_type;
}

#ifndef SIJSON_INTERNAL_H
#define SIJSON_INTERNAL_H

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER) && !defined(__clang__)
typedef union sijson_max_align {
    long double long_double;
    void *pointer;
    long long integer;
} sijson_max_align_t;
#else
typedef max_align_t sijson_max_align_t;
#endif

#if defined(__GNUC__) || defined(__clang__)
#define SIJSON_INTERNAL_API __attribute__((visibility("hidden")))
#else
#define SIJSON_INTERNAL_API
#endif

typedef struct sijson_member {
    char *key;
    sijson_value_t value;
} sijson_member_t;

typedef struct sijson_array {
    sijson_value_t *items;
    size_t len;
    size_t cap;
} sijson_array_t;

typedef struct sijson_object {
    sijson_member_t *items;
    size_t len;
    size_t cap;
} sijson_object_t;

struct sijson_value {
    sijson_type_t type;
    union {
        bool boolean;
        double number;
        char *string;
        sijson_array_t array;
        sijson_object_t object;
    } as;
};

typedef struct sijson_writer {
    char *data;
    size_t len;
    size_t cap;
} sijson_writer_t;

typedef struct sijson_parser {
    const char *cur;
} sijson_parser_t;

SIJSON_INTERNAL_API void sijson_clear_error(void);
SIJSON_INTERNAL_API bool sijson_set_error(const char *message);
SIJSON_INTERNAL_API bool sijson_set_error_at(const char *message, const char *at);

SIJSON_INTERNAL_API char *sijson_dup_range(const char *start, size_t len);
SIJSON_INTERNAL_API char *sijson_dup_cstr(const char *str);

SIJSON_INTERNAL_API void *sijson_arena_alloc(size_t size, size_t align);
SIJSON_INTERNAL_API char *sijson_arena_dup_range(const char *start, size_t len);
SIJSON_INTERNAL_API char *sijson_arena_dup_cstr(const char *str);
SIJSON_INTERNAL_API size_t sijson_arena_mark(void);
SIJSON_INTERNAL_API void sijson_arena_rewind(size_t mark);

SIJSON_INTERNAL_API bool sijson_reserve_array(sijson_array_t *array, size_t need);
SIJSON_INTERNAL_API bool sijson_reserve_object(sijson_object_t *object, size_t need);
SIJSON_INTERNAL_API sijson_value_t sijson_new_value(sijson_type_t type);

SIJSON_INTERNAL_API bool sijson_writer_reserve(sijson_writer_t *writer, size_t extra);
SIJSON_INTERNAL_API bool sijson_writer_putc(sijson_writer_t *writer, char c);
SIJSON_INTERNAL_API bool sijson_writer_write(sijson_writer_t *writer, const char *data, size_t len);
SIJSON_INTERNAL_API bool sijson_writer_cstr(sijson_writer_t *writer, const char *str);
SIJSON_INTERNAL_API bool sijson_writer_string(sijson_writer_t *writer, const char *str);
SIJSON_INTERNAL_API bool sijson_write_value(sijson_writer_t *writer, sijson_value_t value);

#endif

static char g_error[256];

void sijson_clear_error(void) { g_error[0] = '\0'; }

bool sijson_set_error(const char *message) {
    if (message == NULL) {
        message = "unknown sijson error";
    }

    snprintf(g_error, sizeof(g_error), "%s", message);
    return false;
}

bool sijson_set_error_at(const char *message, const char *at) {
    if (at == NULL) {
        return sijson_set_error(message);
    }

    snprintf(g_error, sizeof(g_error), "%s near '%.24s'", message, at);
    return false;
}

const char *sijson_error(void) { return g_error[0] != '\0' ? g_error : NULL; }

static void sijson_skip_ws(sijson_parser_t *parser) {
    while (isspace((unsigned char)*parser->cur)) {
        parser->cur++;
    }
}

static bool sijson_take(sijson_parser_t *parser, char c) {
    sijson_skip_ws(parser);
    if (*parser->cur != c) {
        return false;
    }
    parser->cur++;
    return true;
}

static int sijson_hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + c - 'a';
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + c - 'A';
    }
    return -1;
}

static bool sijson_writer_utf8(sijson_writer_t *writer, unsigned codepoint) {
    char out[4];
    size_t len = 0;

    if (codepoint <= 0x7f) {
        out[len++] = (char)codepoint;
    } else if (codepoint <= 0x7ff) {
        out[len++] = (char)(0xc0 | (codepoint >> 6));
        out[len++] = (char)(0x80 | (codepoint & 0x3f));
    } else {
        out[len++] = (char)(0xe0 | (codepoint >> 12));
        out[len++] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        out[len++] = (char)(0x80 | (codepoint & 0x3f));
    }

    return sijson_writer_write(writer, out, len);
}

static char *sijson_parse_string_raw(sijson_parser_t *parser) {
    sijson_skip_ws(parser);
    if (*parser->cur != '"') {
        sijson_set_error_at("expected JSON string", parser->cur);
        return NULL;
    }
    parser->cur++;

    sijson_writer_t writer = { 0 };
    const char *chunk = parser->cur;
    while (*parser->cur != '\0') {
        unsigned char c = (unsigned char)*parser->cur;
        if (c == '"') {
            if (!sijson_writer_write(&writer, chunk, (size_t)(parser->cur - chunk))) {
                free(writer.data);
                return NULL;
            }
            parser->cur++;
            if (writer.data == NULL) {
                return sijson_arena_dup_cstr("");
            }
            char *result = sijson_arena_dup_cstr(writer.data);
            free(writer.data);
            return result;
        }

        if (c < 0x20) {
            free(writer.data);
            sijson_set_error_at("control character in JSON string", parser->cur);
            return NULL;
        }

        if (c != '\\') {
            parser->cur++;
            continue;
        }

        if (!sijson_writer_write(&writer, chunk, (size_t)(parser->cur - chunk))) {
            free(writer.data);
            return NULL;
        }

        parser->cur++;
        char escaped = *parser->cur++;
        switch (escaped) {
        case '"':
        case '\\':
        case '/':
            if (!sijson_writer_putc(&writer, escaped)) {
                free(writer.data);
                return NULL;
            }
            break;
        case 'b':
            if (!sijson_writer_putc(&writer, '\b')) {
                free(writer.data);
                return NULL;
            }
            break;
        case 'f':
            if (!sijson_writer_putc(&writer, '\f')) {
                free(writer.data);
                return NULL;
            }
            break;
        case 'n':
            if (!sijson_writer_putc(&writer, '\n')) {
                free(writer.data);
                return NULL;
            }
            break;
        case 'r':
            if (!sijson_writer_putc(&writer, '\r')) {
                free(writer.data);
                return NULL;
            }
            break;
        case 't':
            if (!sijson_writer_putc(&writer, '\t')) {
                free(writer.data);
                return NULL;
            }
            break;
        case 'u': {
            unsigned codepoint = 0;
            for (size_t i = 0; i < 4; i++) {
                int digit = sijson_hex_digit(parser->cur[i]);
                if (digit < 0) {
                    free(writer.data);
                    sijson_set_error_at("invalid unicode escape", parser->cur);
                    return NULL;
                }
                codepoint = (codepoint << 4) | (unsigned)digit;
            }
            parser->cur += 4;
            if (!sijson_writer_utf8(&writer, codepoint)) {
                free(writer.data);
                return NULL;
            }
            break;
        }
        default:
            free(writer.data);
            sijson_set_error_at("invalid JSON string escape", parser->cur - 1);
            return NULL;
        }
        chunk = parser->cur;
    }

    free(writer.data);
    sijson_set_error("unterminated JSON string");
    return NULL;
}

static sijson_value_t sijson_parse_value(sijson_parser_t *parser);

static sijson_value_t sijson_parse_array_value(sijson_parser_t *parser) {
    if (!sijson_take(parser, '[')) {
        return NULL;
    }

    sijson_value_t array = sijson_new_value(SIJSON_ARRAY);
    if (array == NULL) {
        return NULL;
    }

    sijson_skip_ws(parser);
    if (*parser->cur == ']') {
        parser->cur++;
        return array;
    }

    for (;;) {
        sijson_value_t item = sijson_parse_value(parser);
        if (item == NULL) {
            return NULL;
        }
        if (!sijson_reserve_array(&array->as.array, array->as.array.len + 1)) {
            return NULL;
        }
        array->as.array.items[array->as.array.len++] = item;

        sijson_skip_ws(parser);
        if (*parser->cur == ']') {
            parser->cur++;
            return array;
        }
        if (*parser->cur != ',') {
            sijson_set_error_at("expected ',' or ']'", parser->cur);
            return NULL;
        }
        parser->cur++;
    }
}

static sijson_value_t sijson_parse_object_value(sijson_parser_t *parser) {
    if (!sijson_take(parser, '{')) {
        return NULL;
    }

    sijson_value_t object = sijson_new_value(SIJSON_OBJECT);
    if (object == NULL) {
        return NULL;
    }

    sijson_skip_ws(parser);
    if (*parser->cur == '}') {
        parser->cur++;
        return object;
    }

    for (;;) {
        char *key = sijson_parse_string_raw(parser);
        if (key == NULL) {
            return NULL;
        }
        if (!sijson_take(parser, ':')) {
            sijson_set_error_at("expected ':'", parser->cur);
            return NULL;
        }

        sijson_value_t item = sijson_parse_value(parser);
        if (item == NULL) {
            return NULL;
        }

        if (!sijson_reserve_object(&object->as.object, object->as.object.len + 1)) {
            return NULL;
        }
        object->as.object.items[object->as.object.len++] = (sijson_member_t){
            .key = key,
            .value = item,
        };

        sijson_skip_ws(parser);
        if (*parser->cur == '}') {
            parser->cur++;
            return object;
        }
        if (*parser->cur != ',') {
            sijson_set_error_at("expected ',' or '}'", parser->cur);
            return NULL;
        }
        parser->cur++;
    }
}

static sijson_value_t sijson_parse_number_value(sijson_parser_t *parser) {
    sijson_skip_ws(parser);
    const char *start = parser->cur;
    const char *scan = start;

    if (*scan == '-') {
        scan++;
    }

    if (*scan == '0') {
        scan++;
    } else if (*scan >= '1' && *scan <= '9') {
        do {
            scan++;
        } while (isdigit((unsigned char)*scan));
    } else {
        sijson_set_error_at("invalid JSON number", start);
        return NULL;
    }

    if (*scan == '.') {
        scan++;
        if (!isdigit((unsigned char)*scan)) {
            sijson_set_error_at("invalid JSON number", start);
            return NULL;
        }
        do {
            scan++;
        } while (isdigit((unsigned char)*scan));
    }

    if (*scan == 'e' || *scan == 'E') {
        scan++;
        if (*scan == '+' || *scan == '-') {
            scan++;
        }
        if (!isdigit((unsigned char)*scan)) {
            sijson_set_error_at("invalid JSON number", start);
            return NULL;
        }
        do {
            scan++;
        } while (isdigit((unsigned char)*scan));
    }

    size_t len = (size_t)(scan - start);
    char stack[128];
    char *number_text = stack;
    if (len >= sizeof(stack)) {
        number_text = malloc(len + 1);
        if (number_text == NULL) {
            sijson_set_error("out of memory");
            return NULL;
        }
    }
    memcpy(number_text, start, len);
    number_text[len] = '\0';

    errno = 0;
    char *end = NULL;
    double number = strtod(number_text, &end);
    if (end == number_text || *end != '\0' || errno == ERANGE || !isfinite(number)) {
        if (number_text != stack) {
            free(number_text);
        }
        sijson_set_error_at("invalid JSON number", start);
        return NULL;
    }
    if (number_text != stack) {
        free(number_text);
    }

    parser->cur = scan;
    sijson_value_t value = sijson_new_value(SIJSON_NUMBER);
    if (value != NULL) {
        value->as.number = number;
    }
    return value;
}

static sijson_value_t
sijson_parse_literal(sijson_parser_t *parser, const char *literal, sijson_value_t value) {
    size_t len = strlen(literal);
    if (strncmp(parser->cur, literal, len) != 0) {
        sijson_set_error_at("invalid JSON literal", parser->cur);
        return NULL;
    }
    parser->cur += len;
    return value;
}

static sijson_value_t sijson_parse_value(sijson_parser_t *parser) {
    sijson_skip_ws(parser);
    switch (*parser->cur) {
    case 'n':
        return sijson_parse_literal(parser, "null", sijson_new_value(SIJSON_NULL));
    case 't': {
        sijson_value_t value = sijson_new_value(SIJSON_BOOL);
        if (value != NULL) {
            value->as.boolean = true;
        }
        return sijson_parse_literal(parser, "true", value);
    }
    case 'f': {
        sijson_value_t value = sijson_new_value(SIJSON_BOOL);
        if (value != NULL) {
            value->as.boolean = false;
        }
        return sijson_parse_literal(parser, "false", value);
    }
    case '"': {
        char *string = sijson_parse_string_raw(parser);
        if (string == NULL) {
            return NULL;
        }
        sijson_value_t value = sijson_new_value(SIJSON_STRING);
        if (value == NULL) {
            return NULL;
        }
        value->as.string = string;
        return value;
    }
    case '[':
        return sijson_parse_array_value(parser);
    case '{':
        return sijson_parse_object_value(parser);
    default:
        if (*parser->cur == '-' || isdigit((unsigned char)*parser->cur)) {
            return sijson_parse_number_value(parser);
        }
        sijson_set_error_at("expected JSON value", parser->cur);
        return NULL;
    }
}

sijson_value_t sijson_parse(const char *json) {
    sijson_clear_error();
    if (json == NULL) {
        sijson_set_error("sijson_parse expects JSON text");
        return NULL;
    }

    size_t mark = sijson_arena_mark();
    sijson_parser_t parser = { .cur = json };
    sijson_value_t value = sijson_parse_value(&parser);
    if (value == NULL) {
        sijson_arena_rewind(mark);
        return NULL;
    }

    sijson_skip_ws(&parser);
    if (*parser.cur != '\0') {
        sijson_set_error_at("trailing characters after JSON value", parser.cur);
        sijson_arena_rewind(mark);
        return NULL;
    }

    return value;
}

#include <limits.h>

static void *g_from_json_buffer;
static size_t g_from_json_capacity;

static sireflect_handle_t
sijson_register_type(sireflect_handle_t *ref, const sireflect_struct_desc_t *desc) {
    if (ref == NULL || desc == NULL) {
        sijson_set_error("missing reflection descriptor");
        return SIREFLECT_INVALID_HANDLE;
    }
    static const sireflect_struct_desc_t value_desc = {
        .name = "sijson_value_t",
        .fields = "{ ptr value; }",
        .size = sizeof(sijson_value_t),
        .align = _Alignof(sijson_value_t),
    };
    sireflect_register_struct(&value_desc);
    *ref = sireflect_register_struct(desc);
    return *ref;
}

static bool sijson_is_value_type(const sireflect_type_info_t *type) {
    return type != NULL && strcmp(type->name, "sijson_value_t") == 0 &&
           type->size == sizeof(sijson_value_t);
}

static bool sijson_is_char_pointer_type(const sireflect_type_info_t *type) {
    if (type == NULL) {
        return false;
    }
    if (type->kind == sireflect_kind_ptr) {
        return true;
    }
    if (type->kind != sireflect_kind_pointer) {
        return false;
    }

    const sireflect_type_info_t *pointee = sireflect_type_info(type->element_type);
    return pointee != NULL && pointee->kind == sireflect_kind_char;
}

static bool
sijson_write_reflected(sijson_writer_t *writer, sireflect_handle_t type, const void *ptr);

static bool sijson_write_reflected_field(
    sijson_writer_t *writer,
    const sireflect_type_info_t *field_type,
    const void *field_ptr
);

static bool sijson_write_reflected_array(
    sijson_writer_t *writer,
    const sireflect_type_info_t *array_type,
    const void *array_ptr
) {
    if (array_type == NULL || array_type->kind != sireflect_kind_array) {
        return sijson_set_error("expected reflected array type");
    }

    const sireflect_type_info_t *element_type =
        sireflect_type_info(array_type->element_type);
    if (element_type == NULL || element_type->size == 0) {
        return sijson_set_error("missing reflected array element type");
    }

    if (!sijson_writer_putc(writer, '[')) {
        return false;
    }

    for (size_t i = 0; i < array_type->element_count; i++) {
        const void *element_ptr = (const unsigned char *)array_ptr + i * element_type->size;

        if (i != 0 && !sijson_writer_putc(writer, ',')) {
            return false;
        }
        if (!sijson_write_reflected_field(writer, element_type, element_ptr)) {
            return false;
        }
    }

    return sijson_writer_putc(writer, ']');
}

static bool sijson_write_reflected_field(
    sijson_writer_t *writer,
    const sireflect_type_info_t *field_type,
    const void *field_ptr
) {
    if (field_type == NULL) {
        return sijson_set_error("missing reflected field type");
    }

    switch (field_type->kind) {
    case sireflect_kind_bool:
        return sijson_writer_cstr(writer, *(const bool *)field_ptr ? "true" : "false");
    default:
        break;
    }

    char number[64];
    switch (field_type->kind) {
    case sireflect_kind_signed_char:
        snprintf(number, sizeof(number), "%d", (int)*(const signed char *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_unsigned_char:
        snprintf(number, sizeof(number), "%u", (unsigned)*(const unsigned char *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_u8:
        snprintf(number, sizeof(number), "%u", (unsigned)*(const u8 *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_u16:
        snprintf(number, sizeof(number), "%u", (unsigned)*(const u16 *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_unsigned_short:
        snprintf(number, sizeof(number), "%u", (unsigned)*(const unsigned short *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_u32:
        snprintf(number, sizeof(number), "%u", *(const u32 *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_unsigned_int:
        snprintf(number, sizeof(number), "%u", *(const unsigned int *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_u64:
        snprintf(number, sizeof(number), "%llu", (unsigned long long)*(const u64 *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_i8:
        snprintf(number, sizeof(number), "%d", (int)*(const i8 *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_i16:
        snprintf(number, sizeof(number), "%d", (int)*(const i16 *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_i32:
        snprintf(number, sizeof(number), "%d", *(const i32 *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_i64:
        snprintf(number, sizeof(number), "%lld", (long long)*(const i64 *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_short:
        snprintf(number, sizeof(number), "%d", (int)*(const short *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_int:
        snprintf(number, sizeof(number), "%d", *(const int *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_long:
        snprintf(number, sizeof(number), "%ld", *(const long *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_unsigned_long:
        snprintf(number, sizeof(number), "%lu", *(const unsigned long *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_long_long:
        snprintf(number, sizeof(number), "%lld", *(const long long *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_unsigned_long_long:
        snprintf(number, sizeof(number), "%llu", *(const unsigned long long *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_f32:
        snprintf(number, sizeof(number), "%.9g", (double)*(const f32 *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_f64:
        snprintf(number, sizeof(number), "%.17g", *(const f64 *)field_ptr);
        return sijson_writer_cstr(writer, number);
    case sireflect_kind_char:
        return sijson_writer_string(writer, (char[2]){ *(const char *)field_ptr, '\0' });
    case sireflect_kind_ptr:
        return sijson_writer_string(writer, *(char *const *)field_ptr);
    case sireflect_kind_pointer:
        if (sijson_is_char_pointer_type(field_type)) {
            return sijson_writer_string(writer, *(char *const *)field_ptr);
        }
        break;
    case sireflect_kind_struct:
        if (sijson_is_value_type(field_type)) {
            return sijson_write_value(writer, *(const sijson_value_t *)field_ptr);
        }
        return sijson_write_reflected(
            writer,
            sireflect_type_by_name(field_type->name),
            field_ptr
        );
    case sireflect_kind_array:
        return sijson_write_reflected_array(writer, field_type, field_ptr);
    case sireflect_kind_bool:
        break;
    default:
        break;
    }

    return sijson_set_error("unsupported field type for serialization");
}

static bool
sijson_write_reflected(sijson_writer_t *writer, sireflect_handle_t type, const void *ptr) {
    const sireflect_type_info_t *info = sireflect_type_info(type);
    if (info == NULL || info->kind != sireflect_kind_struct) {
        return sijson_set_error("expected reflected struct");
    }

    if (!sijson_writer_putc(writer, '{')) {
        return false;
    }

    const sireflect_fields_t *fields = &info->fields;
    for (size_t i = 0; i < fields->field_count; i++) {
        const sireflect_field_info_t *field = &fields->fields[i];
        const sireflect_type_info_t *field_type = sireflect_type_info(field->type);
        const void *field_ptr = (const unsigned char *)ptr + field->offset;

        if (i != 0 && !sijson_writer_putc(writer, ',')) {
            return false;
        }
        if (!sijson_writer_string(writer, field->name) || !sijson_writer_putc(writer, ':')) {
            return false;
        }
        if (!sijson_write_reflected_field(writer, field_type, field_ptr)) {
            return false;
        }
    }

    return sijson_writer_putc(writer, '}');
}

char *
sijson_to_json_impl(sireflect_handle_t *ref, const sireflect_struct_desc_t *desc, const void *ptr) {
    sijson_clear_error();
    if (ptr == NULL) {
        sijson_set_error("sijson_to_json expects a value pointer");
        return NULL;
    }

    sireflect_handle_t type = sijson_register_type(ref, desc);
    if (type == SIREFLECT_INVALID_HANDLE) {
        return NULL;
    }

    sijson_writer_t writer = { 0 };
    if (!sijson_write_reflected(&writer, type, ptr)) {
        free(writer.data);
        return NULL;
    }
    return writer.data;
}

static bool sijson_number_is_integer(double value) {
    if (!isfinite(value) || value < -9007199254740991.0 || value > 9007199254740991.0) {
        return false;
    }

    int64_t integer = (int64_t)value;
    return (double)integer == value;
}

static bool sijson_assign_number(
    const sireflect_type_info_t *field_type,
    void *field_ptr,
    sijson_value_t value
) {
    if (value == NULL || value->type != SIJSON_NUMBER) {
        return sijson_set_error("expected JSON number");
    }

    double number = value->as.number;
    switch (field_type->kind) {
    case sireflect_kind_signed_char:
        if (!sijson_number_is_integer(number) || number < SCHAR_MIN || number > SCHAR_MAX) {
            return sijson_set_error("number out of range for signed char");
        }
        *(signed char *)field_ptr = (signed char)number;
        return true;
    case sireflect_kind_unsigned_char:
        if (!sijson_number_is_integer(number) || number < 0 || number > UCHAR_MAX) {
            return sijson_set_error("number out of range for unsigned char");
        }
        *(unsigned char *)field_ptr = (unsigned char)number;
        return true;
    case sireflect_kind_u8:
        if (!sijson_number_is_integer(number) || number < 0 || number > UINT8_MAX) {
            return sijson_set_error("number out of range for u8");
        }
        *(u8 *)field_ptr = (u8)number;
        return true;
    case sireflect_kind_u16:
        if (!sijson_number_is_integer(number) || number < 0 || number > UINT16_MAX) {
            return sijson_set_error("number out of range for u16");
        }
        *(u16 *)field_ptr = (u16)number;
        return true;
    case sireflect_kind_unsigned_short:
        if (!sijson_number_is_integer(number) || number < 0 || number > USHRT_MAX) {
            return sijson_set_error("number out of range for unsigned short");
        }
        *(unsigned short *)field_ptr = (unsigned short)number;
        return true;
    case sireflect_kind_u32:
        if (!sijson_number_is_integer(number) || number < 0 || number > UINT32_MAX) {
            return sijson_set_error("number out of range for u32");
        }
        *(u32 *)field_ptr = (u32)number;
        return true;
    case sireflect_kind_unsigned_int:
        if (!sijson_number_is_integer(number) || number < 0 || number > UINT_MAX) {
            return sijson_set_error("number out of range for unsigned int");
        }
        *(unsigned int *)field_ptr = (unsigned int)number;
        return true;
    case sireflect_kind_u64:
        if (!sijson_number_is_integer(number) || number < 0) {
            return sijson_set_error("number out of range for u64");
        }
        *(u64 *)field_ptr = (u64)number;
        return true;
    case sireflect_kind_i8:
        if (!sijson_number_is_integer(number) || number < INT8_MIN || number > INT8_MAX) {
            return sijson_set_error("number out of range for i8");
        }
        *(i8 *)field_ptr = (i8)number;
        return true;
    case sireflect_kind_i16:
        if (!sijson_number_is_integer(number) || number < INT16_MIN || number > INT16_MAX) {
            return sijson_set_error("number out of range for i16");
        }
        *(i16 *)field_ptr = (i16)number;
        return true;
    case sireflect_kind_i32:
        if (!sijson_number_is_integer(number) || number < INT32_MIN || number > INT32_MAX) {
            return sijson_set_error("number out of range for i32");
        }
        *(i32 *)field_ptr = (i32)number;
        return true;
    case sireflect_kind_i64:
        if (!sijson_number_is_integer(number)) {
            return sijson_set_error("number out of range for i64");
        }
        *(i64 *)field_ptr = (i64)number;
        return true;
    case sireflect_kind_short:
        if (!sijson_number_is_integer(number)) {
            return sijson_set_error("expected integer for short");
        }
        *(short *)field_ptr = (short)number;
        return true;
    case sireflect_kind_int:
        if (!sijson_number_is_integer(number)) {
            return sijson_set_error("expected integer for int");
        }
        *(int *)field_ptr = (int)number;
        return true;
    case sireflect_kind_long:
        if (!sijson_number_is_integer(number)) {
            return sijson_set_error("expected integer for long");
        }
        *(long *)field_ptr = (long)number;
        return true;
    case sireflect_kind_unsigned_long:
        if (!sijson_number_is_integer(number) || number < 0) {
            return sijson_set_error("number out of range for unsigned long");
        }
        *(unsigned long *)field_ptr = (unsigned long)number;
        return true;
    case sireflect_kind_long_long:
        if (!sijson_number_is_integer(number)) {
            return sijson_set_error("number out of range for long long");
        }
        *(long long *)field_ptr = (long long)number;
        return true;
    case sireflect_kind_unsigned_long_long:
        if (!sijson_number_is_integer(number) || number < 0) {
            return sijson_set_error("number out of range for unsigned long long");
        }
        *(unsigned long long *)field_ptr = (unsigned long long)number;
        return true;
    case sireflect_kind_f32:
        *(f32 *)field_ptr = (f32)number;
        return true;
    case sireflect_kind_f64:
        *(f64 *)field_ptr = number;
        return true;
    default:
        return sijson_set_error("field is not numeric");
    }
}

static bool sijson_assign_reflected(sireflect_handle_t type, void *ptr, sijson_value_t value);

static bool
sijson_assign_field(const sireflect_type_info_t *field_type, void *field_ptr, sijson_value_t value);

static bool sijson_assign_array(
    const sireflect_type_info_t *array_type,
    void *array_ptr,
    sijson_value_t value
) {
    if (array_type == NULL || array_type->kind != sireflect_kind_array) {
        return sijson_set_error("expected reflected array type");
    }
    if (value == NULL || value->type != SIJSON_ARRAY) {
        return sijson_set_error("expected JSON array");
    }
    if (value->as.array.len != array_type->element_count) {
        return sijson_set_error("JSON array length does not match reflected array");
    }

    const sireflect_type_info_t *element_type =
        sireflect_type_info(array_type->element_type);
    if (element_type == NULL || element_type->size == 0) {
        return sijson_set_error("missing reflected array element type");
    }

    for (size_t i = 0; i < array_type->element_count; i++) {
        void *element_ptr = (unsigned char *)array_ptr + i * element_type->size;
        if (!sijson_assign_field(element_type, element_ptr, value->as.array.items[i])) {
            return false;
        }
    }

    return true;
}

static bool sijson_assign_field(
    const sireflect_type_info_t *field_type,
    void *field_ptr,
    sijson_value_t value
) {
    if (field_type == NULL) {
        return sijson_set_error("missing reflected field type");
    }

    switch (field_type->kind) {
    case sireflect_kind_bool:
        if (value == NULL || value->type != SIJSON_BOOL) {
            return sijson_set_error("expected JSON bool");
        }
        *(bool *)field_ptr = value->as.boolean;
        return true;
    case sireflect_kind_signed_char:
    case sireflect_kind_unsigned_char:
    case sireflect_kind_u8:
    case sireflect_kind_u16:
    case sireflect_kind_u32:
    case sireflect_kind_u64:
    case sireflect_kind_i8:
    case sireflect_kind_i16:
    case sireflect_kind_i32:
    case sireflect_kind_i64:
    case sireflect_kind_unsigned_short:
    case sireflect_kind_short:
    case sireflect_kind_unsigned_int:
    case sireflect_kind_int:
    case sireflect_kind_unsigned_long:
    case sireflect_kind_long:
    case sireflect_kind_long_long:
    case sireflect_kind_unsigned_long_long:
    case sireflect_kind_f32:
    case sireflect_kind_f64:
        return sijson_assign_number(field_type, field_ptr, value);
    case sireflect_kind_char:
        if (value == NULL || value->type != SIJSON_STRING || value->as.string[0] == '\0') {
            return sijson_set_error("expected non-empty JSON string for char");
        }
        *(char *)field_ptr = value->as.string[0];
        return true;
    case sireflect_kind_ptr:
    case sireflect_kind_pointer:
        if (!sijson_is_char_pointer_type(field_type)) {
            break;
        }
        if (value == NULL || value->type == SIJSON_NULL) {
            *(char **)field_ptr = NULL;
            return true;
        }
        if (value->type != SIJSON_STRING) {
            return sijson_set_error("expected JSON string for pointer field");
        }
        *(char **)field_ptr = sijson_dup_cstr(value->as.string);
        return *(char **)field_ptr != NULL;
    case sireflect_kind_struct:
        if (sijson_is_value_type(field_type)) {
            *(sijson_value_t *)field_ptr = value;
            return true;
        }
        return sijson_assign_reflected(
            sireflect_type_by_name(field_type->name),
            field_ptr,
            value
        );
    case sireflect_kind_array:
        return sijson_assign_array(field_type, field_ptr, value);
    default:
        break;
    }

    return sijson_set_error("unsupported field type for deserialization");
}

static bool sijson_assign_reflected(sireflect_handle_t type, void *ptr, sijson_value_t value) {
    if (value == NULL || value->type != SIJSON_OBJECT) {
        return sijson_set_error("expected JSON object for reflected struct");
    }

    const sireflect_type_info_t *info = sireflect_type_info(type);
    if (info == NULL || info->kind != sireflect_kind_struct) {
        return sijson_set_error("expected reflected struct type");
    }

    memset(ptr, 0, info->size);
    const sireflect_fields_t *fields = &info->fields;
    for (size_t i = 0; i < fields->field_count; i++) {
        const sireflect_field_info_t *field = &fields->fields[i];
        sijson_value_t member = sijson_object_get(value, field->name);
        if (member == NULL) {
            continue;
        }

        const sireflect_type_info_t *field_type = sireflect_type_info(field->type);
        void *field_ptr = (unsigned char *)ptr + field->offset;
        if (!sijson_assign_field(field_type, field_ptr, member)) {
            return false;
        }
    }

    return true;
}

void *sijson_from_json_impl(
    sireflect_handle_t *ref,
    const sireflect_struct_desc_t *desc,
    const char *json
) {
    sijson_clear_error();
    sireflect_handle_t type = sijson_register_type(ref, desc);
    if (type == SIREFLECT_INVALID_HANDLE) {
        return NULL;
    }

    const sireflect_type_info_t *info = sireflect_type_info(type);
    if (info == NULL) {
        sijson_set_error("missing reflected type info");
        return NULL;
    }

    if (g_from_json_capacity < info->size) {
        void *buffer = realloc(g_from_json_buffer, info->size);
        if (buffer == NULL) {
            sijson_set_error("out of memory");
            return NULL;
        }
        g_from_json_buffer = buffer;
        g_from_json_capacity = info->size;
    }

    memset(g_from_json_buffer, 0, info->size);
    sijson_value_t root = sijson_parse(json);
    if (root == NULL) {
        memset(g_from_json_buffer, 0, info->size);
        return g_from_json_buffer;
    }

    if (!sijson_assign_reflected(type, g_from_json_buffer, root)) {
        memset(g_from_json_buffer, 0, info->size);
        return g_from_json_buffer;
    }

    return g_from_json_buffer;
}

static void sijson_free_reflected_field(const sireflect_type_info_t *field_type, void *field_ptr);

static void sijson_free_reflected(sireflect_handle_t type, void *ptr) {
    if (ptr == NULL) {
        return;
    }

    const sireflect_type_info_t *info = sireflect_type_info(type);
    if (info == NULL || info->kind != sireflect_kind_struct || sijson_is_value_type(info)) {
        return;
    }

    const sireflect_fields_t *fields = &info->fields;
    for (size_t i = 0; i < fields->field_count; i++) {
        const sireflect_field_info_t *field = &fields->fields[i];
        const sireflect_type_info_t *field_type = sireflect_type_info(field->type);
        void *field_ptr = (unsigned char *)ptr + field->offset;

        sijson_free_reflected_field(field_type, field_ptr);
    }
}

static void sijson_free_reflected_field(const sireflect_type_info_t *field_type, void *field_ptr) {
    if (field_type == NULL || field_ptr == NULL) {
        return;
    }

    switch (field_type->kind) {
    case sireflect_kind_ptr:
        free(*(void **)field_ptr);
        *(void **)field_ptr = NULL;
        return;
    case sireflect_kind_pointer:
        if (sijson_is_char_pointer_type(field_type)) {
            free(*(void **)field_ptr);
            *(void **)field_ptr = NULL;
        }
        return;
    case sireflect_kind_struct:
        if (!sijson_is_value_type(field_type)) {
            sijson_free_reflected(sireflect_type_by_name(field_type->name), field_ptr);
        }
        return;
    case sireflect_kind_array: {
        const sireflect_type_info_t *element_type =
            sireflect_type_info(field_type->element_type);
        if (element_type == NULL || element_type->size == 0) {
            return;
        }
        for (size_t i = 0; i < field_type->element_count; i++) {
            void *element_ptr = (unsigned char *)field_ptr + i * element_type->size;
            sijson_free_reflected_field(element_type, element_ptr);
        }
        return;
    }
    case sireflect_kind_u8:
    case sireflect_kind_u16:
    case sireflect_kind_u32:
    case sireflect_kind_u64:
    case sireflect_kind_i8:
    case sireflect_kind_i16:
    case sireflect_kind_i32:
    case sireflect_kind_i64:
    case sireflect_kind_signed_char:
    case sireflect_kind_unsigned_char:
    case sireflect_kind_unsigned_short:
    case sireflect_kind_unsigned_int:
    case sireflect_kind_unsigned_long:
    case sireflect_kind_long_long:
    case sireflect_kind_unsigned_long_long:
    case sireflect_kind_f32:
    case sireflect_kind_f64:
    case sireflect_kind_bool:
    case sireflect_kind_char:
    case sireflect_kind_short:
    case sireflect_kind_int:
    case sireflect_kind_long:
    case sireflect_kind_function_pointer:
        return;
    }
}

void sijson_free_impl(sireflect_handle_t *ref, const sireflect_struct_desc_t *desc, void *ptr) {
    sijson_clear_error();
    sireflect_handle_t type = sijson_register_type(ref, desc);
    if (type == SIREFLECT_INVALID_HANDLE) {
        return;
    }
    sijson_free_reflected(type, ptr);
}

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#ifndef SIJSON_ARENA_RESERVE
#define SIJSON_ARENA_RESERVE ((size_t)1 << 30)
#endif

#ifndef SIJSON_ARENA_FALLBACK_RESERVE
#define SIJSON_ARENA_FALLBACK_RESERVE ((size_t)1 << 20)
#endif

typedef struct sijson_arena {
    unsigned char *data;
    size_t used;
    size_t cap;
    size_t reserve;
    bool mmap_backed;
} sijson_arena_t;

static sijson_arena_t g_arena;

static size_t sijson_align_forward(size_t value, size_t align) {
    size_t mask = align - 1;
    return (value + mask) & ~mask;
}

static size_t sijson_page_size(void) {
#if defined(__unix__) || defined(__APPLE__)
    long page = sysconf(_SC_PAGESIZE);
    if (page > 0) {
        return (size_t)page;
    }
#endif
    return 4096;
}

static bool sijson_arena_init(size_t need) {
    if (g_arena.data != NULL) {
        return true;
    }

    size_t page_size = sijson_page_size();
    size_t reserve = SIJSON_ARENA_RESERVE;
    if (reserve < need) {
        reserve = sijson_align_forward(need, page_size);
    }

#if defined(__unix__) || defined(__APPLE__)
    void *data = mmap(NULL, reserve, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data != MAP_FAILED) {
        g_arena.data = data;
        g_arena.reserve = reserve;
        g_arena.mmap_backed = true;
        return true;
    }
#endif

    size_t fallback_reserve = reserve;
    if (need <= SIJSON_ARENA_FALLBACK_RESERVE && fallback_reserve > SIJSON_ARENA_FALLBACK_RESERVE) {
        fallback_reserve = SIJSON_ARENA_FALLBACK_RESERVE;
    }

    g_arena.data = malloc(fallback_reserve);
    if (g_arena.data == NULL) {
        sijson_set_error("out of memory");
        return false;
    }
    g_arena.cap = fallback_reserve;
    g_arena.reserve = fallback_reserve;
    return true;
}

static bool sijson_arena_commit(size_t need) {
    if (!sijson_arena_init(need)) {
        return false;
    }
    if (need <= g_arena.cap) {
        return true;
    }
    if (need > g_arena.reserve) {
        return sijson_set_error("sijson arena capacity exceeded");
    }

    size_t page_size = sijson_page_size();
    size_t cap = sijson_align_forward(need, page_size);

    if (g_arena.mmap_backed) {
#if defined(__unix__) || defined(__APPLE__)
        if (mprotect(g_arena.data + g_arena.cap, cap - g_arena.cap, PROT_READ | PROT_WRITE) != 0) {
            return sijson_set_error("out of memory");
        }
#else
        return sijson_set_error("sijson arena backend unavailable");
#endif
    }

    g_arena.cap = cap;
    return true;
}

void *sijson_arena_alloc(size_t size, size_t align) {
    if (align == 0) {
        align = _Alignof(sijson_max_align_t);
    }

    size_t offset = sijson_align_forward(g_arena.used, align);
    if (offset < g_arena.used || size > SIZE_MAX - offset) {
        sijson_set_error("out of memory");
        return NULL;
    }

    size_t need = offset + size;
    if (!sijson_arena_commit(need)) {
        return NULL;
    }

    void *ptr = g_arena.data + offset;
    g_arena.used = need;
    return ptr;
}

char *sijson_arena_dup_range(const char *start, size_t len) {
    char *result = sijson_arena_alloc(len + 1, _Alignof(char));
    if (result == NULL) {
        return NULL;
    }

    memcpy(result, start, len);
    result[len] = '\0';
    return result;
}

char *sijson_arena_dup_cstr(const char *str) {
    if (str == NULL) {
        return NULL;
    }

    return sijson_arena_dup_range(str, strlen(str));
}

size_t sijson_arena_mark(void) {
    return g_arena.used;
}

void sijson_arena_rewind(size_t mark) {
    if (mark <= g_arena.used) {
        g_arena.used = mark;
    }
}

void sijson_clean(void) {
    sijson_clear_error();
    g_arena.used = 0;
}

void sijson_release(void) {
    sijson_clear_error();
    if (g_arena.mmap_backed) {
#if defined(__unix__) || defined(__APPLE__)
        munmap(g_arena.data, g_arena.reserve);
#endif
    } else {
        free(g_arena.data);
    }
    g_arena = (sijson_arena_t){ 0 };
}

char *sijson_dup_range(const char *start, size_t len) {
    char *result = malloc(len + 1);
    if (result == NULL) {
        sijson_set_error("out of memory");
        return NULL;
    }

    memcpy(result, start, len);
    result[len] = '\0';
    return result;
}

char *sijson_dup_cstr(const char *str) {
    if (str == NULL) {
        return NULL;
    }

    return sijson_dup_range(str, strlen(str));
}

bool sijson_reserve_array(sijson_array_t *array, size_t need) {
    if (array->cap >= need) {
        return true;
    }

    size_t cap = array->cap != 0 ? array->cap * 2 : 8;
    while (cap < need) {
        cap *= 2;
    }

    sijson_value_t *items = sijson_arena_alloc(cap * sizeof(*items), _Alignof(sijson_value_t));
    if (items == NULL) {
        return false;
    }
    if (array->items != NULL) {
        memcpy(items, array->items, array->len * sizeof(*items));
    }

    array->items = items;
    array->cap = cap;
    return true;
}

bool sijson_reserve_object(sijson_object_t *object, size_t need) {
    if (object->cap >= need) {
        return true;
    }

    size_t cap = object->cap != 0 ? object->cap * 2 : 8;
    while (cap < need) {
        cap *= 2;
    }

    sijson_member_t *items = sijson_arena_alloc(cap * sizeof(*items), _Alignof(sijson_member_t));
    if (items == NULL) {
        return false;
    }
    if (object->items != NULL) {
        memcpy(items, object->items, object->len * sizeof(*items));
    }

    object->items = items;
    object->cap = cap;
    return true;
}

sijson_value_t sijson_new_value(sijson_type_t type) {
    sijson_value_t value = sijson_arena_alloc(sizeof(*value), _Alignof(struct sijson_value));
    if (value == NULL) {
        return NULL;
    }

    memset(value, 0, sizeof(*value));
    value->type = type;
    return value;
}

sijson_value_t sijson_make_null(void) {
    sijson_clear_error();
    return sijson_new_value(SIJSON_NULL);
}

sijson_value_t sijson_make_bool(bool value) {
    sijson_clear_error();
    sijson_value_t result = sijson_new_value(SIJSON_BOOL);
    if (result != NULL) {
        result->as.boolean = value;
    }
    return result;
}

sijson_value_t sijson_make_number(double value) {
    sijson_clear_error();
    sijson_value_t result = sijson_new_value(SIJSON_NUMBER);
    if (result != NULL) {
        result->as.number = value;
    }
    return result;
}

sijson_value_t sijson_make_string(const char *value) {
    sijson_clear_error();
    sijson_value_t result = sijson_new_value(SIJSON_STRING);
    if (result == NULL) {
        return NULL;
    }

    result->as.string = sijson_arena_dup_cstr(value != NULL ? value : "");
    if (result->as.string == NULL) {
        return NULL;
    }

    return result;
}

sijson_value_t sijson_make_array(void) {
    sijson_clear_error();
    return sijson_new_value(SIJSON_ARRAY);
}

sijson_value_t sijson_make_object(void) {
    sijson_clear_error();
    return sijson_new_value(SIJSON_OBJECT);
}

sijson_type_t sijson_type(sijson_value_t value) {
    return value != NULL ? value->type : SIJSON_NULL;
}

bool sijson_bool(sijson_value_t value) {
    return value != NULL && value->type == SIJSON_BOOL ? value->as.boolean : false;
}

double sijson_number(sijson_value_t value) {
    return value != NULL && value->type == SIJSON_NUMBER ? value->as.number : 0.0;
}

const char *sijson_string(sijson_value_t value) {
    return value != NULL && value->type == SIJSON_STRING ? value->as.string : NULL;
}

size_t sijson_array_len(sijson_value_t value) {
    return value != NULL && value->type == SIJSON_ARRAY ? value->as.array.len : 0;
}

sijson_value_t sijson_array_get(sijson_value_t value, size_t index) {
    if (value == NULL || value->type != SIJSON_ARRAY || index >= value->as.array.len) {
        return NULL;
    }

    return value->as.array.items[index];
}

size_t sijson_object_len(sijson_value_t value) {
    return value != NULL && value->type == SIJSON_OBJECT ? value->as.object.len : 0;
}

const char *sijson_object_key(sijson_value_t value, size_t index) {
    if (value == NULL || value->type != SIJSON_OBJECT || index >= value->as.object.len) {
        return NULL;
    }

    return value->as.object.items[index].key;
}

sijson_value_t sijson_object_get(sijson_value_t value, const char *key) {
    if (value == NULL || value->type != SIJSON_OBJECT || key == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < value->as.object.len; i++) {
        if (strcmp(value->as.object.items[i].key, key) == 0) {
            return value->as.object.items[i].value;
        }
    }

    return NULL;
}

bool sijson_array_push(sijson_value_t array, sijson_value_t value) {
    sijson_clear_error();
    if (array == NULL || array->type != SIJSON_ARRAY) {
        return sijson_set_error("sijson_array_push expects an array");
    }

    if (!sijson_reserve_array(&array->as.array, array->as.array.len + 1)) {
        return false;
    }

    array->as.array.items[array->as.array.len++] = value;
    return true;
}

bool sijson_object_set(sijson_value_t object, const char *key, sijson_value_t value) {
    sijson_clear_error();
    if (object == NULL || object->type != SIJSON_OBJECT) {
        return sijson_set_error("sijson_object_set expects an object");
    }
    if (key == NULL) {
        return sijson_set_error("sijson_object_set expects a key");
    }

    for (size_t i = 0; i < object->as.object.len; i++) {
        if (strcmp(object->as.object.items[i].key, key) == 0) {
            object->as.object.items[i].value = value;
            return true;
        }
    }

    if (!sijson_reserve_object(&object->as.object, object->as.object.len + 1)) {
        return false;
    }

    char *owned_key = sijson_arena_dup_cstr(key);
    if (owned_key == NULL) {
        return false;
    }

    object->as.object.items[object->as.object.len++] = (sijson_member_t){
        .key = owned_key,
        .value = value,
    };
    return true;
}

bool sijson_writer_reserve(sijson_writer_t *writer, size_t extra) {
    if (writer->len + extra + 1 <= writer->cap) {
        return true;
    }

    size_t cap = writer->cap != 0 ? writer->cap * 2 : 128;
    while (cap < writer->len + extra + 1) {
        cap *= 2;
    }

    char *data = realloc(writer->data, cap);
    if (data == NULL) {
        return sijson_set_error("out of memory");
    }

    writer->data = data;
    writer->cap = cap;
    return true;
}

bool sijson_writer_putc(sijson_writer_t *writer, char c) {
    if (!sijson_writer_reserve(writer, 1)) {
        return false;
    }

    writer->data[writer->len++] = c;
    writer->data[writer->len] = '\0';
    return true;
}

bool sijson_writer_write(sijson_writer_t *writer, const char *data, size_t len) {
    if (!sijson_writer_reserve(writer, len)) {
        return false;
    }

    memcpy(writer->data + writer->len, data, len);
    writer->len += len;
    writer->data[writer->len] = '\0';
    return true;
}

bool sijson_writer_cstr(sijson_writer_t *writer, const char *str) {
    return sijson_writer_write(writer, str, strlen(str));
}

bool sijson_writer_string(sijson_writer_t *writer, const char *str) {
    if (str == NULL) {
        return sijson_writer_cstr(writer, "null");
    }

    if (!sijson_writer_putc(writer, '"')) {
        return false;
    }

    const unsigned char *cur = (const unsigned char *)str;
    const unsigned char *chunk = cur;
    while (*cur != '\0') {
        unsigned char c = *cur;
        if (c == '"' || c == '\\' || c < 0x20) {
            if (!sijson_writer_write(writer, (const char *)chunk, (size_t)(cur - chunk))) {
                return false;
            }

            switch (c) {
            case '"':
                if (!sijson_writer_cstr(writer, "\\\"")) {
                    return false;
                }
                break;
            case '\\':
                if (!sijson_writer_cstr(writer, "\\\\")) {
                    return false;
                }
                break;
            case '\b':
                if (!sijson_writer_cstr(writer, "\\b")) {
                    return false;
                }
                break;
            case '\f':
                if (!sijson_writer_cstr(writer, "\\f")) {
                    return false;
                }
                break;
            case '\n':
                if (!sijson_writer_cstr(writer, "\\n")) {
                    return false;
                }
                break;
            case '\r':
                if (!sijson_writer_cstr(writer, "\\r")) {
                    return false;
                }
                break;
            case '\t':
                if (!sijson_writer_cstr(writer, "\\t")) {
                    return false;
                }
                break;
            default: {
                char escape[7];
                snprintf(escape, sizeof(escape), "\\u%04x", c);
                if (!sijson_writer_cstr(writer, escape)) {
                    return false;
                }
                break;
            }
            }

            cur++;
            chunk = cur;
            continue;
        }
        cur++;
    }

    if (!sijson_writer_write(writer, (const char *)chunk, (size_t)(cur - chunk))) {
        return false;
    }

    return sijson_writer_putc(writer, '"');
}

bool sijson_write_value(sijson_writer_t *writer, sijson_value_t value);

static bool sijson_write_array(sijson_writer_t *writer, sijson_value_t value) {
    if (!sijson_writer_putc(writer, '[')) {
        return false;
    }

    for (size_t i = 0; i < value->as.array.len; i++) {
        if (i != 0 && !sijson_writer_putc(writer, ',')) {
            return false;
        }
        if (!sijson_write_value(writer, value->as.array.items[i])) {
            return false;
        }
    }

    return sijson_writer_putc(writer, ']');
}

static bool sijson_write_object(sijson_writer_t *writer, sijson_value_t value) {
    if (!sijson_writer_putc(writer, '{')) {
        return false;
    }

    for (size_t i = 0; i < value->as.object.len; i++) {
        if (i != 0 && !sijson_writer_putc(writer, ',')) {
            return false;
        }
        if (!sijson_writer_string(writer, value->as.object.items[i].key)) {
            return false;
        }
        if (!sijson_writer_putc(writer, ':')) {
            return false;
        }
        if (!sijson_write_value(writer, value->as.object.items[i].value)) {
            return false;
        }
    }

    return sijson_writer_putc(writer, '}');
}

bool sijson_write_value(sijson_writer_t *writer, sijson_value_t value) {
    if (value == NULL) {
        return sijson_writer_cstr(writer, "null");
    }

    switch (value->type) {
    case SIJSON_NULL:
        return sijson_writer_cstr(writer, "null");
    case SIJSON_BOOL:
        return sijson_writer_cstr(writer, value->as.boolean ? "true" : "false");
    case SIJSON_NUMBER: {
        if (!isfinite(value->as.number)) {
            return sijson_set_error("cannot write non-finite JSON number");
        }
        char number[64];
        int len = snprintf(number, sizeof(number), "%.17g", value->as.number);
        if (len < 0 || (size_t)len >= sizeof(number)) {
            return sijson_set_error("failed to format JSON number");
        }
        return sijson_writer_write(writer, number, (size_t)len);
    }
    case SIJSON_STRING:
        return sijson_writer_string(writer, value->as.string);
    case SIJSON_ARRAY:
        return sijson_write_array(writer, value);
    case SIJSON_OBJECT:
        return sijson_write_object(writer, value);
    }

    return sijson_set_error("unknown JSON value type");
}

char *sijson_stringify(sijson_value_t value) {
    sijson_clear_error();
    sijson_writer_t writer = { 0 };
    if (!sijson_write_value(&writer, value)) {
        free(writer.data);
        return NULL;
    }
    if (writer.data == NULL) {
        return sijson_dup_cstr("");
    }
    return writer.data;
}

#ifndef SIHTTP_BUFFER_H
#define SIHTTP_BUFFER_H

#include <stddef.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} sihttp_buffer_t;

void sihttp_buffer_init(sihttp_buffer_t *buffer);
void sihttp_buffer_fini(sihttp_buffer_t *buffer);
int sihttp_buffer_append(sihttp_buffer_t *buffer, const char *data, size_t len);

#endif

#ifndef SIHTTP_INTERNAL_H
#define SIHTTP_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#define SIHTTP_MAX_HEADER_BYTES (16u * 1024u)
#define SIHTTP_MAX_BODY_BYTES (1024u * 1024u)
#define SIHTTP_MAX_HEADERS 64u
#define SIHTTP_MAX_PARAMS 16u

typedef struct {
    const char *name;
    const char *value;
} sihttp_pair_t;

typedef struct {
    sihttp_request_t public_req;
    char param_names[SIHTTP_MAX_PARAMS][32];
    char param_values[SIHTTP_MAX_PARAMS][64];
    sihttp_pair_t params[SIHTTP_MAX_PARAMS];
    size_t param_count;
    sihttp_pair_t query[SIHTTP_MAX_PARAMS];
    size_t query_count;
    char *storage;
    size_t storage_len;
} sihttp_request_internal_t;

typedef struct {
    int code;
    size_t expected_len;
} sihttp_parse_result_t;

void sihttp_set_error(const char *fmt, ...) SIHTTP_PRINTF_FORMAT(1, 2);

const char *sihttp_method_name(sihttp_method_t method);
sihttp_method_t sihttp_method_from_name(const char *method, int *ok);

void sihttp_request_internal_init(sihttp_request_internal_t *req);
void sihttp_request_internal_fini(sihttp_request_internal_t *req);
int sihttp_request_add_param(sihttp_request_internal_t *req, const char *name, const char *value);
int sihttp_request_parse(
    sihttp_request_internal_t *req,
    const char *data,
    size_t len,
    sihttp_app_state_t *state
);
sihttp_parse_result_t sihttp_request_parse_state(const char *data, size_t len);

char *sihttp_build_response(sihttp_response_t response, size_t *out_len);
int sihttp_send_response(int fd, sihttp_response_t response);

typedef struct sihttp_route_table_s sihttp_route_table_t;

int sihttp_route_table_init(sihttp_route_table_t *table);
void sihttp_route_table_fini(sihttp_route_table_t *table);
int sihttp_route_table_add(
    sihttp_route_table_t *table,
    sihttp_method_t method,
    const char *path,
    sihttp_handler_t callback
);
sihttp_handler_t sihttp_route_table_match(
    const sihttp_route_table_t *table,
    sihttp_method_t method,
    const char *path,
    sihttp_request_internal_t *req,
    int *method_not_allowed
);

struct sihttp_server_s {
    sihttp_app_state_t *state;
    sihttp_route_table_t *routes;
    int listen_fd;
    uint16_t port;
    int backlog;
    int max_requests_per_poll;
    int running;
};

int sihttp_server_handle_client(sihttp_server_t *server, int client_fd);

#endif

#ifndef SIHTTP_ROUTE_H
#define SIHTTP_ROUTE_H

typedef struct {
    sihttp_method_t method;
    char *path;
    sihttp_handler_t callback;
} sihttp_route_entry_t;

struct sihttp_route_table_s {
    sihttp_route_entry_t *entries;
    size_t count;
    size_t capacity;
};

#endif

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <unistd.h>

static char sihttp_error_buffer[256];

void sihttp_set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(sihttp_error_buffer, sizeof(sihttp_error_buffer), fmt, args);
    va_end(args);
}

SIHTTP_API const char *sihttp_error(void) {
    return sihttp_error_buffer[0] ? sihttp_error_buffer : NULL;
}

static char *sihttp_static_body(const char *body) {
    size_t len = strlen(body);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, body, len + 1);
    return copy;
}

enum {
    SIHTTP_DEFAULT_BACKLOG = 128,
    SIHTTP_DEFAULT_MAX_REQUESTS_PER_POLL = 64,
};

static int sihttp_set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1) {
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }

    return 0;
}

static sihttp_response_t sihttp_error_response(int status, const char *body) {
    return (sihttp_response_t){ .status = status, .body = sihttp_static_body(body) };
}

static sihttp_response_t sihttp_preflight_response(void) {
    return (sihttp_response_t){ .status = 204, .body = sihttp_static_body("") };
}

static sihttp_response_t sihttp_dispatch_request(
    sihttp_server_t *server,
    sihttp_method_t method,
    sihttp_request_internal_t *req
) {
    int method_not_allowed = 0;
    sihttp_handler_t handler;

    if (method == SIHTTP_METHOD_OPTIONS) {
        return sihttp_preflight_response();
    }

    handler = sihttp_route_table_match(
        server->routes,
        method,
        req->public_req.path,
        req,
        &method_not_allowed
    );
    if (!handler) {
        return sihttp_error_response(method_not_allowed ? 405 : 404, "");
    }

    return handler(&req->public_req);
}

SIHTTP_API sihttp_server_t *sihttp_server_init(const sihttp_server_desc_t *desc) {
    sihttp_server_t *server;
    int port = 0;
    int backlog = SIHTTP_DEFAULT_BACKLOG;
    int max_requests_per_poll = SIHTTP_DEFAULT_MAX_REQUESTS_PER_POLL;

    if (desc) {
        if (desc->port < 0 || desc->port > UINT16_MAX) {
            sihttp_set_error("invalid server port: %d", desc->port);
            return NULL;
        }
        port = desc->port;
        backlog = desc->backlog > 0 ? desc->backlog : SIHTTP_DEFAULT_BACKLOG;
        max_requests_per_poll = desc->max_requests_per_poll > 0
            ? desc->max_requests_per_poll
            : SIHTTP_DEFAULT_MAX_REQUESTS_PER_POLL;
    }

    server = calloc(1, sizeof(*server));
    if (!server) {
        sihttp_set_error("out of memory");
        return NULL;
    }

    server->routes = malloc(sizeof(*server->routes));
    if (!server->routes) {
        free(server);
        sihttp_set_error("out of memory");
        return NULL;
    }

    sihttp_route_table_init(server->routes);
    server->port = (uint16_t)port;
    server->backlog = backlog;
    server->max_requests_per_poll = max_requests_per_poll;
    if (desc) {
        server->state = desc->state;
    }
    server->listen_fd = -1;
    return server;
}

SIHTTP_API void sihttp_server_fini(sihttp_server_t *server) {
    if (!server) {
        return;
    }

    sihttp_server_stop(server);
    sihttp_route_table_fini(server->routes);
    free(server->routes);
    free(server);
}

SIHTTP_API int sihttp_server_listen(sihttp_server_t *server, const char *host, uint16_t port) {
    int fd;
    int yes = 1;
    struct sockaddr_in addr;
    socklen_t addr_len;

    if (!server) {
        sihttp_set_error("server is NULL");
        return -1;
    }

    if (server->listen_fd != -1) {
        sihttp_set_error("server is already listening");
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        sihttp_set_error("socket failed: %s", strerror(errno));
        return -1;
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (!host || strcmp(host, "") == 0 || strcmp(host, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        sihttp_set_error("invalid IPv4 host: %s", host);
        return -1;
    }

    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        sihttp_set_error("bind failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, server->backlog) != 0) {
        sihttp_set_error("listen failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    addr_len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &addr_len) == 0) {
        server->port = ntohs(addr.sin_port);
    } else {
        server->port = port;
    }

    server->listen_fd = fd;
    return 0;
}

SIHTTP_API uint16_t sihttp_server_port(const sihttp_server_t *server) {
    return server ? server->port : 0;
}

SIHTTP_API void sihttp_server_stop(sihttp_server_t *server) {
    if (!server) {
        return;
    }

    server->running = 0;
    if (server->listen_fd != -1) {
        int fd = server->listen_fd;
        server->listen_fd = -1;
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
}

int sihttp_server_handle_client(sihttp_server_t *server, int client_fd) {
    sihttp_buffer_t buffer;
    int status = 400;
    sihttp_request_internal_t req;
    int method_ok = 0;
    sihttp_method_t method;
    sihttp_response_t response;

    sihttp_buffer_init(&buffer);

    for (;;) {
        char chunk[4096];
        sihttp_parse_result_t parse_state;
        ssize_t received = recv(client_fd, chunk, sizeof(chunk), 0);

        if (received < 0) {
            status = 400;
            break;
        }
        if (received == 0) {
            parse_state = sihttp_request_parse_state(buffer.data, buffer.len);
            status = parse_state.code == 200 ? 200 : 400;
            break;
        }

        if (sihttp_buffer_append(&buffer, chunk, (size_t)received) != 0) {
            status = 500;
            break;
        }

        parse_state = sihttp_request_parse_state(buffer.data, buffer.len);
        if (parse_state.code == 200) {
            status = 200;
            break;
        }
        if (parse_state.code != 0) {
            status = parse_state.code;
            break;
        }
    }

    if (status != 200) {
        sihttp_send_response(client_fd, sihttp_error_response(status, ""));
        sihttp_buffer_fini(&buffer);
        return -1;
    }

    status = sihttp_request_parse(&req, buffer.data, buffer.len, server->state);
    if (status != 200) {
        sihttp_send_response(client_fd, sihttp_error_response(status, ""));
        sihttp_buffer_fini(&buffer);
        return -1;
    }

    method = sihttp_method_from_name(req.public_req.method, &method_ok);
    if (!method_ok) {
        sihttp_send_response(client_fd, sihttp_error_response(405, ""));
        sihttp_request_internal_fini(&req);
        sihttp_buffer_fini(&buffer);
        return -1;
    }

    response = sihttp_dispatch_request(server, method, &req);
    if (sihttp_send_response(client_fd, response) != 0) {
        status = 500;
    }

    sihttp_request_internal_fini(&req);
    sihttp_buffer_fini(&buffer);
    return status == 200 ? 0 : -1;
}

SIHTTP_API sihttp_response_t sihttp_server_dispatch(
    sihttp_server_t *server,
    sihttp_method_t method,
    const char *path,
    const char *body
) {
    sihttp_request_internal_t req;
    sihttp_response_t response;

    sihttp_request_internal_init(&req);

    req.public_req.method = sihttp_method_name(method);
    req.public_req.path = path;
    req.public_req.body = body;
    req.public_req.state = server->state;

    response = sihttp_dispatch_request(server, method, &req);
    sihttp_request_internal_fini(&req);
    return response;
}

SIHTTP_API int sihttp_server_start(sihttp_server_t *server) {
    if (!server) {
        sihttp_set_error("server is NULL");
        return -1;
    }

    if (server->listen_fd == -1 && sihttp_server_listen(server, NULL, server->port) != 0) {
        return -1;
    }

    if (sihttp_set_nonblocking(server->listen_fd) != 0) {
        sihttp_set_error("could not make server socket non-blocking: %s", strerror(errno));
        return -1;
    }

    server->running = 1;
    return 0;
}

SIHTTP_API int sihttp_server_poll(sihttp_server_t *server) {
    int handled = 0;

    if (!server) {
        sihttp_set_error("server is NULL");
        return -1;
    }

    if (!server->running && sihttp_server_start(server) != 0) {
        return -1;
    }

    while (handled < server->max_requests_per_poll) {
        int client_fd = accept(server->listen_fd, NULL, NULL);

        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return handled;
            }
            if (!server->running || server->listen_fd == -1 || errno == EBADF || errno == EINVAL) {
                return handled;
            }

            sihttp_set_error("accept failed: %s", strerror(errno));
            return -1;
        }

        sihttp_server_handle_client(server, client_fd);
        close(client_fd);
        handled++;
    }

    return handled;
}

SIHTTP_API int sihttp_server_run(sihttp_server_t *server) {
    if (!server) {
        sihttp_set_error("server is NULL");
        return -1;
    }

    if (server->listen_fd == -1 && sihttp_server_listen(server, NULL, server->port) != 0) {
        return -1;
    }

    server->running = 1;
    while (server->running) {
        int client_fd = accept(server->listen_fd, NULL, NULL);
        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            if (!server->running || server->listen_fd == -1 || errno == EBADF || errno == EINVAL) {
                break;
            }
            sihttp_set_error("accept failed: %s", strerror(errno));
            return -1;
        }

        sihttp_server_handle_client(server, client_fd);
        close(client_fd);
    }

    return 0;
}

SIHTTP_API void
sihttp_route_impl(sihttp_server_t *server, const char *path, const sihttp_handler_desc_t *desc) {
    if (!server || !desc) {
        sihttp_set_error("invalid route descriptor");
        return;
    }

    if (sihttp_route_table_add(server->routes, desc->method, path, desc->callback) != 0) {
        sihttp_set_error("could not add route: %s", path ? path : "(null)");
    }
}

SIHTTP_API void sihttp_get(sihttp_server_t *server, const char *path, sihttp_handler_t callback) {
    sihttp_route(server, path, { .method = SIHTTP_METHOD_GET, .callback = callback });
}

SIHTTP_API void sihttp_post(sihttp_server_t *server, const char *path, sihttp_handler_t callback) {
    sihttp_route(server, path, { .method = SIHTTP_METHOD_POST, .callback = callback });
}

SIHTTP_API void sihttp_put(sihttp_server_t *server, const char *path, sihttp_handler_t callback) {
    sihttp_route(server, path, { .method = SIHTTP_METHOD_PUT, .callback = callback });
}

SIHTTP_API void
sihttp_delete(sihttp_server_t *server, const char *path, sihttp_handler_t callback) {
    sihttp_route(server, path, { .method = SIHTTP_METHOD_DELETE, .callback = callback });
}

SIHTTP_API char *siformat(const char *fmt, ...) {
    va_list args;
    va_list copy;
    int size;
    char *str;

    va_start(args, fmt);
    va_copy(copy, args);

    size = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);

    if (size < 0) {
        va_end(args);
        return NULL;
    }

    str = malloc((size_t)size + 1);
    if (!str) {
        va_end(args);
        return NULL;
    }

    vsnprintf(str, (size_t)size + 1, fmt, args);
    va_end(args);

    return str;
}

void sihttp_buffer_init(sihttp_buffer_t *buffer) {
    buffer->data = NULL;
    buffer->len = 0;
    buffer->cap = 0;
}

void sihttp_buffer_fini(sihttp_buffer_t *buffer) {
    free(buffer->data);
    buffer->data = NULL;
    buffer->len = 0;
    buffer->cap = 0;
}

int sihttp_buffer_append(sihttp_buffer_t *buffer, const char *data, size_t len) {
    size_t required;

    if (len == 0) {
        return 0;
    }

    required = buffer->len + len;
    if (required < buffer->len) {
        return -1;
    }

    if (required > buffer->cap) {
        size_t next = buffer->cap ? buffer->cap : 1024;
        char *new_data;

        while (next < required) {
            size_t doubled = next * 2;
            if (doubled < next) {
                return -1;
            }
            next = doubled;
        }

        new_data = realloc(buffer->data, next);
        if (!new_data) {
            return -1;
        }

        buffer->data = new_data;
        buffer->cap = next;
    }

    memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;
    return 0;
}

static const char *sihttp_find_header_end_const(const char *data, size_t len) {
    if (len < 4) {
        return NULL;
    }

    for (size_t i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
            return data + i;
        }
    }

    return NULL;
}

static char *sihttp_find_header_end(char *data, size_t len) {
    if (len < 4) {
        return NULL;
    }

    for (size_t i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
            return data + i;
        }
    }

    return NULL;
}

static int sihttp_streq_icase(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static char *sihttp_trim(char *str) {
    char *end;

    while (*str && isspace((unsigned char)*str)) {
        str++;
    }

    end = str + strlen(str);
    while (end > str && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';

    return str;
}

static int sihttp_parse_size(const char *value, size_t *out) {
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno || end == value || *sihttp_trim(end) != '\0' || parsed > SIZE_MAX) {
        return -1;
    }

    *out = (size_t)parsed;
    return 0;
}

static int sihttp_add_pair(sihttp_pair_t *pairs, size_t *count, const char *name, const char *value) {
    if (*count >= SIHTTP_MAX_PARAMS) {
        return -1;
    }

    pairs[*count].name = name;
    pairs[*count].value = value;
    (*count)++;
    return 0;
}

static void sihttp_parse_query(sihttp_request_internal_t *req, char *query) {
    char *cursor = query;
    while (cursor && *cursor && req->query_count < SIHTTP_MAX_PARAMS) {
        char *next = strchr(cursor, '&');
        char *eq;

        if (next) {
            *next = '\0';
            next++;
        }

        eq = strchr(cursor, '=');
        if (eq) {
            *eq = '\0';
            sihttp_add_pair(req->query, &req->query_count, cursor, eq + 1);
        }

        cursor = next;
    }
}

void sihttp_request_internal_init(sihttp_request_internal_t *req) {
    memset(req, 0, sizeof(*req));
}

void sihttp_request_internal_fini(sihttp_request_internal_t *req) {
    free(req->storage);
    sihttp_request_internal_init(req);
}

int sihttp_request_add_param(sihttp_request_internal_t *req, const char *name, const char *value) {
    size_t name_len;
    size_t value_len;

    if (req->param_count >= SIHTTP_MAX_PARAMS) {
        return -1;
    }

    name_len = strlen(name);
    value_len = strlen(value);
    if (name_len >= sizeof(req->param_names[0]) || value_len >= sizeof(req->param_values[0])) {
        return -1;
    }

    memcpy(req->param_names[req->param_count], name, name_len + 1);
    memcpy(req->param_values[req->param_count], value, value_len + 1);
    req->params[req->param_count].name = req->param_names[req->param_count];
    req->params[req->param_count].value = req->param_values[req->param_count];
    req->param_count++;
    return 0;
}

const char *sihttp_method_name(sihttp_method_t method) {
    switch (method) {
    case SIHTTP_METHOD_GET:
        return "GET";
    case SIHTTP_METHOD_POST:
        return "POST";
    case SIHTTP_METHOD_PUT:
        return "PUT";
    case SIHTTP_METHOD_DELETE:
        return "DELETE";
    case SIHTTP_METHOD_OPTIONS:
        return "OPTIONS";
    }

    return "GET";
}

sihttp_method_t sihttp_method_from_name(const char *method, int *ok) {
    if (strcmp(method, "GET") == 0) {
        *ok = 1;
        return SIHTTP_METHOD_GET;
    }
    if (strcmp(method, "POST") == 0) {
        *ok = 1;
        return SIHTTP_METHOD_POST;
    }
    if (strcmp(method, "PUT") == 0) {
        *ok = 1;
        return SIHTTP_METHOD_PUT;
    }
    if (strcmp(method, "DELETE") == 0) {
        *ok = 1;
        return SIHTTP_METHOD_DELETE;
    }
    if (strcmp(method, "OPTIONS") == 0) {
        *ok = 1;
        return SIHTTP_METHOD_OPTIONS;
    }

    *ok = 0;
    return SIHTTP_METHOD_GET;
}

sihttp_parse_result_t sihttp_request_parse_state(const char *data, size_t len) {
    sihttp_parse_result_t result = { .code = 0, .expected_len = 0 };
    const char *headers_end;
    size_t header_len;
    size_t content_length = 0;
    char *copy;
    char *line;

    headers_end = sihttp_find_header_end_const(data, len);
    if (!headers_end) {
        if (len > SIHTTP_MAX_HEADER_BYTES) {
            result.code = 413;
        }
        return result;
    }

    header_len = (size_t)(headers_end - data) + 4;
    if (header_len > SIHTTP_MAX_HEADER_BYTES) {
        result.code = 413;
        return result;
    }

    copy = malloc(header_len + 1);
    if (!copy) {
        result.code = 500;
        return result;
    }
    memcpy(copy, data, header_len);
    copy[header_len] = '\0';

    line = strstr(copy, "\r\n");
    while (line) {
        char *line_end;
        char *colon;

        line += 2;
        if (*line == '\r' && line[1] == '\n') {
            break;
        }

        line_end = strstr(line, "\r\n");
        if (!line_end) {
            break;
        }
        *line_end = '\0';

        colon = strchr(line, ':');
        if (colon) {
            char *name;
            char *value;

            *colon = '\0';
            name = sihttp_trim(line);
            value = sihttp_trim(colon + 1);
            if (sihttp_streq_icase(name, "Content-Length") && sihttp_parse_size(value, &content_length) != 0) {
                free(copy);
                result.code = 400;
                return result;
            }
        }

        line = line_end;
    }

    free(copy);

    if (content_length > SIHTTP_MAX_BODY_BYTES) {
        result.code = 413;
        return result;
    }

    result.expected_len = header_len + content_length;
    if (len >= result.expected_len) {
        result.code = 200;
    }
    return result;
}

int sihttp_request_parse(
    sihttp_request_internal_t *req,
    const char *data,
    size_t len,
    sihttp_app_state_t *state
) {
    sihttp_parse_result_t state_result;
    char *headers_end;
    char *body;
    char *request_line_end;
    char *method;
    char *target;
    char *version;
    char *query;
    int method_ok = 0;

    sihttp_request_internal_init(req);

    state_result = sihttp_request_parse_state(data, len);
    if (state_result.code != 200) {
        return state_result.code ? state_result.code : 400;
    }

    req->storage = malloc(state_result.expected_len + 1);
    if (!req->storage) {
        return 500;
    }

    memcpy(req->storage, data, state_result.expected_len);
    req->storage[state_result.expected_len] = '\0';
    req->storage_len = state_result.expected_len;

    headers_end = sihttp_find_header_end(req->storage, req->storage_len);
    if (!headers_end) {
        return 400;
    }

    body = headers_end + 4;
    *headers_end = '\0';

    request_line_end = strstr(req->storage, "\r\n");
    if (!request_line_end) {
        return 400;
    }
    *request_line_end = '\0';

    method = req->storage;
    target = strchr(method, ' ');
    if (!target) {
        return 400;
    }
    *target++ = '\0';

    version = strchr(target, ' ');
    if (!version) {
        return 400;
    }
    *version++ = '\0';

    if (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) {
        return 400;
    }

    query = strchr(target, '?');
    if (query) {
        *query++ = '\0';
        sihttp_parse_query(req, query);
    }

    sihttp_method_from_name(method, &method_ok);
    if (!method_ok) {
        return 405;
    }

    req->public_req.method = method;
    req->public_req.path = target;
    req->public_req.body = body;
    req->public_req.state = state;
    return 200;
}

SIHTTP_API int64_t sihttp_param(const sihttp_request_t *public_req, const char *name) {
    const sihttp_request_internal_t *req = (const sihttp_request_internal_t *)public_req;

    for (size_t i = 0; i < req->param_count; i++) {
        if (strcmp(req->params[i].name, name) == 0) {
            return strtoll(req->params[i].value, NULL, 10);
        }
    }

    for (size_t i = 0; i < req->query_count; i++) {
        if (strcmp(req->query[i].name, name) == 0) {
            return strtoll(req->query[i].value, NULL, 10);
        }
    }

    const char *query = strchr(req->public_req.path, '?');
    if (query) {
        size_t name_len = strlen(name);
        query++;
        while (*query) {
            const char *value = strchr(query, '=');
            const char *end = strchr(query, '&');
            if (!end) {
                end = query + strlen(query);
            }
            if (value && value < end && (size_t)(value - query) == name_len &&
                memcmp(query, name, name_len) == 0) {
                return strtoll(value + 1, NULL, 10);
            }
            query = *end ? end + 1 : end;
        }
    }

    return 0;
}

SIHTTP_API void sihttp_response_fini(sihttp_response_t *response) {
    if (!response) {
        return;
    }

    free(response->body);
    response->body = NULL;
    response->status = 0;
    response->content_type = SIHTTP_CONTENT_AUTO;
}

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

static const char *sihttp_content_type_name(sihttp_content_type_t content_type) {
    switch (content_type) {
    case SIHTTP_CONTENT_AUTO:
    case SIHTTP_CONTENT_TEXT:
        return "text/plain; charset=utf-8";
    case SIHTTP_CONTENT_JSON:
        return "application/json";
    case SIHTTP_CONTENT_HTML:
        return "text/html; charset=utf-8";
    case SIHTTP_CONTENT_BINARY:
        return "application/octet-stream";
    }

    return "text/plain; charset=utf-8";
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
    const char *content_type = sihttp_content_type_name(response.content_type);
    const char *body = response.body ? response.body : "";
    size_t body_len = response.body ? strlen(response.body) : 0;
    int header_len;
    size_t total;
    char *message;

    header_len = snprintf(
        NULL,
        0,
        "HTTP/1.1 %d %s\r\n"
        "Content-Length: %zu\r\n"
        "Content-Type: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "Connection: close\r\n"
        "\r\n",
        status,
        reason,
        body_len,
        content_type
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
        "HTTP/1.1 %d %s\r\n"
        "Content-Length: %zu\r\n"
        "Content-Type: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "Connection: close\r\n"
        "\r\n",
        status,
        reason,
        body_len,
        content_type
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
        sihttp_response_fini(&response);
        return -1;
    }

    result = sihttp_send_all(fd, message, len);
    free(message);
    sihttp_response_fini(&response);
    return result;
}

static char *sihttp_strdup(const char *str) {
    size_t len = strlen(str);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, str, len + 1);
    return copy;
}

static int
sihttp_route_path_matches(const char *pattern, const char *path, sihttp_request_internal_t *req) {
    const char *p = pattern;
    const char *s = path;

    while (*p && *s) {
        if (*p == ':') {
            const char *name_start;
            const char *value_start;
            size_t name_len;
            size_t value_len;
            char name[32];
            char value[64];
            int ok;

            p++;
            name_start = p;
            while (*p && *p != '/') {
                p++;
            }

            value_start = s;
            while (*s && *s != '/' && *s != '?') {
                s++;
            }

            name_len = (size_t)(p - name_start);
            value_len = (size_t)(s - value_start);
            if (name_len == 0 || value_len == 0 || req->param_count >= SIHTTP_MAX_PARAMS) {
                return 0;
            }

            if (name_len >= sizeof(name) || value_len >= sizeof(value)) {
                return 0;
            }
            memcpy(name, name_start, name_len);
            name[name_len] = '\0';
            memcpy(value, value_start, value_len);
            value[value_len] = '\0';

            ok = sihttp_request_add_param(req, name, value) == 0;
            if (!ok) {
                return 0;
            }
        } else if (*p == *s) {
            p++;
            s++;
        } else {
            return 0;
        }
    }

    return *p == '\0' && (*s == '\0' || *s == '?');
}

int sihttp_route_table_init(sihttp_route_table_t *table) {
    table->entries = NULL;
    table->count = 0;
    table->capacity = 0;
    return 0;
}

void sihttp_route_table_fini(sihttp_route_table_t *table) {
    for (size_t i = 0; i < table->count; i++) {
        free(table->entries[i].path);
    }
    free(table->entries);
    table->entries = NULL;
    table->count = 0;
    table->capacity = 0;
}

int sihttp_route_table_add(
    sihttp_route_table_t *table,
    sihttp_method_t method,
    const char *path,
    sihttp_handler_t callback
) {
    char *path_copy;

    if (!path || path[0] != '/' || !callback) {
        return -1;
    }

    if (table->count == table->capacity) {
        size_t next = table->capacity ? table->capacity * 2 : 8;
        sihttp_route_entry_t *entries = realloc(table->entries, next * sizeof(*entries));
        if (!entries) {
            return -1;
        }
        table->entries = entries;
        table->capacity = next;
    }

    path_copy = sihttp_strdup(path);
    if (!path_copy) {
        return -1;
    }

    table->entries[table->count++] = (sihttp_route_entry_t){
        .method = method,
        .path = path_copy,
        .callback = callback,
    };
    return 0;
}

sihttp_handler_t sihttp_route_table_match(
    const sihttp_route_table_t *table,
    sihttp_method_t method,
    const char *path,
    sihttp_request_internal_t *req,
    int *method_not_allowed
) {
    *method_not_allowed = 0;

    for (size_t i = 0; i < table->count; i++) {
        const sihttp_route_entry_t *entry = &table->entries[i];
        size_t saved_count = req->param_count;
        if (!sihttp_route_path_matches(entry->path, path, req)) {
            req->param_count = saved_count;
            continue;
        }

        if (entry->method == method) {
            return entry->callback;
        }

        req->param_count = saved_count;
        *method_not_allowed = 1;
    }

    return NULL;
}

