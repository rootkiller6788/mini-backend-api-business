#ifndef JSON_HELPER_H
#define JSON_HELPER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * L1 - Core Definitions: JSON value types (RFC 8259), token types
 * L2 - Core Concepts: JSON data model — object, array, string, number, boolean, null
 * L3 - Engineering Structures: Recursive descent parser with tokenizer pipeline
 * L4 - Standards/Theorems: RFC 8259 / ECMA-404 JSON specification
 * L5 - Algorithms: Finite state machine tokenizer, recursive descent parser
 * L6 - Canonical Problem: Data interchange format parsing/validation
 * L7 - Application: REST API request/response body handling, config file parsing
 * L8 - Advanced: Streaming JSON parser (SAX-style), schema validation
 * L9 - Industry: JSON vs Protocol Buffers vs MessagePack trade-offs
 */

#define JSON_MAX_DEPTH        32
#define JSON_MAX_KEY_LEN     256
#define JSON_MAX_STRING_LEN 8192
#define JSON_MAX_NUMBER_LEN   64

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef enum {
    JSON_TOK_LBRACE,
    JSON_TOK_RBRACE,
    JSON_TOK_LBRACKET,
    JSON_TOK_RBRACKET,
    JSON_TOK_COMMA,
    JSON_TOK_COLON,
    JSON_TOK_STRING,
    JSON_TOK_NUMBER,
    JSON_TOK_TRUE,
    JSON_TOK_FALSE,
    JSON_TOK_NULL,
    JSON_TOK_EOF,
    JSON_TOK_ERROR
} JsonTokenType;

typedef struct {
    JsonTokenType type;
    char          text[JSON_MAX_STRING_LEN];
    double        num_value;
    int           line;
    int           col;
} JsonToken;

typedef struct JsonValue JsonValue;

struct JsonValue {
    JsonType type;

    union {
        bool    bool_val;
        double  num_val;
        char   *str_val;
        struct {
            JsonValue *items;
            int        count;
            int        capacity;
        } array;
        struct {
            char      **keys;
            JsonValue  *values;
            int         count;
            int         capacity;
        } object;
    } data;
};

/* ── Tokenizer ────────────────────────────────────────────────────────────── */
typedef struct {
    const char *input;
    size_t      pos;
    size_t      len;
    int         line;
    int         col;
} JsonTokenizer;

void json_tokenizer_init(JsonTokenizer *t, const char *input);
bool json_tokenizer_next(JsonTokenizer *t, JsonToken *tok);

/* ── Parser ───────────────────────────────────────────────────────────────── */
JsonValue *json_parse(const char *json_str);
void       json_value_free(JsonValue *val);

/* ── Accessors ────────────────────────────────────────────────────────────── */
JsonType  json_value_type(const JsonValue *val);
bool      json_value_get_bool(const JsonValue *val, bool def);
double    json_value_get_number(const JsonValue *val, double def);
const char *json_value_get_string(const JsonValue *val, const char *def);

int       json_array_size(const JsonValue *val);
JsonValue *json_array_get(const JsonValue *val, int index);

int       json_object_size(const JsonValue *val);
JsonValue *json_object_get(const JsonValue *val, const char *key);
const char **json_object_keys(const JsonValue *val, int *out_count);

/* ── Builder ──────────────────────────────────────────────────────────────── */
JsonValue *json_build_null(void);
JsonValue *json_build_bool(bool val);
JsonValue *json_build_number(double val);
JsonValue *json_build_string(const char *val);
JsonValue *json_build_array(void);
JsonValue *json_build_object(void);

bool json_array_push(JsonValue *arr, JsonValue *item);
bool json_object_set(JsonValue *obj, const char *key, JsonValue *val);

/* ── Serialization ────────────────────────────────────────────────────────── */
int  json_serialize(const JsonValue *val, char *buf, size_t buf_sz);
void json_pretty_print(const JsonValue *val);

/* ── Path Access (dot notation) ───────────────────────────────────────────── */
JsonValue *json_path_get(const JsonValue *root, const char *path);

#endif /* JSON_HELPER_H */
