#include <stdio.h>
#include "../include/graphql_engine.h"
int main() {
    printf("sizeof(gql_argument_t) = %zu\n", sizeof(gql_argument_t));
    printf("sizeof(gql_field_t) = %zu\n", sizeof(gql_field_t));
    printf("sizeof(gql_type_t) = %zu\n", sizeof(gql_type_t));
    printf("sizeof(gql_schema_t) = %zu\n", sizeof(gql_schema_t));
    printf("sizeof(gql_engine_t) = %zu\n", sizeof(gql_engine_t));
    return 0;
}
