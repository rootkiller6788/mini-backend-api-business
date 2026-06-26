#ifndef GRAPHQL_ENGINE_H
#define GRAPHQL_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define GQL_MAX_NAME_LEN       128
#define GQL_MAX_TYPE_LEN       64
#define GQL_MAX_ARGS           8
#define GQL_MAX_FIELDS         16
#define GQL_MAX_TYPES          32
#define GQL_MAX_RESOLVERS      64
#define GQL_MAX_QUERY_LEN      8192
#define GQL_MAX_SELECTION      64
#define GQL_MAX_SCHEMA_LEN     16384
#define GQL_MAX_ERRORS         16

typedef enum {
    GQL_TYPE_STRING,
    GQL_TYPE_INT,
    GQL_TYPE_FLOAT,
    GQL_TYPE_BOOLEAN,
    GQL_TYPE_ID,
    GQL_TYPE_OBJECT,
    GQL_TYPE_LIST,
    GQL_TYPE_NON_NULL,
    GQL_TYPE_ENUM,
    GQL_TYPE_INPUT_OBJECT,
    GQL_TYPE_SCALAR
} gql_scalar_t;

typedef enum {
    GQL_OP_QUERY,
    GQL_OP_MUTATION,
    GQL_OP_SUBSCRIPTION
} gql_operation_t;

typedef struct {
    char name[GQL_MAX_NAME_LEN];
    char type[GQL_MAX_TYPE_LEN];
    bool is_required;
    char default_value[256];
} gql_argument_t;

typedef struct {
    char             name[GQL_MAX_NAME_LEN];
    gql_scalar_t     scalar;
    char             type_name[GQL_MAX_TYPE_LEN];
    bool             is_list;
    bool             is_non_null;
    gql_argument_t   args[GQL_MAX_ARGS];
    int32_t          arg_count;
} gql_field_t;

typedef struct {
    char         name[GQL_MAX_NAME_LEN];
    gql_field_t  fields[GQL_MAX_FIELDS];
    int32_t      field_count;
    bool         is_input;
    bool         is_enum;
    char         enum_values[GQL_MAX_FIELDS][GQL_MAX_NAME_LEN];
    int32_t      enum_count;
} gql_type_t;

typedef struct {
    char   field_name[GQL_MAX_NAME_LEN];
    bool   is_leaf;
} gql_selection_t;

typedef struct {
    gql_operation_t   operation;
    char              operation_name[GQL_MAX_NAME_LEN];
    char              target_field[GQL_MAX_NAME_LEN];
    gql_selection_t   selections[GQL_MAX_SELECTION];
    int32_t           selection_count;
    gql_argument_t    args[GQL_MAX_ARGS];
    int32_t           arg_count;
} gql_parsed_query_t;

typedef void* (*gql_resolver_fn)(void* parent, void* args, void* context);

typedef struct {
    char            type_name[GQL_MAX_NAME_LEN];
    char            field_name[GQL_MAX_NAME_LEN];
    gql_resolver_fn resolver;
    void*           context;
} gql_resolver_t;

typedef struct {
    gql_type_t     types[GQL_MAX_TYPES];
    int32_t        type_count;
    gql_resolver_t resolvers[GQL_MAX_RESOLVERS];
    int32_t        resolver_count;
    char           query_type[GQL_MAX_NAME_LEN];
    char           mutation_type[GQL_MAX_NAME_LEN];
    char           subscription_type[GQL_MAX_NAME_LEN];
} gql_schema_t;

typedef struct {
    char message[512];
    int  line;
    int  column;
} gql_error_t;

typedef struct {
    gql_schema_t         schema;
    gql_parsed_query_t   parsed;
    char                 errors[GQL_MAX_ERRORS][512];
    int32_t              error_count;
} gql_engine_t;

const char* gql_scalar_name(gql_scalar_t s);
const char* gql_operation_name(gql_operation_t op);

void gql_engine_init(gql_engine_t* e);
void gql_schema_init(gql_schema_t* s);

void gql_schema_add_type(gql_schema_t* s, const char* name);
void gql_schema_add_field(gql_schema_t* s, const char* type_name, const char* field_name,
                          gql_scalar_t scalar, const char* type, bool is_list, bool is_non_null);
void gql_schema_add_field_arg(gql_schema_t* s, const char* type_name, const char* field_name,
                              const char* arg_name, const char* arg_type, bool required);
void gql_schema_add_enum_value(gql_schema_t* s, const char* type_name, const char* value);
void gql_schema_set_query_type(gql_schema_t* s, const char* name);
void gql_schema_set_mutation_type(gql_schema_t* s, const char* name);

void gql_schema_add_resolver(gql_schema_t* s, const char* type_name, const char* field_name,
                             gql_resolver_fn fn, void* context);

bool gql_parse_query(gql_engine_t* e, const char* query);
bool gql_parse_schema_sdl(gql_engine_t* e, const char* sdl);
bool gql_validate_query(gql_engine_t* e);
void* gql_execute(gql_engine_t* e, void* parent_value);

char* gql_introspect_schema(gql_engine_t* e, char* buf, size_t len);
char* gql_introspect_type(gql_engine_t* e, const char* type_name, char* buf, size_t len);

const gql_type_t* gql_find_type(gql_engine_t* e, const char* name);
int32_t gql_error_count(gql_engine_t* e);
const char* gql_error_message(gql_engine_t* e, int32_t index);

int32_t gql_calculate_query_cost(gql_engine_t* e);
int32_t gql_calculate_query_depth(gql_engine_t* e);
bool gql_limit_query_complexity(gql_engine_t* e, int32_t max_cost, int32_t max_depth);

char* gql_sdl_from_schema(gql_engine_t* e, char* buf, size_t len);
bool gql_validate_schema(gql_engine_t* e);
int32_t gql_type_field_count(gql_engine_t* e, const char* type_name);
bool gql_has_field(gql_engine_t* e, const char* type_name, const char* field_name);

#endif
