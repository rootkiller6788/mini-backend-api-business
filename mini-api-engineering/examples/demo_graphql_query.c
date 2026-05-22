#include <stdio.h>
#include <string.h>
#include "../include/graphql_engine.h"

static void* resolve_hello(void* parent, void* args, void* context) {
    (void)parent;
    (void)args;
    (void)context;
    printf("  [resolver] hello() called\n");
    return (void*)"Hello, World!";
}

static void* resolve_user(void* parent, void* args, void* context) {
    (void)parent;
    (void)args;
    (void)context;
    printf("  [resolver] user() called\n");
    return (void*)"user_data";
}

static void* resolve_user_name(void* parent, void* args, void* context) {
    (void)parent;
    (void)args;
    (void)context;
    printf("  [resolver] User.name called\n");
    return (void*)"Alice";
}

static void* resolve_search(void* parent, void* args, void* context) {
    (void)parent;
    (void)args;
    (void)context;
    printf("  [resolver] search() called\n");
    return NULL;
}

static void demo_parse_simple_query(void) {
    printf("\n--- Demo: Parse Simple Query ---\n");
    gql_engine_t e;
    gql_engine_init(&e);
    const char* query = "{ hello }";
    printf("Input: %s\n", query);
    bool ok = gql_parse_query(&e, query);
    printf("Parsed: %s\n", ok ? "SUCCESS" : "FAIL");
    if (ok) {
        printf("Selections: %d\n", e.parsed.selection_count);
        for (int32_t i = 0; i < e.parsed.selection_count; i++) {
            printf("  field: %s\n", e.parsed.selections[i].field_name);
        }
    }
}

static void demo_parse_with_args(void) {
    printf("\n--- Demo: Parse Query with Arguments ---\n");
    gql_engine_t e;
    gql_engine_init(&e);
    const char* query = "{ user(id: 42) { name email } }";
    printf("Input: %s\n", query);
    bool ok = gql_parse_query(&e, query);
    printf("Parsed: %s\n", ok ? "SUCCESS" : "FAIL");
    if (ok) {
        printf("Target field: %s\n", e.parsed.target_field);
        printf("Args: %d\n", e.parsed.arg_count);
        for (int32_t i = 0; i < e.parsed.arg_count; i++) {
            printf("  %s = %s\n", e.parsed.args[i].name, e.parsed.args[i].default_value);
        }
        printf("Selections: %d\n", e.parsed.selection_count);
        for (int32_t i = 0; i < e.parsed.selection_count; i++) {
            printf("  field: %s (leaf: %s)\n",
                   e.parsed.selections[i].field_name,
                   e.parsed.selections[i].is_leaf ? "yes" : "no");
        }
    }
}

static void demo_schema_building(void) {
    printf("\n--- Demo: Build GraphQL Schema ---\n");
    gql_engine_t e;
    gql_engine_init(&e);

    gql_schema_add_type(&e.schema, "User");
    gql_schema_add_field(&e.schema, "User", "id", GQL_TYPE_ID, "ID", false, true);
    gql_schema_add_field(&e.schema, "User", "name", GQL_TYPE_STRING, "String", false, true);
    gql_schema_add_field(&e.schema, "User", "email", GQL_TYPE_STRING, "String", false, false);
    gql_schema_add_field(&e.schema, "User", "posts", GQL_TYPE_OBJECT, "Post", true, false);
    gql_schema_add_field(&e.schema, "User", "age", GQL_TYPE_INT, "Int", false, false);

    gql_schema_add_type(&e.schema, "Post");
    gql_schema_add_field(&e.schema, "Post", "title", GQL_TYPE_STRING, "String", false, true);
    gql_schema_add_field(&e.schema, "Post", "content", GQL_TYPE_STRING, "String", false, false);
    gql_schema_add_field(&e.schema, "Post", "published", GQL_TYPE_BOOLEAN, "Boolean", false, false);

    gql_schema_add_type(&e.schema, "Query");
    gql_schema_add_field(&e.schema, "Query", "hello", GQL_TYPE_STRING, "String", false, false);
    gql_schema_add_field_arg(&e.schema, "Query", "user", "id", "ID", true);
    gql_schema_add_field(&e.schema, "Query", "user", GQL_TYPE_OBJECT, "User", false, false);
    gql_schema_add_field(&e.schema, "Query", "search", GQL_TYPE_OBJECT, "Post", true, false);
    gql_schema_add_field_arg(&e.schema, "Query", "search", "query", "String", true);

    gql_schema_set_query_type(&e.schema, "Query");

    gql_schema_add_type(&e.schema, "Role");
    gql_schema_add_enum_value(&e.schema, "Role", "ADMIN");
    gql_schema_add_enum_value(&e.schema, "Role", "USER");
    gql_schema_add_enum_value(&e.schema, "Role", "GUEST");

    printf("Schema types: %d\n", e.schema.type_count);
    for (int32_t i = 0; i < e.schema.type_count; i++) {
        gql_type_t* t = &e.schema.types[i];
        printf("  %s%s (%d fields)\n", t->name, t->is_enum ? " [ENUM]" : "", t->field_count);
        if (t->is_enum) {
            for (int32_t j = 0; j < t->enum_count; j++)
                printf("    - %s\n", t->enum_values[j]);
        } else {
            for (int32_t j = 0; j < t->field_count; j++)
                printf("    %s: %s%s%s\n",
                       t->fields[j].name, t->fields[j].type_name,
                       t->fields[j].is_list ? "[]" : "",
                       t->fields[j].is_non_null ? "!" : "");
        }
    }
}

static void demo_resolvers(void) {
    printf("\n--- Demo: Resolver Registration & Execution ---\n");
    gql_engine_t e;
    gql_engine_init(&e);

    gql_schema_add_type(&e.schema, "Query");
    gql_schema_add_field(&e.schema, "Query", "hello", GQL_TYPE_STRING, "String", false, false);
    gql_schema_add_field(&e.schema, "Query", "user", GQL_TYPE_OBJECT, "User", false, false);
    gql_schema_set_query_type(&e.schema, "Query");

    gql_schema_add_resolver(&e.schema, "Query", "hello", resolve_hello, NULL);
    gql_schema_add_resolver(&e.schema, "Query", "user", resolve_user, NULL);
    gql_schema_add_resolver(&e.schema, "User", "name", resolve_user_name, NULL);
    gql_schema_add_resolver(&e.schema, "Query", "search", resolve_search, NULL);

    printf("Resolvers registered: %d\n", e.schema.resolver_count);
    for (int32_t i = 0; i < e.schema.resolver_count; i++) {
        printf("  %s.%s\n", e.schema.resolvers[i].type_name, e.schema.resolvers[i].field_name);
    }

    const char* query = "{ hello user { name } }";
    printf("\nExecuting: %s\n", query);
    gql_parse_query(&e, query);
    gql_execute(&e, NULL);
}

static void demo_sdl_parsing(void) {
    printf("\n--- Demo: Schema SDL Parsing ---\n");
    gql_engine_t e;
    gql_engine_init(&e);
    const char* sdl =
        "type Query {\n"
        "  hello: String\n"
        "  user(id: ID!): User\n"
        "}\n"
        "type User {\n"
        "  id: ID!\n"
        "  name: String!\n"
        "  email: String\n"
        "}\n"
        "enum Status {\n"
        "  ACTIVE\n"
        "  INACTIVE\n"
        "}\n";
    printf("SDL:\n%s\n", sdl);
    bool ok = gql_parse_schema_sdl(&e, sdl);
    printf("SDL parsed: %s\n", ok ? "OK" : "FAIL");
    for (int32_t i = 0; i < e.schema.type_count; i++) {
        gql_type_t* t = &e.schema.types[i];
        printf("  type %s (%d fields)%s\n",
               t->name, t->field_count, t->is_enum ? " [ENUM]" : "");
    }
}

static void demo_introspection(void) {
    printf("\n--- Demo: Schema Introspection ---\n");
    gql_engine_t e;
    gql_engine_init(&e);
    gql_schema_add_type(&e.schema, "Query");
    gql_schema_add_field(&e.schema, "Query", "hello", GQL_TYPE_STRING, "String", false, false);
    gql_schema_add_field(&e.schema, "Query", "version", GQL_TYPE_STRING, "String", false, false);
    gql_schema_set_query_type(&e.schema, "Query");

    char buf[2048];
    gql_introspect_schema(&e, buf, sizeof(buf));
    printf("Introspection result:\n%s\n", buf);
}

void demo_mutation_parsing(void) {
    printf("\n--- Demo: Mutation Parsing ---\n");
    gql_engine_t e;
    gql_engine_init(&e);
    const char* query = "mutation { createUser(name: \"Eve\") { id name } }";
    printf("Input: %s\n", query);
    bool ok = gql_parse_query(&e, query);
    printf("Parsed: %s (op=%s)\n", ok ? "OK" : "FAIL", gql_operation_name(e.parsed.operation));
}

int main(void) {
    printf("=== mini-api-engineering: GraphQL Query Demo ===\n");
    demo_parse_simple_query();
    demo_parse_with_args();
    demo_schema_building();
    demo_resolvers();
    demo_sdl_parsing();
    demo_introspection();
    demo_mutation_parsing();
    printf("\n=== Demo Complete ===\n");
    return 0;
}
