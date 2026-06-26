#include "json_helper.h"
#include "http_core.h"
#include "router.h"
#include "middleware.h"
#include <stdio.h>
#include <string.h>

/*
 * L7 Application: JSON API demo - RESTful endpoint with JSON parsing/response.
 * Demonstrates: parse request body JSON, build response JSON, middleware chain.
 */

typedef struct {
    const char *name;
    int         age;
    const char *email;
} User;

static User g_users[] = {
    {"Alice", 30, "alice@example.com"},
    {"Bob",   25, "bob@example.com"},
    {"Carol", 35, "carol@example.com"},
};
static int g_user_count = 3;

static bool list_users_handler(const HttpRequest *req, HttpResponse *res,
                                const RouteParam *params, int count) {
    (void)req; (void)params; (void)count;

    JsonValue *root = json_build_object();
    JsonValue *arr  = json_build_array();

    for (int i = 0; i < g_user_count; i++) {
        JsonValue *user_obj = json_build_object();
        json_object_set(user_obj, "name",  json_build_string(g_users[i].name));
        json_object_set(user_obj, "age",   json_build_number(g_users[i].age));
        json_object_set(user_obj, "email", json_build_string(g_users[i].email));
        json_array_push(arr, user_obj);
    }

    json_object_set(root, "users", arr);
    json_object_set(root, "count", json_build_number(g_user_count));

    char buf[8192];
    int len = json_serialize(root, buf, sizeof(buf));
    http_response_set_body(res, buf, (size_t)len);
    http_response_add_header(res, "Content-Type", "application/json");
    json_value_free(root);
    return 1;
}

static bool get_user_handler(const HttpRequest *req, HttpResponse *res,
                              const RouteParam *params, int count) {
    (void)req;
    const char *user_name = NULL;
    for (int i = 0; i < count; i++) {
        if (strcmp(params[i].name, "name") == 0) {
            user_name = params[i].value;
            break;
        }
    }

    if (!user_name) {
        http_response_set_status(res, HTTP_STATUS_BAD_REQUEST);
        http_response_set_body_str(res, "{\"error\":\"missing name param\"}");
        return 1;
    }

    for (int i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].name, user_name) == 0) {
            JsonValue *user = json_build_object();
            json_object_set(user, "name",  json_build_string(g_users[i].name));
            json_object_set(user, "age",   json_build_number(g_users[i].age));
            json_object_set(user, "email", json_build_string(g_users[i].email));

            char buf[4096];
            int len = json_serialize(user, buf, sizeof(buf));
            http_response_set_body(res, buf, (size_t)len);
            http_response_add_header(res, "Content-Type", "application/json");
            json_value_free(user);
            return 1;
        }
    }

    http_response_set_status(res, HTTP_STATUS_NOT_FOUND);
    http_response_set_body_str(res, "{\"error\":\"user not found\"}");
    return 1;
}

static bool create_user_handler(const HttpRequest *req, HttpResponse *res,
                                 const RouteParam *params, int count) {
    (void)params; (void)count;

    if (!req->body || req->body_len == 0) {
        http_response_set_status(res, HTTP_STATUS_BAD_REQUEST);
        http_response_set_body_str(res, "{\"error\":\"empty body\"}");
        return 1;
    }

    JsonValue *json = json_parse(req->body);
    if (!json) {
        http_response_set_status(res, HTTP_STATUS_BAD_REQUEST);
        http_response_set_body_str(res, "{\"error\":\"invalid JSON\"}");
        return 1;
    }

    const char *name  = json_value_get_string(json_object_get(json, "name"), NULL);
    double      age   = json_value_get_number(json_object_get(json, "age"), -1);
    const char *email = json_value_get_string(json_object_get(json, "email"), NULL);

    if (!name || age < 0 || !email) {
        http_response_set_status(res, HTTP_STATUS_BAD_REQUEST);
        http_response_set_body_str(res, "{\"error\":\"missing fields\"}");
        json_value_free(json);
        return 1;
    }

    printf("  [CREATE] User: %s, age: %.0f, email: %s\n", name, age, email);

    JsonValue *resp = json_build_object();
    json_object_set(resp, "status", json_build_string("created"));
    json_object_set(resp, "name",   json_build_string(name));

    char buf[4096];
    int len = json_serialize(resp, buf, sizeof(buf));
    http_response_set_status(res, HTTP_STATUS_CREATED);
    http_response_set_body(res, buf, (size_t)len);
    http_response_add_header(res, "Content-Type", "application/json");

    json_value_free(json);
    json_value_free(resp);
    return 1;
}

int main(void) {
    printf("=== JSON API Demo ===\n\n");

    /* 1. Build router */
    Router *router = router_create();
    router_add(router, HTTP_GET,  "/api/users",        list_users_handler);
    router_add(router, HTTP_GET,  "/api/users/:name",  get_user_handler);
    router_add(router, HTTP_POST, "/api/users",        create_user_handler);

    printf("[ROUTER] 3 routes registered\n\n");

    /* 2. Simulate GET /api/users */
    printf("--- GET /api/users ---\n");
    HttpRequest req1;
    http_request_init(&req1);
    http_parse_request_line("GET /api/users HTTP/1.1", &req1);

    HttpResponse res1;
    http_response_init(&res1);

    bool ok = router_dispatch(router, HTTP_GET, "/api/users", &req1, &res1);
    if (ok && res1.body) {
        printf("Status: %d\n", res1.status_code);
        printf("Body: %.*s\n", (int)res1.body_len, res1.body);

        /* Parse and display the JSON response */
        JsonValue *parsed = json_parse(res1.body);
        if (parsed) {
            JsonValue *users = json_object_get(parsed, "users");
            int n = json_array_size(users);
            printf("[PARSED] %d users:\n", n);
            for (int i = 0; i < n; i++) {
                JsonValue *u = json_array_get(users, i);
                printf("  - %s (%s)\n",
                       json_value_get_string(json_object_get(u, "name"), "?"),
                       json_value_get_string(json_object_get(u, "email"), "?"));
            }
            json_value_free(parsed);
        }
    }
    http_request_free(&req1);
    http_response_free(&res1);

    /* 3. Simulate GET /api/users/Alice */
    printf("\n--- GET /api/users/Alice ---\n");
    HttpRequest req2;
    http_request_init(&req2);
    http_parse_request_line("GET /api/users/Alice HTTP/1.1", &req2);

    HttpResponse res2;
    http_response_init(&res2);

    ok = router_dispatch(router, HTTP_GET, "/api/users/Alice", &req2, &res2);
    if (ok && res2.body) {
        printf("Body: %.*s\n", (int)res2.body_len, res2.body);
    }
    http_request_free(&req2);
    http_response_free(&res2);

    /* 4. Simulate POST /api/users with JSON body */
    printf("\n--- POST /api/users ---\n");
    HttpRequest req3;
    http_request_init(&req3);
    http_parse_request_line("POST /api/users HTTP/1.1", &req3);
    http_parse_header("Content-Type: application/json", &req3);
    const char *post_body = "{\"name\":\"Dave\",\"age\":28,\"email\":\"dave@example.com\"}";
    req3.body = strdup(post_body);
    req3.body_len = strlen(post_body);

    HttpResponse res3;
    http_response_init(&res3);

    ok = router_dispatch(router, HTTP_POST, "/api/users", &req3, &res3);
    if (ok && res3.body) {
        printf("Status: %d\n", res3.status_code);
        printf("Body: %.*s\n", (int)res3.body_len, res3.body);
    }
    http_request_free(&req3);
    http_response_free(&res3);

    /* 5. JSON builder demo */
    printf("\n--- JSON Builder Demo ---\n");
    JsonValue *doc = json_build_object();
    json_object_set(doc, "server", json_build_string("mini-web-server"));
    json_object_set(doc, "version", json_build_number(0.2));

    JsonValue *features = json_build_array();
    char *feat_list[] = {"HTTP/1.1", "WebSocket", "JSON API", "CGI", "Sessions"};
    for (int i = 0; i < 5; i++) {
        json_array_push(features, json_build_string(feat_list[i]));
    }
    json_object_set(doc, "features", features);

    printf("Built JSON document:\n");
    json_pretty_print(doc);
    json_value_free(doc);

    router_destroy(router);
    printf("\n=== Done ===\n");
    return 0;
}
