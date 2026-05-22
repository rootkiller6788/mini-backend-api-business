#ifndef DDD_MODEL_H
#define DDD_MODEL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define DDD_ID_LEN    64
#define DDD_NAME_LEN 128
#define DDD_DESC_LEN 512

typedef struct {
    char value[DDD_ID_LEN];
} EntityId;

typedef struct {
    uint64_t version;
    EntityId id;
} Entity;

typedef struct {
    EntityId id;
    char      name[DDD_NAME_LEN];
    char      email[DDD_NAME_LEN];
} CustomerEntity;

typedef struct {
    EntityId   id;
    EntityId   customer_id;
    double     total_amount;
    char       currency[8];
    uint64_t   line_item_count;
} OrderEntity;

typedef enum {
    ORDER_STATUS_DRAFT = 0,
    ORDER_STATUS_PLACED,
    ORDER_STATUS_CONFIRMED,
    ORDER_STATUS_SHIPPED,
    ORDER_STATUS_DELIVERED,
    ORDER_STATUS_CANCELLED
} OrderStatus;

typedef struct {
    char   street[DDD_NAME_LEN];
    char   city[DDD_DESC_LEN / 4];
    char   postal_code[32];
    char   country[64];
} AddressValueObject;

typedef struct {
    double amount;
    char   currency[8];
} MoneyValueObject;

typedef struct {
    EntityId product_id;
    char     product_name[DDD_NAME_LEN];
    int      quantity;
    MoneyValueObject unit_price;
} OrderLineValueObject;

typedef struct ValueObjectNode {
    void                   *data;
    size_t                  data_size;
    struct ValueObjectNode *next;
} ValueObjectNode;

typedef struct {
    EntityId           root_id;
    uint64_t           root_version;
    OrderEntity        root;
    OrderStatus        status;
    AddressValueObject shipping_address;
    AddressValueObject billing_address;
    MoneyValueObject   total;
    OrderLineValueObject *line_items;
    uint64_t            line_item_capacity;
    uint64_t            line_item_count;
} OrderAggregate;

typedef enum {
    REPO_ERROR_NONE = 0,
    REPO_ERROR_NOT_FOUND,
    REPO_ERROR_CONCURRENCY,
    REPO_ERROR_IO
} RepositoryError;

typedef struct {
    RepositoryError code;
    char            message[DDD_DESC_LEN];
} RepositoryResult;

typedef struct RepositoryVTable {
    RepositoryResult (*find_by_id)(void *self, EntityId id, void *out_aggregate);
    RepositoryResult (*save)(void *self, void *aggregate);
    RepositoryResult (*delete)(void *self, EntityId id);
    RepositoryResult (*find_all)(void *self, void **out_list, size_t *out_count);
    void             (*destroy)(void *self);
} RepositoryVTable;

typedef struct {
    RepositoryVTable *vtable;
    void             *ctx;
} Repository;

typedef struct {
    EntityId product_id;
    int      requested_quantity;
    int      available_inventory;
    bool     is_feasible;
} InventoryCheckResult;

bool           entity_id_equals(EntityId a, EntityId b);
EntityId       entity_id_generate(void);
const char    *entity_id_string(EntityId id);

bool           address_equals(AddressValueObject a, AddressValueObject b);
AddressValueObject address_copy(AddressValueObject src);
bool           money_equals(MoneyValueObject a, MoneyValueObject b);
MoneyValueObject money_add(MoneyValueObject a, MoneyValueObject b);
MoneyValueObject money_multiply(MoneyValueObject a, double factor);
bool           order_line_equals(OrderLineValueObject a, OrderLineValueObject b);

OrderAggregate order_aggregate_create(EntityId customer_id);
RepositoryResult order_aggregate_add_line_item(OrderAggregate *agg,
    EntityId product_id, const char *name, int quantity,
    double unit_price, const char *currency);
RepositoryResult order_aggregate_remove_line_item(OrderAggregate *agg,
    EntityId product_id);
RepositoryResult order_aggregate_place(OrderAggregate *agg,
    AddressValueObject shipping, AddressValueObject billing);
RepositoryResult order_aggregate_ship(OrderAggregate *agg);
RepositoryResult order_aggregate_cancel(OrderAggregate *agg);
MoneyValueObject order_aggregate_calculate_total(OrderAggregate *agg);
void            order_aggregate_destroy(OrderAggregate *agg);

Repository      repository_create_in_memory(void);

typedef struct {
    Repository     *order_repo;
} OrderDomainService;

OrderDomainService order_domain_service_create(Repository *repo);
RepositoryResult   order_domain_service_place_order(
    OrderDomainService *svc, OrderAggregate *order);
RepositoryResult   order_domain_service_cancel_order(
    OrderDomainService *svc, OrderAggregate *order);
RepositoryResult   order_domain_service_check_inventory(
    OrderDomainService *svc, OrderAggregate *order,
    InventoryCheckResult *out_results, size_t max_results, size_t *out_count);

#endif
