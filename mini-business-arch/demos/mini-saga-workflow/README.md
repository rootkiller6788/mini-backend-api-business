# mini-saga-workflow — Saga & Workflow Engine Deep Dive

A comprehensive guide to Saga patterns, workflow engines, and BPMN process
execution as implemented in `mini-business-arch`. Covers choreography vs
orchestration, compensating transactions, state machines, and token-based
BPMN execution — all in C99.

---

## Table of Contents

1. [The Saga Pattern](#the-saga-pattern)
2. [Why Sagas? The Distributed Transaction Problem](#why-sagas-the-distributed-transaction-problem)
3. [Orchestration-Based Saga](#orchestration-based-saga)
4. [Choreography-Based Saga](#choreography-based-saga)
5. [Orchestration vs Choreography](#orchestration-vs-choreography)
6. [Compensating Transactions](#compensating-transactions)
7. [Saga Lifecycle](#saga-lifecycle)
8. [Saga Implementation in this Library](#saga-implementation-in-this-library)
9. [Workflow Engine Foundations](#workflow-engine-foundations)
10. [State Machines](#state-machines)
11. [Transitions, Guards, and Actions](#transitions-guards-and-actions)
12. [Parallel Execution: Fork and Join](#parallel-execution-fork-and-join)
13. [Timer Events](#timer-events)
14. [Approval Workflows](#approval-workflows)
15. [BPMN 2.0 Process Execution](#bpmn-20-process-execution)
16. [Token-Based Execution Model](#token-based-execution-model)
17. [BPMN Node Types](#bpmn-node-types)
18. [Gateways: Exclusive, Parallel, and Inclusive](#gateways-exclusive-parallel-and-inclusive)
19. [BPMN Process Instance Lifecycle](#bpmn-process-instance-lifecycle)
20. [Putting It All Together](#putting-it-all-together)
21. [When to Use Each Pattern](#when-to-use-each-pattern)

---

## The Saga Pattern

A **Saga** is a pattern for managing distributed transactions across multiple
services without using distributed (XA) transactions. Instead of a single
atomic transaction, a saga breaks the work into a sequence of local
transactions, each with a corresponding compensating transaction that
can undo its effects.

### The Core Idea

```
Instead of:  BEGIN TRANSACTION
               ServiceA.doWork()
               ServiceB.doWork()
               ServiceC.doWork()
             COMMIT TRANSACTION  ← two-phase commit, locks, fragility

Use:         Saga = Step1(do, undo) → Step2(do, undo) → Step3(do, undo)

If Step3 fails: Step3 undo, then Step2 undo, then Step1 undo
```

This pattern was introduced by Hector Garcia-Molina and Kenneth Salem in
their 1987 paper "Sagas". The term was revived for microservices because
distributed transactions are impractical in loosely coupled systems.

---

## Why Sagas? The Distributed Transaction Problem

### The Two-Phase Commit Problem

2PC (Two-Phase Commit) requires:
1. **Prepare phase** — all participants agree they can commit.
2. **Commit phase** — all participants commit.

Problems:
- **Locking** — resources are locked for the duration of both phases.
- **Blocking** — if the coordinator fails, participants are stuck waiting.
- **Coupling** — all participants must support the XA protocol.
- **Availability** — if any participant is unreachable, the whole transaction fails.

### The Saga Alternative

Sagas use **eventual consistency** instead of strong consistency:
- Each step is a local ACID transaction in its own database.
- If a step fails, compensating transactions undo the preceding steps.
- There is no global lock; intermediate states are visible.

### Example: Order Fulfillment Saga

```
Step 1: CreateOrder        comp: CancelOrder
Step 2: ReserveInventory   comp: ReleaseInventory
Step 3: ChargePayment      comp: RefundPayment
Step 4: ScheduleShipment   comp: CancelShipment

Normal flow:     1 → 2 → 3 → 4 → done
Failure at 3:    1 → 2 → 3(fail) → undo 2 → undo 1
```

---

## Orchestration-Based Saga

In **orchestration**, a central coordinator (orchestrator) tells each
participant what to do and when.

```
                    ┌─────────────┐
                    │ Orchestrator │
                    └──────┬──────┘
                   /       |        \
                  v        v         v
            ┌────────┐ ┌────────┐ ┌────────┐
            │ServiceA│ │ServiceB│ │ServiceC│
            └────────┘ └────────┘ └────────┘
```

### Orchestrator Flow

```
Orchestrator:
  result = ServiceA.doWork()
  if result.ok:
    result = ServiceB.doWork()
    if result.ok:
      result = ServiceC.doWork()
      if !result.ok:
        ServiceB.compensate()
        ServiceA.compensate()
    else:
      ServiceA.compensate()
```

### Implementation in This Library

```c
typedef struct {
    SagaInstance *active_sagas;
    size_t        capacity;
    size_t        count;
} SagaOrchestrator;

SagaOrchestrator *saga_orchestrator_create(void);
void              saga_orchestrator_enqueue(SagaOrchestrator *orch, SagaInstance saga);
void              saga_orchestrator_tick(SagaOrchestrator *orch);
SagaInstance     *saga_orchestrator_find(SagaOrchestrator *orch, const char *saga_id);
void              saga_orchestrator_cancel(SagaOrchestrator *orch, const char *saga_id);
void              saga_orchestrator_destroy(SagaOrchestrator *orch);
```

### Advantages of Orchestration

- **Central visibility** — the orchestrator knows the entire saga state.
- **Simpler participants** — services just expose do/undo operations.
- **Easier to reason about** — the flow is explicit in one place.
- **Easier to version** — the saga definition is versioned as a unit.
- **Transaction boundary** — the orchestrator owns the saga's state.

### Disadvantages of Orchestration

- **Single point of failure** — if the orchestrator crashes, sagas may stall.
- **Tight coupling** — the orchestrator knows about every service.
- **Central bottleneck** — all saga traffic goes through the orchestrator.
- **Risk of anemic orchestrator** — business logic leaks into the coordinator.

---

## Choreography-Based Saga

In **choreography**, there is no central coordinator. Each service publishes
events and reacts to events from other services.

```
┌────────┐  OrderCreated  ┌──────────┐  InventoryReserved  ┌─────────┐
│ Orders │ ──────────────→│Inventory │ ─────────────────→ │ Payment │
└────────┘                └──────────┘                    └─────────┘
     ^                        │                                 │
     │    OrderFailed         │    OutOfStock                   │ PaymentCharged
     └────────────────────────┴─────────────────────────────────┘
```

### Choreography Flow

```
Orders:       CreateOrder → publish "OrderCreated"
Inventory:    On "OrderCreated" → ReserveInventory → publish "InventoryReserved"
              On failure → publish "OrderFailed"
Payment:      On "InventoryReserved" → ChargePayment → publish "PaymentCharged"
              On failure → publish "PaymentFailed"
Shipping:     On "PaymentCharged" → ScheduleShipment
Orders:       On "OrderFailed" → CancelOrder
Inventory:    On "PaymentFailed" → ReleaseInventory
```

### Implementation in This Library

```c
typedef enum {
    CHOREO_EVENT_TYPE_STARTED = 0,
    CHOREO_EVENT_TYPE_COMPLETED,
    CHOREO_EVENT_TYPE_FAILED,
    CHOREO_EVENT_TYPE_COMPENSATING,
    CHOREO_EVENT_TYPE_COMPENSATED
} ChoreoEventType;

typedef struct {
    ChoreoEventType type;
    char            saga_id[SAGA_ID_LEN];
    char            step_name[SAGA_NAME_LEN];
    char            payload[SAGA_PAYLOAD_LEN];
    size_t          payload_size;
} ChoreoEvent;

typedef struct {
    char saga_id[SAGA_ID_LEN];
    int  (*on_event)(void *ctx, ChoreoEvent *event);
    int  (*compensate)(void *ctx, ChoreoEvent *event);
    void *context;
} ChoreoParticipant;
```

### Advantages of Choreography

- **No central point of failure** — each service is autonomous.
- **Loose coupling** — services only know about events, not each other.
- **Better scalability** — no bottleneck at a central orchestrator.
- **Natural for event-driven architectures** — aligns with EDA patterns.

### Disadvantages of Choreography

- **Distributed logic** — the saga flow is spread across services.
- **Harder to understand** — no single place shows the complete flow.
- **Cyclic dependencies** — events can form cycles if not careful.
- **Debugging complexity** — tracing a saga across services is challenging.

---

## Orchestration vs Choreography

| Aspect | Orchestration | Choreography |
|--------|--------------|--------------|
| Coordinator | Central orchestrator | Distributed (events) |
| Coupling | Orchestrator knows all services | Services know only events |
| Visibility | Full saga state in orchestrator | Saga state distributed |
| Failure handling | Orchestrator triggers compensation | Each service reacts to failures |
| Complexity location | In orchestrator | Spread across services |
| Scalability | Bottleneck at orchestrator | No central bottleneck |
| Debugging | Trace through orchestrator | Need distributed tracing |
| Change impact | Modify orchestrator | Modify affected services |
| Best for | Well-defined linear flows | Dynamic, event-driven flows |
| Team structure | Centralized team owns saga | Services owned by separate teams |

### When to Choose Which

```
Use Orchestration when:
  - The saga flow is well-defined and linear.
  - You need central visibility and monitoring.
  - One team owns the entire saga.
  - The flow changes infrequently.

Use Choreography when:
  - Services are owned by different teams.
  - The event flow is dynamic and can branch.
  - You already have an event-driven architecture.
  - Loose coupling is the top priority.
```

---

## Compensating Transactions

A **compensating transaction** reverses the effects of a committed local
transaction. It is not a rollback (which undoes an uncommitted transaction);
it is a semantic undo of a committed change.

### Compensation vs Rollback

| Operation | Rollback | Compensation |
|-----------|---------|--------------|
| Create order | Delete row (never committed) | Set status=CANCELLED (committed) |
| Reserve inventory | Release lock | Increment available quantity |
| Charge payment | Void authorization | Issue refund |
| Send email | Unsent (never sent) | Send correction email |

### Compensation Design Principles

1. **Idempotent** — running compensation twice should have the same effect as
   running it once. This is critical because retries can cause double execution.

2. **Commutative** — the order of compensation execution should not matter for
   parallel steps. Undoing A then B should equal undoing B then A.

3. **Must not fail** — compensations should be designed to always succeed.
   If a compensation can fail, it needs its own retry/compensation strategy.

4. **Semantic, not technical** — cancellation is a business operation, not a
   database operation. CancelOrder sets a status and potentially triggers
   notification events.

### Compensation Chain

```c
Step 1: CreateOrder       → comp: CancelOrder (set CANCELLED, release references)
Step 2: ReserveInventory   → comp: ReleaseInventory (increment available)
Step 3: ChargePayment      → comp: RefundPayment (create refund transaction)
Step 4: ScheduleShipment   → comp: CancelShipment (void tracking number)
```

If Step 3 fails:
1. `RefundPayment` is not needed (charge never succeeded).
2. `ReleaseInventory` undoes Step 2.
3. `CancelOrder` undoes Step 1.

### Forward Recovery

Sometimes compensation is impractical (e.g., a shipped package cannot be
"unshipped"). In these cases, **forward recovery** is used: take corrective
action to bring the system to a consistent state going forward.

```
Example: Package shipped to wrong address
  - Cannot "unship" (compensation)
  - Forward recovery: contact carrier, re-route, or initiate return
```

---

## Saga Lifecycle

```
                    ┌─────────┐
                    │ RUNNING │
                    └────┬────┘
                         │
              ┌──────────┼──────────┐
              │          │          │
              v          v          v
        ┌───────────┐ ┌──────────┐ ┌────────┐
        │ COMPLETED │ │COMPENSATING│ │ FAILED │
        └───────────┘ └─────┬─────┘ └────────┘
                            │
                            v
                     ┌─────────────┐
                     │ COMPENSATED │
                     └─────────────┘
```

### Status Transitions

| From | To | Trigger |
|------|----|---------|
| RUNNING | COMPLETED | All steps completed successfully |
| RUNNING | FAILED | A step failed and has no compensation defined |
| RUNNING | COMPENSATING | A step failed with compensation available |
| COMPENSATING | COMPENSATED | All compensations completed |
| COMPENSATING | FAILED | A compensation itself failed |

### SagaStep States

Each step within a saga also has its own lifecycle:

```
PENDING → EXECUTING → COMPLETED
                ↓
           COMPENSATING → COMPENSATED
                ↓
             FAILED
```

---

## Saga Implementation in This Library

### Defining a Saga

```c
SagaInstance saga = saga_create_order_saga(
    order_id, customer_id, 199.99, "USD", true);
```

The `saga_create_order_saga` function creates a pre-configured saga with steps
for order creation, inventory reservation, payment processing, and shipping.

### Step Execution

```c
typedef struct {
    char    step_name[SAGA_NAME_LEN];
    int     (*execute)(void *ctx, void *saga_data);
    int     (*compensate)(void *ctx, void *saga_data);
    void    *context;
    SagaStepStatus status;
    int             retry_count;
    int             max_retries;
} SagaStep;
```

Each step has:
- `execute` — forward operation (must succeed for saga to continue).
- `compensate` — rollback operation (called during compensation phase).
- `retry_count` / `max_retries` — built-in retry support for transient failures.

### Orchestrator Tick

```c
saga_orchestrator_tick(orch);
```

`tick` processes the next step of all active sagas in the orchestrator. It:
1. Finds the current step for each saga.
2. Calls `execute()` on the step.
3. If success, advances to the next step.
4. If failure, initiates compensation chain.
5. Marks saga COMPLETED or COMPENSATING as appropriate.

### Choreography Example

```c
ChoreoParticipant inventory_service = {
    .saga_id = saga_id,
    .on_event = inventory_on_event,
    .compensate = inventory_compensate,
    .context = &inventory_data
};
```

---

## Workflow Engine Foundations

The Workflow Engine generalizes the saga pattern into a full state machine
execution engine. While sagas are specialized for distributed transaction
management, the workflow engine handles arbitrary business processes.

### Key Concepts

| Concept | Description |
|---------|-------------|
| State | A stable condition that a process instance can be in |
| Transition | A directed edge from one state to another |
| Event | A trigger that causes a transition |
| Guard | A condition that must be true for a transition to fire |
| Action | Code executed during state entry, exit, or transition |
| Fork | Split execution into parallel branches |
| Join | Wait for all parallel branches to complete |
| Timer | A time-based trigger for transitions |

---

## State Machines

The workflow engine implements a **finite state machine** (FSM) where states,
transitions, guards, and actions are explicitly modeled.

### State Definition

```c
typedef struct {
    char name[WORKFLOW_STATE_LEN];
    char display_name[WORKFLOW_NAME_LEN];
    bool is_initial;
    bool is_final;
    WorkflowActionList on_entry;
    WorkflowActionList on_exit;
} WorkflowState;
```

### Transition Definition

```c
typedef struct {
    char         source_state[WORKFLOW_STATE_LEN];
    char         target_state[WORKFLOW_STATE_LEN];
    char         event[WORKFLOW_EVENT_LEN];
    WFBranchType branch_type;
    wf_guard_fn   guard;
    void         *guard_context;
    char          guard_desc[WORKFLOW_GUARD_LEN];
    WorkflowActionList on_transition;
} WorkflowTransition;
```

### Example: Order Processing Workflow

```
States:        DRAFT → PENDING_APPROVAL → APPROVED → FULFILLING → SHIPPED → DELIVERED
Transitions:   submit  approve          fulfill     ship         deliver
Guards:        ───     amount > $1000   ───        inventory_ok ───
```

```c
WorkflowState states[] = {
    { .name = "DRAFT",            .is_initial = true },
    { .name = "PENDING_APPROVAL", .on_entry = {notify_manager} },
    { .name = "APPROVED",         .on_entry = {allocate_inventory} },
    { .name = "FULFILLING",       .on_entry = {pick_and_pack} },
    { .name = "SHIPPED",          .on_entry = {send_tracking_email} },
    { .name = "DELIVERED",        .is_final = true },
};

WorkflowTransition transitions[] = {
    { .source = "DRAFT",    .target = "PENDING_APPROVAL", .event = "submit" },
    { .source = "PENDING_APPROVAL", .target = "APPROVED", .event = "approve",
      .guard = amount_over_threshold, .guard_desc = "amount > $1000" },
    { .source = "APPROVED", .target = "FULFILLING", .event = "fulfill" },
    { .source = "FULFILLING", .target = "SHIPPED", .event = "ship",
      .guard = inventory_available, .guard_desc = "inventory > 0" },
    { .source = "SHIPPED",  .target = "DELIVERED",  .event = "deliver" },
};
```

---

## Transitions, Guards, and Actions

### Events Drive Transitions

```c
int workflow_instance_handle_event(
    WorkflowInstance *instance,
    const char       *event_name,
    void             *event_data
);
```

When an event arrives, the engine:
1. Looks up the current state.
2. Finds transitions from that state matching the event name.
3. Evaluates guards on matching transitions.
4. If a guard passes, executes: `on_exit(source)` → `on_transition` → `on_entry(target)`.
5. Updates the instance's current state.

### Guard Functions

```c
typedef bool (*wf_guard_fn)(void *ctx, void *instance_data);
```

Guards are pure boolean predicates. They read instance data and return true/false.
Guards must be side-effect free — they should not modify state.

```c
bool amount_over_threshold(void *ctx, void *instance_data) {
    WorkflowInstance *inst = (WorkflowInstance *)instance_data;
    return inst->data.order_total > 1000.0;
}
```

### Action Functions

```c
typedef int (*wf_action_fn)(void *ctx, void *instance_data);
```

Actions perform operations when entering/exiting states or during transitions.
Actions return 0 for success, non-zero for failure.

```c
int notify_manager(void *ctx, void *instance_data) {
    WorkflowInstance *inst = (WorkflowInstance *)instance_data;
    // Send notification to manager
    return 0;
}
```

### Action Lists

```c
typedef struct {
    WorkflowAction actions[WORKFLOW_MAX_ACTIONS];
    int count;
} WorkflowActionList;
```

Multiple actions can be chained in entry, exit, and transition handlers.
They execute in order. If any action fails, the transition is blocked.

---

## Parallel Execution: Fork and Join

### Fork: Split into Parallel Branches

A **fork** transition splits execution into multiple concurrent branches,
each with its own target state.

```
                    ┌→ State A
CurrentState ──fork─┼→ State B
                    └→ State C
```

### Join: Wait for All Branches

A **join** waits for all parallel branches to reach their completion states
before continuing.

```
State A ─┐
State B ─┼─join→ NextState
State C ─┘
```

### Branch Types

```c
typedef enum {
    WF_BRANCH_SIMPLE = 0,  // simple transition (default)
    WF_BRANCH_FORK,         // split into parallel branches
    WF_BRANCH_JOIN,         // merge parallel branches
    WF_BRANCH_TIMER         // time-based transition
} WFBranchType;
```

### Workflow Instance with Parallel Branches

```c
typedef struct {
    char  workflow_name[WORKFLOW_NAME_LEN];
    char  current_state[WORKFLOW_STATE_LEN];
    char *active_branch_states;   // states of parallel branches
    int   active_branch_count;
    int   active_branch_capacity;
    WorkflowActionList *pending_actions;
    int   pending_action_count;
    int   pending_action_capacity;
    char  instance_data[WORKFLOW_NAME_LEN * 4];  // user-defined data
    int   status;
} WorkflowInstance;
```

---

## Timer Events

Timer events trigger transitions after a specified duration or at a specific
time. They are essential for modeling timeouts, SLAs, and scheduled actions.

```c
WF_BRANCH_TIMER — marks a transition as time-triggered (not event-triggered)
```

### Example: Payment Timeout

```
States:      AWAITING_PAYMENT → PAYMENT_RECEIVED (event: "payment_received")
             AWAITING_PAYMENT → PAYMENT_TIMEOUT  (timer: 30 minutes)
```

After 30 minutes in AWAITING_PAYMENT without receiving the "payment_received"
event, the timer fires and the order moves to PAYMENT_TIMEOUT.

### Timer Implementation Pattern

The workflow engine does not implement timers itself (no scheduler). Instead,
it exposes timer metadata so the application can manage timers:

1. On entering a state with timer transitions, the application creates a timer.
2. When the timer fires, the application sends the corresponding event.
3. The workflow engine processes the event normally.

---

## Approval Workflows

Approval workflows model human decision points in business processes. Common
patterns include:

### Simple Approval

```
DRAFT → PENDING_APPROVAL → APPROVED → EXECUTING → COMPLETED
                ↓
             REJECTED → CORRECTING → PENDING_APPROVAL (revision cycle)
```

Approve sends the workflow forward. Reject sends it back for correction,
optionally with feedback.

### Multi-Level Approval

```
DRAFT → MANAGER_APPROVAL → DIRECTOR_APPROVAL → VP_APPROVAL → EXECUTING
```

Each level has its own approval criteria. The workflow only proceeds when all
levels have approved.

### Delegation

```
PENDING_APPROVAL → DELEGATED (event: "delegate", data: {new_approver: "..."})
DELEGATED → APPROVED/REJECTED (event: "delegated_approve"/"delegated_reject")
```

### Escalation

```
PENDING_APPROVAL → ESCALATED (timer: 48 hours)
ESCALATED → MANAGER_APPROVAL (event: "escalated_approve")
```

If the approver does not act within the SLA, the workflow escalates to the
next level automatically.

---

## BPMN 2.0 Process Execution

BPMN (Business Process Model and Notation) 2.0 is a standard for modeling
business processes. The `bpmn_model.h` module implements a subset of BPMN 2.0
with token-based execution.

### Process Definition

```c
typedef struct {
    char       id[BPMN_ID_LEN];
    char       name[BPMN_NAME_LEN];
    BPMNNode  *nodes;
    int        node_count;
    int        node_capacity;
    BPMNSequenceFlow *flows;
    int        flow_count;
    int        flow_capacity;
    char       start_node_id[BPMN_ID_LEN];
} BPMNProcess;
```

### Sequence Flow

```c
typedef struct {
    char id[BPMN_ID_LEN];
    char source_node_id[BPMN_ID_LEN];
    char target_node_id[BPMN_ID_LEN];
    char name[BPMN_NAME_LEN];
    char condition_expression[DDD_DESC_LEN / 2];
    bool is_default;
    int  evaluation_order;
} BPMNSequenceFlow;
```

Sequence flows are the edges in a BPMN diagram. They carry tokens between
nodes. Conditional sequence flows include a condition expression that
evaluates against process data.

---

## Token-Based Execution Model

Token-based execution is the standard BPMN 2.0 execution semantics. Tokens
flow through the process graph like tokens in a Petri net.

### What Are Tokens?

A **token** represents a point of execution within a process instance. Tokens
start at start events, flow through nodes and sequence flows, and are consumed
at end events.

```
Token lifetime:  Created at StartEvent → Travels through Tasks/Gateways → Destroyed at EndEvent
```

### Token Queue

```c
typedef struct {
    char    token_id[BPMN_ID_LEN];
    char    current_node_id[BPMN_ID_LEN];
    int64_t arrival_time;
    bool    is_active;
} BPMNToken;

typedef struct {
    BPMNToken tokens[BPMN_MAX_TOKENS];
    int       token_count;
} BPMNTokenQueue;
```

### Execution Step

1. Select an active token from the queue.
2. Determine its current node.
3. Evaluate the node's behavior (task execution, gateway routing).
4. Consume the token from incoming and generate tokens on outgoing flows.
5. Repeat until no active tokens remain or the process reaches an end event.

### Parallel Token Execution

```
StartEvent → [token] → ParallelGateway → [token] → TaskA → EndEvent
                                       → [token] → TaskB → EndEvent
```

A parallel gateway duplicates the incoming token into multiple tokens, one
for each outgoing flow. Each token proceeds independently.

---

## BPMN Node Types

```c
typedef enum {
    BPMN_NODE_START_EVENT = 0,
    BPMN_NODE_END_EVENT,
    BPMN_NODE_USER_TASK,
    BPMN_NODE_SERVICE_TASK,
    BPMN_NODE_SCRIPT_TASK,
    BPMN_NODE_EXCLUSIVE_GATEWAY,
    BPMN_NODE_PARALLEL_GATEWAY,
    BPMN_NODE_INCLUSIVE_GATEWAY,
    BPMN_NODE_INTERMEDIATE_TIMER,
    BPMN_NODE_SUB_PROCESS
} BPMNNodeType;
```

### Start Event

The entry point of a process. Creates the initial token and places it on
the outgoing sequence flow. A process may have multiple start events
(message start, timer start, signal start).

### End Event

Terminates a token. When all tokens in a process instance have reached
end events, the process instance is complete. End events may carry a
result (normal, error, escalation, compensation, terminate).

### User Task

A task performed by a human. The process pauses at a user task until a
human actor completes it. User tasks typically have:
- An assignee or candidate group
- A form for data entry
- A due date (SLA)

### Service Task

An automated task performed by a service. When a token arrives at a
service task, the engine calls the registered `service_fn` callback.

```c
typedef int (*bpmn_service_task_fn)(void *ctx, void *process_data);
```

If the function returns 0, the token proceeds. If non-zero, the task
fails and an error event may be raised.

### Script Task

An inline script executed by the process engine. Similar to a service
task but typically shorter and embedded in the process model.

```c
typedef int (*bpmn_script_fn)(void *ctx, void *process_data);
```

### Sub-Process

A nested process definition. When a token enters a sub-process, a child
process instance is created. The parent token waits at the sub-process
until the child completes, then continues on the outgoing flow.

---

## Gateways: Exclusive, Parallel, and Inclusive

### Exclusive Gateway (XOR)

Exactly one outgoing path is taken. Conditions are evaluated in order.
The first condition that evaluates to true is selected. The `is_default`
flag marks the fallback path if no conditions match.

```
         ┌→ Path A (amount > 1000)
Gateway ─┼→ Path B (amount > 100)    ← evaluated in order
         └→ Path C (default)
```

### Parallel Gateway (AND)

All outgoing paths are taken simultaneously. An incoming parallel gateway
at a join waits for tokens on all incoming paths before producing a single
outgoing token.

```
Fork:  [token] → [AND Gateway] → [token on Path A] + [token on Path B]

Join:  [token on Path A] ┐
       [token on Path B] ┘ → [AND Gateway] → [token]
```

### Inclusive Gateway (OR)

A combination of exclusive and parallel. Each condition is evaluated
independently. All paths with true conditions are taken (potentially
multiple, potentially zero). At the joining inclusive gateway, tokens
are synchronized only for the paths that were activated.

```
         ┌→ Path A (condition: amount > 1000)   ← true → take
Gateway ─┼→ Path B (condition: premium_customer) ← true → take
         └→ Path C (condition: weekend)         ← false → skip
```

---

## BPMN Process Instance Lifecycle

### Instance States

```c
typedef enum {
    BPMN_INSTANCE_READY = 0,
    BPMN_INSTANCE_ACTIVE,
    BPMN_INSTANCE_WAITING,     // waiting for user task or timer
    BPMN_INSTANCE_COMPLETED,
    BPMN_INSTANCE_TERMINATED,
    BPMN_INSTANCE_FAILED
} BPMNInstanceStatus;
```

### Instance Data

```c
typedef struct {
    char               process_id[BPMN_ID_LEN];
    char               instance_id[BPMN_ID_LEN];
    BPMNInstanceStatus status;
    BPMNTokenQueue     token_queue;
    BPMNNode          *current_node;
    char               process_data[BPMN_NAME_LEN * 16];
    char              *active_timers[BPMN_MAX_TIMERS];
    int                active_timer_count;
    int64_t            started_at;
    int64_t            completed_at;
} BPMNInstance;
```

### Lifecycle

```
READY → ACTIVE → WAITING ─┐
                   ↑       │ (user task complete / timer fired / signal received)
                   └───────┘
                   
ACTIVE → COMPLETED (all tokens at end events)
ACTIVE → TERMINATED (terminate end event reached)
ACTIVE → FAILED (unhandled error)
```

### Process Engine

```c
typedef struct {
    BPMNProcess   *processes;
    int            process_count;
    int            process_capacity;
    BPMNInstance  *instances;
    int            instance_count;
    int            instance_capacity;
    char           engine_id[BPMN_ID_LEN];
} BPMNEngine;

BPMNEngine *bpmn_engine_create(void);
int         bpmn_engine_deploy(BPMNEngine *engine, BPMNProcess process);
char       *bpmn_engine_start_instance(BPMNEngine *engine, const char *process_id,
                                       void *initial_data);
int         bpmn_engine_trigger(BPMNEngine *engine, const char *instance_id,
                                const char *node_id);
int         bpmn_engine_tick(BPMNEngine *engine);
void        bpmn_engine_destroy(BPMNEngine *engine);
```

---

## Putting It All Together

### When Saga → When Workflow → When BPMN

```
Is it a distributed transaction across services?
  Yes → Use Saga (orchestration or choreography)
  No  → Is it a multi-step process with human tasks?
           Yes → Use Workflow Engine
           No  → Is it a complex process requiring BPMN compliance?
                    Yes → Use BPMN Engine
                    No  → Use simple state machine or direct code
```

### Combining Patterns

These patterns can be combined:
- A BPMN process can include a **service task** that executes a saga step.
- A saga step can delegate to a **workflow engine** for complex validation.
- A workflow can use **BPMN** for its process definition and visualization.

### Example: End-to-End Order Fulfillment

```
BPMN Process: "OrderFulfillment"
├── Start Event: OrderReceived
├── Service Task: ValidateOrder → calls Saga Step 1 (CreateOrder)
├── Service Task: AllocateInventory → calls Saga Step 2 (ReserveInventory)
├── Exclusive Gateway: PaymentMethod?
│   ├── CreditCard → Service Task: ChargeCard → Saga Step 3 (ChargePayment)
│   └── Invoice → User Task: ApproveInvoice
├── Parallel Gateway: Fork
│   ├── Service Task: ScheduleShipment → Saga Step 4
│   └── Service Task: SendConfirmation → notification
├── Parallel Gateway: Join
└── End Event: OrderComplete
```

If `AllocateInventory` fails, the saga's compensating transactions run:
`CancelOrder` undoes the `CreateOrder` step. The BPMN process receives
the failure and can route to an error handling path.

---

## When to Use Each Pattern

### Saga Pattern

| Use When | Avoid When |
|----------|-----------|
| Distributed transactions across services | Single-database applications |
| Polyglot persistence (different DBs per service) | Monolithic applications with one DB |
| Eventual consistency is acceptable | Strong consistency is required |
| Services are independently deployable | Tightly coupled components |
| Need compensating rollback logic | Simple success/failure without undo |

### Workflow Engine

| Use When | Avoid When |
|----------|-----------|
| Multi-step processes with human tasks | Fully automated, single-step operations |
| Approval chains and revision cycles | Simple CRUD operations |
| State-dependent behavior | Stateless transformations |
| Long-running processes (hours/days/weeks) | Request-response (sub-second) operations |
| Need process visibility and monitoring | Ad-hoc, unstructured work |

### BPMN Engine

| Use When | Avoid When |
|----------|-----------|
| Need for standard process notation (BPMN 2.0) | Simple flows with few states |
| Business analysts model processes visually | Developers are the only process designers |
| Complex routing (parallel, inclusive, conditional) | Linear, single-path processes |
| Compliance and audit requirements | Internal tools without compliance needs |
| Integration with BPMN modeling tools | Processes change rarely |

### Complexity Comparison

| Pattern | States | Transitions | Parallelism | Human Tasks | Standards | Learning |
|---------|--------|-------------|-------------|-------------|-----------|----------|
| Direct Code | Implicit | Nested if/else | Thread-based | None | None | Low |
| Saga | Limited | Sequential + Compensation | None | None | None | Medium |
| Workflow | Explicit | Event-driven + Guards | Fork/Join | Via events | None | Medium |
| BPMN | Explicit | Token-based + Gateways | Full | Native support | BPMN 2.0 | High |

---

## References

- Garcia-Molina, Hector, and Kenneth Salem. *Sagas*. ACM SIGMOD, 1987.
- Richardson, Chris. *Microservices Patterns*. Manning, 2018.
- Newman, Sam. *Building Microservices*. O'Reilly, 2015.
- OMG. *Business Process Model and Notation (BPMN) Version 2.0*. OMG, 2011.
- Freund, Jakob, and Bernd Rücker. *Real-Life BPMN*. Camunda, 2014.
- `mini-business-arch/saga_orch.h` — saga orchestration and choreography API
- `mini-business-arch/workflow_engine.h` — workflow state machine API
- `mini-business-arch/bpmn_model.h` — BPMN process execution API
- `mini-business-arch/docs/API.md` — complete function reference
- `mini-business-arch/docs/ARCHITECTURE.md` — architecture diagrams and module overview
