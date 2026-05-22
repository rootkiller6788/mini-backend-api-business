# mini-business-arch Architecture

## Overview

`mini-business-arch` is a C99 library providing enterprise business architecture patterns:
DDD (Domain-Driven Design), CQRS/Event Sourcing, Saga patterns, Workflow Engine, and BPMN.

## Layer Diagram

```
┌─────────────────────────────────────────────────────────┐
│                    Application Layer                     │
│  (example_ddd.c, example_cqrs.c, example_saga.c)        │
├─────────────────────────────────────────────────────────┤
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│  │   DDD    │ │CQRS + ES │ │  Saga    │ │ Workflow │   │
│  │  Model   │ │  Engine  │ │Orch/Chor │ │  Engine  │   │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘   │
│       │             │            │             │         │
│  ┌────┴─────────────┴────────────┴─────────────┴─────┐  │
│  │                 BPMN Process Engine               │  │
│  └──────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────┤
│                   Domain Layer                          │
│  (Entities, ValueObjects, Aggregates, DomainServices)   │
├─────────────────────────────────────────────────────────┤
│                Infrastructure Layer                     │
│  (In-Memory Repositories, EventStore, Projections)      │
└─────────────────────────────────────────────────────────┘
```

## Module Dependencies

```
ddd_model.h  ─────────────────────────────────────────────
    ^              ^              ^              ^
    │              │              │              │
cqrs_es.h    saga_orch.h   workflow_engine.h   bpmn_model.h
```

- `ddd_model.h` — Foundation: Entity, ValueObject, Aggregate, Repository
- `cqrs_es.h` — Depends on `ddd_model.h` for EntityId
- `saga_orch.h` — Depends on `ddd_model.h` for EntityId
- `workflow_engine.h` — Depends on `ddd_model.h` for entity IDs
- `bpmn_model.h` — Independent (uses its own ID system)

## Key Patterns

### Domain-Driven Design (ddd_model.h/c)

- **Entity**: Identity-based equality (`entity_id_equals`)
- **ValueObject**: Immutable, equality by value (`address_equals`, `money_equals`)
- **Aggregate**: Consistency boundary (`OrderAggregate` with line items)
- **Repository**: Collection-like persistence (`Repository` with vtable)
- **DomainService**: Stateless operations (`OrderDomainService`)

### CQRS + Event Sourcing (cqrs_es.h/c)

- **Command Bus**: Dispatch `PlaceOrder`, `CancelOrder`, `ShipOrder`
- **Query Bus**: Dispatch `OrderDetail`, `OrdersByCustomer`
- **EventStore**: Append-only event persistence
- **Projection Engine**: Rebuild read models from events
- **Snapshots**: Optimize aggregate rebuilds

### Saga Pattern (saga_orch.h/c)

- **Orchestration**: Central coordinator (`SagaOrchestrator`)
- **Choreography**: Event-driven (`ChoreographyBus`)
- **Compensating Transactions**: Rollback on failure

### Workflow Engine (workflow_engine.h/c)

- **State Machine**: States + transitions + guards + actions
- **Fork/Join**: Parallel branches
- **Timer Events**: Time-based transitions
- **Approval Workflow**: draft → pending_approval → approved/rejected

### BPMN Models (bpmn_model.h/c)

- **Nodes**: Start/End Events, Tasks, Gateways
- **Token-based Execution**: Parallel/serial flow control
- **Pre-built Processes**: Order Fulfillment, Payment, Expense Approval

## Memory Management

All structures use C99 conventions:
- `*_create()` returns heap-allocated objects
- `*_destroy()` frees all owned memory
- Stack-allocated objects use `*_init()` / `*_deinit()` patterns where appropriate
- No global state (thread-safe by design)

## Compilation

```bash
make all          # Build library + examples + demos
make examples     # Build example programs only
make demos        # Build demo programs only
make clean        # Remove build artifacts
```
