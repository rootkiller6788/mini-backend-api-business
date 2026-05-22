#ifndef OPENAPI_BUILDER_H
#define OPENAPI_BUILDER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define OA_MAX_PATHS            64
#define OA_MAX_SCHEMAS          64
#define OA_MAX_OPERATIONS       128
#define OA_MAX_PARAMETERS       16
#define OA_MAX_RESPONSES        8
#define OA_MAX_SECURITY_SCHEMES 8
#define OA_MAX_TAGS             16
#define OA_MAX_SERVERS          4
#define OA_MAX_PROPERTIES       32
#define OA_MAX_ENUM_VALUES      16
#define OA_MAX_CONTENT_TYPES    4
#define OA_NAME_LEN             128
#define OA_DESC_LEN             512
#define OA_URL_LEN              256
#define OA_REF_LEN              256
#define OA_EXAMPLE_LEN          1024
#define OA_SPEC_BUF_LEN         65536

typedef enum {
    OA_STRING  = 0,
    OA_INTEGER = 1,
    OA_NUMBER  = 2,
    OA_BOOLEAN = 3,
    OA_ARRAY   = 4,
    OA_OBJECT  = 5
} oa_type_t;

typedef enum {
    OA_GET     = 0,
    OA_POST    = 1,
    OA_PUT     = 2,
    OA_DELETE  = 3,
    OA_PATCH   = 4,
    OA_HEAD    = 5,
    OA_OPTIONS = 6,
    OA_TRACE   = 7
} oa_http_method_t;

typedef enum {
    OA_IN_QUERY  = 0,
    OA_IN_HEADER = 1,
    OA_IN_PATH   = 2,
    OA_IN_COOKIE = 3,
    OA_IN_BODY   = 4
} oa_param_in_t;

typedef enum {
    OA_SEC_APIKEY  = 0,
    OA_SEC_HTTP    = 1,
    OA_SEC_OAUTH2  = 2,
    OA_SEC_OPENID  = 3,
    OA_SEC_MUTUAL  = 4
} oa_security_type_t;

typedef struct {
    char name[OA_NAME_LEN];
    oa_type_t type;
    char format[32];
    char description[OA_DESC_LEN];
    char example[OA_EXAMPLE_LEN];
    bool is_required;
    char ref[OA_REF_LEN];
    oa_type_t array_item_type;
    char array_item_ref[OA_REF_LEN];
    int32_t min_length;
    int32_t max_length;
    double  minimum;
    double  maximum;
    char    enum_values[OA_MAX_ENUM_VALUES][OA_NAME_LEN];
    int32_t enum_count;
    char    default_value[OA_EXAMPLE_LEN];
    bool    nullable;
    bool    read_only;
    bool    write_only;
    bool    deprecated;
} oa_property_t;

typedef struct {
    char          name[OA_NAME_LEN];
    oa_type_t     type;
    char          description[OA_DESC_LEN];
    oa_property_t properties[OA_MAX_PROPERTIES];
    int32_t       prop_count;
    bool          is_enum;
    char          enum_values[OA_MAX_ENUM_VALUES][OA_NAME_LEN];
    int32_t       enum_count;
    char          ref[OA_REF_LEN];
    bool          is_array;
    oa_type_t     array_item_type;
    char          array_item_ref[OA_REF_LEN];
} oa_schema_t;

typedef struct {
    char         name[OA_NAME_LEN];
    oa_param_in_t in;
    char         description[OA_DESC_LEN];
    bool         required;
    oa_schema_t  schema;
    char         example[OA_EXAMPLE_LEN];
    bool         deprecated;
    bool         allow_empty_value;
    bool         explode;
} oa_parameter_t;

typedef struct {
    char          status_code[8];
    char          description[OA_DESC_LEN];
    char          content_types[OA_MAX_CONTENT_TYPES][64];
    int32_t       content_count;
    char          schema_ref[OA_REF_LEN];
    oa_schema_t   inline_schema;
    bool          use_inline;
    char          example_json[OA_EXAMPLE_LEN];
} oa_response_t;

typedef struct {
    char            operation_id[OA_NAME_LEN];
    char            summary[OA_DESC_LEN];
    char            description[OA_DESC_LEN];
    oa_http_method_t method;
    char            path[OA_URL_LEN];
    char            tags[OA_MAX_TAGS][OA_NAME_LEN];
    int32_t         tag_count;
    oa_parameter_t*  parameters[OA_MAX_PARAMETERS];
    int32_t         param_count;
    oa_response_t   responses[OA_MAX_RESPONSES];
    int32_t         response_count;
    bool            deprecated;
    char            security_requirements[OA_MAX_SECURITY_SCHEMES][OA_NAME_LEN];
    int32_t         sec_count;
    char            request_body_ref[OA_REF_LEN];
    oa_schema_t     request_body_schema;
    bool            has_request_body;
} oa_operation_t;

typedef struct {
    char           scheme_name[OA_NAME_LEN];
    oa_security_type_t type;
    char           description[OA_DESC_LEN];
    char           name[OA_NAME_LEN];
    char           in[16];
    char           scheme[32];
    char           bearer_format[32];
    char           flows_json[1024];
    char           open_id_url[OA_URL_LEN];
} oa_security_scheme_t;

typedef struct {
    char url[OA_URL_LEN];
    char description[OA_DESC_LEN];
} oa_server_t;

typedef struct {
    char                title[OA_NAME_LEN];
    char                version[32];
    char                description[OA_DESC_LEN];
    char                terms_url[OA_URL_LEN];
    char                contact_name[OA_NAME_LEN];
    char                contact_email[OA_NAME_LEN];
    char                contact_url[OA_URL_LEN];
    char                license_name[OA_NAME_LEN];
    char                license_url[OA_URL_LEN];
    oa_server_t         servers[OA_MAX_SERVERS];
    int32_t             server_count;
    oa_operation_t      operations[OA_MAX_OPERATIONS];
    int32_t             op_count;
    oa_schema_t         schemas[OA_MAX_SCHEMAS];
    int32_t             schema_count;
    oa_security_scheme_t sec_schemes[OA_MAX_SECURITY_SCHEMES];
    int32_t             sec_count;
    char                tags[OA_MAX_TAGS][OA_NAME_LEN];
    char                tag_descs[OA_MAX_TAGS][OA_DESC_LEN];
    int32_t             tag_count;
} oa_spec_t;

const char* oa_type_string(oa_type_t t);
const char* oa_method_string(oa_http_method_t m);
const char* oa_param_in_string(oa_param_in_t in);
const char* oa_security_type_string(oa_security_type_t t);

void oa_spec_init(oa_spec_t* spec, const char* title, const char* version, const char* description);
void oa_spec_add_server(oa_spec_t* spec, const char* url, const char* description);
void oa_spec_add_tag(oa_spec_t* spec, const char* name, const char* description);

oa_schema_t* oa_spec_add_schema(oa_spec_t* spec, const char* name, oa_type_t type, const char* description);
oa_property_t* oa_schema_add_prop(oa_schema_t* s, const char* name, oa_type_t type, const char* format,
                                   bool required, const char* description);
void oa_schema_add_enum_value(oa_schema_t* s, const char* value);
void oa_schema_set_array(oa_schema_t* s, oa_type_t item_type, const char* item_ref);
void oa_schema_set_ref(oa_schema_t* s, const char* ref);

oa_operation_t* oa_spec_add_operation(oa_spec_t* spec, const char* path, oa_http_method_t method,
                                      const char* operation_id, const char* summary);
void oa_operation_add_tag(oa_operation_t* op, const char* tag);
void oa_operation_add_param(oa_operation_t* op, const char* name, oa_param_in_t in,
                             oa_type_t type, bool required, const char* description);
void oa_operation_add_response(oa_operation_t* op, const char* status_code, const char* description,
                                const char* content_type, const char* schema_ref);
void oa_operation_add_body(oa_operation_t* op, const char* schema_ref, oa_type_t type);
void oa_operation_set_deprecated(oa_operation_t* op, bool deprecated);

oa_security_scheme_t* oa_spec_add_security(oa_spec_t* spec, const char* name, oa_security_type_t type,
                                            const char* description);
void oa_security_set_api_key(oa_security_scheme_t* s, const char* name, const char* in);
void oa_security_set_http_bearer(oa_security_scheme_t* s, const char* scheme, const char* bearer);

char* oa_spec_to_json(oa_spec_t* spec, char* buf, size_t len);
bool oa_spec_validate(oa_spec_t* spec);
void oa_spec_merge(oa_spec_t* dst, oa_spec_t* src);

oa_operation_t* oa_spec_find_operation(oa_spec_t* spec, const char* operation_id);
oa_schema_t* oa_spec_find_schema(oa_spec_t* spec, const char* name);

#endif
