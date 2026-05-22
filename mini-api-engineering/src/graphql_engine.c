#include "graphql_engine.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

const char* gql_scalar_name(gql_scalar_t s) {
    static const char* names[] = {
        "String", "Int", "Float", "Boolean", "ID",
        "Object", "List", "NonNull", "Enum", "InputObject", "Scalar"
    };
    if (s > GQL_TYPE_SCALAR) return "Unknown";
    return names[s];
}

const char* gql_operation_name(gql_operation_t op) {
    static const char* names[] = { "query", "mutation", "subscription" };
    if (op > GQL_OP_SUBSCRIPTION) return "unknown";
    return names[op];
}

void gql_engine_init(gql_engine_t* e) {
    if (!e) return;
    memset(e, 0, sizeof(*e));
    gql_schema_init(&e->schema);
}

void gql_schema_init(gql_schema_t* s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
}

void gql_schema_add_type(gql_schema_t* s, const char* name) {
    if (!s || s->type_count >= GQL_MAX_TYPES || !name) return;
    gql_type_t* t = &s->types[s->type_count++];
    memset(t, 0, sizeof(*t));
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->is_input = false;
    t->is_enum = false;
}

void gql_schema_add_field(gql_schema_t* s, const char* type_name, const char* field_name,
                          gql_scalar_t scalar, const char* type, bool is_list, bool is_non_null) {
    if (!s || !type_name || !field_name) return;
    gql_type_t* t = (gql_type_t*)gql_find_type_schema(s, type_name);
    if (!t || t->field_count >= GQL_MAX_FIELDS) return;
    gql_field_t* f = &t->fields[t->field_count++];
    memset(f, 0, sizeof(*f));
    strncpy(f->name, field_name, sizeof(f->name) - 1);
    f->scalar = scalar;
    if (type) strncpy(f->type_name, type, sizeof(f->type_name) - 1);
    f->is_list = is_list;
    f->is_non_null = is_non_null;
}

void gql_schema_add_field_arg(gql_schema_t* s, const char* type_name, const char* field_name,
                              const char* arg_name, const char* arg_type, bool required) {
    if (!s || !type_name || !field_name || !arg_name) return;
    gql_type_t* t = (gql_type_t*)gql_find_type_schema(s, type_name);
    if (!t) return;
    gql_field_t* f = NULL;
    for (int32_t i = 0; i < t->field_count; i++) {
        if (strcmp(t->fields[i].name, field_name) == 0) {
            f = &t->fields[i];
            break;
        }
    }
    if (!f || f->arg_count >= GQL_MAX_ARGS) return;
    gql_argument_t* a = &f->args[f->arg_count++];
    memset(a, 0, sizeof(*a));
    strncpy(a->name, arg_name, sizeof(a->name) - 1);
    if (arg_type) strncpy(a->type, arg_type, sizeof(a->type) - 1);
    a->is_required = required;
}

void gql_schema_add_enum_value(gql_schema_t* s, const char* type_name, const char* value) {
    if (!s || !type_name || !value) return;
    gql_type_t* t = (gql_type_t*)gql_find_type_schema(s, type_name);
    if (!t || t->enum_count >= GQL_MAX_FIELDS) return;
    t->is_enum = true;
    strncpy(t->enum_values[t->enum_count++], value, GQL_MAX_NAME_LEN - 1);
}

void gql_schema_set_query_type(gql_schema_t* s, const char* name) {
    if (!s || !name) return;
    strncpy(s->query_type, name, sizeof(s->query_type) - 1);
}

void gql_schema_set_mutation_type(gql_schema_t* s, const char* name) {
    if (!s || !name) return;
    strncpy(s->mutation_type, name, sizeof(s->mutation_type) - 1);
}

void gql_schema_add_resolver(gql_schema_t* s, const char* type_name, const char* field_name,
                             gql_resolver_fn fn, void* context) {
    if (!s || s->resolver_count >= GQL_MAX_RESOLVERS) return;
    gql_resolver_t* r = &s->resolvers[s->resolver_count++];
    strncpy(r->type_name, type_name, sizeof(r->type_name) - 1);
    strncpy(r->field_name, field_name, sizeof(r->field_name) - 1);
    r->resolver = fn;
    r->context = context;
}

static const gql_type_t* gql_find_type_schema(gql_schema_t* s, const char* name) {
    if (!s || !name) return NULL;
    for (int32_t i = 0; i < s->type_count; i++) {
        if (strcmp(s->types[i].name, name) == 0) return &s->types[i];
    }
    return NULL;
}

const gql_type_t* gql_find_type(gql_engine_t* e, const char* name) {
    if (!e || !name) return NULL;
    return gql_find_type_schema(&e->schema, name);
}

static void gql_skip_whitespace(const char** p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static char* gql_parse_name(const char** p, char* buf, size_t len) {
    gql_skip_whitespace(p);
    const char* start = *p;
    if (!isalpha((unsigned char)**p) && **p != '_') return NULL;
    while (**p && (isalnum((unsigned char)**p) || **p == '_')) (*p)++;
    size_t n = (size_t)(*p - start);
    if (n >= len) n = len - 1;
    memcpy(buf, start, n);
    buf[n] = '\0';
    gql_skip_whitespace(p);
    return buf;
}

static bool gql_parse_selection_set(gql_engine_t* e, const char** p) {
    if (**p != '{') return false;
    (*p)++;
    while (**p) {
        gql_skip_whitespace(p);
        if (**p == '}') { (*p)++; break; }
        if (e->parsed.selection_count >= GQL_MAX_SELECTION) return false;
        char name[GQL_MAX_NAME_LEN];
        if (!gql_parse_name(p, name, sizeof(name))) return false;
        gql_selection_t* sel = &e->parsed.selections[e->parsed.selection_count++];
        strncpy(sel->field_name, name, sizeof(sel->field_name) - 1);
        sel->is_leaf = true;
        gql_skip_whitespace(p);
        if (**p == '(') {
            (*p)++;
            while (**p && **p != ')') {
                gql_skip_whitespace(p);
                char arg_name[GQL_MAX_NAME_LEN];
                if (!gql_parse_name(p, arg_name, sizeof(arg_name))) return false;
                gql_skip_whitespace(p);
                if (**p != ':') return false;
                (*p)++;
                gql_skip_whitespace(p);
                char arg_val[256];
                if (**p == '"') {
                    (*p)++;
                    const char* vs = *p;
                    while (**p && **p != '"') (*p)++;
                    size_t vn = (size_t)(*p - vs);
                    if (vn >= sizeof(arg_val)) vn = sizeof(arg_val) - 1;
                    memcpy(arg_val, vs, vn);
                    arg_val[vn] = '\0';
                    if (**p == '"') (*p)++;
                } else {
                    if (!gql_parse_name(p, arg_val, sizeof(arg_val))) return false;
                }
                if (e->parsed.arg_count < GQL_MAX_ARGS) {
                    gql_argument_t* a = &e->parsed.args[e->parsed.arg_count++];
                    strncpy(a->name, arg_name, sizeof(a->name) - 1);
                    strncpy(a->default_value, arg_val, sizeof(a->default_value) - 1);
                }
                gql_skip_whitespace(p);
                if (**p == ',') (*p)++;
            }
            if (**p == ')') (*p)++;
        }
        if (**p == '{') {
            sel->is_leaf = false;
            (*p)++;
            int32_t depth = 1;
            while (**p && depth > 0) {
                if (**p == '{') depth++;
                else if (**p == '}') depth--;
                (*p)++;
            }
        }
        if (**p == ',') (*p)++;
    }
    return true;
}

bool gql_parse_query(gql_engine_t* e, const char* query) {
    if (!e || !query) return false;
    e->parsed.selection_count = 0;
    e->parsed.arg_count = 0;
    e->error_count = 0;
    e->parsed.operation = GQL_OP_QUERY;
    e->parsed.operation_name[0] = '\0';
    e->parsed.target_field[0] = '\0';

    const char* p = query;
    gql_skip_whitespace(&p);
    if (strncmp(p, "query", 5) == 0 || strncmp(p, "mutation", 8) == 0 ||
        strncmp(p, "subscription", 12) == 0) {
        if (strncmp(p, "mutation", 8) == 0) { e->parsed.operation = GQL_OP_MUTATION; p += 8; }
        else if (strncmp(p, "subscription", 12) == 0) { e->parsed.operation = GQL_OP_SUBSCRIPTION; p += 12; }
        else { e->parsed.operation = GQL_OP_QUERY; p += 5; }
        gql_skip_whitespace(&p);
        if (isalpha((unsigned char)*p)) {
            gql_parse_name(&p, e->parsed.operation_name, sizeof(e->parsed.operation_name));
        }
        gql_skip_whitespace(&p);
    }

    if (**p == '{') {
        return gql_parse_selection_set(e, &p);
    }

    gql_parse_name(&p, e->parsed.target_field, sizeof(e->parsed.target_field));
    if (strlen(e->parsed.target_field) == 0) return false;

    gql_skip_whitespace(&p);
    if (**p == '(') {
        (*p)++;
        while (**p && **p != ')') {
            gql_skip_whitespace(&p);
            char arg_name[GQL_MAX_NAME_LEN];
            if (!gql_parse_name(&p, arg_name, sizeof(arg_name))) return false;
            gql_skip_whitespace(&p);
            if (**p != ':') return false;
            (*p)++;
            gql_skip_whitespace(&p);
            char arg_val[256];
            if (**p == '"') {
                (*p)++;
                const char* vs = *p;
                while (**p && **p != '"') (*p)++;
                size_t vn = (size_t)(*p - vs);
                if (vn >= sizeof(arg_val)) vn = sizeof(arg_val) - 1;
                memcpy(arg_val, vs, vn); arg_val[vn] = '\0';
                if (**p == '"') (*p)++;
            } else {
                if (!gql_parse_name(&p, arg_val, sizeof(arg_val))) return false;
            }
            if (e->parsed.arg_count < GQL_MAX_ARGS) {
                gql_argument_t* a = &e->parsed.args[e->parsed.arg_count++];
                strncpy(a->name, arg_name, sizeof(a->name) - 1);
                strncpy(a->default_value, arg_val, sizeof(a->default_value) - 1);
            }
            gql_skip_whitespace(&p);
            if (**p == ',') (*p)++;
        }
        if (**p == ')') (*p)++;
    }

    gql_skip_whitespace(&p);
    if (**p == '{') return gql_parse_selection_set(e, &p);
    return e->parsed.selection_count > 0;
}

bool gql_parse_schema_sdl(gql_engine_t* e, const char* sdl) {
    if (!e || !sdl) return false;
    const char* p = sdl;
    char current_type[GQL_MAX_NAME_LEN] = {0};

    while (*p) {
        gql_skip_whitespace(&p);
        if (!*p) break;
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }

        if (strncmp(p, "type", 4) == 0 && !isalnum((unsigned char)p[4])) {
            p += 4;
            gql_skip_whitespace(&p);
            char name[GQL_MAX_NAME_LEN];
            if (!gql_parse_name(&p, name, sizeof(name))) break;
            gql_schema_add_type(&e->schema, name);
            strncpy(current_type, name, sizeof(current_type) - 1);
            gql_skip_whitespace(&p);
            if (*p == '{') {
                p++;
                while (*p && *p != '}') {
                    gql_skip_whitespace(&p);
                    if (*p == '}' || *p == '\0') break;
                    char field[GQL_MAX_NAME_LEN];
                    if (!gql_parse_name(&p, field, sizeof(field))) break;
                    gql_skip_whitespace(&p);
                    if (*p == '(') {
                        p++;
                        while (*p && *p != ')') {
                            gql_skip_whitespace(&p);
                            char a_n[GQL_MAX_NAME_LEN];
                            if (!gql_parse_name(&p, a_n, sizeof(a_n))) break;
                            gql_skip_whitespace(&p);
                            if (*p != ':') break;
                            p++;
                            gql_skip_whitespace(&p);
                            char a_t[GQL_MAX_NAME_LEN];
                            if (!gql_parse_name(&p, a_t, sizeof(a_t))) break;
                            bool req = false;
                            if (**p == '!') { req = true; (*p)++; }
                            gql_schema_add_field_arg(&e->schema, name, field, a_n, a_t, req);
                            if (*p == ',') p++;
                        }
                        if (*p == ')') p++;
                    }
                    gql_skip_whitespace(&p);
                    if (*p != ':') continue;
                    p++;
                    gql_skip_whitespace(&p);
                    bool is_list = false, is_non_null = false;
                    if (*p == '[') { is_list = true; p++; }
                    char ftype[GQL_MAX_NAME_LEN];
                    if (!gql_parse_name(&p, ftype, sizeof(ftype))) break;
                    if (is_list) { gql_skip_whitespace(&p); if (*p == ']') p++; }
                    if (*p == '!') { is_non_null = true; p++; }
                    gql_scalar_t sc = GQL_TYPE_STRING;
                    if (strcmp(ftype, "Int") == 0) sc = GQL_TYPE_INT;
                    else if (strcmp(ftype, "Float") == 0) sc = GQL_TYPE_FLOAT;
                    else if (strcmp(ftype, "Boolean") == 0) sc = GQL_TYPE_BOOLEAN;
                    else if (strcmp(ftype, "ID") == 0) sc = GQL_TYPE_ID;
                    else if (strcmp(ftype, "String") == 0) sc = GQL_TYPE_STRING;
                    gql_schema_add_field(&e->schema, name, field, sc, ftype, is_list, is_non_null);
                }
                if (*p == '}') p++;
            }
        } else if (strncmp(p, "enum", 4) == 0 && !isalnum((unsigned char)p[4])) {
            p += 4;
            gql_skip_whitespace(&p);
            char name[GQL_MAX_NAME_LEN];
            if (!gql_parse_name(&p, name, sizeof(name))) break;
            gql_type_t* t = (gql_type_t*)gql_find_type_schema(&e->schema, name);
            if (!t) { gql_schema_add_type(&e->schema, name); t = (gql_type_t*)gql_find_type_schema(&e->schema, name); }
            if (t) t->is_enum = true;
            gql_skip_whitespace(&p);
            if (*p == '{') {
                p++;
                while (*p && *p != '}') {
                    gql_skip_whitespace(&p);
                    if (*p == '}' || *p == '\0') break;
                    char ev[GQL_MAX_NAME_LEN];
                    if (!gql_parse_name(&p, ev, sizeof(ev))) break;
                    if (t && t->enum_count < GQL_MAX_FIELDS) {
                        strncpy(t->enum_values[t->enum_count++], ev, GQL_MAX_NAME_LEN - 1);
                    }
                }
                if (*p == '}') p++;
            }
        } else {
            p++;
        }
    }
    return true;
}

bool gql_validate_query(gql_engine_t* e) {
    if (!e) return false;
    e->error_count = 0;
    const gql_type_t* qtype = gql_find_type(e, e->schema.query_type);
    if (!qtype && strlen(e->schema.query_type) > 0) {
        strncpy(e->errors[e->error_count++], "Query type not found in schema", 511);
        return false;
    }
    if (e->parsed.selection_count == 0 && strlen(e->parsed.target_field) == 0) {
        strncpy(e->errors[e->error_count++], "No fields selected in query", 511);
        return false;
    }
    return e->error_count == 0;
}

void* gql_execute(gql_engine_t* e, void* parent_value) {
    if (!e) return NULL;
    for (int32_t i = 0; i < e->parsed.selection_count; i++) {
        for (int32_t j = 0; j < e->schema.resolver_count; j++) {
            gql_resolver_t* r = &e->schema.resolvers[j];
            if (strcmp(r->field_name, e->parsed.selections[i].field_name) == 0) {
                if (r->resolver) {
                    r->resolver(parent_value, NULL, r->context);
                }
            }
        }
    }
    return NULL;
}

char* gql_introspect_schema(gql_engine_t* e, char* buf, size_t len) {
    if (!e || !buf) return NULL;
    int off = snprintf(buf, len, "{\"__schema\":{\"queryType\":{\"name\":\"%s\"},\"types\":[",
                       e->schema.query_type);
    for (int32_t i = 0; i < e->schema.type_count; i++) {
        gql_type_t* t = &e->schema.types[i];
        if (off >= (int)len) break;
        off += snprintf(buf + off, len - off, "%s{\"name\":\"%s\",\"kind\":\"%s\",\"fields\":[",
                        i > 0 ? "," : "", t->name, t->is_enum ? "ENUM" : "OBJECT");
        for (int32_t j = 0; j < t->field_count; j++) {
            off += snprintf(buf + off, len - off, "%s{\"name\":\"%s\",\"type\":{\"name\":\"%s\"}}",
                            j > 0 ? "," : "", t->fields[j].name, t->fields[j].type_name);
        }
        off += snprintf(buf + off, len - off, "]}");
    }
    off += snprintf(buf + off, len - off, "]}}");
    return buf;
}

char* gql_introspect_type(gql_engine_t* e, const char* type_name, char* buf, size_t len) {
    if (!e || !type_name || !buf) return NULL;
    const gql_type_t* t = gql_find_type(e, type_name);
    if (!t) {
        snprintf(buf, len, "{\"error\":\"Type '%s' not found\"}", type_name);
        return buf;
    }
    int off = snprintf(buf, len, "{\"name\":\"%s\",\"fields\":[", t->name);
    for (int32_t j = 0; j < t->field_count; j++) {
        off += snprintf(buf + off, len - off, "%s{\"name\":\"%s\",\"type\":\"%s\"}",
                        j > 0 ? "," : "", t->fields[j].name, t->fields[j].type_name);
    }
    off += snprintf(buf + off, len - off, "]}");
    return buf;
}

int32_t gql_error_count(gql_engine_t* e) {
    return e ? e->error_count : 0;
}

const char* gql_error_message(gql_engine_t* e, int32_t index) {
    if (!e || index < 0 || index >= e->error_count) return NULL;
    return e->errors[index];
}
