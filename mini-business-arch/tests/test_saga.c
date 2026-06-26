#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "saga_orch.h"

static int failures = 0;
#define TEST(name) printf("  %-55s", name)
#define CHECK(cond) do { \
    if (!(cond)) { printf(" FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
    else printf(" PASS\n"); \
} while(0)

static void test_saga_step(void) {
    TEST("saga_step_create initializes correctly");
    SagaStep step = saga_step_create("TestStep", NULL, NULL, NULL, 3);
    CHECK(strcmp(step.step_name, "TestStep") == 0);
    CHECK(step.status == SAGA_STEP_PENDING);
    CHECK(step.max_retries == 3);

    TEST("saga_step_set_status updates status");
    saga_step_set_status(&step, SAGA_STEP_COMPLETED);
    CHECK(step.status == SAGA_STEP_COMPLETED);

    TEST("saga_step_status_string returns correct strings");
    CHECK(strcmp(saga_step_status_string(SAGA_STEP_PENDING), "PENDING") == 0);
    CHECK(strcmp(saga_step_status_string(SAGA_STEP_COMPLETED), "COMPLETED") == 0);
    CHECK(strcmp(saga_step_status_string(SAGA_STEP_FAILED), "FAILED") == 0);

    TEST("saga_step_can_retry before retries");
    step.retry_count = 0;
    CHECK(saga_step_can_retry(&step));

    TEST("saga_step_can_retry after max retries");
    step.retry_count = 3;
    CHECK(!saga_step_can_retry(&step));
}

static int test_exec_fn(void *ctx, void *data) {
    (void)ctx;
    (void)data;
    return 0;
}

static int test_comp_fn(void *ctx, void *data) {
    (void)ctx;
    (void)data;
    return 0;
}

static void test_saga_orchestration(void) {
    TEST("saga_orch_create creates saga instance");
    SagaInstance saga = saga_orch_create("TestSaga", true);
    CHECK(strcmp(saga.saga_name, "TestSaga") == 0);
    CHECK(saga.status == SAGA_STATUS_RUNNING);
    CHECK(saga.is_orchestrated);

    TEST("saga_orch_add_step adds step");
    SagaStep step = saga_step_create("Step1", test_exec_fn,
        test_comp_fn, NULL, 2);
    int rc = saga_orch_add_step(&saga, step);
    CHECK(rc == 0);
    CHECK(saga.step_count == 1);

    TEST("saga_orch_add_steps adds multiple steps");
    SagaStep steps[2];
    steps[0] = saga_step_create("Step2", test_exec_fn, test_comp_fn, NULL, 1);
    steps[1] = saga_step_create("Step3", test_exec_fn, test_comp_fn, NULL, 1);
    rc = saga_orch_add_steps(&saga, steps, 2);
    CHECK(rc == 0);
    CHECK(saga.step_count == 3);

    TEST("saga_orch_execute runs all steps successfully");
    SagaInstance saga2 = saga_orch_create("SuccessSaga", true);
    SagaStep s1 = saga_step_create("S1", test_exec_fn, test_comp_fn, NULL, 1);
    SagaStep s2 = saga_step_create("S2", test_exec_fn, test_comp_fn, NULL, 1);
    saga_orch_add_step(&saga2, s1);
    saga_orch_add_step(&saga2, s2);
    rc = saga_orch_execute(&saga2);
    CHECK(rc == 0);
    CHECK(saga2.status == SAGA_STATUS_COMPLETED);

    TEST("saga_status_string returns correct strings");
    CHECK(strcmp(saga_status_string(SAGA_STATUS_RUNNING), "RUNNING") == 0);
    CHECK(strcmp(saga_status_string(SAGA_STATUS_COMPLETED), "COMPLETED") == 0);
    CHECK(strcmp(saga_status_string(SAGA_STATUS_FAILED), "FAILED") == 0);

    TEST("saga_is_terminal for completed saga");
    saga2.status = SAGA_STATUS_COMPLETED;
    CHECK(saga_is_terminal(&saga2));
}

static void test_saga_orchestrator(void) {
    TEST("saga_orchestrator_create initializes");
    SagaOrchestrator *orch = saga_orchestrator_create();
    CHECK(orch != NULL);

    TEST("saga_orchestrator_enqueue adds saga");
    SagaInstance saga = saga_orch_create("Saga1", true);
    SagaStep step = saga_step_create("S1", test_exec_fn, test_comp_fn, NULL, 1);
    saga_orch_add_step(&saga, step);
    int rc = saga_orchestrator_enqueue(orch, saga);
    CHECK(rc == 0);
    CHECK(orch->count == 1);

    TEST("saga_orchestrator_tick processes sagas");
    rc = saga_orchestrator_tick(orch);
    CHECK(rc == 0);

    TEST("saga_orchestrator_find finds by ID");
    SagaInstance *found = saga_orchestrator_find(orch, saga.saga_id);
    CHECK(found != NULL);

    TEST("saga_orchestrator_find returns NULL for unknown ID");
    found = saga_orchestrator_find(orch, "INVALID_ID");
    CHECK(found == NULL);

    saga_orchestrator_destroy(orch);
}

static void test_choreography(void) {
    TEST("choreography_bus_create initializes");
    ChoreographyBus *bus = choreography_bus_create();
    CHECK(bus != NULL);

    TEST("choreography_bus_register_service adds service");
    ChoreographyService svc;
    memset(&svc, 0, sizeof(svc));
    strcpy(svc.service_name, "InventoryService");
    int rc = choreography_bus_register_service(bus, svc);
    CHECK(rc == 0);

    TEST("choreography_bus_publish adds event");
    ChoreographyEvent ev = ch_event_started("SAGA-1", "ReserveInv");
    rc = choreography_bus_publish(bus, ev);
    CHECK(rc == 0);
    CHECK(bus->event_log_count == 1);

    TEST("ch_event_type_string returns correct strings");
    CHECK(strlen(ch_event_type_string(CHOREO_EVENT_TYPE_STARTED)) > 0);
    CHECK(strlen(ch_event_type_string(CHOREO_EVENT_TYPE_STEP_COMPLETED)) > 0);

    TEST("choreography_bus_find_service finds service");
    ChoreographyService *found = choreography_bus_find_service(bus, "InventoryService");
    CHECK(found != NULL);

    TEST("choreography_bus_find_service returns NULL for unknown");
    found = choreography_bus_find_service(bus, "UnknownService");
    CHECK(found == NULL);

    choreography_bus_destroy(bus);
}

static void test_create_order_saga(void) {
    TEST("saga_create_order_saga creates complete saga");
    EntityId oid = entity_id_generate();
    EntityId cid = entity_id_generate();
    SagaInstance saga = saga_create_order_saga(oid, cid, 99.99, "USD", true);
    CHECK(saga.step_count == 4);
    CHECK(saga.status == SAGA_STATUS_RUNNING);

    OrderSagaData *data = (OrderSagaData *)saga.saga_data;
    CHECK(entity_id_equals(data->order_id, oid));
    CHECK(entity_id_equals(data->customer_id, cid));
    CHECK(data->amount == 99.99);
}

int main(void) {
    printf("=== Test: Saga Orchestration/Choreography ===\n\n");
    test_saga_step();
    test_saga_orchestration();
    test_saga_orchestrator();
    test_choreography();
    test_create_order_saga();
    printf("\nResult: %d failures\n", failures);
    return failures;
}
