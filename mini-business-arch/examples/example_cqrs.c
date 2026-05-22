#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cqrs_es.h"

int main(void) {
    printf("=== CQRS + Event Sourcing Example ===\n\n");

    EventStore *store = event_store_create();
    printf("EventStore created (global_seq=%llu)\n",
        (unsigned long long)store->global_sequence);

    CommandBus *cmd_bus = command_bus_create(store);
    command_bus_register_handler(cmd_bus, "PlaceOrder",
        command_place_order_handler, NULL);
    command_bus_register_handler(cmd_bus, "CancelOrder",
        command_cancel_order_handler, NULL);
    command_bus_register_handler(cmd_bus, "ShipOrder",
        command_ship_order_handler, NULL);
    printf("CommandBus registered 3 handlers\n");

    QueryBus *query_bus = query_bus_create();
    query_bus_register_handler(query_bus, "OrderDetail",
        query_order_detail_handler, NULL);
    query_bus_register_handler(query_bus, "OrdersByCustomer",
        query_orders_by_customer_handler, NULL);
    printf("QueryBus registered 2 handlers\n");

    EntityId order_id = entity_id_generate();

    Command place_cmd;
    memset(&place_cmd, 0, sizeof(place_cmd));
    strncpy(place_cmd.command_type, "PlaceOrder", DDD_NAME_LEN);
    place_cmd.target_aggregate_id = order_id;
    snprintf(place_cmd.payload, CQRS_PAYLOAD_LEN,
        "{\"customer_id\":\"CUST-001\",\"total\":99.99,\"currency\":\"USD\"}");

    size_t event_count = 0;
    int rc = command_bus_dispatch(cmd_bus, &place_cmd, &event_count);
    printf("PlaceOrder dispatched: rc=%d events=%zu\n", rc, event_count);

    Query detail_query;
    memset(&detail_query, 0, sizeof(detail_query));
    strncpy(detail_query.query_type, "OrderDetail", DDD_NAME_LEN);
    snprintf(detail_query.payload, CQRS_PAYLOAD_LEN, "%s",
        entity_id_string(order_id));

    QueryResult qr = query_bus_dispatch(query_bus, &detail_query);
    printf("OrderDetail query result: %s\n",
        qr.data ? (char *)qr.data : "NULL");
    free(qr.data);

    OrderListView *view = order_list_view_create();
    ProjectionEngine *proj = projection_engine_create(store);
    projection_engine_register(proj, "OrderListView",
        order_list_view_projection, view);
    printf("ProjectionEngine set up with OrderListView\n");

    EventStoreEvent sample_ev = event_make_json("OrderCreated", order_id, 1,
        "{\"order_id\":\"sample\",\"amount\":150.00}");
    projection_engine_replay_events(proj, order_id);
    printf("Replay events completed\n");

    order_list_view_projection(view, &sample_ev);

    const OrderReadModel *found = order_list_view_find(view, order_id);
    printf("Read model lookup by order_id: %s\n", found ? "FOUND" : "NULL");

    projection_engine_destroy(proj);
    order_list_view_destroy(view);
    query_bus_destroy(query_bus);
    command_bus_destroy(cmd_bus);
    event_store_destroy(store);

    printf("\n=== CQRS+ES Example Complete ===\n");
    return 0;
}
