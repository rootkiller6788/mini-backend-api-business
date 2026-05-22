# mini-ddd-cqrs — DDD + CQRS Deep Dive

A comprehensive guide to Domain-Driven Design and CQRS with Event Sourcing
as implemented in `mini-business-arch`. This demo explores the theory, patterns,
and practical use of DDD tactical building blocks and command/query separation
in a C99 codebase.

---

## Table of Contents

1. [Domain-Driven Design Foundations](#domain-driven-design-foundations)
2. [Entities and Identity](#entities-and-identity)
3. [Value Objects](#value-objects)
4. [Aggregates and Aggregate Roots](#aggregates-and-aggregate-roots)
5. [Repositories](#repositories)
6. [Domain Services](#domain-services)
7. [Domain Events](#domain-events)
8. [CQRS: Command Query Responsibility Segregation](#cqrs-command-query-responsibility-segregation)
9. [The Command Bus](#the-command-bus)
10. [The Query Bus](#the-query-bus)
11. [Event Sourcing](#event-sourcing)
12. [Event Store](#event-store)
13. [Projections and Read Models](#projections-and-read-models)
14. [Snapshots](#snapshots)
15. [Putting It All Together](#putting-it-all-together)
16. [Trade-offs and When to Use](#trade-offs-and-when-to-use)

---

## Domain-Driven Design Foundations

Domain-Driven Design (DDD) is a software design approach that places the
primary focus on the core domain and domain logic. Eric Evans introduced DDD
in his 2003 book as a way to tackle complexity in enterprise software.

### Ubiquitous Language

The most important concept in DDD is **Ubiquitous Language** — a common,
rigorous language shared by domain experts and developers. Every term in
the code should match how the business talks about it. In our library:

- `OrderAggregate` — not `OrderModel` or `OrderRecord`
- `AddressValueObject` — not `AddressDto` or `AddressStruct`
- `entity_id_generate()` — consistently named identity generation
- `order_aggregate_place()` — verb-first naming from the business domain

This naming convention is enforced throughout `ddd_model.h` and `ddd_model.c`.
When a domain expert says "place an order," the code says `order_aggregate_place()`.

### Bounded Contexts

A Bounded Context defines the boundary within which a particular model applies.
In a large system, "Order" means different things in Sales, Shipping, and
Billing contexts. Each context has its own model, its own ubiquitous language,
and its own persistence.

While `mini-business-arch` focuses on a single bounded context for simplicity,
the patterns it provides (entities, value objects, aggregates) are designed to
compose across multiple bounded contexts.

### Strategic vs Tactical DDD

| Aspect | Strategic DDD | Tactical DDD |
|--------|---------------|--------------|
| Focus | Big picture | Implementation details |
| Concepts | Bounded Contexts, Context Maps, Subdomains | Entities, Value Objects, Aggregates, Repositories, Domain Services, Domain Events |
| This library | Not covered (architectural concern) | Fully implemented |

---

## Entities and Identity

An **Entity** is an object defined primarily by its identity — not by its
attributes. Two entities with the same attribute values are still different
entities if they have different identities.

### EntityId

```c
typedef struct {
    char value[DDD_ID_LEN];  // 64-character UUID-like string
} EntityId;

EntityId entity_id_generate(void);
bool     entity_id_equals(EntityId a, EntityId b);
```

Identities are generated as 64-character hex strings (32-byte randomness).
They are immutable once created. Equality is based solely on identity comparison,
not on attribute equality.

### Entity Base

```c
typedef struct {
    uint64_t version;  // optimistic concurrency version
    EntityId id;       // unique identifier
} Entity;
```

The `version` field enables optimistic concurrency control. Every mutation
increments the version, allowing conflict detection when multiple actors
modify the same entity concurrently.

### Concrete Entities

```c
typedef struct {
    EntityId   id;
    char       name[DDD_NAME_LEN];
    char       email[DDD_NAME_LEN];
} CustomerEntity;

typedef struct {
    EntityId   id;
    EntityId   customer_id;
    double     total_amount;
    char       currency[8];
    uint64_t   line_item_count;
} OrderEntity;
```

Entities always carry their identity and business-relevant attributes. They are
the "nouns" of the domain model — things that have continuity and a lifecycle.

### Entity vs Value Object Decision Guide

| Question | Answer: Entity | Answer: Value Object |
|----------|---------------|----------------------|
| Does it have a lifecycle? | Yes | No |
| Does identity matter? | Yes | No |
| Is it immutable? | No | Yes |
| Can it be shared? | No | Yes |
| Example | Customer, Order, Product | Address, Money, DateRange |

---

## Value Objects

A **Value Object** is an immutable object defined by its attributes. Two value
objects with the same attribute values are considered equal, regardless of
identity.

### AddressValueObject

```c
typedef struct {
    char street[DDD_NAME_LEN];
    char city[DDD_DESC_LEN / 4];
    char postal_code[32];
    char country[64];
} AddressValueObject;

bool             address_equals(AddressValueObject a, AddressValueObject b);
AddressValueObject address_copy(AddressValueObject src);
```

Addresses are classic value objects. An address is fully defined by its
components. Copy operations are explicit to maintain immutability discipline.

### MoneyValueObject

```c
typedef struct {
    double amount;
    char   currency[8];
} MoneyValueObject;

bool   money_equals(MoneyValueObject a, MoneyValueObject b);
MoneyValueObject money_add(MoneyValueObject a, MoneyValueObject b);
MoneyValueObject money_multiply(MoneyValueObject a, double factor);
```

Money is a canonical value object. Adding two Money values with different
currencies should raise an error. Multiplying a Money value by a quantity
yields a new Money value.

### OrderLineValueObject

```c
typedef struct {
    EntityId   product_id;
    char       product_name[DDD_NAME_LEN];
    int        quantity;
    MoneyValueObject unit_price;
} OrderLineValueObject;
```

An order line is a value object within the Order aggregate. Its identity is
derived from the product it references, and it cannot exist independently of
an order.

### Value Object Design Principles

1. **Immutability** — once created, never modified. Operations return new instances.
2. **Structural equality** — equality based on all constituent attributes.
3. **Self-validation** — value objects enforce their own invariants on creation.
4. **Side-effect free** — methods return results without modifying state.
5. **Replaceability** — you replace a value object, you never update it.

---

## Aggregates and Aggregate Roots

An **Aggregate** is a cluster of domain objects (entities + value objects)
treated as a single unit. The **Aggregate Root** is the single entry point
for all modifications to the aggregate.

### OrderAggregate

```c
typedef struct {
    EntityId   root_id;            // aggregate root identity
    OrderEntity root;              // the entity at the root
    OrderLineValueObject *line_items;  // child value objects
    uint64_t   line_item_capacity;
    uint64_t   line_item_count;
    AddressValueObject shipping_address;
    AddressValueObject billing_address;
    OrderStatus status;
    uint64_t    version;
} OrderAggregate;
```

The `OrderAggregate` is the aggregate root. All modifications to line items,
addresses, and status flow through its methods:

```c
OrderAggregate order_aggregate_create(EntityId customer_id);
void           order_aggregate_add_line_item(OrderAggregate *agg, ...);
void           order_aggregate_remove_line_item(OrderAggregate *agg, EntityId product_id);
void           order_aggregate_place(OrderAggregate *agg, AddressValueObject shipping, AddressValueObject billing);
void           order_aggregate_ship(OrderAggregate *agg);
void           order_aggregate_cancel(OrderAggregate *agg);
MoneyValueObject order_aggregate_calculate_total(OrderAggregate *agg);
void           order_aggregate_destroy(OrderAggregate *agg);
```

### Aggregate Design Rules

1. **Protect invariants** — the aggregate root enforces all business rules.
2. **Reference by identity** — aggregates reference other aggregates by ID only.
3. **Small aggregates** — prefer many small aggregates over one large one.
4. **Transactional boundary** — one aggregate = one transaction scope.
5. **Eventual consistency** — between aggregates, use domain events.

### Invariant Examples in OrderAggregate

- An order cannot be shipped while in DRAFT status.
- A cancelled order cannot be modified.
- Line item quantity must be positive.
- The shipping address must be set before placing.
- The billing address must match shipping country for international orders.

These invariants are enforced inside the aggregate methods in `ddd_model.c`,
not spread across the application layer.

### OrderStatus Lifecycle

```
DRAFT → PLACED → CONFIRMED → SHIPPED → DELIVERED
  ↓
CANCELLED (from DRAFT or PLACED only)
```

The status transitions are enforced by the aggregate. Attempting an invalid
transition (e.g., SHIPPED → DRAFT) is rejected.

---

## Repositories

A **Repository** provides the illusion of an in-memory collection of aggregates.
It abstracts the persistence mechanism behind a collection-like interface.

```c
typedef struct {
    OrderAggregate *orders;
    size_t          capacity;
    size_t          count;
} OrderRepository;

OrderRepository *order_repository_create(void);
void             order_repository_save(OrderRepository *repo, OrderAggregate agg);
OrderAggregate  *order_repository_find_by_id(OrderRepository *repo, EntityId id);
OrderAggregate  *order_repository_find_by_customer(OrderRepository *repo, EntityId customer_id, size_t *out_count);
void             order_repository_delete(OrderRepository *repo, EntityId id);
void             order_repository_destroy(OrderRepository *repo);
```

The repository pattern in this library uses in-memory storage for simplicity.
In production, the `save` and `find_by_id` methods would delegate to a database
or event store.

### Repository vs DAO

| Aspect | Repository | DAO |
|--------|-----------|-----|
| Abstraction level | Domain (collection of aggregates) | Data (table/collection of rows) |
| Return types | Aggregate roots | Data transfer objects |
| Query language | Domain-specific (findByCustomer) | Generic (SELECT ... WHERE) |
| Transaction scope | One aggregate | Arbitrary queries |

---

## Domain Services

A **Domain Service** encapsulates domain logic that does not naturally belong
to any single entity or value object.

```c
typedef struct {
    EntityId id;
    char     name[DDD_NAME_LEN];
    int      (*execute)(void *context, void *input, void *output);
    void    *context;
} DomainService;
```

### When to Use a Domain Service

1. The operation involves multiple aggregates.
2. The operation is stateless (no entity owns it naturally).
3. The operation is a significant business process with its own name.

### Example: PricingService

```c
DomainService pricing_svc = {
    .name = "PricingService",
    .execute = pricing_calculate_discount,
    .context = &pricing_rules
};
```

Domain services are part of the ubiquitous language. "PricingService"
should be a term the business uses.

---

## Domain Events

A **Domain Event** captures something significant that happened in the domain.
Events are immutable facts — they describe what happened, not what to do.

```c
typedef enum {
    DOMAIN_EVENT_ORDER_PLACED = 0,
    DOMAIN_EVENT_ORDER_SHIPPED,
    DOMAIN_EVENT_ORDER_CANCELLED,
    DOMAIN_EVENT_ORDER_LINE_ADDED,
    DOMAIN_EVENT_ORDER_LINE_REMOVED
} DomainEventType;

typedef struct {
    DomainEventType type;
    EntityId        aggregate_id;
    uint64_t        version;
    char            payload[DDD_DESC_LEN];
    size_t          payload_size;
} DomainEvent;
```

### Event-Driven Flow

```
User Action → Command → Aggregate Method → Domain Event(s) → Event Store → Projections
```

Domain events bridge DDD and Event Sourcing. They are the "source of truth"
in event-sourced systems.

---

## CQRS: Command Query Responsibility Segregation

CQRS separates the read model from the write model. Commands mutate state
without returning data. Queries return data without side effects.

### The Core Insight

In traditional CRUD systems, the same model serves both reads and writes.
This creates tension: the write model needs strong consistency and invariant
enforcement; the read model needs to be optimized for query patterns, joins,
and denormalization.

CQRS resolves this tension by using **separate models** for each concern.

```
            ┌──────────┐
User ──────→│  Command  │────→ Write Model ────→ Event Store
            │   Bus     │
            └──────────┘
                                    │
            ┌──────────┐            ▼
User ──────→│  Query    │────→ Read Model (Projections)
            │   Bus     │
            └──────────┘
```

### Command

```c
typedef struct {
    char     command_type[DDD_NAME_LEN];
    EntityId target_aggregate_id;
    char     payload[CQRS_PAYLOAD_LEN];
    size_t   payload_size;
    char     metadata[CQRS_META_LEN];
} Command;
```

A Command is an imperative: "PlaceOrder", "CancelOrder", "ShipOrder". Commands
are named in the imperative mood. They target a specific aggregate and carry
the data needed to perform the operation.

### Command Handler

```c
typedef struct {
    char command_type[DDD_NAME_LEN];
    int  (*handler)(void *ctx, Command *cmd, EventStoreEvent *out_events,
                    size_t max_events, size_t *out_count);
    void *context;
} CommandHandler;
```

Handlers return the resulting domain events rather than modified state. This
is a key CQRS+ES design: the handler does not directly mutate an aggregate.
Instead, the events are stored, and the aggregate is reconstructed from them.

### Query and Query Handler

```c
typedef struct {
    char  query_type[DDD_NAME_LEN];
    int  (*handler)(void *ctx, void *criteria, void *result,
                    size_t max_result_size, size_t *out_size);
    void *context;
} QueryHandler;

typedef struct {
    QueryHandler *handlers;
    size_t        handler_capacity;
    size_t        handler_count;
} QueryBus;
```

Queries are named as nouns: "OrderDetail", "OrdersByCustomer". They return
DTOs (or projections) without side effects.

---

## The Command Bus

The Command Bus decouples command dispatch from command handling.

```c
CommandBus *command_bus_create(EventStore *store);
void        command_bus_register_handler(CommandBus *bus, const char *type,
                   handler_fn fn, void *context);
int         command_bus_dispatch(CommandBus *bus, Command *cmd);
void        command_bus_destroy(CommandBus *bus);
```

### Registration Pattern

```c
CommandBus *bus = command_bus_create(store);
command_bus_register_handler(bus, "PlaceOrder",
    command_place_order_handler, NULL);
command_bus_register_handler(bus, "CancelOrder",
    command_cancel_order_handler, NULL);
command_bus_register_handler(bus, "ShipOrder",
    command_ship_order_handler, NULL);
```

### Dispatch Flow

1. Client creates a `Command` struct with the command type and payload.
2. `command_bus_dispatch()` looks up the registered handler.
3. Handler executes business logic and produces `EventStoreEvent` array.
4. Events are appended to the `EventStore`.
5. Handler returns success/failure.

### Middleware Possibilities

The command bus is a natural interception point for cross-cutting concerns:
- **Validation** — validate commands before dispatch
- **Logging** — log every command and its result
- **Authorization** — check permissions before execution
- **Retry** — retry failed commands with backoff
- **Metrics** — measure command execution time and throughput

---

## The Query Bus

The Query Bus follows the same pattern for read operations.

```c
QueryBus *query_bus_create(void);
void      query_bus_register_handler(QueryBus *bus, const char *type,
                 handler_fn fn, void *context);
int       query_bus_query(QueryBus *bus, void *criteria, const char *type,
                 void *result, size_t max_size, size_t *out_size);
void      query_bus_destroy(QueryBus *bus);
```

### Query Handler Pattern

```c
int query_order_detail_handler(void *ctx, void *criteria, void *result,
                                size_t max_result_size, size_t *out_size) {
    EntityId *order_id = (EntityId *)criteria;
    OrderProjection *proj = (OrderProjection *)result;

    // Read from projection (pre-built read model)
    // No aggregates are loaded or modified
    // No side effects
    return CQRS_OK;
}
```

### Read Model Optimization

Since queries never touch the write model, the read side can be aggressively
optimized:
- Denormalized tables for fast joins
- Materialized views for common queries
- Separate database technology (SQL for writes, Elasticsearch for reads)
- Pre-computed aggregations and summaries

---

## Event Sourcing

Event Sourcing stores the state of an aggregate as a sequence of events, rather
than as a current snapshot.

### Traditional Persistence vs Event Sourcing

```
Traditional:  Store current state → UPDATE orders SET status='SHIPPED' WHERE id=42

Event Sourcing: Append event → INSERT INTO events (OrderShipped, order_id=42, ...)

Current state = fold(reduce, initial_state, all_events)
```

### Benefits of Event Sourcing

1. **Complete audit trail** — every state change is recorded, forever.
2. **Temporal queries** — ask "what was the state at time T?"
3. **Event replay** — rebuild any projection from scratch.
4. **Debugging** — replay events to reproduce bugs.
5. **Business intelligence** — events are raw data for analytics.

### Costs of Event Sourcing

1. **Eventual consistency** — read models lag behind writes.
2. **Event schema evolution** — old events must remain compatible.
3. **Storage growth** — events accumulate indefinitely.
4. **Learning curve** — different mental model than CRUD.
5. **Rebuild time** — rebuilding large aggregates is slow without snapshots.

---

## Event Store

```c
typedef struct {
    uint64_t global_sequence;
} EventStore;

typedef struct {
    uint64_t    sequence;
    EntityId    aggregate_id;
    uint64_t    aggregate_version;
    char        event_type[CQRS_EVENT_TYPE_LEN];
    char        payload[CQRS_PAYLOAD_LEN];
    size_t      payload_size;
    char        metadata[CQRS_META_LEN];
    uint64_t    timestamp;
} EventStoreEvent;
```

### Event Stream

```c
typedef struct {
    EntityId         aggregate_id;
    uint64_t         current_version;
    EventStoreEvent *events;
    uint64_t         event_capacity;
    uint64_t         event_count;
} EventStream;
```

Each aggregate has its own event stream, identified by `aggregate_id`. Events
within a stream are ordered by `sequence` number. The `global_sequence` in the
EventStore provides a total order across all streams.

### Appending Events

```c
EventStore *event_store_create(void);
int         event_store_append(EventStore *store, EntityId aggregate_id,
                   EventStoreEvent *events, size_t count);
```

### Reading Events

```c
int event_store_read_stream(EventStore *store, EntityId aggregate_id,
        EventStoreEvent *out, size_t max, size_t *count);
int event_store_read_all(EventStore *store, uint64_t from_seq,
        EventStoreEvent *out, size_t max, size_t *count);
```

The `read_stream` method loads events for a single aggregate (used for
aggregate rehydration). The `read_all` method reads all events in global order
(used for building projections).

### Optimistic Concurrency

When appending to a stream, the expected `aggregate_version` must match the
current version in the store. If another process has appended events since the
last read, the append is rejected. This prevents lost updates without using locks.

---

## Projections and Read Models

A **Projection** is a read model derived from the event stream. It transforms
events into a shape optimized for queries.

```c
typedef struct {
    EntityId order_id;
    EntityId customer_id;
    OrderStatus status;
    double total_amount;
    char   currency[8];
    int    line_item_count;
    char   shipping_city[DDD_DESC_LEN / 4];
    uint64_t last_event_seq;
} OrderProjection;
```

### Building Projections

```c
void projection_apply_event(OrderProjection *proj, EventStoreEvent *event) {
    if (strcmp(event->event_type, "OrderPlaced") == 0) {
        // Parse event payload, update projection fields
        proj->status = ORDER_STATUS_PLACED;
        proj->last_event_seq = event->sequence;
    } else if (strcmp(event->event_type, "OrderShipped") == 0) {
        proj->status = ORDER_STATUS_SHIPPED;
        proj->last_event_seq = event->sequence;
    }
    // ... other event types
}
```

### Catch-up Subscription

A projection can subscribe to new events by tracking its `last_event_seq`.
On startup, it reads all events from `last_event_seq + 1` forward and applies
them. This is how read models stay eventually consistent with the write side.

### Multiple Projections

The same events can power many projections:
- **OrderDetailProjection** — full order view for the order detail page
- **OrderSummaryProjection** — summary for listing pages
- **CustomerOrderHistory** — orders grouped by customer
- **DailySalesReport** — aggregated sales data for dashboards
- **ShippingQueue** — orders ready to ship

Each projection can use its own database, its own schema, and its own indexing
strategy.

---

## Snapshots

A **Snapshot** is a point-in-time capture of an aggregate's full state. It
avoids replaying the entire event history for aggregates with long event streams.

```c
typedef struct {
    EntityId aggregate_id;
    uint64_t version;
    char     state[CQRS_PAYLOAD_LEN];
    size_t   state_size;
    uint64_t timestamp;
} Snapshot;
```

### Snapshot Strategy

1. Load the most recent snapshot.
2. Read events from snapshot version + 1 to current.
3. Apply only those events to reconstruct current state.
4. Periodically create new snapshots (every N events or on a schedule).

Without snapshots, an aggregate with 10,000 events would replay all 10,000
events on every load. With snapshots, it replays only events since the last
snapshot (potentially just 10-100 events).

### When to Snapshot

| Frequency | Pros | Cons |
|-----------|------|------|
| Every event | Fastest load | High write overhead |
| Every N events (e.g., 50) | Balance | Tuning required |
| On demand | Storage efficient | Loads can be slow |
| Never | No snapshot overhead | Slow loads for long-lived aggregates |

---

## Putting It All Together

### Complete Request Lifecycle

```
1. POST /orders { customer_id, line_items, shipping_address, billing_address }
   ↓
2. Command cmd = { .command_type = "PlaceOrder", .payload = {...} }
   ↓
3. command_bus_dispatch(bus, &cmd)
   ↓
4. Handler: validate input, load aggregate from event store
   ↓
5. Aggregate: order_aggregate_place(&agg, shipping, billing)
   ↓
6. Aggregate produces: [OrderPlaced, OrderLineAdded x2]
   ↓
7. event_store_append(store, aggregate_id, events, count)
   ↓
8. Projection updates: OrderProjection, OrderSummaryProjection
   ↓
9. Return success to caller
```

### Code Example

```c
#include "cqrs_es.h"

int main(void) {
    EventStore *store = event_store_create();
    CommandBus *cmd_bus = command_bus_create(store);
    command_bus_register_handler(cmd_bus, "PlaceOrder",
        command_place_order_handler, NULL);

    QueryBus *query_bus = query_bus_create();
    query_bus_register_handler(query_bus, "OrderDetail",
        query_order_detail_handler, NULL);

    EntityId order_id = entity_id_generate();

    Command cmd = {
        .command_type = "PlaceOrder",
        .target_aggregate_id = order_id,
        .payload = "{\"customer_id\":\"...\",\"amount\":199.99}",
        .payload_size = 42
    };

    command_bus_dispatch(cmd_bus, &cmd);

    OrderProjection projection;
    size_t out_size;
    query_bus_query(query_bus, &order_id, "OrderDetail",
        &projection, sizeof(projection), &out_size);

    command_bus_destroy(cmd_bus);
    query_bus_destroy(query_bus);
    event_store_destroy(store);
    return 0;
}
```

---

## Trade-offs and When to Use

### DDD is a good fit when

- The domain is complex with many business rules.
- Domain experts and developers need a shared language.
- The business logic changes frequently.
- You need to model real-world processes.

### DDD may be overkill when

- The application is mostly CRUD with simple validation.
- There is no domain expert to collaborate with.
- The domain is well-understood and stable.
- You are building a technical service (logging, monitoring, file storage).

### CQRS is a good fit when

- Read and write workloads have very different shapes.
- Read models need to be optimized differently from write models.
- You have high read-to-write ratios with complex queries.
- You need independent scalability of reads and writes.

### CQRS may be overkill when

- The application has simple CRUD views of the same data.
- Reads and writes share the same shape.
- You don't need separate optimization for reads.
- The added complexity outweighs the benefits.

### Event Sourcing is a good fit when

- You need a complete audit trail for compliance.
- You need temporal queries (what was the state at time T?).
- You need to be able to rebuild state from events.
- The domain is naturally event-driven.

### Event Sourcing may be overkill when

- The domain is mostly CRUD.
- You don't need audit trails or temporal queries.
- Storage costs are a concern.
- Your team is not familiar with the pattern.

### Decision Matrix

| Pattern | Complexity | Audit Trail | Query Flexibility | Learning Curve |
|---------|-----------|-------------|-------------------|---------------|
| CRUD | Low | No | Limited | Low |
| DDD only | Medium | No | Limited | Medium |
| CQRS only | Medium | No | High | Medium |
| DDD + CQRS | High | No | High | High |
| DDD + CQRS + ES | Very High | Yes | Very High | Very High |

---

## References

- Evans, Eric. *Domain-Driven Design: Tackling Complexity in the Heart of Software*. Addison-Wesley, 2003.
- Vernon, Vaughn. *Implementing Domain-Driven Design*. Addison-Wesley, 2013.
- Young, Greg. *CQRS and Event Sourcing*. CodeBetter, 2010.
- Fowler, Martin. *CQRS*. martinfowler.com, 2011.
- `mini-business-arch/ddd_model.h` — full API reference
- `mini-business-arch/cqrs_es.h` — command/query bus and event store API
- `mini-business-arch/docs/API.md` — complete function reference
- `mini-business-arch/docs/ARCHITECTURE.md` — architecture diagrams and module overview
