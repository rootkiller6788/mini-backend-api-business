#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "ddd_model.h"

static int failures = 0;
#define TEST(name) printf("  %-55s", name)
#define CHECK(cond) do { \
    if (!(cond)) { printf(" FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
    else printf(" PASS\n"); \
} while(0)

static void test_entity_id_operations(void) {
    TEST("entity_id_generate creates unique IDs");
    EntityId id1 = entity_id_generate();
    EntityId id2 = entity_id_generate();
    CHECK(!entity_id_equals(id1, id2));

    TEST("entity_id_equals same ID returns true");
    EntityId copy = id1;
    CHECK(entity_id_equals(id1, copy));

    TEST("entity_id_string returns printable string");
    const char *s = entity_id_string(id1);
    CHECK(s != NULL && strlen(s) > 0);
}

static void test_value_objects(void) {
    TEST("address_equals identical addresses");
    AddressValueObject a1, a2;
    memset(&a1, 0, sizeof(a1));
    strcpy(a1.street, "100 Main St");
    strcpy(a1.city, "NYC");
    strcpy(a1.postal_code, "10001");
    strcpy(a1.country, "USA");
    a2 = a1;
    CHECK(address_equals(a1, a2));

    TEST("address_equals different addresses");
    a2.city[0] = 'L';
    CHECK(!address_equals(a1, a2));

    TEST("address_copy creates independent copy");
    AddressValueObject a3 = address_copy(a1);
    CHECK(address_equals(a1, a3));

    TEST("money_equals same values");
    MoneyValueObject m1 = {100.50, "USD"};
    MoneyValueObject m2 = {100.50, "USD"};
    CHECK(money_equals(m1, m2));

    TEST("money_equals different amounts");
    m2.amount = 200.00;
    CHECK(!money_equals(m1, m2));

    TEST("money_add sums correctly");
    MoneyValueObject m3 = money_add(m1, (MoneyValueObject){50.25, "USD"});
    CHECK(m3.amount == 150.75);

    TEST("money_multiply scales correctly");
    MoneyValueObject m4 = money_multiply((MoneyValueObject){10.0, "USD"}, 3.0);
    CHECK(m4.amount == 30.0);
}

static void test_order_aggregate(void) {
    TEST("order_aggregate_create starts in DRAFT");
    EntityId cid = entity_id_generate();
    OrderAggregate agg = order_aggregate_create(cid);
    CHECK(agg.status == ORDER_STATUS_DRAFT);
    CHECK(agg.line_item_count == 0);

    TEST("add_line_item adds to order");
    EntityId pid = entity_id_generate();
    RepositoryResult r = order_aggregate_add_line_item(
        &agg, pid, "Test Product", 2, 29.99, "USD");
    CHECK(r.code == REPO_ERROR_NONE);
    CHECK(agg.line_item_count == 1);

    TEST("remove_line_item removes from order");
    r = order_aggregate_remove_line_item(&agg, pid);
    CHECK(r.code == REPO_ERROR_NONE);
    CHECK(agg.line_item_count == 0);

    TEST("remove_line_item not found returns error");
    r = order_aggregate_remove_line_item(&agg, pid);
    CHECK(r.code == REPO_ERROR_NOT_FOUND);

    TEST("place order transitions to PLACED");
    order_aggregate_add_line_item(&agg, pid, "Product", 1, 10.0, "USD");
    AddressValueObject addr;
    memset(&addr, 0, sizeof(addr));
    strcpy(addr.street, "1 Test Rd");
    strcpy(addr.city, "TestCity");
    r = order_aggregate_place(&agg, addr, addr);
    CHECK(r.code == REPO_ERROR_NONE);
    CHECK(agg.status == ORDER_STATUS_PLACED);

    TEST("calculate_total sums line items");
    MoneyValueObject total = order_aggregate_calculate_total(&agg);
    CHECK(total.amount == 10.0);

    TEST("cancel order transitions to CANCELLED");
    r = order_aggregate_cancel(&agg);
    CHECK(r.code == REPO_ERROR_NONE);
    CHECK(agg.status == ORDER_STATUS_CANCELLED);

    order_aggregate_destroy(&agg);
}

static void test_repository(void) {
    TEST("repository_create_in_memory returns valid repo");
    Repository repo = repository_create_in_memory();
    CHECK(repo.vtable != NULL);
    CHECK(repo.ctx != NULL);

    TEST("save and find_by_id work");
    EntityId cid = entity_id_generate();
    OrderAggregate agg = order_aggregate_create(cid);
    EntityId saved_id = agg.root_id;
    RepositoryResult r = repo.vtable->save(repo.ctx, &agg);
    CHECK(r.code == REPO_ERROR_NONE);

    OrderAggregate loaded;
    r = repo.vtable->find_by_id(repo.ctx, saved_id, &loaded);
    CHECK(r.code == REPO_ERROR_NONE);
    CHECK(entity_id_equals(loaded.root_id, saved_id));

    TEST("find_by_id not found returns error");
    EntityId unknown = entity_id_generate();
    r = repo.vtable->find_by_id(repo.ctx, unknown, &loaded);
    CHECK(r.code == REPO_ERROR_NOT_FOUND);

    TEST("delete removes from repo");
    r = repo.vtable->delete(repo.ctx, saved_id);
    CHECK(r.code == REPO_ERROR_NONE);
    r = repo.vtable->find_by_id(repo.ctx, saved_id, &loaded);
    CHECK(r.code == REPO_ERROR_NOT_FOUND);

    /* Repo shallow-copies aggregates; destroy repo (frees its copy) but
     * NOT agg since delete already freed line_items via repo's copy. */
    repo.vtable->destroy(repo.ctx);
}

static void test_domain_service(void) {
    TEST("domain_service_place_order validates total");
    Repository repo = repository_create_in_memory();
    OrderDomainService svc = order_domain_service_create(&repo);
    EntityId cid = entity_id_generate();
    OrderAggregate agg = order_aggregate_create(cid);

    RepositoryResult r = order_domain_service_place_order(&svc, &agg);
    CHECK(r.code == REPO_ERROR_CONCURRENCY);

    TEST("inventory check returns results");
    EntityId pid = entity_id_generate();
    order_aggregate_add_line_item(&agg, pid, "Item", 5, 10.0, "USD");
    InventoryCheckResult checks[4];
    size_t count = 0;
    r = order_domain_service_check_inventory(&svc, &agg, checks, 4, &count);
    CHECK(r.code == REPO_ERROR_NONE);
    CHECK(count == 1);
    CHECK(checks[0].is_feasible);

    repo.vtable->destroy(repo.ctx);
    order_aggregate_destroy(&agg);
}

int main(void) {
    printf("=== Test: DDD Model ===\n\n");
    test_entity_id_operations();
    test_value_objects();
    test_order_aggregate();
    test_repository();
    test_domain_service();
    printf("\nResult: %d failures\n", failures);
    return failures;
}
