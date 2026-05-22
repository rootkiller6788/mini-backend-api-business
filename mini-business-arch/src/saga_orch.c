#include "saga_orch.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

SagaStep saga_step_create(const char *name,
    int (*execute)(void *, void *), int (*compensate)(void *, void *),
    void *context, int max_retries) {
    SagaStep step;
    memset(&step, 0, sizeof(step));
    strncpy(step.step_name, name, SAGA_NAME_LEN);
    step.step_name[SAGA_NAME_LEN - 1] = '\0';
    step.execute = execute;
    step.compensate = compensate;
    step.context = context;
    step.status = SAGA_STEP_PENDING;
    step.retry_count = 0;
    step.max_retries = max_retries;
    return step;
}

void saga_step_set_status(SagaStep *step, SagaStepStatus status) {
    step->status = status;
}

const char *saga_step_status_string(SagaStepStatus status) {
    switch (status) {
        case SAGA_STEP_PENDING:      return "PENDING";
        case SAGA_STEP_EXECUTING:    return "EXECUTING";
        case SAGA_STEP_COMPLETED:    return "COMPLETED";
        case SAGA_STEP_COMPENSATING: return "COMPENSATING";
        case SAGA_STEP_COMPENSATED:  return "COMPENSATED";
        case SAGA_STEP_FAILED:       return "FAILED";
        default:                     return "UNKNOWN";
    }
}

bool saga_step_can_retry(SagaStep *step) {
    return step->retry_count < step->max_retries;
}

SagaInstance saga_orch_create(const char *name, bool orchestrated) {
    SagaInstance saga;
    memset(&saga, 0, sizeof(saga));
    snprintf(saga.saga_id, SAGA_ID_LEN, "SAGA-%010llu-%04x",
        (unsigned long long)time(NULL), (unsigned int)(rand() & 0xFFFF));
    strncpy(saga.saga_name, name, SAGA_NAME_LEN);
    saga.saga_name[SAGA_NAME_LEN - 1] = '\0';
    saga.step_count = 0;
    saga.current_step = 0;
    saga.status = SAGA_STATUS_RUNNING;
    saga.is_orchestrated = orchestrated;
    return saga;
}

int saga_orch_add_step(SagaInstance *saga, SagaStep step) {
    if (saga->step_count >= SAGA_MAX_STEPS) return -1;
    saga->steps[saga->step_count++] = step;
    return 0;
}

int saga_orch_add_steps(SagaInstance *saga, SagaStep *steps, int count) {
    for (int i = 0; i < count; i++) {
        if (saga_orch_add_step(saga, steps[i]) != 0) return -1;
    }
    return 0;
}

int saga_orch_execute(SagaInstance *saga) {
    for (int i = saga->current_step; i < saga->step_count; i++) {
        int result = saga_orch_execute_step(saga, i);
        if (result != 0) {
            saga_orch_compensate(saga, i);
            return result;
        }
    }
    saga->status = SAGA_STATUS_COMPLETED;
    return 0;
}

int saga_orch_execute_step(SagaInstance *saga, int step_index) {
    if (step_index < 0 || step_index >= saga->step_count) return -1;
    SagaStep *step = &saga->steps[step_index];
    step->status = SAGA_STEP_EXECUTING;
    int result = step->execute(step->context, saga->saga_data);
    if (result == 0) {
        step->status = SAGA_STEP_COMPLETED;
        saga->current_step = step_index + 1;
    } else {
        if (saga_step_can_retry(step)) {
            step->retry_count++;
            return saga_orch_execute_step(saga, step_index);
        }
        step->status = SAGA_STEP_FAILED;
        saga->status = SAGA_STATUS_FAILED;
    }
    return result;
}

int saga_orch_compensate(SagaInstance *saga, int failed_step_index) {
    saga->status = SAGA_STATUS_COMPENSATING;
    int start = failed_step_index;
    if (start >= saga->step_count) start = saga->step_count - 1;
    for (int i = start; i >= 0; i--) {
        SagaStep *step = &saga->steps[i];
        if (step->status == SAGA_STEP_COMPLETED && step->compensate) {
            step->status = SAGA_STEP_COMPENSATING;
            int result = step->compensate(step->context, saga->saga_data);
            if (result == 0) {
                step->status = SAGA_STEP_COMPENSATED;
            } else {
                step->retry_count++;
                if (saga_step_can_retry(step)) {
                    i++;
                }
            }
        }
    }
    saga->status = SAGA_STATUS_COMPENSATED;
    return 0;
}

int saga_orch_resume(SagaInstance *saga) {
    if (saga->status != SAGA_STATUS_RUNNING) return -1;
    return saga_orch_execute(saga);
}

const char *saga_status_string(SagaStatus status) {
    switch (status) {
        case SAGA_STATUS_RUNNING:       return "RUNNING";
        case SAGA_STATUS_COMPLETED:     return "COMPLETED";
        case SAGA_STATUS_COMPENSATING:  return "COMPENSATING";
        case SAGA_STATUS_COMPENSATED:   return "COMPENSATED";
        case SAGA_STATUS_FAILED:        return "FAILED";
        default:                        return "UNKNOWN";
    }
}

bool saga_is_terminal(SagaInstance *saga) {
    return saga->status == SAGA_STATUS_COMPLETED ||
           saga->status == SAGA_STATUS_COMPENSATED ||
           saga->status == SAGA_STATUS_FAILED;
}

SagaOrchestrator *saga_orchestrator_create(void) {
    SagaOrchestrator *orch = (SagaOrchestrator *)malloc(
        sizeof(SagaOrchestrator));
    orch->capacity = 8;
    orch->count = 0;
    orch->active_sagas = (SagaInstance *)malloc(
        orch->capacity * sizeof(SagaInstance));
    return orch;
}

void saga_orchestrator_destroy(SagaOrchestrator *orch) {
    free(orch->active_sagas);
    free(orch);
}

int saga_orchestrator_enqueue(SagaOrchestrator *orch, SagaInstance saga) {
    if (orch->count >= orch->capacity) {
        orch->capacity *= 2;
        orch->active_sagas = (SagaInstance *)realloc(orch->active_sagas,
            orch->capacity * sizeof(SagaInstance));
    }
    orch->active_sagas[orch->count++] = saga;
    return 0;
}

int saga_orchestrator_tick(SagaOrchestrator *orch) {
    for (size_t i = 0; i < orch->count; i++) {
        if (!saga_is_terminal(&orch->active_sagas[i])) {
            saga_orch_execute(&orch->active_sagas[i]);
        }
    }
    return 0;
}

SagaInstance *saga_orchestrator_find(SagaOrchestrator *orch,
    const char *saga_id) {
    for (size_t i = 0; i < orch->count; i++) {
        if (strcmp(orch->active_sagas[i].saga_id, saga_id) == 0) {
            return &orch->active_sagas[i];
        }
    }
    return NULL;
}

int saga_orchestrator_cancel(SagaOrchestrator *orch, const char *saga_id) {
    SagaInstance *saga = saga_orchestrator_find(orch, saga_id);
    if (!saga) return -1;
    if (saga_is_terminal(saga)) return -2;
    saga_orch_compensate(saga, saga->current_step);
    return 0;
}

ChoreographyBus *choreography_bus_create(void) {
    ChoreographyBus *bus = (ChoreographyBus *)malloc(sizeof(ChoreographyBus));
    bus->service_capacity = 8;
    bus->service_count = 0;
    bus->services = (ChoreographyService *)malloc(
        bus->service_capacity * sizeof(ChoreographyService));
    bus->event_log_capacity = 32;
    bus->event_log_count = 0;
    bus->event_log = (ChoreographyEvent *)malloc(
        bus->event_log_capacity * sizeof(ChoreographyEvent));
    return bus;
}

void choreography_bus_destroy(ChoreographyBus *bus) {
    free(bus->services);
    free(bus->event_log);
    free(bus);
}

int choreography_bus_register_service(ChoreographyBus *bus,
    ChoreographyService service) {
    if (bus->service_count >= bus->service_capacity) {
        bus->service_capacity *= 2;
        bus->services = (ChoreographyService *)realloc(bus->services,
            bus->service_capacity * sizeof(ChoreographyService));
    }
    bus->services[bus->service_count++] = service;
    return 0;
}

int choreography_bus_publish(ChoreographyBus *bus, ChoreographyEvent event) {
    if (bus->event_log_count >= bus->event_log_capacity) {
        bus->event_log_capacity *= 2;
        bus->event_log = (ChoreographyEvent *)realloc(bus->event_log,
            bus->event_log_capacity * sizeof(ChoreographyEvent));
    }
    bus->event_log[bus->event_log_count++] = event;
    return 0;
}

int choreography_bus_dispatch_pending(ChoreographyBus *bus) {
    for (size_t i = 0; i < bus->event_log_count; i++) {
        ChoreographyEvent *ev = &bus->event_log[i];
        for (size_t j = 0; j < bus->service_count; j++) {
            if (strcmp(bus->services[j].service_name, ev->service_name) == 0) {
                if (bus->services[j].on_event) {
                    bus->services[j].on_event(
                        bus->services[j].context, ev);
                }
            }
        }
    }
    bus->event_log_count = 0;
    return 0;
}

ChoreographyService *choreography_bus_find_service(ChoreographyBus *bus,
    const char *service_name) {
    for (size_t i = 0; i < bus->service_count; i++) {
        if (strcmp(bus->services[i].service_name, service_name) == 0) {
            return &bus->services[i];
        }
    }
    return NULL;
}

ChoreographyEvent ch_event_started(const char *saga_id, const char *step) {
    ChoreographyEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.event_type = CHOREO_EVENT_TYPE_STARTED;
    strncpy(ev.saga_id, saga_id, SAGA_ID_LEN);
    strncpy(ev.step_name, step, SAGA_NAME_LEN);
    ev.step_status = SAGA_STEP_PENDING;
    return ev;
}

ChoreographyEvent ch_event_completed(const char *saga_id, const char *step,
    const char *service) {
    ChoreographyEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.event_type = CHOREO_EVENT_TYPE_STEP_COMPLETED;
    strncpy(ev.saga_id, saga_id, SAGA_ID_LEN);
    strncpy(ev.step_name, step, SAGA_NAME_LEN);
    strncpy(ev.service_name, service, SAGA_NAME_LEN);
    ev.step_status = SAGA_STEP_COMPLETED;
    return ev;
}

ChoreographyEvent ch_event_failed(const char *saga_id, const char *step,
    const char *service) {
    ChoreographyEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.event_type = CHOREO_EVENT_TYPE_STEP_FAILED;
    strncpy(ev.saga_id, saga_id, SAGA_ID_LEN);
    strncpy(ev.step_name, step, SAGA_NAME_LEN);
    strncpy(ev.service_name, service, SAGA_NAME_LEN);
    ev.step_status = SAGA_STEP_FAILED;
    return ev;
}

ChoreographyEvent ch_event_compensation_needed(const char *saga_id,
    const char *failed_step) {
    ChoreographyEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.event_type = CHOREO_EVENT_TYPE_COMPENSATION_TRIGGERED;
    strncpy(ev.saga_id, saga_id, SAGA_ID_LEN);
    strncpy(ev.step_name, failed_step, SAGA_NAME_LEN);
    return ev;
}

ChoreographyEvent ch_event_compensated(const char *saga_id, const char *step,
    const char *service) {
    ChoreographyEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.event_type = CHOREO_EVENT_TYPE_COMPENSATED;
    strncpy(ev.saga_id, saga_id, SAGA_ID_LEN);
    strncpy(ev.step_name, step, SAGA_NAME_LEN);
    strncpy(ev.service_name, service, SAGA_NAME_LEN);
    return ev;
}

const char *ch_event_type_string(ChoreoEventType type) {
    switch (type) {
        case CHOREO_EVENT_TYPE_STARTED:               return "STARTED";
        case CHOREO_EVENT_TYPE_STEP_COMPLETED:        return "STEP_COMPLETED";
        case CHOREO_EVENT_TYPE_STEP_FAILED:           return "STEP_FAILED";
        case CHOREO_EVENT_TYPE_COMPENSATION_TRIGGERED:return "COMPENSATION_TRIGGERED";
        case CHOREO_EVENT_TYPE_COMPENSATED:           return "COMPENSATED";
        case CHOREO_EVENT_TYPE_SAGA_COMPLETED:        return "SAGA_COMPLETED";
        case CHOREO_EVENT_TYPE_SAGA_FAILED:           return "SAGA_FAILED";
        default:                                      return "UNKNOWN";
    }
}

SagaInstance saga_create_order_saga(EntityId order_id, EntityId customer_id,
    double amount, const char *currency, bool orchestrated) {
    SagaInstance saga = saga_orch_create("CreateOrderSaga", orchestrated);
    OrderSagaData data;
    memset(&data, 0, sizeof(data));
    data.order_id = order_id;
    data.customer_id = customer_id;
    data.amount = amount;
    strncpy(data.currency, currency, 8);
    memcpy(saga.saga_data, &data, sizeof(data));
    saga.saga_data_size = sizeof(data);

    SagaStep step1 = saga_step_create("ReserveInventory",
        saga_step_reserve_inventory_exec,
        saga_step_reserve_inventory_comp, NULL, 3);
    SagaStep step2 = saga_step_create("ProcessPayment",
        saga_step_process_payment_exec,
        saga_step_process_payment_comp, NULL, 3);
    SagaStep step3 = saga_step_create("ConfirmOrder",
        saga_step_confirm_order_exec,
        saga_step_confirm_order_comp, NULL, 3);
    SagaStep step4 = saga_step_create("ShipOrder",
        saga_step_ship_order_exec,
        saga_step_ship_order_comp, NULL, 3);

    SagaStep steps[] = {step1, step2, step3, step4};
    saga_orch_add_steps(&saga, steps, 4);
    return saga;
}

int saga_step_reserve_inventory_exec(void *ctx, void *data) {
    (void)ctx;
    (void)data;
    return 0;
}

int saga_step_reserve_inventory_comp(void *ctx, void *data) {
    (void)ctx;
    (void)data;
    return 0;
}

int saga_step_process_payment_exec(void *ctx, void *data) {
    (void)ctx;
    (void)data;
    return 0;
}

int saga_step_process_payment_comp(void *ctx, void *data) {
    (void)ctx;
    (void)data;
    return 0;
}

int saga_step_confirm_order_exec(void *ctx, void *data) {
    (void)ctx;
    (void)data;
    return 0;
}

int saga_step_confirm_order_comp(void *ctx, void *data) {
    (void)ctx;
    (void)data;
    return 0;
}

int saga_step_ship_order_exec(void *ctx, void *data) {
    (void)ctx;
    (void)data;
    return 0;
}

int saga_step_ship_order_comp(void *ctx, void *data) {
    (void)ctx;
    (void)data;
    return 0;
}
