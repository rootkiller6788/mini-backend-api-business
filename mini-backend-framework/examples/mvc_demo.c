#include "mvc_pattern.h"
#include "validator.h"
#include "serializer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int    id;
    char   name[64];
    char   email[128];
    int    age;
} UserData;

static Serializer user_serializer;

static int user_list_action(MVCModel *model, MVCView *view, void *context) {
    (void)model;
    (void)context;

    mvc_view_assign(view, "title", "User List");
    mvc_view_assign(view, "users", "[{\"id\":1,\"name\":\"Alice\"},{\"id\":2,\"name\":\"Bob\"}]");
    mvc_view_assign(view, "count", "2");
    mvc_view_assign(view, "status", "OK");

    mvc_view_render(view);
    return 0;
}

static int user_create_action(MVCModel *model, MVCView *view, void *context) {
    Validator *v = (Validator *)context;
    (void)view;

    const char *name  = mvc_model_get(model, "name");
    const char *email = mvc_model_get(model, "email");
    const char *age   = mvc_model_get(model, "age");

    if (!mvc_model_validate(model)) {
        mvc_view_assign(view, "title", "Create User");
        mvc_view_assign(view, "error", "Validation failed");
        return -1;
    }

    if (v) {
        int errs = 0;
        errs += validator_validate(v, "name", name);
        errs += validator_validate(v, "email", email);
        errs += validator_validate(v, "age", age);
        if (errs > 0) {
            mvc_view_assign(view, "title", "Create User");
            mvc_view_assign(view, "error", "Validation failed");
            return -2;
        }
    }

    mvc_view_assign(view, "title", "Create User");
    mvc_view_assign(view, "message", "User created successfully");
    mvc_view_assign(view, "name", name ? name : "");
    mvc_view_assign(view, "email", email ? email : "");

    mvc_view_render(view);
    return 0;
}

static int user_get_action(MVCModel *model, MVCView *view, void *context) {
    (void)context;
    const char *id = mvc_model_get(model, "id");

    mvc_view_assign(view, "title", "User Detail");
    mvc_view_assign(view, "user_id", id ? id : "unknown");
    mvc_view_assign(view, "user_name", "Alice");
    mvc_view_assign(view, "user_email", "alice@example.com");

    mvc_view_render(view);
    return 0;
}

int main(void) {
    MVCController controller;
    MVCModel      model;
    MVCView       view;
    Validator     validator;

    const char *list_template =
        "<html><body>" \
        "<h1>{{ title }}</h1>" \
        "<p>Status: {{ status }}</p>" \
        "<p>Total users: {{ count }}</p>" \
        "<pre>{{ users }}</pre>" \
        "</body></html>";

    const char *create_template =
        "<html><body>" \
        "<h1>{{ title }}</h1>" \
        "{{#error}}<p style='color:red'>Error: {{ error }}</p>{{/error}}" \
        "{{#message}}<p style='color:green'>{{ message }}</p>{{/message}}" \
        "<p>Name: {{ name }}</p>" \
        "<p>Email: {{ email }}</p>" \
        "</body></html>";

    const char *get_template =
        "<html><body>" \
        "<h1>{{ title }}</h1>" \
        "<p>ID: {{ user_id }}</p>" \
        "<p>Name: {{ user_name }}</p>" \
        "<p>Email: {{ user_email }}</p>" \
        "</body></html>";

    printf("=== MVC Pattern Demo ===\n\n");

    printf("[1] MVC Initialization\n");
    mvc_controller_init(&controller);
    printf("    Controller initialized\n");

    printf("\n[2] Registering Routes\n");
    mvc_register_route(&controller, MVC_GET,    "/users",      "user.list",   user_list_action);
    mvc_register_route(&controller, MVC_POST,   "/users",      "user.create", user_create_action);
    mvc_register_route(&controller, MVC_GET,    "/users/{id}", "user.get",    user_get_action);
    mvc_register_route(&controller, MVC_DELETE, "/users/{id}", "user.delete", user_get_action);
    printf("    GET    /users       -> user.list\n");
    printf("    POST   /users       -> user.create\n");
    printf("    GET    /users/{id}  -> user.get\n");
    printf("    DELETE /users/{id}  -> user.delete\n");

    printf("\n[3] Model: Defining fields with validation\n");
    mvc_model_init(&model, "UserForm");
    mvc_model_add_field(&model, "name",  true,  2, 64, "alpha");
    mvc_model_add_field(&model, "email", true,  5, 128, NULL);
    mvc_model_add_field(&model, "age",   false, 0, 0,  "numeric");
    printf("    Model 'UserForm' with 3 fields defined\n");

    printf("\n[4] Validator: Setting up validation rules\n");
    validator_init(&validator);
    validator_rule_required(&validator, "name", "Name is required");
    validator_rule_min_length(&validator, "name", 2, "Name too short");
    validator_rule_email(&validator, "email", "Invalid email address");
    validator_rule_integer(&validator, "age", "Age must be a number");
    validator_rule_min_value(&validator, "age", 0, "Age must be positive");
    validator_rule_max_value(&validator, "age", 150, "Age must be <= 150");
    printf("    6 validation rules configured\n");

    printf("\n[5] Serializer: Struct-to-JSON setup\n");
    ser_init(&user_serializer, sizeof(UserData), "user");
    ser_add_int(&user_serializer,    "id",    offsetof(UserData, id),    "id");
    ser_add_string(&user_serializer, "name",  offsetof(UserData, name),  "name");
    ser_add_string(&user_serializer, "email", offsetof(UserData, email), "email");
    ser_add_int(&user_serializer,    "age",   offsetof(UserData, age),   "age");
    printf("    Serializer 'user' with 4 fields configured\n");

    printf("\n[6] Dispatch: GET /users (List)\n");
    mvc_view_init(&view);
    mvc_view_set_template(&view, list_template);
    mvc_model_init(&model, "UserForm");
    {
        int result = mvc_dispatch(&controller, MVC_GET, "/users",
                                  &model, &view, NULL);
        printf("    Dispatch result: %d\n", result);
        printf("    Rendered view:\n%s\n", mvc_view_output(&view));
    }

    printf("\n[7] Dispatch: POST /users (Create with valid data)\n");
    mvc_view_init(&view);
    mvc_view_set_template(&view, create_template);
    mvc_model_init(&model, "UserForm");
    mvc_model_set(&model, "name", "Charlie");
    mvc_model_set(&model, "email", "charlie@example.com");
    mvc_model_set(&model, "age", "28");
    {
        int result = mvc_dispatch(&controller, MVC_POST, "/users",
                                  &model, &view, &validator);
        printf("    Dispatch result: %d\n", result);
        printf("    Rendered view:\n%s\n", mvc_view_output(&view));
    }

    printf("\n[8] Dispatch: POST /users (Create with invalid email)\n");
    mvc_view_init(&view);
    mvc_view_set_template(&view, create_template);
    mvc_model_init(&model, "UserForm");
    mvc_model_set(&model, "name", "Dave");
    mvc_model_set(&model, "email", "not-an-email");
    mvc_model_set(&model, "age", "42");
    {
        int result = mvc_dispatch(&controller, MVC_POST, "/users",
                                  &model, &view, &validator);
        printf("    Dispatch result: %d (validation failed)\n", result);
        if (result != 0) {
            validator_validate(&validator, "email", "not-an-email");
            validator_print_errors(&validator);
        }
    }

    printf("\n[9] Dispatch: GET /users/42 (Detail with path param)\n");
    mvc_view_init(&view);
    mvc_view_set_template(&view, get_template);
    mvc_model_init(&model, "UserForm");
    mvc_model_set(&model, "id", "42");
    {
        int result = mvc_dispatch(&controller, MVC_GET, "/users/42",
                                  &model, &view, NULL);
        printf("    Dispatch result: %d\n", result);
        printf("    Rendered view:\n%s\n", mvc_view_output(&view));
    }

    printf("\n[10] Dispatch: DELETE /users/99\n");
    mvc_view_init(&view);
    mvc_view_set_template(&view, get_template);
    mvc_model_init(&model, "UserForm");
    mvc_model_set(&model, "id", "99");
    {
        int result = mvc_dispatch(&controller, MVC_DELETE, "/users/99",
                                  &model, &view, NULL);
        printf("    Dispatch result: %d\n", result);
        printf("    Rendered view:\n%s\n", mvc_view_output(&view));
    }

    printf("\n=== MVC Pattern Demo Complete ===\n");
    return 0;
}
