#include <stdio.h>
#include <stdlib.h>
#include "ddd_model.h"

int main(void) {
    printf("=== DDD Example: Order Management ===\n\n");

    EntityId customer_id = entity_id_generate();
    printf("Customer ID: %s\n", entity_id_string(customer_id));

    OrderAggregate order = order_aggregate_create(customer_id);
    printf("Order ID: %s (DRAFT)\n", entity_id_string(order.root_id));

    EntityId product_a = entity_id_generate();
    EntityId product_b = entity_id_generate();

    order_aggregate_add_line_item(&order, product_a, "Widget A", 2, 29.99, "USD");
    order_aggregate_add_line_item(&order, product_b, "Widget B", 1, 49.99, "USD");
    printf("Added %llu line items\n", (unsigned long long)order.line_item_count);

    MoneyValueObject total = order_aggregate_calculate_total(&order);
    printf("Order total: %.2f %s\n", total.amount, total.currency);

    AddressValueObject shipping;
    strncpy(shipping.street, "123 Main St", DDD_NAME_LEN);
    strncpy(shipping.city, "San Francisco", DDD_DESC_LEN / 4);
    strncpy(shipping.postal_code, "94105", 32);
    strncpy(shipping.country, "USA", 64);

    AddressValueObject billing = shipping;

    RepositoryResult result = order_aggregate_place(&order, shipping, billing);
    printf("Place order: %s (status=%d)\n",
        result.code == REPO_ERROR_NONE ? "OK" : "FAILED", order.status);

    order_aggregate_ship(&order);
    printf("Ship order: status=%d\n", order.status);

    Repository repo = repository_create_in_memory();
    repo.vtable->save(repo.ctx, &order);

    OrderAggregate loaded;
    RepositoryResult loaded_result = repo.vtable->find_by_id(
        repo.ctx, order.root_id, &loaded);
    printf("Find by ID: %s\n",
        loaded_result.code == REPO_ERROR_NONE ? "FOUND" : "NOT FOUND");

    OrderDomainService svc = order_domain_service_create(&repo);

    InventoryCheckResult checks[10];
    size_t check_count = 0;
    order_domain_service_check_inventory(&svc, &order, checks, 10, &check_count);
    printf("Inventory checked: %zu items feasible\n", check_count);

    repo.vtable->destroy(repo.ctx);
    order_aggregate_destroy(&order);

    printf("\n=== DDD Example Complete ===\n");
    return 0;
}
