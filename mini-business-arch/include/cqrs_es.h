#ifndef CQRS_ES_H
#define CQRS_ES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "ddd_model.h"

#define CQRS_EVENT_TYPE_LEN 128
#define CQRS_PAYLOAD_LEN   4096
#define CQRS_STREAM_LEN     128
#define CQRS_META_LEN       256

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

typedef struct {
    EntityId         aggregate_id;
    uint64_t         current_version;
    EventStoreEvent *events;
    uint64_t         event_capacity;
    uint64_t         event_count;
} EventStream;

typedef struct {
    uint64_t global_sequence;
} EventStore;

typedef struct {
    char    command_type[DDD_NAME_LEN];
    EntityId target_aggregate_id;
    char    payload[CQRS_PAYLOAD_LEN];
    size_t  payload_size;
    char    metadata[CQRS_META_LEN];
} Command;

typedef struct {
    char    command_type[DDD_NAME_LEN];
    int     (*handler)(void *ctx, Command *cmd, EventStoreEvent *out_events,
                       size_t max_events, size_t *out_count);
    void    *context;
} CommandHandler;

typedef struct {
    CommandHandler *handlers;
    size_t          handler_capacity;
    size_t          handler_count;
    EventStore     *event_store;
} CommandBus;

typedef struct {
    char    query_type[DDD_NAME_LEN];
    char    payload[CQRS_PAYLOAD_LEN];
    size_t  payload_size;
} Query;

typedef struct {
    void   *data;
    size_t  data_size;
} QueryResult;

typedef struct {
    char        query_type[DDD_NAME_LEN];
    QueryResult (*handler)(void *ctx, Query *q);
    void        *context;
} QueryHandler;

typedef struct {
    QueryHandler *handlers;
    size_t        handler_capacity;
    size_t        handler_count;
} QueryBus;

typedef struct {
    char    projection_name[DDD_NAME_LEN];
    int     (*event_handler)(void *ctx, EventStoreEvent *event);
    void    *context;
    bool     is_active;
} Projection;

typedef struct {
    Projection *projections;
    size_t      projection_capacity;
    size_t      projection_count;
    EventStore *event_store;
    uint64_t    last_sequence_processed;
} ProjectionEngine;

EventStoreEvent event_make(const char *event_type, EntityId aggregate_id,
    uint64_t version, const char *payload, size_t payload_size);
EventStoreEvent event_make_json(const char *event_type, EntityId aggregate_id,
    uint64_t version, const char *json_payload);
const char     *event_type_string(EventStoreEvent *e);
const char     *event_payload_string(EventStoreEvent *e);
uint64_t         event_sequence(EventStoreEvent *e);
bool             event_is_type(EventStoreEvent *e, const char *type);

EventStore      *event_store_create(void);
void             event_store_destroy(EventStore *store);
EventStoreEvent *event_store_append(EventStore *store, EntityId aggregate_id,
    EventStoreEvent *events, size_t count);
EventStream     *event_store_load_stream(EventStore *store, EntityId aggregate_id);
EventStream     *event_store_load_since(EventStore *store, EntityId aggregate_id,
    uint64_t since_version);
EventStoreEvent *event_store_get_events_since(EventStore *store,
    uint64_t since_global_sequence, size_t *out_count);
void             event_stream_destroy(EventStream *stream);

CommandBus      *command_bus_create(EventStore *store);
void             command_bus_destroy(CommandBus *bus);
int              command_bus_register_handler(CommandBus *bus,
    const char *command_type, int (*handler)(void *, Command *,
    EventStoreEvent *, size_t, size_t *), void *context);
int              command_bus_dispatch(CommandBus *bus, Command *cmd,
    size_t *out_event_count);

QueryBus        *query_bus_create(void);
void             query_bus_destroy(QueryBus *bus);
int              query_bus_register_handler(QueryBus *bus,
    const char *query_type, QueryResult (*handler)(void *, Query *),
    void *context);
QueryResult      query_bus_dispatch(QueryBus *bus, Query *q);

ProjectionEngine *projection_engine_create(EventStore *store);
void              projection_engine_destroy(ProjectionEngine *engine);
int               projection_engine_register(ProjectionEngine *engine,
    const char *name, int (*handler)(void *, EventStoreEvent *),
    void *context);
int               projection_engine_replay_events(ProjectionEngine *engine,
    EntityId aggregate_id);
int               projection_engine_poll(ProjectionEngine *engine);

typedef struct {
    EntityId   order_id;
    EntityId   customer_id;
    double     total_amount;
    char       currency[8];
    OrderStatus status;
    uint64_t   line_item_count;
    uint64_t   created_at;
    uint64_t   updated_at;
} OrderReadModel;

typedef struct {
    OrderReadModel *orders;
    size_t           capacity;
    size_t           count;
} OrderListView;

OrderListView   *order_list_view_create(void);
void             order_list_view_destroy(OrderListView *view);
int              order_list_view_projection(void *ctx, EventStoreEvent *event);
const OrderReadModel *order_list_view_find(OrderListView *view, EntityId id);

int              command_place_order_handler(void *ctx, Command *cmd,
    EventStoreEvent *out, size_t max, size_t *cnt);
int              command_cancel_order_handler(void *ctx, Command *cmd,
    EventStoreEvent *out, size_t max, size_t *cnt);
int              command_ship_order_handler(void *ctx, Command *cmd,
    EventStoreEvent *out, size_t max, size_t *cnt);

QueryResult      query_order_detail_handler(void *ctx, Query *q);
QueryResult      query_orders_by_customer_handler(void *ctx, Query *q);

typedef struct {
    EntityId aggregate_id;
    void     *snapshot_data;
    size_t   snapshot_size;
    uint64_t version;
} EventSourcedSnapshot;

typedef int (*snapshot_applier)(void *aggregate, EventSourcedSnapshot *snap);

int event_sourced_rebuild(void *aggregate, EventStore *store,
    EntityId aggregate_id, int (*apply)(void *, EventStoreEvent *));
int event_sourced_save_snapshot(EventSourcedSnapshot *snap,
    EntityId aggregate_id, void *data, size_t size, uint64_t version);
int event_sourced_load_from_snapshot(void *aggregate, EventStore *store,
    EntityId aggregate_id, EventSourcedSnapshot *snap,
    snapshot_applier apply_snapshot,
    int (*apply_event)(void *, EventStoreEvent *));

#endif
