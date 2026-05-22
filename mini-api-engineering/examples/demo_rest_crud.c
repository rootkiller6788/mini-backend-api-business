#include <stdio.h>
#include <string.h>
#include "../include/rest_design.h"

static void print_pagination(rest_pagination_t* p) {
    printf("Page   : %d / %d\n", p->page, p->total_pages);
    printf("Items  : %d total (page size %d)\n", p->total_items, p->page_size);
    printf("Has prev: %s, Has next: %s\n", p->has_prev ? "yes" : "no", p->has_next ? "yes" : "no");
    if (p->has_prev) printf("Prev link: %s\n", p->prev_link);
    if (p->has_next) printf("Next link: %s\n", p->next_link);
    printf("First   : %s\n", p->first_link);
    printf("Last    : %s\n", p->last_link);
}

static void print_resource(rest_resource_t* r) {
    printf("\n=== REST Resource ===\n");
    printf("Name    : %s\n", r->name);
    printf("Path    : %s\n", r->path);
    printf("Method  : %s\n", rest_method_string(r->method));
    printf("Status  : %s\n", rest_status_string(r->status));
    printf("Content : %s\n", r->content_type);
    if (r->response_len > 0) printf("Body    : %s\n", r->response_body);
    if (r->link_count > 0) {
        printf("Links   :\n");
        for (int32_t i = 0; i < r->link_count; i++)
            printf("  [%s] %s %s\n", r->links[i].rel, r->links[i].method, r->links[i].href);
    }
    if (r->header_count > 0) {
        printf("Headers :\n");
        for (int32_t i = 0; i < r->header_count; i++)
            printf("  %s: %s\n", r->headers[i].key, r->headers[i].value);
    }
    if (r->pagination.total_items > 0) print_pagination(&r->pagination);
}

static void demo_users_crud(void) {
    printf("\n--- Demo: Users CRUD (RESTful) ---\n");

    rest_resource_t get_users, get_user, create_user, update_user, delete_user;

    rest_resource_init(&get_users, "list_users", "/api/v1/users", REST_GET);
    rest_resource_set_status(&get_users, REST_200_OK);
    rest_resource_set_body(&get_users,
        "[{\"id\":1,\"name\":\"Alice\"},{\"id\":2,\"name\":\"Bob\"}]");
    rest_resource_set_content_type(&get_users, "application/json");
    rest_pagination_init(&get_users.pagination, 1, 20, 42);
    rest_resource_add_link(&get_users, "self", "/api/v1/users?page=1&page_size=20", "GET");
    rest_resource_add_link(&get_users, "create", "/api/v1/users", "POST");
    print_resource(&get_users);

    rest_resource_init(&get_user, "get_user", "/api/v1/users/{id}", REST_GET);
    rest_resource_add_path_param(&get_user, "id", "42");
    rest_resource_set_body(&get_user, "{\"id\":42,\"name\":\"Charlie\",\"email\":\"charlie@example.com\"}");
    rest_resource_add_link(&get_user, "self", "/api/v1/users/42", "GET");
    rest_resource_add_link(&get_user, "update", "/api/v1/users/42", "PUT");
    rest_resource_add_link(&get_user, "delete", "/api/v1/users/42", "DELETE");
    rest_resource_add_link(&get_user, "orders", "/api/v1/users/42/orders", "GET");
    print_resource(&get_user);

    rest_resource_init(&create_user, "create_user", "/api/v1/users", REST_POST);
    rest_resource_set_status(&create_user, REST_201_CREATED);
    rest_resource_set_body(&create_user, "{\"id\":43,\"name\":\"Diana\"}");
    rest_resource_add_link(&create_user, "self", "/api/v1/users/43", "GET");
    print_resource(&create_user);

    rest_resource_init(&update_user, "update_user", "/api/v1/users/43", REST_PATCH);
    rest_resource_set_status(&update_user, REST_200_OK);
    rest_resource_set_body(&update_user, "{\"id\":43,\"name\":\"Diana Updated\"}");
    print_resource(&update_user);

    rest_resource_init(&delete_user, "delete_user", "/api/v1/users/43", REST_DELETE);
    rest_resource_set_status(&delete_user, REST_204_NO_CONTENT);
    print_resource(&delete_user);
}

static void demo_error_responses(void) {
    printf("\n--- Demo: Error Responses ---\n");
    rest_status_t errors[] = {
        REST_400_BAD_REQUEST, REST_401_UNAUTHORIZED, REST_403_FORBIDDEN,
        REST_404_NOT_FOUND, REST_409_CONFLICT, REST_422_UNPROCESSABLE_ENTITY,
        REST_429_TOO_MANY_REQUESTS, REST_500_INTERNAL_SERVER_ERROR,
        REST_503_SERVICE_UNAVAILABLE
    };
    for (int32_t i = 0; i < 9; i++) {
        printf("  %-30s -> %s\n", rest_status_string(errors[i]), rest_status_reason(errors[i]));
    }
}

static void demo_hateoas(void) {
    printf("\n--- Demo: HATEOAS Order Resource ---\n");
    rest_resource_t order;
    rest_resource_init(&order, "order", "/api/v1/orders/1001", REST_GET);
    rest_resource_set_body(&order,
        "{\"id\":1001,\"status\":\"shipped\",\"total\":99.95,\"items\":["
        "{\"sku\":\"ABC-123\",\"qty\":2},"
        "{\"sku\":\"XYZ-789\",\"qty\":1}]}");
    rest_resource_add_link(&order, "self",    "/api/v1/orders/1001",      "GET");
    rest_resource_add_link(&order, "cancel",  "/api/v1/orders/1001/cancel","POST");
    rest_resource_add_link(&order, "invoice", "/api/v1/invoices/5001",    "GET");
    rest_resource_add_link(&order, "track",   "/api/v1/orders/1001/track","GET");
    rest_resource_add_link(&order, "return",  "/api/v1/returns",          "POST");
    print_resource(&order);
}

static void demo_router(void) {
    printf("\n--- Demo: Router with Versioning ---\n");
    rest_router_t router;
    rest_router_init(&router, "/api", 1);
    printf("Router base : %s\n", router.base_path);
    printf("Router URI  : %s\n", router.uri);
    printf("API Version : v%d\n", router.version);
}

int main(void) {
    printf("=== mini-api-engineering: REST CRUD Demo ===\n");
    demo_users_crud();
    demo_error_responses();
    demo_hateoas();
    demo_router();
    printf("\n=== Demo Complete ===\n");
    return 0;
}
