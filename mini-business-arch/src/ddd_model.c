#include "ddd_model.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

typedef struct {
    OrderAggregate *orders;
    size_t          capacity;
    size_t          count;
} InMemoryOrderRepo;

static uint64_t g_id_counter = 0;

bool entity_id_equals(EntityId a, EntityId b) {
    return strncmp(a.value, b.value, DDD_ID_LEN) == 0;
}

EntityId entity_id_generate(void) {
    EntityId id;
    snprintf(id.value, DDD_ID_LEN, "ENT-%010llu-%04x",
        (unsigned long long)++g_id_counter,
        (unsigned int)(rand() & 0xFFFF));
    return id;
}

const char *entity_id_string(EntityId id) {
    static char buf[DDD_ID_LEN + 1];
    memcpy(buf, id.value, DDD_ID_LEN);
    buf[DDD_ID_LEN] = '\0';
    return buf;
}

bool address_equals(AddressValueObject a, AddressValueObject b) {
    return strcmp(a.street, b.street) == 0 &&
           strcmp(a.city, b.city) == 0 &&
           strcmp(a.postal_code, b.postal_code) == 0 &&
           strcmp(a.country, b.country) == 0;
}

AddressValueObject address_copy(AddressValueObject src) {
    AddressValueObject dst;
    memcpy(&dst, &src, sizeof(AddressValueObject));
    return dst;
}

bool money_equals(MoneyValueObject a, MoneyValueObject b) {
    return a.amount == b.amount && strcmp(a.currency, b.currency) == 0;
}

MoneyValueObject money_add(MoneyValueObject a, MoneyValueObject b) {
    if (strcmp(a.currency, b.currency) == 0) {
        a.amount += b.amount;
    }
    return a;
}

MoneyValueObject money_multiply(MoneyValueObject a, double factor) {
    a.amount *= factor;
    return a;
}

bool order_line_equals(OrderLineValueObject a, OrderLineValueObject b) {
    return entity_id_equals(a.product_id, b.product_id) &&
           a.quantity == b.quantity && money_equals(a.unit_price, b.unit_price);
}

OrderAggregate order_aggregate_create(EntityId customer_id) {
    OrderAggregate agg;
    memset(&agg, 0, sizeof(agg));
    agg.root_id = entity_id_generate();
    agg.root_version = 0;
    agg.root.id = agg.root_id;
    agg.root.customer_id = customer_id;
    agg.status = ORDER_STATUS_DRAFT;
    agg.line_item_capacity = 8;
    agg.line_items = (OrderLineValueObject *)malloc(
        agg.line_item_capacity * sizeof(OrderLineValueObject));
    agg.line_item_count = 0;
    return agg;
}

RepositoryResult order_aggregate_add_line_item(OrderAggregate *agg,
    EntityId product_id, const char *name, int quantity,
    double unit_price, const char *currency) {
    if (agg->status != ORDER_STATUS_DRAFT) {
        RepositoryResult r = {REPO_ERROR_CONCURRENCY,
            "Cannot add items to a non-draft order"};
        return r;
    }
    if (agg->line_item_count >= agg->line_item_capacity) {
        agg->line_item_capacity *= 2;
        agg->line_items = (OrderLineValueObject *)realloc(agg->line_items,
            agg->line_item_capacity * sizeof(OrderLineValueObject));
    }
    OrderLineValueObject *line = &agg->line_items[agg->line_item_count];
    line->product_id = product_id;
    strncpy(line->product_name, name, DDD_NAME_LEN);
    line->product_name[DDD_NAME_LEN - 1] = '\0';
    line->quantity = quantity;
    line->unit_price.amount = unit_price;
    strncpy(line->unit_price.currency, currency, 8);
    line->unit_price.currency[7] = '\0';
    agg->line_item_count++;
    RepositoryResult r = {REPO_ERROR_NONE, ""};
    return r;
}

RepositoryResult order_aggregate_remove_line_item(OrderAggregate *agg,
    EntityId product_id) {
    if (agg->status != ORDER_STATUS_DRAFT) {
        RepositoryResult r = {REPO_ERROR_CONCURRENCY,
            "Cannot modify a non-draft order"};
        return r;
    }
    for (uint64_t i = 0; i < agg->line_item_count; i++) {
        if (entity_id_equals(agg->line_items[i].product_id, product_id)) {
            size_t remain = (agg->line_item_count - i - 1) *
                sizeof(OrderLineValueObject);
            if (remain > 0) {
                memmove(&agg->line_items[i], &agg->line_items[i + 1], remain);
            }
            agg->line_item_count--;
            RepositoryResult r = {REPO_ERROR_NONE, ""};
            return r;
        }
    }
    RepositoryResult r = {REPO_ERROR_NOT_FOUND, "Line item not found"};
    return r;
}

RepositoryResult order_aggregate_place(OrderAggregate *agg,
    AddressValueObject shipping, AddressValueObject billing) {
    if (agg->status != ORDER_STATUS_DRAFT) {
        RepositoryResult r = {REPO_ERROR_CONCURRENCY,
            "Order already placed or processed"};
        return r;
    }
    if (agg->line_item_count == 0) {
        RepositoryResult r = {REPO_ERROR_CONCURRENCY,
            "Cannot place an empty order"};
        return r;
    }
    agg->shipping_address = address_copy(shipping);
    agg->billing_address = address_copy(billing);
    agg->status = ORDER_STATUS_PLACED;
    agg->root_version++;
    agg->total = order_aggregate_calculate_total(agg);
    RepositoryResult r = {REPO_ERROR_NONE, ""};
    return r;
}

RepositoryResult order_aggregate_ship(OrderAggregate *agg) {
    if (agg->status != ORDER_STATUS_PLACED &&
        agg->status != ORDER_STATUS_CONFIRMED) {
        RepositoryResult r = {REPO_ERROR_CONCURRENCY,
            "Order must be placed or confirmed before shipping"};
        return r;
    }
    agg->status = ORDER_STATUS_SHIPPED;
    agg->root_version++;
    RepositoryResult r = {REPO_ERROR_NONE, ""};
    return r;
}

RepositoryResult order_aggregate_cancel(OrderAggregate *agg) {
    if (agg->status == ORDER_STATUS_DELIVERED) {
        RepositoryResult r = {REPO_ERROR_CONCURRENCY,
            "Cannot cancel a delivered order"};
        return r;
    }
    agg->status = ORDER_STATUS_CANCELLED;
    agg->root_version++;
    RepositoryResult r = {REPO_ERROR_NONE, ""};
    return r;
}

MoneyValueObject order_aggregate_calculate_total(OrderAggregate *agg) {
    MoneyValueObject total = {0.0, "USD"};
    if (agg->line_item_count > 0) {
        strncpy(total.currency, agg->line_items[0].unit_price.currency, 8);
        total.currency[7] = '\0';
    }
    for (uint64_t i = 0; i < agg->line_item_count; i++) {
        double line_total = agg->line_items[i].unit_price.amount *
            agg->line_items[i].quantity;
        total.amount += line_total;
    }
    return total;
}

void order_aggregate_destroy(OrderAggregate *agg) {
    free(agg->line_items);
    agg->line_items = NULL;
    agg->line_item_capacity = 0;
    agg->line_item_count = 0;
}

static RepositoryResult inmem_order_find_by_id(void *self, EntityId id,
    void *out_aggregate) {
    InMemoryOrderRepo *repo = (InMemoryOrderRepo *)self;
    for (size_t i = 0; i < repo->count; i++) {
        if (entity_id_equals(repo->orders[i].root_id, id)) {
            memcpy(out_aggregate, &repo->orders[i], sizeof(OrderAggregate));
            RepositoryResult r = {REPO_ERROR_NONE, ""};
            return r;
        }
    }
    RepositoryResult r = {REPO_ERROR_NOT_FOUND, "Order not found"};
    return r;
}

static RepositoryResult inmem_order_save(void *self, void *aggregate) {
    InMemoryOrderRepo *repo = (InMemoryOrderRepo *)self;
    OrderAggregate *agg = (OrderAggregate *)aggregate;
    for (size_t i = 0; i < repo->count; i++) {
        if (entity_id_equals(repo->orders[i].root_id, agg->root_id)) {
            order_aggregate_destroy(&repo->orders[i]);
            memcpy(&repo->orders[i], agg, sizeof(OrderAggregate));
            RepositoryResult r = {REPO_ERROR_NONE, ""};
            return r;
        }
    }
    if (repo->count >= repo->capacity) {
        repo->capacity = repo->capacity == 0 ? 8 : repo->capacity * 2;
        repo->orders = (OrderAggregate *)realloc(repo->orders,
            repo->capacity * sizeof(OrderAggregate));
    }
    memcpy(&repo->orders[repo->count], agg, sizeof(OrderAggregate));
    repo->count++;
    RepositoryResult r = {REPO_ERROR_NONE, ""};
    return r;
}

static RepositoryResult inmem_order_delete(void *self, EntityId id) {
    InMemoryOrderRepo *repo = (InMemoryOrderRepo *)self;
    for (size_t i = 0; i < repo->count; i++) {
        if (entity_id_equals(repo->orders[i].root_id, id)) {
            order_aggregate_destroy(&repo->orders[i]);
            size_t remain = (repo->count - i - 1) * sizeof(OrderAggregate);
            if (remain > 0) {
                memmove(&repo->orders[i], &repo->orders[i + 1], remain);
            }
            repo->count--;
            RepositoryResult r = {REPO_ERROR_NONE, ""};
            return r;
        }
    }
    RepositoryResult r = {REPO_ERROR_NOT_FOUND, "Order not found"};
    return r;
}

static RepositoryResult inmem_order_find_all(void *self, void **out_list,
    size_t *out_count) {
    InMemoryOrderRepo *repo = (InMemoryOrderRepo *)self;
    *out_list = repo->orders;
    *out_count = repo->count;
    RepositoryResult r = {REPO_ERROR_NONE, ""};
    return r;
}

static void inmem_order_destroy(void *self) {
    InMemoryOrderRepo *repo = (InMemoryOrderRepo *)self;
    for (size_t i = 0; i < repo->count; i++) {
        order_aggregate_destroy(&repo->orders[i]);
    }
    free(repo->orders);
    free(repo);
}

Repository repository_create_in_memory(void) {
    InMemoryOrderRepo *repo = (InMemoryOrderRepo *)malloc(
        sizeof(InMemoryOrderRepo));
    memset(repo, 0, sizeof(InMemoryOrderRepo));
    Repository r;
    r.vtable = (RepositoryVTable *)malloc(sizeof(RepositoryVTable));
    r.vtable->find_by_id = inmem_order_find_by_id;
    r.vtable->save = inmem_order_save;
    r.vtable->delete = inmem_order_delete;
    r.vtable->find_all = inmem_order_find_all;
    r.vtable->destroy = inmem_order_destroy;
    r.ctx = repo;
    return r;
}

OrderDomainService order_domain_service_create(Repository *repo) {
    OrderDomainService svc;
    svc.order_repo = repo;
    return svc;
}

RepositoryResult order_domain_service_place_order(
    OrderDomainService *svc, OrderAggregate *order) {
    MoneyValueObject total = order_aggregate_calculate_total(order);
    if (total.amount <= 0.0) {
        RepositoryResult r = {REPO_ERROR_CONCURRENCY,
            "Order total must be positive"};
        return r;
    }
    RepositoryResult res = svc->order_repo->vtable->save(
        svc->order_repo->ctx, order);
    return res;
}

RepositoryResult order_domain_service_cancel_order(
    OrderDomainService *svc, OrderAggregate *order) {
    if (order->status == ORDER_STATUS_DELIVERED) {
        RepositoryResult r = {REPO_ERROR_CONCURRENCY,
            "Cannot cancel delivered order"};
        return r;
    }
    order->status = ORDER_STATUS_CANCELLED;
    order->root_version++;
    return svc->order_repo->vtable->save(svc->order_repo->ctx, order);
}

RepositoryResult order_domain_service_check_inventory(
    OrderDomainService *svc, OrderAggregate *order,
    InventoryCheckResult *out_results, size_t max_results, size_t *out_count) {
    (void)svc;
    *out_count = 0;
    size_t count = order->line_item_count < max_results
        ? order->line_item_count : max_results;
    for (size_t i = 0; i < count; i++) {
        out_results[i].product_id = order->line_items[i].product_id;
        out_results[i].requested_quantity = order->line_items[i].quantity;
        out_results[i].available_inventory = 100;
        out_results[i].is_feasible = true;
        (*out_count)++;
    }
    RepositoryResult r = {REPO_ERROR_NONE, ""};
    return r;
}
