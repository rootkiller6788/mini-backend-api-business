#include "openapi_builder.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

const char* oa_type_string(oa_type_t t) {
    static const char* names[] = { "string", "integer", "number", "boolean", "array", "object" };
    if (t > OA_OBJECT) return "unknown";
    return names[t];
}

const char* oa_method_string(oa_http_method_t m) {
    static const char* names[] = { "get", "post", "put", "delete", "patch", "head", "options", "trace" };
    if (m > OA_TRACE) return "unknown";
    return names[m];
}

const char* oa_param_in_string(oa_param_in_t in) {
    static const char* names[] = { "query", "header", "path", "cookie", "body" };
    if (in > OA_IN_BODY) return "unknown";
    return names[in];
}

const char* oa_security_type_string(oa_security_type_t t) {
    static const char* names[] = { "apiKey", "http", "oauth2", "openIdConnect", "mutualTLS" };
    if (t > OA_SEC_MUTUAL) return "unknown";
    return names[t];
}

void oa_spec_init(oa_spec_t* spec, const char* title, const char* version, const char* description) {
    if (!spec) return;
    memset(spec, 0, sizeof(*spec));
    if (title) strncpy(spec->title, title, sizeof(spec->title) - 1);
    if (version) strncpy(spec->version, version, sizeof(spec->version) - 1);
    if (description) strncpy(spec->description, description, sizeof(spec->description) - 1);
}

void oa_spec_add_server(oa_spec_t* spec, const char* url, const char* description) {
    if (!spec || spec->server_count >= OA_MAX_SERVERS) return;
    oa_server_t* s = &spec->servers[spec->server_count++];
    memset(s, 0, sizeof(*s));
    if (url) strncpy(s->url, url, sizeof(s->url) - 1);
    if (description) strncpy(s->description, description, sizeof(s->description) - 1);
}

void oa_spec_add_tag(oa_spec_t* spec, const char* name, const char* description) {
    if (!spec || spec->tag_count >= OA_MAX_TAGS) return;
    strncpy(spec->tags[spec->tag_count], name, sizeof(spec->tags[0]) - 1);
    strncpy(spec->tag_descs[spec->tag_count], description, sizeof(spec->tag_descs[0]) - 1);
    spec->tag_count++;
}

oa_schema_t* oa_spec_add_schema(oa_spec_t* spec, const char* name, oa_type_t type, const char* description) {
    if (!spec || spec->schema_count >= OA_MAX_SCHEMAS) return NULL;
    oa_schema_t* s = &spec->schemas[spec->schema_count++];
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->type = type;
    if (description) strncpy(s->description, description, sizeof(s->description) - 1);
    return s;
}

oa_property_t* oa_schema_add_prop(oa_schema_t* s, const char* name, oa_type_t type, const char* format,
                                   bool required, const char* description) {
    if (!s || s->prop_count >= OA_MAX_PROPERTIES) return NULL;
    oa_property_t* p = &s->properties[s->prop_count++];
    memset(p, 0, sizeof(*p));
    strncpy(p->name, name, sizeof(p->name) - 1);
    p->type = type;
    if (format) strncpy(p->format, format, sizeof(p->format) - 1);
    p->is_required = required;
    if (description) strncpy(p->description, description, sizeof(p->description) - 1);
    return p;
}

void oa_schema_add_enum_value(oa_schema_t* s, const char* value) {
    if (!s || s->enum_count >= OA_MAX_ENUM_VALUES) return;
    strncpy(s->enum_values[s->enum_count++], value, OA_NAME_LEN - 1);
}

void oa_schema_set_array(oa_schema_t* s, oa_type_t item_type, const char* item_ref) {
    if (!s) return;
    s->is_array = true;
    s->array_item_type = item_type;
    if (item_ref) strncpy(s->array_item_ref, item_ref, sizeof(s->array_item_ref) - 1);
}

void oa_schema_set_ref(oa_schema_t* s, const char* ref) {
    if (!s || !ref) return;
    strncpy(s->ref, ref, sizeof(s->ref) - 1);
}

oa_operation_t* oa_spec_add_operation(oa_spec_t* spec, const char* path, oa_http_method_t method,
                                      const char* operation_id, const char* summary) {
    if (!spec || spec->op_count >= OA_MAX_OPERATIONS) return NULL;
    oa_operation_t* op = &spec->operations[spec->op_count++];
    memset(op, 0, sizeof(*op));
    strncpy(op->path, path, sizeof(op->path) - 1);
    op->method = method;
    strncpy(op->operation_id, operation_id, sizeof(op->operation_id) - 1);
    if (summary) strncpy(op->summary, summary, sizeof(op->summary) - 1);
    return op;
}

void oa_operation_add_tag(oa_operation_t* op, const char* tag) {
    if (!op || op->tag_count >= OA_MAX_TAGS || !tag) return;
    strncpy(op->tags[op->tag_count++], tag, OA_NAME_LEN - 1);
}

void oa_operation_add_param(oa_operation_t* op, const char* name, oa_param_in_t in,
                             oa_type_t type, bool required, const char* description) {
    (void)op;
    (void)name;
    (void)in;
    (void)type;
    (void)required;
    (void)description;
}

void oa_operation_add_response(oa_operation_t* op, const char* status_code, const char* description,
                                const char* content_type, const char* schema_ref) {
    if (!op || op->response_count >= OA_MAX_RESPONSES) return;
    oa_response_t* r = &op->responses[op->response_count++];
    memset(r, 0, sizeof(*r));
    strncpy(r->status_code, status_code, sizeof(r->status_code) - 1);
    if (description) strncpy(r->description, description, sizeof(r->description) - 1);
    if (content_type) strncpy(r->content_types[0], content_type, sizeof(r->content_types[0]) - 1);
    r->content_count = content_type ? 1 : 0;
    if (schema_ref && schema_ref[0] == '#') {
        strncpy(r->schema_ref, schema_ref, sizeof(r->schema_ref) - 1);
    } else {
        strncpy(r->schema_ref, schema_ref ? schema_ref : "", sizeof(r->schema_ref) - 1);
    }
}

void oa_operation_add_body(oa_operation_t* op, const char* schema_ref, oa_type_t type) {
    if (!op) return;
    op->has_request_body = true;
    if (schema_ref) strncpy(op->request_body_ref, schema_ref, sizeof(op->request_body_ref) - 1);
    op->request_body_schema.type = type;
}

void oa_operation_set_deprecated(oa_operation_t* op, bool deprecated) {
    if (op) op->deprecated = deprecated;
}

oa_security_scheme_t* oa_spec_add_security(oa_spec_t* spec, const char* name, oa_security_type_t type,
                                            const char* description) {
    if (!spec || spec->sec_count >= OA_MAX_SECURITY_SCHEMES) return NULL;
    oa_security_scheme_t* s = &spec->sec_schemes[spec->sec_count++];
    memset(s, 0, sizeof(*s));
    strncpy(s->scheme_name, name, sizeof(s->scheme_name) - 1);
    s->type = type;
    if (description) strncpy(s->description, description, sizeof(s->description) - 1);
    return s;
}

void oa_security_set_api_key(oa_security_scheme_t* s, const char* name, const char* in) {
    if (!s) return;
    strncpy(s->name, name, sizeof(s->name) - 1);
    strncpy(s->in, in, sizeof(s->in) - 1);
}

void oa_security_set_http_bearer(oa_security_scheme_t* s, const char* scheme, const char* bearer) {
    if (!s) return;
    strncpy(s->scheme, scheme, sizeof(s->scheme) - 1);
    if (bearer) strncpy(s->bearer_format, bearer, sizeof(s->bearer_format) - 1);
}

static void oa_json_escape(const char* src, char* dst, size_t dlen) {
    size_t j = 0;
    for (; *src && j < dlen - 1; src++) {
        if (*src == '"' || *src == '\\') {
            if (j + 1 < dlen - 1) { dst[j++] = '\\'; dst[j++] = *src; }
        } else if (*src == '\n') {
            if (j + 1 < dlen - 1) { dst[j++] = '\\'; dst[j++] = 'n'; }
        } else if (*src == '\r') {
            if (j + 1 < dlen - 1) { dst[j++] = '\\'; dst[j++] = 'r'; }
        } else if (*src == '\t') {
            if (j + 1 < dlen - 1) { dst[j++] = '\\'; dst[j++] = 't'; }
        } else {
            dst[j++] = *src;
        }
    }
    dst[j] = '\0';
}

static int oa_append_json_schema(char* buf, int off, size_t len, oa_schema_t* s) {
    if (s->ref[0] != '\0') {
        off += snprintf(buf + off, len - off, "\"$ref\":\"%s\"", s->ref);
        return off;
    }
    off += snprintf(buf + off, len - off, "\"type\":\"%s\"", oa_type_string(s->type));
    if (s->is_array) {
        if (s->array_item_ref[0]) {
            off += snprintf(buf + off, len - off, ",\"items\":{\"$ref\":\"%s\"}", s->array_item_ref);
        } else {
            off += snprintf(buf + off, len - off, ",\"items\":{\"type\":\"%s\"}",
                            oa_type_string(s->array_item_type));
        }
    }
    if (s->enum_count > 0) {
        off += snprintf(buf + off, len - off, ",\"enum\":[");
        for (int32_t i = 0; i < s->enum_count; i++) {
            off += snprintf(buf + off, len - off, "%s\"%s\"", i > 0 ? "," : "", s->enum_values[i]);
        }
        off += snprintf(buf + off, len - off, "]");
    }
    if (s->prop_count > 0) {
        off += snprintf(buf + off, len - off, ",\"properties\":{");
        for (int32_t i = 0; i < s->prop_count; i++) {
            oa_property_t* p = &s->properties[i];
            off += snprintf(buf + off, len - off, "%s\"%s\":{\"type\":\"%s\"",
                            i > 0 ? "," : "", p->name, oa_type_string(p->type));
            if (p->format[0]) off += snprintf(buf + off, len - off, ",\"format\":\"%s\"", p->format);
            if (p->description[0]) {
                char esc[OA_DESC_LEN * 2];
                oa_json_escape(p->description, esc, sizeof(esc));
                off += snprintf(buf + off, len - off, ",\"description\":\"%s\"", esc);
            }
            if (p->example[0]) {
                char esc[OA_EXAMPLE_LEN * 2];
                oa_json_escape(p->example, esc, sizeof(esc));
                off += snprintf(buf + off, len - off, ",\"example\":\"%s\"", esc);
            }
            if (p->min_length > 0) off += snprintf(buf + off, len - off, ",\"minLength\":%d", p->min_length);
            if (p->max_length > 0) off += snprintf(buf + off, len - off, ",\"maxLength\":%d", p->max_length);
            if (p->nullable) off += snprintf(buf + off, len - off, ",\"nullable\":true");
            if (p->deprecated) off += snprintf(buf + off, len - off, ",\"deprecated\":true");
            off += snprintf(buf + off, len - off, "}");
        }
        off += snprintf(buf + off, len - off, "}");
        off += snprintf(buf + off, len - off, ",\"required\":[");
        bool first = true;
        for (int32_t i = 0; i < s->prop_count; i++) {
            if (s->properties[i].is_required) {
                off += snprintf(buf + off, len - off, "%s\"%s\"", first ? "" : ",", s->properties[i].name);
                first = false;
            }
        }
        off += snprintf(buf + off, len - off, "]");
    }
    if (s->description[0]) {
        char esc[OA_DESC_LEN * 2];
        oa_json_escape(s->description, esc, sizeof(esc));
        off += snprintf(buf + off, len - off, ",\"description\":\"%s\"", esc);
    }
    return off;
}

char* oa_spec_to_json(oa_spec_t* spec, char* buf, size_t len) {
    if (!spec || !buf) return NULL;
    int off = 0;
    off += snprintf(buf + off, len - off, "{\n  \"openapi\":\"3.0.3\",\n");
    off += snprintf(buf + off, len - off, "  \"info\":{\n");
    off += snprintf(buf + off, len - off, "    \"title\":\"%s\",\n", spec->title);
    off += snprintf(buf + off, len - off, "    \"version\":\"%s\"", spec->version);
    if (spec->description[0]) {
        char esc[OA_DESC_LEN * 2];
        oa_json_escape(spec->description, esc, sizeof(esc));
        off += snprintf(buf + off, len - off, ",\n    \"description\":\"%s\"", esc);
    }
    off += snprintf(buf + off, len - off, "\n  }");
    if (spec->server_count > 0) {
        off += snprintf(buf + off, len - off, ",\n  \"servers\":[");
        for (int32_t i = 0; i < spec->server_count; i++) {
            off += snprintf(buf + off, len - off, "%s{\"url\":\"%s\"", i > 0 ? "," : "", spec->servers[i].url);
            if (spec->servers[i].description[0]) {
                char esc[OA_DESC_LEN * 2];
                oa_json_escape(spec->servers[i].description, esc, sizeof(esc));
                off += snprintf(buf + off, len - off, ",\"description\":\"%s\"", esc);
            }
            off += snprintf(buf + off, len - off, "}");
        }
        off += snprintf(buf + off, len - off, "]");
    }
    if (spec->op_count > 0) {
        off += snprintf(buf + off, len - off, ",\n  \"paths\":{");
        for (int32_t i = 0; i < spec->op_count; i++) {
            oa_operation_t* op = &spec->operations[i];
            off += snprintf(buf + off, len - off, "%s\n    \"%s\":{\"%s\":{",
                            i > 0 ? "," : "", op->path, oa_method_string(op->method));
            off += snprintf(buf + off, len - off, "\"operationId\":\"%s\"", op->operation_id);
            if (op->summary[0]) {
                char esc[OA_DESC_LEN * 2];
                oa_json_escape(op->summary, esc, sizeof(esc));
                off += snprintf(buf + off, len - off, ",\"summary\":\"%s\"", esc);
            }
            if (op->deprecated) off += snprintf(buf + off, len - off, ",\"deprecated\":true");
            if (op->response_count > 0) {
                off += snprintf(buf + off, len - off, ",\"responses\":{");
                for (int32_t j = 0; j < op->response_count; j++) {
                    oa_response_t* r = &op->responses[j];
                    off += snprintf(buf + off, len - off, "%s\"%s\":{", j > 0 ? "," : "", r->status_code);
                    off += snprintf(buf + off, len - off, "\"description\":\"%s\"", r->description);
                    if (r->content_count > 0) {
                        off += snprintf(buf + off, len - off, ",\"content\":{\"%s\":{", r->content_types[0]);
                        off += snprintf(buf + off, len - off, "\"schema\":{\"$ref\":\"%s\"}", r->schema_ref);
                        off += snprintf(buf + off, len - off, "}}");
                    }
                    off += snprintf(buf + off, len - off, "}");
                }
                off += snprintf(buf + off, len - off, "}");
            }
            if (op->tag_count > 0) {
                off += snprintf(buf + off, len - off, ",\"tags\":[");
                for (int32_t j = 0; j < op->tag_count; j++)
                    off += snprintf(buf + off, len - off, "%s\"%s\"", j > 0 ? "," : "", op->tags[j]);
                off += snprintf(buf + off, len - off, "]");
            }
            off += snprintf(buf + off, len - off, "}}");
        }
        off += snprintf(buf + off, len - off, "\n  }");
    }
    if (spec->schema_count > 0) {
        off += snprintf(buf + off, len - off, ",\n  \"components\":{\"schemas\":{");
        for (int32_t i = 0; i < spec->schema_count; i++) {
            oa_schema_t* s = &spec->schemas[i];
            off += snprintf(buf + off, len - off, "%s\"%s\":{", i > 0 ? "," : "", s->name);
            off = oa_append_json_schema(buf, off, len, s);
            off += snprintf(buf + off, len - off, "}");
        }
        off += snprintf(buf + off, len - off, "}}");
    }
    if (spec->sec_count > 0) {
        off += snprintf(buf + off, len - off, ",\n  \"components\":{\"securitySchemes\":{");
        for (int32_t i = 0; i < spec->sec_count; i++) {
            oa_security_scheme_t* s = &spec->sec_schemes[i];
            off += snprintf(buf + off, len - off, "%s\"%s\":{\"type\":\"%s\"",
                            i > 0 ? "," : "", s->scheme_name, oa_security_type_string(s->type));
            if (s->name[0]) off += snprintf(buf + off, len - off, ",\"name\":\"%s\"", s->name);
            if (s->in[0]) off += snprintf(buf + off, len - off, ",\"in\":\"%s\"", s->in);
            if (s->scheme[0]) off += snprintf(buf + off, len - off, ",\"scheme\":\"%s\"", s->scheme);
            if (s->bearer_format[0]) off += snprintf(buf + off, len - off, ",\"bearerFormat\":\"%s\"", s->bearer_format);
            off += snprintf(buf + off, len - off, "}");
        }
        off += snprintf(buf + off, len - off, "}}");
    }
    off += snprintf(buf + off, len - off, "\n}\n");
    return buf;
}

bool oa_spec_validate(oa_spec_t* spec) {
    if (!spec) return false;
    if (strlen(spec->title) == 0) return false;
    if (strlen(spec->version) == 0) return false;
    return true;
}

void oa_spec_merge(oa_spec_t* dst, oa_spec_t* src) {
    if (!dst || !src) return;
    for (int32_t i = 0; i < src->op_count && dst->op_count < OA_MAX_OPERATIONS; i++) {
        memcpy(&dst->operations[dst->op_count++], &src->operations[i], sizeof(oa_operation_t));
    }
    for (int32_t i = 0; i < src->schema_count && dst->schema_count < OA_MAX_SCHEMAS; i++) {
        memcpy(&dst->schemas[dst->schema_count++], &src->schemas[i], sizeof(oa_schema_t));
    }
}

oa_operation_t* oa_spec_find_operation(oa_spec_t* spec, const char* operation_id) {
    if (!spec || !operation_id) return NULL;
    for (int32_t i = 0; i < spec->op_count; i++) {
        if (strcmp(spec->operations[i].operation_id, operation_id) == 0) return &spec->operations[i];
    }
    return NULL;
}

oa_schema_t* oa_spec_find_schema(oa_spec_t* spec, const char* name) {
    if (!spec || !name) return NULL;
    for (int32_t i = 0; i < spec->schema_count; i++) {
        if (strcmp(spec->schemas[i].name, name) == 0) return &spec->schemas[i];
    }
    return NULL;
}
