#include "cqrs_es.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

EventStoreEvent event_make(const char *event_type, EntityId aggregate_id,
    uint64_t version, const char *payload, size_t payload_size) {
    EventStoreEvent e;
    memset(&e, 0, sizeof(e));
    e.sequence = 0;
    e.aggregate_id = aggregate_id;
    e.aggregate_version = version;
    strncpy(e.event_type, event_type, CQRS_EVENT_TYPE_LEN);
    e.event_type[CQRS_EVENT_TYPE_LEN - 1] = '\0';
    e.payload_size = payload_size < CQRS_PAYLOAD_LEN
        ? payload_size : CQRS_PAYLOAD_LEN - 1;
    memcpy(e.payload, payload, e.payload_size);
    e.payload[e.payload_size] = '\0';
    e.timestamp = (uint64_t)time(NULL);
    return e;
}

EventStoreEvent event_make_json(const char *event_type, EntityId aggregate_id,
    uint64_t version, const char *json_payload) {
    return event_make(event_type, aggregate_id, version,
        json_payload, strlen(json_payload));
}

const char *event_type_string(EventStoreEvent *e) {
    return e->event_type;
}

const char *event_payload_string(EventStoreEvent *e) {
    return e->payload;
}

uint64_t event_sequence(EventStoreEvent *e) {
    return e->sequence;
}

bool event_is_type(EventStoreEvent *e, const char *type) {
    return strcmp(e->event_type, type) == 0;
}

EventStore *event_store_create(void) {
    EventStore *store = (EventStore *)malloc(sizeof(EventStore));
    store->global_sequence = 0;
    return store;
}

void event_store_destroy(EventStore *store) {
    free(store);
}

EventStoreEvent *event_store_append(EventStore *store, EntityId aggregate_id,
    EventStoreEvent *events, size_t count) {
    (void)store;
    (void)aggregate_id;
    for (size_t i = 0; i < count; i++) {
        events[i].sequence = ++store->global_sequence;
    }
    return events;
}

EventStream *event_store_load_stream(EventStore *store, EntityId aggregate_id) {
    (void)store;
    (void)aggregate_id;
    EventStream *stream = (EventStream *)malloc(sizeof(EventStream));
    stream->aggregate_id = aggregate_id;
    stream->current_version = 0;
    stream->event_capacity = 16;
    stream->events = (EventStoreEvent *)malloc(
        stream->event_capacity * sizeof(EventStoreEvent));
    stream->event_count = 0;
    return stream;
}

EventStream *event_store_load_since(EventStore *store, EntityId aggregate_id,
    uint64_t since_version) {
    (void)store;
    (void)aggregate_id;
    (void)since_version;
    return event_store_load_stream(store, aggregate_id);
}

EventStoreEvent *event_store_get_events_since(EventStore *store,
    uint64_t since_global_sequence, size_t *out_count) {
    (void)store;
    (void)since_global_sequence;
    *out_count = 0;
    return NULL;
}

void event_stream_destroy(EventStream *stream) {
    free(stream->events);
    free(stream);
}

CommandBus *command_bus_create(EventStore *store) {
    CommandBus *bus = (CommandBus *)malloc(sizeof(CommandBus));
    bus->handler_capacity = 8;
    bus->handler_count = 0;
    bus->handlers = (CommandHandler *)malloc(
        bus->handler_capacity * sizeof(CommandHandler));
    bus->event_store = store;
    return bus;
}

void command_bus_destroy(CommandBus *bus) {
    free(bus->handlers);
    free(bus);
}

int command_bus_register_handler(CommandBus *bus, const char *command_type,
    int (*handler)(void *, Command *, EventStoreEvent *, size_t, size_t *),
    void *context) {
    if (bus->handler_count >= bus->handler_capacity) {
        bus->handler_capacity *= 2;
        bus->handlers = (CommandHandler *)realloc(bus->handlers,
            bus->handler_capacity * sizeof(CommandHandler));
    }
    CommandHandler *h = &bus->handlers[bus->handler_count];
    strncpy(h->command_type, command_type, DDD_NAME_LEN);
    h->command_type[DDD_NAME_LEN - 1] = '\0';
    h->handler = handler;
    h->context = context;
    bus->handler_count++;
    return 0;
}

int command_bus_dispatch(CommandBus *bus, Command *cmd,
    size_t *out_event_count) {
    for (size_t i = 0; i < bus->handler_count; i++) {
        if (strcmp(bus->handlers[i].command_type, cmd->command_type) == 0) {
            EventStoreEvent events[16];
            size_t count = 0;
            int result = bus->handlers[i].handler(
                bus->handlers[i].context, cmd, events, 16, &count);
            if (result == 0 && count > 0) {
                event_store_append(bus->event_store, cmd->target_aggregate_id,
                    events, count);
            }
            if (out_event_count) *out_event_count = count;
            return result;
        }
    }
    return -1;
}

QueryBus *query_bus_create(void) {
    QueryBus *bus = (QueryBus *)malloc(sizeof(QueryBus));
    bus->handler_capacity = 8;
    bus->handler_count = 0;
    bus->handlers = (QueryHandler *)malloc(
        bus->handler_capacity * sizeof(QueryHandler));
    return bus;
}

void query_bus_destroy(QueryBus *bus) {
    free(bus->handlers);
    free(bus);
}

int query_bus_register_handler(QueryBus *bus, const char *query_type,
    QueryResult (*handler)(void *, Query *), void *context) {
    if (bus->handler_count >= bus->handler_capacity) {
        bus->handler_capacity *= 2;
        bus->handlers = (QueryHandler *)realloc(bus->handlers,
            bus->handler_capacity * sizeof(QueryHandler));
    }
    QueryHandler *h = &bus->handlers[bus->handler_count];
    strncpy(h->query_type, query_type, DDD_NAME_LEN);
    h->query_type[DDD_NAME_LEN - 1] = '\0';
    h->handler = handler;
    h->context = context;
    bus->handler_count++;
    return 0;
}

QueryResult query_bus_dispatch(QueryBus *bus, Query *q) {
    QueryResult empty = {NULL, 0};
    for (size_t i = 0; i < bus->handler_count; i++) {
        if (strcmp(bus->handlers[i].query_type, q->query_type) == 0) {
            return bus->handlers[i].handler(bus->handlers[i].context, q);
        }
    }
    return empty;
}

ProjectionEngine *projection_engine_create(EventStore *store) {
    ProjectionEngine *engine = (ProjectionEngine *)malloc(
        sizeof(ProjectionEngine));
    engine->projection_capacity = 8;
    engine->projection_count = 0;
    engine->projections = (Projection *)malloc(
        engine->projection_capacity * sizeof(Projection));
    engine->event_store = store;
    engine->last_sequence_processed = 0;
    return engine;
}

void projection_engine_destroy(ProjectionEngine *engine) {
    free(engine->projections);
    free(engine);
}

int projection_engine_register(ProjectionEngine *engine, const char *name,
    int (*handler)(void *, EventStoreEvent *), void *context) {
    if (engine->projection_count >= engine->projection_capacity) {
        engine->projection_capacity *= 2;
        engine->projections = (Projection *)realloc(engine->projections,
            engine->projection_capacity * sizeof(Projection));
    }
    Projection *p = &engine->projections[engine->projection_count];
    strncpy(p->projection_name, name, DDD_NAME_LEN);
    p->projection_name[DDD_NAME_LEN - 1] = '\0';
    p->event_handler = handler;
    p->context = context;
    p->is_active = true;
    engine->projection_count++;
    return 0;
}

int projection_engine_replay_events(ProjectionEngine *engine,
    EntityId aggregate_id) {
    EventStream *stream = event_store_load_stream(
        engine->event_store, aggregate_id);
    for (uint64_t i = 0; i < stream->event_count; i++) {
        for (size_t j = 0; j < engine->projection_count; j++) {
            if (engine->projections[j].is_active) {
                engine->projections[j].event_handler(
                    engine->projections[j].context, &stream->events[i]);
            }
        }
    }
    event_stream_destroy(stream);
    return 0;
}

int projection_engine_poll(ProjectionEngine *engine) {
    size_t count = 0;
    EventStoreEvent *events = event_store_get_events_since(
        engine->event_store, engine->last_sequence_processed, &count);
    if (events && count > 0) {
        for (size_t i = 0; i < count; i++) {
            for (size_t j = 0; j < engine->projection_count; j++) {
                if (engine->projections[j].is_active) {
                    engine->projections[j].event_handler(
                        engine->projections[j].context, &events[i]);
                }
            }
            engine->last_sequence_processed = events[i].sequence;
        }
    }
    return (int)count;
}

OrderListView *order_list_view_create(void) {
    OrderListView *view = (OrderListView *)malloc(sizeof(OrderListView));
    view->capacity = 8;
    view->count = 0;
    view->orders = (OrderReadModel *)malloc(
        view->capacity * sizeof(OrderReadModel));
    return view;
}

void order_list_view_destroy(OrderListView *view) {
    free(view->orders);
    free(view);
}

int order_list_view_projection(void *ctx, EventStoreEvent *event) {
    OrderListView *view = (OrderListView *)ctx;
    if (!event_is_type(event, "OrderCreated")) return 0;
    if (view->count >= view->capacity) {
        view->capacity *= 2;
        view->orders = (OrderReadModel *)realloc(view->orders,
            view->capacity * sizeof(OrderReadModel));
    }
    OrderReadModel *m = &view->orders[view->count];
    memset(m, 0, sizeof(OrderReadModel));
    m->order_id = event->aggregate_id;
    m->status = ORDER_STATUS_PLACED;
    m->created_at = event->timestamp;
    m->updated_at = event->timestamp;
    view->count++;
    return 0;
}

const OrderReadModel *order_list_view_find(OrderListView *view, EntityId id) {
    for (size_t i = 0; i < view->count; i++) {
        if (entity_id_equals(view->orders[i].order_id, id)) {
            return &view->orders[i];
        }
    }
    return NULL;
}

int command_place_order_handler(void *ctx, Command *cmd,
    EventStoreEvent *out, size_t max, size_t *cnt) {
    (void)ctx;
    (void)max;
    EventStoreEvent ev = event_make_json("OrderPlaced",
        cmd->target_aggregate_id, 1, cmd->payload);
    out[0] = ev;
    *cnt = 1;
    return 0;
}

int command_cancel_order_handler(void *ctx, Command *cmd,
    EventStoreEvent *out, size_t max, size_t *cnt) {
    (void)ctx;
    (void)max;
    EventStoreEvent ev = event_make_json("OrderCancelled",
        cmd->target_aggregate_id, 1, cmd->payload);
    out[0] = ev;
    *cnt = 1;
    return 0;
}

int command_ship_order_handler(void *ctx, Command *cmd,
    EventStoreEvent *out, size_t max, size_t *cnt) {
    (void)ctx;
    (void)max;
    EventStoreEvent ev = event_make_json("OrderShipped",
        cmd->target_aggregate_id, 2, cmd->payload);
    out[0] = ev;
    *cnt = 1;
    return 0;
}

QueryResult query_order_detail_handler(void *ctx, Query *q) {
    (void)ctx;
    QueryResult result;
    result.data = malloc(CQRS_PAYLOAD_LEN);
    snprintf((char *)result.data, CQRS_PAYLOAD_LEN,
        "{\"order_id\":\"%s\",\"status\":\"PLACED\",\"total\":99.99}",
        q->payload);
    result.data_size = strlen((char *)result.data) + 1;
    return result;
}

QueryResult query_orders_by_customer_handler(void *ctx, Query *q) {
    (void)ctx;
    QueryResult result;
    result.data = malloc(CQRS_PAYLOAD_LEN);
    snprintf((char *)result.data, CQRS_PAYLOAD_LEN,
        "{\"customer_id\":\"%s\",\"orders\":[]}", q->payload);
    result.data_size = strlen((char *)result.data) + 1;
    return result;
}

int event_sourced_rebuild(void *aggregate, EventStore *store,
    EntityId aggregate_id, int (*apply)(void *, EventStoreEvent *)) {
    EventStream *stream = event_store_load_stream(store, aggregate_id);
    for (uint64_t i = 0; i < stream->event_count; i++) {
        apply(aggregate, &stream->events[i]);
    }
    event_stream_destroy(stream);
    return 0;
}

int event_sourced_save_snapshot(EventSourcedSnapshot *snap,
    EntityId aggregate_id, void *data, size_t size, uint64_t version) {
    snap->aggregate_id = aggregate_id;
    snap->snapshot_data = malloc(size);
    memcpy(snap->snapshot_data, data, size);
    snap->snapshot_size = size;
    snap->version = version;
    return 0;
}

int event_sourced_load_from_snapshot(void *aggregate, EventStore *store,
    EntityId aggregate_id, EventSourcedSnapshot *snap,
    snapshot_applier apply_snapshot,
    int (*apply_event)(void *, EventStoreEvent *)) {
    apply_snapshot(aggregate, snap);
    EventStream *stream = event_store_load_since(
        store, aggregate_id, snap->version + 1);
    for (uint64_t i = 0; i < stream->event_count; i++) {
        apply_event(aggregate, &stream->events[i]);
    }
    event_stream_destroy(stream);
    return 0;
}
