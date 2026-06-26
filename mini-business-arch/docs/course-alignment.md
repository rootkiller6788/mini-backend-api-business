# Course Alignment — mini-business-arch

## Nine-School Curriculum Mapping

### MIT
| Course | Topic | Implementation |
|--------|-------|---------------|
| 6.824 Distributed Systems | Fault Tolerance (Circuit Breaker) | `business_patterns.c` — cb_allow_request |
| 6.824 Distributed Systems | Raft Consensus (Event Sourcing) | `cqrs_es.c` — event_store_append |
| 6.824 Distributed Systems | Linearizability (Idempotency) | `business_patterns.c` — ik_store_check |
| 6.858 Computer Security | API Security Patterns | `business_patterns.c` — resilience_policy_create |

### Stanford
| Course | Topic | Implementation |
|--------|-------|---------------|
| CS 144 Networking | Rate Limiting (Token Bucket) | `business_patterns.c` — tb_try_consume |
| CS 244B Distributed Systems | Distributed Transactions (Saga) | `saga_orch.c` — saga_orch_execute |
| CS 245 Database Systems | Event Sourcing | `cqrs_es.c` — EventStore + Projections |
| CS 349D Cloud Computing | Microservices Resilience | `business_patterns.c` — resilience_execute |

### Berkeley
| Course | Topic | Implementation |
|--------|-------|---------------|
| CS 162 Operating Systems | Concurrency (Bulkhead Semaphore) | `business_patterns.c` — bh_try_acquire |
| CS 186 Database Systems | Transactions (Saga Compensation) | `saga_orch.c` — saga_orch_compensate |
| CS 294 AI Systems | ML-Driven Gateway (L9 doc) | docs/ |

### CMU
| Course | Topic | Implementation |
|--------|-------|---------------|
| 15-410 Operating Systems | Failure Detection | `business_patterns.c` — hc_compute_phi |
| 15-440 Distributed Systems | Consensus & Fault Tolerance | `saga_orch.c` + `business_patterns.c` |
| 15-445 Database Systems | Read/Write Separation (CQRS) | `cqrs_es.c` — CommandBus + QueryBus |
| 15-721 Advanced DB | Snapshot Optimization | `cqrs_es.c` — event_sourced_load_from_snapshot |

### UT Austin
| Course | Topic | Implementation |
|--------|-------|---------------|
| CS 380D Distributed Systems | State Machine Replication | `workflow_engine.c` |
| CS 395T Systems for ML | Retry Strategies (Backoff) | `business_patterns.c` — eb_next_delay_ms |

### ETH Zurich
| Course | Topic | Implementation |
|--------|-------|---------------|
| 263-3501 Parallel Programming | Fork/Join Parallelism | `workflow_engine.c` — workflow_add_fork |
| 263-0006 Computer Architecture | State Machines | `workflow_engine.c` — FSM engine |

### Cambridge
| Course | Topic | Implementation |
|--------|-------|---------------|
| Part II: OS | Concurrency Patterns | `business_patterns.c` — Bulkhead |
| Concurrent Systems | Distributed Coordination | `saga_orch.c` — Orchestration vs Choreography |
| Compiler Construction | DSL for Workflows | `bpmn_model.c` — BPMN process DSL |

### 清华 (Tsinghua)
| Course | Topic | Implementation |
|--------|-------|---------------|
| 操作系统 (OS) | 并发控制 | `business_patterns.c` — Bulkhead Semaphore |
| 编译原理 (Compilers) | 状态机 | `workflow_engine.c` |
| 计算机网络 (Networks) | 流量整形 | `business_patterns.c` — Token Bucket |
| 计算机体系结构 | 故障检测 | `business_patterns.c` — Phi-Accrual |

### Georgia Tech
| Course | Topic | Implementation |
|--------|-------|---------------|
| CS 6210 Advanced OS | Distributed Coordination | `saga_orch.c` |
| CS 6290 HPCA | Pipeline Parallelism (BPMN Tokens) | `bpmn_model.c` |
| CS 7641 Machine Learning | Automated Decision Gates (L9) | docs/ |
