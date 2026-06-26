#include <stdio.h>
#include "../include/openapi_builder.h"
int main() {
    printf("sizeof(oa_property_t) = %zu\n", sizeof(oa_property_t));
    printf("sizeof(oa_schema_t) = %zu\n", sizeof(oa_schema_t));
    printf("sizeof(oa_response_t) = %zu\n", sizeof(oa_response_t));
    printf("sizeof(oa_operation_t) = %zu\n", sizeof(oa_operation_t));
    printf("sizeof(oa_spec_t) = %zu\n", sizeof(oa_spec_t));
    return 0;
}
