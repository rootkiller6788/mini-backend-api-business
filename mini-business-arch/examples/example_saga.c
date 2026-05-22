#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "saga_orch.h"

int main(void) {
    printf("=== Saga Pattern Example ===\n\n");

    printf("--- Orchestration-based Saga ---\n");
    EntityId order_id = entity_id_generate();
    EntityId customer_id = entity_id_generate();

    SagaInstance saga = saga_create_order_saga(
        order_id, customer_id, 199.99, "USD", true);
    printf("Saga created: id=%s name=%s steps=%d\n",
        saga.saga_id, saga.saga_name, saga.step_count);

    SagaOrchestrator *orch = saga_orchestrator_create();
    saga_orchestrator_enqueue(orch, saga);
    printf("Saga enqueued in orchestrator\n");

    saga_orchestrator_tick(orch);
    printf("Orchestrator tick processed, status=%s\n",
        saga_status_string(orch->active_sagas[0].status));

    SagaInstance *found = saga_orchestrator_find(orch, saga.saga_id);
    printf("Find saga by ID: %s\n", found ? "FOUND" : "NOT FOUND");

    saga_orchestrator_cancel(orch, saga.saga_id);
    SagaInstance *after_cancel = saga_orchestrator_find(orch, saga.saga_id);
    printf("After cancel: status=%s\n",
        saga_status_string(after_cancel->status));

    printf("\n--- Choreography-based Saga ---\n");
    SagaInstance choreo_saga = saga_create_order_saga(
        order_id, customer_id, 299.99, "EUR", false);

    ChoreographyBus *bus = choreography_bus_create();

    ChoreographyService inventory_svc;
    memset(&inventory_svc, 0, sizeof(inventory_svc));
    strncpy(inventory_svc.service_name, "InventoryService", SAGA_NAME_LEN);
    inventory_svc.on_event = NULL;

    ChoreographyService payment_svc;
    memset(&payment_svc, 0, sizeof(payment_svc));
    strncpy(payment_svc.service_name, "PaymentService", SAGA_NAME_LEN);
    payment_svc.on_event = NULL;

    choreography_bus_register_service(bus, inventory_svc);
    choreography_bus_register_service(bus, payment_svc);
    printf("Registered %zu services on choreography bus\n",
        bus->service_count);

    ChoreographyEvent ev1 = ch_event_started(choreo_saga.saga_id,
        "ReserveInventory");
    ev1.service_name[0] = '\0';
    choreography_bus_publish(bus, ev1);

    ChoreographyEvent ev2 = ch_event_completed(choreo_saga.saga_id,
        "ReserveInventory", "InventoryService");
    choreography_bus_publish(bus, ev2);

    ChoreographyEvent ev3 = ch_event_failed(choreo_saga.saga_id,
        "ProcessPayment", "PaymentService");
    choreography_bus_publish(bus, ev3);

    ChoreographyEvent ev4 = ch_event_compensation_needed(
        choreo_saga.saga_id, "ProcessPayment");
    choreography_bus_publish(bus, ev4);

    printf("Published %zu choreography events\n", bus->event_log_count);
    for (size_t i = 0; i < bus->event_log_count; i++) {
        printf("  [%zu] type=%s step=%s service=%s\n", i,
            ch_event_type_string(bus->event_log[i].event_type),
            bus->event_log[i].step_name,
            bus->event_log[i].service_name);
    }

    choreography_bus_dispatch_pending(bus);
    printf("Pending events dispatched\n");

    printf("\n--- Saga Step Status ---\n");
    SagaStep test_step = saga_step_create("TestStep", NULL, NULL, NULL, 3);
    printf("Step status: %s\n", saga_step_status_string(test_step.status));
    saga_step_set_status(&test_step, SAGA_STEP_COMPLETED);
    printf("After complete: %s\n", saga_step_status_string(test_step.status));
    printf("Can retry: %s\n", saga_step_can_retry(&test_step) ? "yes" : "no");

    choreography_bus_destroy(bus);
    saga_orchestrator_destroy(orch);

    printf("\n=== Saga Example Complete ===\n");
    return 0;
}
