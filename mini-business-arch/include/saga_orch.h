#ifndef SAGA_ORCH_H
#define SAGA_ORCH_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "ddd_model.h"

#define SAGA_ID_LEN      64
#define SAGA_NAME_LEN   128
#define SAGA_PAYLOAD_LEN 2048
#define SAGA_MAX_STEPS    20

typedef enum {
    SAGA_STEP_PENDING = 0,
    SAGA_STEP_EXECUTING,
    SAGA_STEP_COMPLETED,
    SAGA_STEP_COMPENSATING,
    SAGA_STEP_COMPENSATED,
    SAGA_STEP_FAILED
} SagaStepStatus;

typedef struct {
    char    step_name[SAGA_NAME_LEN];
    int     (*execute)(void *ctx, void *saga_data);
    int     (*compensate)(void *ctx, void *saga_data);
    void    *context;
    SagaStepStatus status;
    int             retry_count;
    int             max_retries;
} SagaStep;

typedef enum {
    SAGA_STATUS_RUNNING = 0,
    SAGA_STATUS_COMPLETED,
    SAGA_STATUS_COMPENSATING,
    SAGA_STATUS_COMPENSATED,
    SAGA_STATUS_FAILED
} SagaStatus;

typedef struct {
    char        saga_id[SAGA_ID_LEN];
    char        saga_name[SAGA_NAME_LEN];
    SagaStep    steps[SAGA_MAX_STEPS];
    int         step_count;
    int         current_step;
    SagaStatus  status;
    char        saga_data[SAGA_PAYLOAD_LEN];
    size_t      saga_data_size;
    bool        is_orchestrated;
} SagaInstance;

typedef struct {
    SagaInstance *active_sagas;
    size_t        capacity;
    size_t        count;
} SagaOrchestrator;

typedef enum {
    CHOREO_EVENT_TYPE_STARTED = 0,
    CHOREO_EVENT_TYPE_STEP_COMPLETED,
    CHOREO_EVENT_TYPE_STEP_FAILED,
    CHOREO_EVENT_TYPE_COMPENSATION_TRIGGERED,
    CHOREO_EVENT_TYPE_COMPENSATED,
    CHOREO_EVENT_TYPE_SAGA_COMPLETED,
    CHOREO_EVENT_TYPE_SAGA_FAILED
} ChoreoEventType;

typedef struct {
    ChoreoEventType event_type;
    char            saga_id[SAGA_ID_LEN];
    char            step_name[SAGA_NAME_LEN];
    char            service_name[SAGA_NAME_LEN];
    char            payload[SAGA_PAYLOAD_LEN];
    size_t          payload_size;
    SagaStepStatus  step_status;
} ChoreographyEvent;

typedef void (*choreo_event_handler)(void *service_ctx, ChoreographyEvent *event);

typedef struct {
    char                 service_name[SAGA_NAME_LEN];
    void                *context;
    choreo_event_handler on_event;
    SagaStep             known_steps[SAGA_MAX_STEPS];
    int                  known_step_count;
} ChoreographyService;

typedef struct {
    ChoreographyService *services;
    size_t               service_capacity;
    size_t               service_count;
    ChoreographyEvent   *event_log;
    size_t               event_log_capacity;
    size_t               event_log_count;
} ChoreographyBus;

SagaStep    saga_step_create(const char *name,
    int (*execute)(void *, void *), int (*compensate)(void *, void *),
    void *context, int max_retries);
void        saga_step_set_status(SagaStep *step, SagaStepStatus status);
const char *saga_step_status_string(SagaStepStatus status);
bool        saga_step_can_retry(SagaStep *step);

SagaInstance saga_orch_create(const char *name, bool orchestrated);
int          saga_orch_add_step(SagaInstance *saga, SagaStep step);
int          saga_orch_add_steps(SagaInstance *saga, SagaStep *steps, int count);
int          saga_orch_execute(SagaInstance *saga);
int          saga_orch_execute_step(SagaInstance *saga, int step_index);
int          saga_orch_compensate(SagaInstance *saga, int failed_step_index);
int          saga_orch_resume(SagaInstance *saga);
const char  *saga_status_string(SagaStatus status);
bool         saga_is_terminal(SagaInstance *saga);

SagaOrchestrator *saga_orchestrator_create(void);
void              saga_orchestrator_destroy(SagaOrchestrator *orch);
int               saga_orchestrator_enqueue(SagaOrchestrator *orch,
    SagaInstance saga);
int               saga_orchestrator_tick(SagaOrchestrator *orch);
SagaInstance     *saga_orchestrator_find(SagaOrchestrator *orch,
    const char *saga_id);
int               saga_orchestrator_cancel(SagaOrchestrator *orch,
    const char *saga_id);

ChoreographyBus *choreography_bus_create(void);
void             choreography_bus_destroy(ChoreographyBus *bus);
int              choreography_bus_register_service(ChoreographyBus *bus,
    ChoreographyService service);
int              choreography_bus_publish(ChoreographyBus *bus,
    ChoreographyEvent event);
int              choreography_bus_dispatch_pending(ChoreographyBus *bus);
ChoreographyService *choreography_bus_find_service(ChoreographyBus *bus,
    const char *service_name);

ChoreographyEvent ch_event_started(const char *saga_id, const char *step);
ChoreographyEvent ch_event_completed(const char *saga_id, const char *step,
    const char *service);
ChoreographyEvent ch_event_failed(const char *saga_id, const char *step,
    const char *service);
ChoreographyEvent ch_event_compensation_needed(const char *saga_id,
    const char *failed_step);
ChoreographyEvent ch_event_compensated(const char *saga_id, const char *step,
    const char *service);
const char       *ch_event_type_string(ChoreoEventType type);

typedef struct {
    EntityId order_id;
    EntityId customer_id;
    double   amount;
    char     currency[8];
} OrderSagaData;

SagaInstance saga_create_order_saga(EntityId order_id, EntityId customer_id,
    double amount, const char *currency, bool orchestrated);

int saga_step_reserve_inventory_exec(void *ctx, void *data);
int saga_step_reserve_inventory_comp(void *ctx, void *data);
int saga_step_process_payment_exec(void *ctx, void *data);
int saga_step_process_payment_comp(void *ctx, void *data);
int saga_step_confirm_order_exec(void *ctx, void *data);
int saga_step_confirm_order_comp(void *ctx, void *data);
int saga_step_ship_order_exec(void *ctx, void *data);
int saga_step_ship_order_comp(void *ctx, void *data);

#endif
