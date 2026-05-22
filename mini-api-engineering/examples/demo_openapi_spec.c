#include <stdio.h>
#include <string.h>
#include "../include/openapi_builder.h"

static void demo_basic_spec(void) {
    printf("\n--- Demo: Basic OpenAPI Spec ---\n");
    oa_spec_t spec;
    oa_spec_init(&spec, "Pet Store API", "1.0.0", "A sample pet store API");
    oa_spec_add_server(&spec, "https://api.petstore.example.com/v1", "Production server");
    oa_spec_add_server(&spec, "https://staging.petstore.example.com/v1", "Staging server");

    oa_spec_add_tag(&spec, "pets", "Everything about your Pets");
    oa_spec_add_tag(&spec, "store", "Access to PetStore orders");

    printf("Title: %s\n", spec.title);
    printf("Version: %s\n", spec.version);
    printf("Servers: %d\n", spec.server_count);
    printf("Tags: %d\n", spec.tag_count);
}

static void demo_pet_schema(void) {
    printf("\n--- Demo: Pet Schema Definition ---\n");
    oa_spec_t spec;
    oa_spec_init(&spec, "Pet Store API", "1.0.0", "A sample pet store API");

    oa_schema_t* pet = oa_spec_add_schema(&spec, "Pet", OA_OBJECT, "Pet object");
    oa_schema_add_prop(pet, "id", OA_INTEGER, "int64", true, "Pet ID");
    oa_schema_add_prop(pet, "name", OA_STRING, NULL, true, "Pet name");
    oa_schema_add_prop(pet, "tag", OA_STRING, NULL, false, "Optional tag");
    oa_schema_add_prop(pet, "status", OA_STRING, NULL, false, "Pet status");

    oa_schema_t* error = oa_spec_add_schema(&spec, "Error", OA_OBJECT, "Error object");
    oa_schema_add_prop(error, "code", OA_INTEGER, "int32", true, "Error code");
    oa_schema_add_prop(error, "message", OA_STRING, NULL, true, "Error message");

    oa_schema_t* status_enum = oa_spec_add_schema(&spec, "PetStatus", OA_STRING, "Pet status enum");
    oa_schema_add_enum_value(status_enum, "available");
    oa_schema_add_enum_value(status_enum, "pending");
    oa_schema_add_enum_value(status_enum, "sold");

    printf("Schemas: %d\n", spec.schema_count);
    for (int32_t i = 0; i < spec.schema_count; i++) {
        oa_schema_t* s = &spec.schemas[i];
        printf("  %s (%s, %d props, %d enum values)\n",
               s->name, oa_type_string(s->type), s->prop_count, s->enum_count);
        if (s->enum_count > 0) {
            printf("    enum: [");
            for (int32_t j = 0; j < s->enum_count; j++)
                printf("%s\"%s\"", j > 0 ? ", " : "", s->enum_values[j]);
            printf("]\n");
        }
        for (int32_t j = 0; j < s->prop_count; j++)
            printf("    %s: %s%s\n",
                   s->properties[j].name, oa_type_string(s->properties[j].type),
                   s->properties[j].is_required ? " (required)" : "");
    }
}

static void demo_operations(void) {
    printf("\n--- Demo: API Operations ---\n");
    oa_spec_t spec;
    oa_spec_init(&spec, "Pet Store API", "1.0.0", "A sample pet store API");

    oa_operation_t* list_pets = oa_spec_add_operation(&spec, "/pets", OA_GET, "listPets", "List all pets");
    oa_operation_add_tag(list_pets, "pets");
    oa_operation_add_response(list_pets, "200", "A list of pets", "application/json", "#/components/schemas/PetList");
    oa_operation_add_response(list_pets, "500", "Unexpected error", "application/json", "#/components/schemas/Error");

    oa_operation_t* create_pet = oa_spec_add_operation(&spec, "/pets", OA_POST, "createPet", "Create a pet");
    oa_operation_add_tag(create_pet, "pets");
    oa_operation_add_body(create_pet, "#/components/schemas/Pet", OA_OBJECT);
    oa_operation_add_response(create_pet, "201", "Pet created", "application/json", "#/components/schemas/Pet");
    oa_operation_add_response(create_pet, "400", "Invalid input", "application/json", "#/components/schemas/Error");

    oa_operation_t* get_pet = oa_spec_add_operation(&spec, "/pets/{petId}", OA_GET, "getPetById", "Get a pet by ID");
    oa_operation_add_tag(get_pet, "pets");
    oa_operation_add_response(get_pet, "200", "A pet", "application/json", "#/components/schemas/Pet");
    oa_operation_add_response(get_pet, "404", "Pet not found", "application/json", "#/components/schemas/Error");
    oa_operation_set_deprecated(get_pet, false);

    oa_operation_t* delete_pet = oa_spec_add_operation(&spec, "/pets/{petId}", OA_DELETE, "deletePet", "Delete a pet");
    oa_operation_add_tag(delete_pet, "pets");
    oa_operation_add_response(delete_pet, "204", "Pet deleted", NULL, NULL);
    oa_operation_set_deprecated(delete_pet, true);

    printf("Operations: %d\n", spec.op_count);
    for (int32_t i = 0; i < spec.op_count; i++) {
        oa_operation_t* op = &spec.operations[i];
        printf("  %s %s [%s]%s\n",
               oa_method_string(op->method), op->path, op->operation_id,
               op->deprecated ? " (deprecated)" : "");
    }
}

static void demo_security_schemes(void) {
    printf("\n--- Demo: Security Schemes ---\n");
    oa_spec_t spec;
    oa_spec_init(&spec, "Secure API", "2.0.0", "API with auth");

    oa_security_scheme_t* api_key = oa_spec_add_security(&spec, "ApiKeyAuth", OA_SEC_APIKEY,
        "API Key authentication");
    oa_security_set_api_key(api_key, "X-API-Key", "header");

    oa_security_scheme_t* bearer = oa_spec_add_security(&spec, "BearerAuth", OA_SEC_HTTP,
        "JWT Bearer token authentication");
    oa_security_set_http_bearer(bearer, "bearer", "JWT");

    oa_security_scheme_t* oauth2 = oa_spec_add_security(&spec, "OAuth2", OA_SEC_OAUTH2,
        "OAuth 2.0 with implicit flow");

    printf("Security schemes: %d\n", spec.sec_count);
    for (int32_t i = 0; i < spec.sec_count; i++) {
        printf("  %s (%s)\n", spec.sec_schemes[i].scheme_name,
               oa_security_type_string(spec.sec_schemes[i].type));
    }
}

static void demo_generate_swagger_json(void) {
    printf("\n--- Demo: Generate swagger.json ---\n");
    oa_spec_t spec;
    oa_spec_init(&spec, "Mini Pet Store", "1.0.0", "A minimal pet store API built with OpenAPI 3.0");

    oa_spec_add_server(&spec, "http://localhost:8080/api/v1", "Local development");

    oa_schema_t* pet = oa_spec_add_schema(&spec, "Pet", OA_OBJECT, "Pet entity");
    oa_schema_add_prop(pet, "id", OA_INTEGER, "int64", true, "Unique identifier");
    oa_schema_add_prop(pet, "name", OA_STRING, NULL, true, "Pet name");
    oa_schema_add_prop(pet, "status", OA_STRING, NULL, false, "Pet status");

    oa_operation_t* list = oa_spec_add_operation(&spec, "/pets", OA_GET, "listPets", "List all pets");
    oa_operation_add_tag(list, "pets");
    oa_operation_add_response(list, "200", "A list of pets", "application/json",
                              "#/components/schemas/Pet");

    oa_operation_t* create = oa_spec_add_operation(&spec, "/pets", OA_POST, "createPet", "Create a new pet");
    oa_operation_add_tag(create, "pets");
    oa_operation_add_response(create, "201", "Pet created", "application/json",
                              "#/components/schemas/Pet");
    oa_operation_add_response(create, "400", "Invalid input", "application/json",
                              "#/components/schemas/Error");

    char json_buf[OA_SPEC_BUF_LEN];
    oa_spec_to_json(&spec, json_buf, sizeof(json_buf));
    printf("%s", json_buf);
}

static void demo_validate(void) {
    printf("\n--- Demo: Spec Validation ---\n");
    oa_spec_t valid, invalid;

    oa_spec_init(&valid, "Valid Spec", "1.0.0", "Has title and version");
    printf("Valid spec: %s\n", oa_spec_validate(&valid) ? "PASS" : "FAIL");

    oa_spec_init(&invalid, "", "", "");
    printf("Invalid spec (no title/version): %s\n", oa_spec_validate(&invalid) ? "PASS" : "FAIL");
}

int main(void) {
    printf("=== mini-api-engineering: OpenAPI Spec Builder Demo ===\n");
    demo_basic_spec();
    demo_pet_schema();
    demo_operations();
    demo_security_schemes();
    demo_generate_swagger_json();
    demo_validate();
    printf("\n=== Demo Complete ===\n");
    return 0;
}
