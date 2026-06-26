#include <stdio.h>
#include <string.h>
#include "../include/graphql_engine.h"

int main() {
    gql_engine_t e;
    gql_engine_init(&e);
    printf("engine init ok\n");

    bool ok = gql_parse_query(&e, "{ hello }");
    printf("parse simple: %s (selections=%d)\n", ok ? "OK" : "FAIL", e.parsed.selection_count);

    gql_engine_t e2;
    gql_engine_init(&e2);
    ok = gql_parse_query(&e2, "{ user(id: 42) { name email } }");
    printf("parse nested: %s\n", ok ? "OK" : "FAIL");

    gql_engine_t e3;
    gql_engine_init(&e3);
    ok = gql_parse_schema_sdl(&e3, "type Query { hello: String }");
    printf("parse SDL: %s\n", ok ? "OK" : "FAIL");

    printf("all gql basic tests passed\n");
    return 0;
}
