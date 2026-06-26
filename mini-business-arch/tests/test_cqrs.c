#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "cqrs_es.h"

static int failures = 0;
#define TEST(name) printf("  %-55s", name)
#define CHECK(cond) do { \
    if (!(cond)) { printf(" FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
    else printf(" PASS\n"); \
} while(0)

static void test_event_basics(void) {
    TEST("event_make creates event with correct fields");
    EntityId aid = entity_id_generate();
    EventStoreEvent ev = event_make("OrderPlaced", aid, 1,
        "test_payload", 13);
    CHECK(event_is_type(&ev, "OrderPlaced"));
    CHECK(entity_id_equals(ev.aggregate_id, aid));
    CHECK(ev.aggregate_version == 1);
    CHECK(strcmp(event_type_string(&ev), "OrderPlaced") == 0);
    CHECK(strcmp(event_payload_string(&ev), "test_payload") == 0);

    TEST("event_make_json creates from JSON string");
    EventStoreEvent ev2 = event_make_json("OrderCreated", aid, 2,
        "{\"amount\":100}");
    CHECK(event_is_type(&ev2, "OrderCreated"));
    CHECK(ev2.aggregate_version == 2);

    TEST("event_is_type returns false for mismatched type");
    CHECK(!event_is_type(&ev, "OrderCancelled"));
}

static void test_event_store(void) {
    TEST("event_store_create initializes properly");
    EventStore *store = event_store_create();
    CHECK(store != NULL);

    TEST("event_store_append increments global sequence");
    EntityId aid = entity_id_generate();
    EventStoreEvent events[3];
    events[0] = event_make("Type1", aid, 1, "p1", 2);
    events[1] = event_make("Type2", aid, 2, "p2", 2);
    events[2] = event_make("Type3", aid, 3, "p3", 2);
    EventStoreEvent *appended = event_store_append(store, aid, events, 3);
    CHECK(appended != NULL);
    CHECK(events[0].sequence >= 1);
    CHECK(events[1].sequence > events[0].sequence);

    TEST("event_store_load_stream returns stream");
    EventStream *stream = event_store_load_stream(store, aid);
    CHECK(stream != NULL);
    CHECK(entity_id_equals(stream->aggregate_id, aid));
    event_stream_destroy(stream);

    TEST("event_store_load_since returns stream");
    EventStream *stream2 = event_store_load_since(store, aid, 1);
    CHECK(stream2 != NULL);
    event_stream_destroy(stream2);

    event_store_destroy(store);
}

static void test_command_bus(void) {
    TEST("command_bus_create initializes");
    EventStore *store = event_store_create();
    CommandBus *bus = command_bus_create(store);
    CHECK(bus != NULL);

    TEST("command_bus_register_handler registers handler");
    int rc = command_bus_register_handler(bus, "PlaceOrder",
        command_place_order_handler, NULL);
    CHECK(rc == 0);
    CHECK(bus->handler_count == 1);

    TEST("command_bus_dispatch invokes handler");
    Command cmd;
    memset(&cmd, 0, sizeof(cmd));
    strcpy(cmd.command_type, "PlaceOrder");
    cmd.target_aggregate_id = entity_id_generate();
    strcpy(cmd.payload, "{\"test\":true}");
    size_t event_count = 0;
    rc = command_bus_dispatch(bus, &cmd, &event_count);
    CHECK(rc == 0);
    CHECK(event_count == 1);

    TEST("command_bus_dispatch unknown command returns -1");
    Command bad_cmd;
    memset(&bad_cmd, 0, sizeof(bad_cmd));
    strcpy(bad_cmd.command_type, "UnknownCommand");
    rc = command_bus_dispatch(bus, &bad_cmd, NULL);
    CHECK(rc == -1);

    command_bus_destroy(bus);
    event_store_destroy(store);
}

static void test_query_bus(void) {
    TEST("query_bus_create initializes");
    QueryBus *bus = query_bus_create();
    CHECK(bus != NULL);

    TEST("query_bus_register_handler registers handler");
    int rc = query_bus_register_handler(bus, "OrderDetail",
        query_order_detail_handler, NULL);
    CHECK(rc == 0);

    TEST("query_bus_dispatch returns result");
    Query q;
    memset(&q, 0, sizeof(q));
    strcpy(q.query_type, "OrderDetail");
    strcpy(q.payload, "TEST-ORDER");
    QueryResult result = query_bus_dispatch(bus, &q);
    CHECK(result.data != NULL);
    free(result.data);

    query_bus_destroy(bus);
}

static void test_projection_engine(void) {
    TEST("projection_engine_create initializes");
    EventStore *store = event_store_create();
    ProjectionEngine *engine = projection_engine_create(store);
    CHECK(engine != NULL);

    TEST("projection_engine_register adds projection");
    OrderListView *view = order_list_view_create();
    int rc = projection_engine_register(engine, "OrderListView",
        order_list_view_projection, view);
    CHECK(rc == 0);

    TEST("projection_engine_replay processes events");
    EntityId aid = entity_id_generate();
    EventStoreEvent ev = event_make_json("OrderCreated", aid, 1,
        "{\"order_id\":\"test\"}");
    event_store_append(store, aid, &ev, 1);
    rc = projection_engine_replay_events(engine, aid);
    CHECK(rc == 0);

    projection_engine_destroy(engine);
    order_list_view_destroy(view);
    event_store_destroy(store);
}

static void test_event_sourced_rebuild(void) {
    TEST("event_sourced_rebuild applies events");
    EventStore *store = event_store_create();
    OrderAggregate dummy;
    memset(&dummy, 0, sizeof(dummy));

    static int apply_count = 0;
    int apply_cb(void *agg, EventStoreEvent *ev) {
        (void)agg; (void)ev;
        apply_count++;
        return 0;
    }

    apply_count = 0;
    int rc = event_sourced_rebuild(&dummy, store,
        entity_id_generate(), apply_cb);
    CHECK(rc == 0);

    event_store_destroy(store);
}

int main(void) {
    printf("=== Test: CQRS + Event Sourcing ===\n\n");
    test_event_basics();
    test_event_store();
    test_command_bus();
    test_query_bus();
    test_projection_engine();
    test_event_sourced_rebuild();
    printf("\nResult: %d failures\n", failures);
    return failures;
}
