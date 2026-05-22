#include "serializer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void ser_init(Serializer *s, size_t struct_size, const char *root_name) {
    memset(s, 0, sizeof(Serializer));
    s->struct_size = struct_size;
    if (root_name) {
        strncpy(s->root_name, root_name, SER_MAX_NAME - 1);
        s->root_name[SER_MAX_NAME - 1] = '\0';
    }
}

static void ser_add_field(Serializer *s, const char *name, size_t offset,
                          const char *json_name, SerFieldType type) {
    SerField *f;
    if (s->field_count >= SER_MAX_FIELDS) return;

    f = &s->fields[s->field_count];
    strncpy(f->name, name, SER_MAX_NAME - 1);
    f->name[SER_MAX_NAME - 1] = '\0';
    strncpy(f->json_name, json_name ? json_name : name, SER_MAX_NAME - 1);
    f->json_name[SER_MAX_NAME - 1] = '\0';
    f->type   = type;
    f->offset = offset;
    f->ignore = false;
    f->is_array = false;
    f->is_nested = false;
    f->encoder  = NULL;
    f->decoder  = NULL;
    f->array_len = 0;
    s->field_count++;
}

void ser_add_int(Serializer *s, const char *name, size_t offset,
                 const char *json_name) {
    ser_add_field(s, name, offset, json_name, SER_TYPE_INT);
}

void ser_add_int64(Serializer *s, const char *name, size_t offset,
                   const char *json_name) {
    ser_add_field(s, name, offset, json_name, SER_TYPE_INT64);
}

void ser_add_float(Serializer *s, const char *name, size_t offset,
                   const char *json_name) {
    ser_add_field(s, name, offset, json_name, SER_TYPE_FLOAT);
}

void ser_add_double(Serializer *s, const char *name, size_t offset,
                    const char *json_name) {
    ser_add_field(s, name, offset, json_name, SER_TYPE_DOUBLE);
}

void ser_add_string(Serializer *s, const char *name, size_t offset,
                    const char *json_name) {
    ser_add_field(s, name, offset, json_name, SER_TYPE_STRING);
}

void ser_add_bool(Serializer *s, const char *name, size_t offset,
                  const char *json_name) {
    ser_add_field(s, name, offset, json_name, SER_TYPE_BOOL);
}

void ser_add_object(Serializer *s, const char *name, size_t offset,
                    const char *json_name, const Serializer *nested) {
    ser_add_field(s, name, offset, json_name, SER_TYPE_OBJECT);
    if (s->field_count > 0) {
        s->fields[s->field_count - 1].is_nested = true;
    }
    (void)nested;
}

void ser_add_array(Serializer *s, const char *name, size_t offset,
                   const char *json_name, int array_len, SerFieldType elem_type) {
    ser_add_field(s, name, offset, json_name, SER_TYPE_ARRAY);
    if (s->field_count > 0) {
        s->fields[s->field_count - 1].is_array  = true;
        s->fields[s->field_count - 1].array_len = array_len;
    }
    (void)elem_type;
}

void ser_add_custom(Serializer *s, const char *name, size_t offset,
                    const char *json_name, SerEncoder enc, SerDecoder dec) {
    ser_add_field(s, name, offset, json_name, SER_TYPE_CUSTOM);
    if (s->field_count > 0) {
        s->fields[s->field_count - 1].encoder = enc;
        s->fields[s->field_count - 1].decoder = dec;
    }
}

void ser_ignore(Serializer *s, const char *name) {
    int i;
    for (i = 0; i < s->field_count; i++) {
        if (strcmp(s->fields[i].name, name) == 0) {
            s->fields[i].ignore = true;
            return;
        }
    }
}

void ser_strip_whitespace(char *s) {
    char *p, *q;
    if (!s) return;
    for (p = q = s; *p; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            *q++ = *p;
        }
    }
    *q = '\0';
}

static int ser_escape_json(const char *src, char *dst, int max_len) {
    int pos = 0;
    while (*src && pos < max_len - 2) {
        switch (*src) {
        case '"':  dst[pos++] = '\\'; dst[pos++] = '"';  break;
        case '\\': dst[pos++] = '\\'; dst[pos++] = '\\'; break;
        case '\n': dst[pos++] = '\\'; dst[pos++] = 'n';  break;
        case '\r': dst[pos++] = '\\'; dst[pos++] = 'r';  break;
        case '\t': dst[pos++] = '\\'; dst[pos++] = 't';  break;
        default:   dst[pos++] = *src; break;
        }
        src++;
    }
    dst[pos] = '\0';
    return pos;
}

static int ser_write_value(const SerField *f, const void *base,
                           char *out, int max_len) {
    const char *ptr = (const char *)base + f->offset;
    switch (f->type) {
    case SER_TYPE_INT:
        return snprintf(out, max_len, "%d", *(const int *)ptr);
    case SER_TYPE_INT64:
        return snprintf(out, max_len, "%lld", (long long)*(const int64_t *)ptr);
    case SER_TYPE_FLOAT:
        return snprintf(out, max_len, "%g", (double)*(const float *)ptr);
    case SER_TYPE_DOUBLE:
        return snprintf(out, max_len, "%g", *(const double *)ptr);
    case SER_TYPE_STRING: {
        char escaped[2048];
        ser_escape_json((const char *)ptr, escaped, (int)sizeof(escaped));
        return snprintf(out, max_len, "\"%s\"", escaped);
    }
    case SER_TYPE_BOOL:
        return snprintf(out, max_len, "%s", *(const bool *)ptr ? "true" : "false");
    case SER_TYPE_CUSTOM:
        if (f->encoder) return f->encoder(ptr, out, max_len);
        return 0;
    default:
        return 0;
    }
}

int ser_to_json(const Serializer *s, const void *obj, char *out, int max_len) {
    int i;
    int pos = 0;
    int written;
    bool first = true;

    if (!obj || !out) return -1;

    written = snprintf(out + pos, max_len - pos, "{");
    if (written < 0) return -1;
    pos += written;

    for (i = 0; i < s->field_count; i++) {
        const SerField *f = &s->fields[i];
        char val_buf[1024];

        if (f->ignore) continue;

        if (!first) {
            written = snprintf(out + pos, max_len - pos, ",");
            if (written < 0) return -1;
            pos += written;
        }

        ser_write_value(f, obj, val_buf, (int)sizeof(val_buf));

        written = snprintf(out + pos, max_len - pos,
                           "\"%s\":%s", f->json_name, val_buf);
        if (written < 0 || written >= max_len - pos) return -1;
        pos += written;

        first = false;
    }

    written = snprintf(out + pos, max_len - pos, "}");
    if (written < 0 || written >= max_len - pos) return -1;
    pos += written;

    return pos;
}

static const char *ser_find_json_value(const char *json, const char *key,
                                       int *value_start, int *value_end) {
    char search[128];
    int key_len;
    const char *p;

    key_len = snprintf(search, sizeof(search), "\"%s\"", key);
    p = strstr(json, search);
    if (!p) return NULL;

    p += key_len;
    while (*p == ' ' || *p == ':') p++;

    *value_start = (int)(p - json);

    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            if (*p == '\\' && *(p + 1)) p++;
            p++;
        }
        if (*p == '"') p++;
    } else if (*p == '{' || *p == '[') {
        char open = *p;
        char close = (open == '{') ? '}' : ']';
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == open) depth++;
            else if (*p == close) depth--;
            p++;
        }
    } else {
        while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    }

    *value_end = (int)(p - json);
    return p;
}

int ser_from_json(const Serializer *s, void *obj, const char *json) {
    int i;

    if (!s || !obj || !json) return -1;

    for (i = 0; i < s->field_count; i++) {
        const SerField *f = &s->fields[i];
        char *target;
        int vs, ve;
        const char *found;

        if (f->ignore) continue;

        found = ser_find_json_value(json, f->json_name, &vs, &ve);
        if (!found) continue;

        target = (char *)obj + f->offset;

        switch (f->type) {
        case SER_TYPE_INT: {
            char copy[32];
            int len = ve - vs;
            if (len >= (int)sizeof(copy)) len = (int)sizeof(copy) - 1;
            memcpy(copy, json + vs, len);
            copy[len] = '\0';
            *(int *)target = atoi(copy);
            break;
        }
        case SER_TYPE_INT64:
            *(int64_t *)target = 0;
            break;
        case SER_TYPE_FLOAT:
            *(float *)target = 0.0f;
            break;
        case SER_TYPE_DOUBLE:
            *(double *)target = 0.0;
            break;
        case SER_TYPE_STRING: {
            const char *start = json + vs;
            if (*start == '"') start++;
            int slen = 0;
            while (start[slen] && start[slen] != '"' && slen < 254) slen++;
            memcpy(target, start, slen);
            target[slen] = '\0';
            break;
        }
        case SER_TYPE_BOOL: {
            char copy[8];
            int len = ve - vs;
            if (len >= (int)sizeof(copy)) len = (int)sizeof(copy) - 1;
            memcpy(copy, json + vs, len);
            copy[len] = '\0';
            *(bool *)target = (strcmp(copy, "true") == 0);
            break;
        }
        case SER_TYPE_CUSTOM:
            if (f->decoder) f->decoder(target, json);
            break;
        default:
            break;
        }
    }

    return 0;
}

int ser_json_get_string(const char *json, const char *key, char *out, int max) {
    int vs, ve;
    const char *found = ser_find_json_value(json, key, &vs, &ve);
    if (!found) return -1;

    const char *start = json + vs;
    int len = ve - vs;

    if (*start == '"') { start++; len -= 2; }

    if (len >= max) len = max - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return len;
}

int ser_json_get_int(const char *json, const char *key, int default_val) {
    char buf[32];
    if (ser_json_get_string(json, key, buf, sizeof(buf)) < 0) return default_val;
    return atoi(buf);
}

int ser_json_get_bool(const char *json, const char *key) {
    char buf[8];
    if (ser_json_get_string(json, key, buf, sizeof(buf)) < 0) return 0;
    return (strcmp(buf, "true") == 0);
}
