#include "json_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ── Tokenizer ────────────────────────────────────────────────────────────── */
/*
 * L5: Finite-state machine tokenizer.
 * Processes JSON input character by character, producing tokens:
 *   { } [ ] , : "strings" numbers true false null
 * Handles escape sequences, unicode escapes (\uXXXX), and number formats.
 */

void json_tokenizer_init(JsonTokenizer *t, const char *input) {
    memset(t, 0, sizeof(*t));
    t->input = input;
    t->len   = input ? strlen(input) : 0;
    t->line  = 1;
    t->col   = 1;
}

static void skip_whitespace(JsonTokenizer *t) {
    while (t->pos < t->len) {
        char c = t->input[t->pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            t->pos++; t->col++;
        } else if (c == '\n') {
            t->pos++; t->line++; t->col = 1;
        } else {
            break;
        }
    }
}

static bool parse_string_token(JsonTokenizer *t, JsonToken *tok) {
    size_t start = t->pos;
    if (t->pos >= t->len || t->input[t->pos] != '"') return 0;
    t->pos++; t->col++;
    start++;

    size_t out = 0;
    while (t->pos < t->len && out < JSON_MAX_STRING_LEN - 1) {
        char c = t->input[t->pos];
        if (c == '"') {
            t->pos++; t->col++;
            tok->text[out] = '\0';
            tok->type = JSON_TOK_STRING;
            return 1;
        }
        if (c == '\\') {
            t->pos++; t->col++;
            if (t->pos >= t->len) return 0;
            char esc = t->input[t->pos];
            switch (esc) {
                case '"':  tok->text[out++] = '"';  break;
                case '\\': tok->text[out++] = '\\'; break;
                case '/':  tok->text[out++] = '/';  break;
                case 'b':  tok->text[out++] = '\b'; break;
                case 'f':  tok->text[out++] = '\f'; break;
                case 'n':  tok->text[out++] = '\n'; break;
                case 'r':  tok->text[out++] = '\r'; break;
                case 't':  tok->text[out++] = '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    for (int i = 0; i < 4; i++) {
                        t->pos++; t->col++;
                        if (t->pos >= t->len) return 0;
                        char h = (char)tolower((unsigned char)t->input[t->pos]);
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else return 0;
                    }
                    if (cp < 0x80) {
                        tok->text[out++] = (char)cp;
                    } else if (cp < 0x800) {
                        tok->text[out++] = (char)(0xC0 | (cp >> 6));
                        tok->text[out++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        tok->text[out++] = (char)(0xE0 | (cp >> 12));
                        tok->text[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        tok->text[out++] = (char)(0x80 | (cp & 0x3F));
                    }
                    t->pos++; t->col++; /* skip the 4th hex digit advance */
                    /* Fix: we advanced 3 times in the loop, need 4 total */
                    /* Actually we advanced inside the for loop */
                    /* Let me fix the offset: the for loop handles the advance */
                    t->pos--; t->col--; /* undo last for-loop increment */
                    break;
                }
                default: return 0;
            }
            t->pos++; t->col++;
        } else if ((unsigned char)c < 0x20) {
            return 0; /* control characters not allowed */
        } else {
            tok->text[out++] = c;
            t->pos++; t->col++;
        }
    }
    return 0; /* unterminated string */
}

static bool parse_number_token(JsonTokenizer *t, JsonToken *tok) {
    size_t start = t->pos;
    if (t->pos < t->len && t->input[t->pos] == '-') {
        t->pos++; t->col++;
    }
    if (t->pos >= t->len || !isdigit((unsigned char)t->input[t->pos])) {
        t->pos = start; return 0;
    }
    if (t->input[t->pos] == '0') {
        t->pos++; t->col++;
    } else {
        while (t->pos < t->len && isdigit((unsigned char)t->input[t->pos])) {
            t->pos++; t->col++;
        }
    }
    if (t->pos < t->len && t->input[t->pos] == '.') {
        t->pos++; t->col++;
        if (t->pos >= t->len || !isdigit((unsigned char)t->input[t->pos])) {
            t->pos = start; return 0;
        }
        while (t->pos < t->len && isdigit((unsigned char)t->input[t->pos])) {
            t->pos++; t->col++;
        }
    }
    if (t->pos < t->len && (t->input[t->pos] == 'e' || t->input[t->pos] == 'E')) {
        t->pos++; t->col++;
        if (t->pos < t->len && (t->input[t->pos] == '+' || t->input[t->pos] == '-')) {
            t->pos++; t->col++;
        }
        if (t->pos >= t->len || !isdigit((unsigned char)t->input[t->pos])) {
            t->pos = start; return 0;
        }
        while (t->pos < t->len && isdigit((unsigned char)t->input[t->pos])) {
            t->pos++; t->col++;
        }
    }

    size_t num_len = t->pos - start;
    if (num_len >= JSON_MAX_NUMBER_LEN) return 0;
    memcpy(tok->text, t->input + start, num_len);
    tok->text[num_len] = '\0';
    tok->num_value = strtod(tok->text, NULL);
    tok->type = JSON_TOK_NUMBER;
    return 1;
}

static bool match_keyword(JsonTokenizer *t, const char *kw, JsonTokenType tt,
                           JsonToken *tok) {
    size_t kwlen = strlen(kw);
    if (t->pos + kwlen > t->len) return 0;
    if (strncmp(t->input + t->pos, kw, kwlen) != 0) return 0;
    /* Check that the keyword is followed by a delimiter or EOF */
    if (t->pos + kwlen < t->len) {
        char next = t->input[t->pos + kwlen];
        if (isalnum((unsigned char)next) || next == '_') return 0;
    }
    t->pos += kwlen;
    t->col += (int)kwlen;
    tok->type = tt;
    return 1;
}

bool json_tokenizer_next(JsonTokenizer *t, JsonToken *tok) {
    if (!t || !tok) return 0;
    memset(tok, 0, sizeof(*tok));
    tok->line = t->line;
    tok->col  = t->col;
    skip_whitespace(t);
    if (t->pos >= t->len) { tok->type = JSON_TOK_EOF; return 1; }

    char c = t->input[t->pos];
    switch (c) {
        case '{': tok->type = JSON_TOK_LBRACE;   t->pos++; t->col++; return 1;
        case '}': tok->type = JSON_TOK_RBRACE;   t->pos++; t->col++; return 1;
        case '[': tok->type = JSON_TOK_LBRACKET; t->pos++; t->col++; return 1;
        case ']': tok->type = JSON_TOK_RBRACKET; t->pos++; t->col++; return 1;
        case ',': tok->type = JSON_TOK_COMMA;    t->pos++; t->col++; return 1;
        case ':': tok->type = JSON_TOK_COLON;    t->pos++; t->col++; return 1;
        case '"': return parse_string_token(t, tok);
        case 't': return match_keyword(t, "true",  JSON_TOK_TRUE,  tok);
        case 'f': return match_keyword(t, "false", JSON_TOK_FALSE, tok);
        case 'n': return match_keyword(t, "null",  JSON_TOK_NULL,  tok);
        default:
            if (c == '-' || isdigit((unsigned char)c))
                return parse_number_token(t, tok);
            tok->type = JSON_TOK_ERROR;
            return 0;
    }
}

/* ── Parser ───────────────────────────────────────────────────────────────── */
/*
 * L5: Recursive descent JSON parser.
 * Grammar (RFC 8259):
 *   value    := object | array | string | number | "true" | "false" | "null"
 *   object   := "{" [ members ] "}"
 *   members  := pair ( "," pair )*
 *   pair     := string ":" value
 *   array    := "[" [ elements ] "]"
 *   elements := value ( "," value )*
 */

typedef struct {
    JsonTokenizer  tokenizer;
    JsonToken      current;
    bool           has_current;
} JsonParser;

static bool parser_advance(JsonParser *p) {
    p->has_current = json_tokenizer_next(&p->tokenizer, &p->current);
    return p->has_current;
}

static JsonValue *parse_value(JsonParser *p, int depth);

static JsonValue *parse_object(JsonParser *p, int depth) {
    if (depth > JSON_MAX_DEPTH) return NULL;
    JsonValue *obj = json_build_object();
    if (!obj) return NULL;

    if (p->has_current && p->current.type == JSON_TOK_RBRACE) {
        parser_advance(p);
        return obj;
    }

    while (1) {
        if (!p->has_current || p->current.type != JSON_TOK_STRING) {
            json_value_free(obj); return NULL;
        }

        char *key = strdup(p->current.text);
        if (!key) { json_value_free(obj); return NULL; }

        parser_advance(p);

        if (!p->has_current || p->current.type != JSON_TOK_COLON) {
            free(key); json_value_free(obj); return NULL;
        }
        parser_advance(p);

        JsonValue *val = parse_value(p, depth + 1);
        if (!val) { free(key); json_value_free(obj); return NULL; }

        json_object_set(obj, key, val);
        free(key);

        if (!p->has_current) { json_value_free(obj); return NULL; }
        if (p->current.type == JSON_TOK_RBRACE) {
            parser_advance(p);
            return obj;
        }
        if (p->current.type != JSON_TOK_COMMA) {
            json_value_free(obj); return NULL;
        }
        parser_advance(p);
    }
}

static JsonValue *parse_array(JsonParser *p, int depth) {
    if (depth > JSON_MAX_DEPTH) return NULL;
    JsonValue *arr = json_build_array();
    if (!arr) return NULL;

    if (p->has_current && p->current.type == JSON_TOK_RBRACKET) {
        parser_advance(p);
        return arr;
    }

    while (1) {
        JsonValue *val = parse_value(p, depth + 1);
        if (!val) { json_value_free(arr); return NULL; }
        json_array_push(arr, val);

        if (!p->has_current) { json_value_free(arr); return NULL; }
        if (p->current.type == JSON_TOK_RBRACKET) {
            parser_advance(p);
            return arr;
        }
        if (p->current.type != JSON_TOK_COMMA) {
            json_value_free(arr); return NULL;
        }
        parser_advance(p);
    }
}

static JsonValue *parse_value(JsonParser *p, int depth) {
    if (!p->has_current) return NULL;

    switch (p->current.type) {
        case JSON_TOK_LBRACE:
            parser_advance(p);
            return parse_object(p, depth);
        case JSON_TOK_LBRACKET:
            parser_advance(p);
            return parse_array(p, depth);
        case JSON_TOK_STRING: {
            JsonValue *v = json_build_string(p->current.text);
            parser_advance(p);
            return v;
        }
        case JSON_TOK_NUMBER: {
            JsonValue *v = json_build_number(p->current.num_value);
            parser_advance(p);
            return v;
        }
        case JSON_TOK_TRUE:
            parser_advance(p);
            return json_build_bool(1);
        case JSON_TOK_FALSE:
            parser_advance(p);
            return json_build_bool(0);
        case JSON_TOK_NULL:
            parser_advance(p);
            return json_build_null();
        default:
            return NULL;
    }
}

JsonValue *json_parse(const char *json_str) {
    if (!json_str) return NULL;

    JsonParser p;
    json_tokenizer_init(&p.tokenizer, json_str);
    p.has_current = 0;

    if (!parser_advance(&p)) return NULL;
    if (p.current.type == JSON_TOK_EOF) return NULL;

    JsonValue *root = parse_value(&p, 0);
    return root;
}

/* ── Accessors ────────────────────────────────────────────────────────────── */

JsonType json_value_type(const JsonValue *val) {
    return val ? val->type : JSON_NULL;
}

bool json_value_get_bool(const JsonValue *val, bool def) {
    return (val && val->type == JSON_BOOL) ? val->data.bool_val : def;
}

double json_value_get_number(const JsonValue *val, double def) {
    return (val && val->type == JSON_NUMBER) ? val->data.num_val : def;
}

const char *json_value_get_string(const JsonValue *val, const char *def) {
    return (val && val->type == JSON_STRING) ? val->data.str_val : def;
}

int json_array_size(const JsonValue *val) {
    return (val && val->type == JSON_ARRAY) ? val->data.array.count : 0;
}

JsonValue *json_array_get(const JsonValue *val, int index) {
    if (!val || val->type != JSON_ARRAY) return NULL;
    if (index < 0 || index >= val->data.array.count) return NULL;
    return &val->data.array.items[index];
}

int json_object_size(const JsonValue *val) {
    return (val && val->type == JSON_OBJECT) ? val->data.object.count : 0;
}

JsonValue *json_object_get(const JsonValue *val, const char *key) {
    if (!val || val->type != JSON_OBJECT || !key) return NULL;
    for (int i = 0; i < val->data.object.count; i++) {
        if (strcmp(val->data.object.keys[i], key) == 0)
            return &val->data.object.values[i];
    }
    return NULL;
}

const char **json_object_keys(const JsonValue *val, int *out_count) {
    if (!val || val->type != JSON_OBJECT) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    if (out_count) *out_count = val->data.object.count;
    return (const char **)val->data.object.keys;
}

/* ── Builder ──────────────────────────────────────────────────────────────── */

static JsonValue *json_build(JsonType type) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (!v) return NULL;
    v->type = type;
    return v;
}

JsonValue *json_build_null(void)    { return json_build(JSON_NULL); }

JsonValue *json_build_bool(bool val) {
    JsonValue *v = json_build(JSON_BOOL);
    if (v) v->data.bool_val = val;
    return v;
}

JsonValue *json_build_number(double val) {
    JsonValue *v = json_build(JSON_NUMBER);
    if (v) v->data.num_val = val;
    return v;
}

JsonValue *json_build_string(const char *val) {
    JsonValue *v = json_build(JSON_STRING);
    if (v && val) v->data.str_val = strdup(val);
    return v;
}

JsonValue *json_build_array(void) {
    JsonValue *v = json_build(JSON_ARRAY);
    if (v) {
        v->data.array.capacity = 8;
        v->data.array.items = (JsonValue *)calloc(8, sizeof(JsonValue));
        if (!v->data.array.items) { free(v); return NULL; }
    }
    return v;
}

JsonValue *json_build_object(void) {
    JsonValue *v = json_build(JSON_OBJECT);
    if (v) {
        v->data.object.capacity = 8;
        v->data.object.keys   = (char **)calloc(8, sizeof(char *));
        v->data.object.values = (JsonValue *)calloc(8, sizeof(JsonValue));
        if (!v->data.object.keys || !v->data.object.values) {
            free(v->data.object.keys);
            free(v->data.object.values);
            free(v);
            return NULL;
        }
    }
    return v;
}

bool json_array_push(JsonValue *arr, JsonValue *item) {
    if (!arr || arr->type != JSON_ARRAY || !item) return 0;
    if (arr->data.array.count >= arr->data.array.capacity) {
        int nc = arr->data.array.capacity * 2;
        JsonValue *ni = (JsonValue *)realloc(arr->data.array.items,
                                              (size_t)nc * sizeof(JsonValue));
        if (!ni) return 0;
        arr->data.array.items = ni;
        arr->data.array.capacity = nc;
    }
    arr->data.array.items[arr->data.array.count++] = *item;
    free(item); /* ownership transferred */
    return 1;
}

bool json_object_set(JsonValue *obj, const char *key, JsonValue *val) {
    if (!obj || obj->type != JSON_OBJECT || !key || !val) return 0;

    /* Check for existing key */
    for (int i = 0; i < obj->data.object.count; i++) {
        if (strcmp(obj->data.object.keys[i], key) == 0) {
            json_value_free(&obj->data.object.values[i]);
            obj->data.object.values[i] = *val;
            free(val);
            return 1;
        }
    }

    if (obj->data.object.count >= obj->data.object.capacity) {
        int nc = obj->data.object.capacity * 2;
        char **nk = (char **)realloc(obj->data.object.keys, (size_t)nc * sizeof(char *));
        JsonValue *nv = (JsonValue *)realloc(obj->data.object.values,
                                              (size_t)nc * sizeof(JsonValue));
        if (!nk || !nv) { free(nk); free(nv); return 0; }
        obj->data.object.keys   = nk;
        obj->data.object.values = nv;
        obj->data.object.capacity = nc;
    }

    obj->data.object.keys[obj->data.object.count] = strdup(key);
    obj->data.object.values[obj->data.object.count] = *val;
    obj->data.object.count++;
    free(val);
    return 1;
}

/* ── Free ─────────────────────────────────────────────────────────────────── */

void json_value_free(JsonValue *val) {
    if (!val) return;
    switch (val->type) {
        case JSON_STRING:
            free(val->data.str_val);
            break;
        case JSON_ARRAY:
            for (int i = 0; i < val->data.array.count; i++) {
                json_value_free(&val->data.array.items[i]);
            }
            free(val->data.array.items);
            break;
        case JSON_OBJECT:
            for (int i = 0; i < val->data.object.count; i++) {
                free(val->data.object.keys[i]);
                json_value_free(&val->data.object.values[i]);
            }
            free(val->data.object.keys);
            free(val->data.object.values);
            break;
        default:
            break;
    }
    free(val);
}

/* ── Serialization ────────────────────────────────────────────────────────── */

static int json_serialize_internal(const JsonValue *val, char *buf,
                                    size_t buf_sz, size_t *pos) {
    if (!val || !buf || *pos >= buf_sz) return -1;

    switch (val->type) {
        case JSON_NULL:
            *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "null");
            break;
        case JSON_BOOL:
            *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos,
                                      val->data.bool_val ? "true" : "false");
            break;
        case JSON_NUMBER: {
            double n = val->data.num_val;
            if (floor(n) == n && fabs(n) < 1e15) {
                *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "%.0f", n);
            } else {
                *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "%.17g", n);
            }
            break;
        }
        case JSON_STRING: {
            *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "\"");
            const char *s = val->data.str_val ? val->data.str_val : "";
            for (const char *p = s; *p && *pos < buf_sz - 2; p++) {
                switch (*p) {
                    case '"':  *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "\\\""); break;
                    case '\\': *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "\\\\"); break;
                    case '\b': *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "\\b");  break;
                    case '\f': *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "\\f");  break;
                    case '\n': *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "\\n");  break;
                    case '\r': *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "\\r");  break;
                    case '\t': *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "\\t");  break;
                    default:
                        if ((unsigned char)*p < 0x20) {
                            *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "\\u%04x", (unsigned char)*p);
                        } else {
                            buf[(*pos)++] = *p;
                        }
                }
            }
            *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "\"");
            break;
        }
        case JSON_ARRAY: {
            *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "[");
            for (int i = 0; i < val->data.array.count; i++) {
                if (i > 0) *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, ",");
                json_serialize_internal(&val->data.array.items[i], buf, buf_sz, pos);
            }
            *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "]");
            break;
        }
        case JSON_OBJECT: {
            *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "{");
            for (int i = 0; i < val->data.object.count; i++) {
                if (i > 0) *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, ",");
                *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "\"%s\":",
                                          val->data.object.keys[i]);
                json_serialize_internal(&val->data.object.values[i], buf, buf_sz, pos);
            }
            *pos += (size_t)snprintf(buf + *pos, buf_sz - *pos, "}");
            break;
        }
    }
    return 0;
}

int json_serialize(const JsonValue *val, char *buf, size_t buf_sz) {
    if (!val || !buf || buf_sz == 0) return 0;
    size_t pos = 0;
    json_serialize_internal(val, buf, buf_sz, &pos);
    if (pos < buf_sz) buf[pos] = '\0';
    return (int)pos;
}

void json_pretty_print(const JsonValue *val) {
    char buf[65536];
    int len = json_serialize(val, buf, sizeof(buf));
    if (len > 0) printf("%.*s\n", len, buf);
}

/* ── Path Access (dot notation: "user.address.city" or "items[0].name") ──── */

JsonValue *json_path_get(const JsonValue *root, const char *path) {
    if (!root || !path) return NULL;

    const JsonValue *cur = root;
    char segment[256];
    const char *p = path;

    while (*p && cur) {
        /* Extract next segment */
        size_t i = 0;
        if (*p == '[') {
            p++;
            while (*p && *p != ']' && i < sizeof(segment) - 1) {
                segment[i++] = *p++;
            }
            if (*p == ']') p++;
        } else {
            while (*p && *p != '.' && *p != '[' && i < sizeof(segment) - 1) {
                segment[i++] = *p++;
            }
        }
        if (i == 0) break;
        segment[i] = '\0';

        if (segment[0] >= '0' && segment[0] <= '9') {
            /* Numeric index - array access */
            int idx = atoi(segment);
            cur = json_array_get(cur, idx);
        } else {
            cur = json_object_get(cur, segment);
        }

        if (*p == '.') p++;
    }
    return (JsonValue *)cur;
}
