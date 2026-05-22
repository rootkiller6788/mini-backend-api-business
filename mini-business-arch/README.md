# mini-business-arch — 业务架构 (C 语言实现)

A C99 library implementing core business architecture patterns: Domain-Driven Design,
CQRS with Event Sourcing, Saga orchestration/choreography, workflow engines, and BPMN
process modeling.

## Modules

| Header | Pattern | Description |
|--------|---------|-------------|
| `ddd_model.h` | Domain-Driven Design | Entities, ValueObjects, Aggregates, Repositories, DomainServices |
| `cqrs_es.h` | CQRS + Event Sourcing | Command/Query buses, EventStore, Projections, Snapshots |
| `saga_orch.h` | Saga Pattern | Orchestration, Choreography, Compensating Transactions |
| `workflow_engine.h` | Workflow Engine | State machines, Fork/Join, Timers, Approval workflows |
| `bpmn_model.h` | BPMN 2.0 | Token-based process execution, Gateways, Tasks, Events |

## Quick Start

```bash
make all
./build/example_ddd
./build/example_cqrs
./build/example_saga
./build/demo_workflow
./build/demo_bpmn
```

## Structure

```
mini-business-arch/
├── ddd_model.h/c           # Domain-Driven Design
├── cqrs_es.h/c             # CQRS & Event Sourcing
├── saga_orch.h/c           # Saga Pattern
├── workflow_engine.h/c     # Workflow Engine
├── bpmn_model.h/c          # BPMN Process Models
├── example_ddd.c           # DDD usage example
├── example_cqrs.c          # CQRS+ES usage example
├── example_saga.c          # Saga usage example
├── demo_workflow.c         # Workflow engine demos
├── demo_bpmn.c             # BPMN model demos
├── docs/
│   ├── ARCHITECTURE.md     # Architecture overview
│   └── API.md              # API reference
├── Makefile
└── README.md
```

## Requirements

- C99 compiler (GCC, Clang, MSVC)
- Standard C library only (no external dependencies)

## Key Concepts

### DDD: Ubiquitous Language
All domain concepts use business terminology: `OrderAggregate`, `AddressValueObject`,
`OrderPlaced`, `OrderShipped`. The code speaks the language of the business domain.

### CQRS: Separate Read/Write
Commands mutate state without returning data. Queries return DTOs without side effects.
Event Sourcing provides an append-only audit trail.

### Saga: Distributed Transactions
Orchestration uses a central coordinator. Choreography uses events between services.
Failed steps trigger compensating transactions to rollback completed work.

### Workflow: State Machines
Define states, transitions, guards, and actions. Support parallel branches (fork/join),
timer events, and approval workflows with revision cycles.

### BPMN: Token-Based Execution
Standard BPMN 2.0 elements: start/end events, user/service/script tasks,
exclusive/parallel/inclusive gateways. Tokens flow through sequence flows.

## License

Internal use.
