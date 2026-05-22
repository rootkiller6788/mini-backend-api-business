# DI & ORM Architecture — 依赖注入与 ORM 架构设计

> 深入分析 mini-backend-framework 中两个核心组件的架构设计决策、内部实现原理与技术权衡。

## 1. DI Container Architecture

### 1.1 Core Design Decisions

#### Explicit Registration vs Convention-Based

This DI container uses **explicit registration** rather than convention-based auto-wiring:

```c
di_register(&container, "user_svc", "UserService",
            DI_SCOPE_SINGLETON, svc_factory, svc_deps, 1);
```

**Why explicit registration for C:**

1. **No reflection**: C has no runtime type information. Convention-based discovery would require code generation.
2. **C safety**: Explicit registration provides compile-time visibility of the dependency graph.
3. **Debugability**: Every service and its dependencies are visible in a single registration block.
4. **Configuration clarity**: The wiring is explicit — no hidden annotations or configuration files.

#### Factory Functions (Not Automatic Construction)

Each service provides a factory function (typed as `DIFactory`) rather than allowing the container to automatically `malloc` + zero:

```c
typedef void *(*DIFactory)(void *container, void **deps, int dep_count);
```

**Why factory functions:**

1. **Custom initialization**: Services can require non-trivial initialization (config loading, connection setup, etc.)
2. **Dependencies as arguments**: Dependencies are received as typed `void *` pointers in a known order
3. **Memory strategy**: Services can use custom allocators, static buffers, or memory pools
4. **Partial construction handling**: Factory can return NULL on failure

### 1.2 Memory Model and Ownership

```
┌─────────────────────────────────────────────────┐
│ DIContainer                                      │
│                                                  │
│  entries[0] → "database"  (SINGLETON)            │
│    .instance ──→ [Database struct] ← malloc'd    │
│                                                  │
│  entries[1] → "user_repo" (TRANSIENT)            │
│    .instance = NULL (not tracked)                │
│                                                  │
│  entries[2] → "user_svc"  (SINGLETON)            │
│    .instance ──→ [UserService struct] ← malloc'd │
│     └─ .repo ──→ ???                             │
│         (transient, not owned by container)      │
│                                                  │
│  entries[3] → "request_ctx" (REQUEST)            │
│    .instance = NULL                              │
│    request_instances[3] = (per-request cache)    │
└─────────────────────────────────────────────────┘
```

**Ownership rules:**

| Scope | Creation | Ownership | Destruction |
|---|---|---|---|
| SINGLETON | First `di_resolve()` | Container owns | `di_destroy()` calls `free()` |
| TRANSIENT | Every `di_resolve()` | Caller owns | Caller must free |
| REQUEST | First resolve in request | Container during request | `di_end_request()` clears |

**Critical observation**: Transient services injected into Singletons create a lifetime mismatch. The singleton holds a pointer to freed memory once the transient is freed. This is a known DI anti-pattern — **never inject a shorter-lived service into a longer-lived one**.

```
DO:   Singleton → Singleton      (Safe)
DO:   Singleton → Transient      (BAD — dangling pointer)
DO:   Transient → Singleton      (Safe)
DO:   Request   → Singleton      (Safe)
DO:   Request   → Request        (Safe)
```

### 1.3 Lazy Initialization

The container uses lazy initialization for singletons:

```c
case DI_SCOPE_SINGLETON:
    if (!entry->initialized) {
        entry->instance    = di_build(container, entry);
        entry->initialized = true;
    }
    return entry->instance;
```

**Benefits:**
- Services are only created when needed
- Registration order doesn't matter (as long as the dependency graph is acyclic)
- Services not used in a request path incur zero cost

**Tradeoffs:**
- First resolution of a deep dependency tree is slow
- All singletons are created at some point — no lazy tree pruning

### 1.4 Recursive Resolution Algorithm

```
di_resolve("user_ctrl")
  │
  ├── di_build(entry)
  │     │
  │     ├── deps[0] = di_resolve("user_svc")         // Depth-first
  │     │               │
  │     │               ├── di_build(entry)
  │     │               │     ├── deps[0] = di_resolve("user_repo")
  │     │               │     │               │
  │     │               │     │               └── di_build(entry)
  │     │               │     │                     ├── deps[0] = di_resolve("database")
  │     │               │     │                     │               └── factory(container, [])
  │     │               │     │                     │                    → malloc(Database)
  │     │               │     │                     └── factory(container, [Database])
  │     │               │     │                          → malloc(UserRepository)
  │     │               │     └── factory(container, [UserRepository])
  │     │               │           → malloc(UserService)
  │     │               └── factory(container, [UserService])
  │     │                     → malloc(UserController)
  │     └── return UserController
  └── return UserController
```

**Complexity**: O(n) where n is the number of services in the dependency chain. Each service is resolved at most once per scope.

## 2. ORM Architecture

### 2.1 Metadata Registry Design

The ORM uses a **global metadata registry** rather than per-model metadata:

```c
static ORMMeta g_meta_registry[ORM_MAX_META];
static int     g_meta_count = 0;
```

**Why global registry:**

1. **Schema as global knowledge**: In a C application, table schemas are known at compile-time
2. **Query building**: SQL generation needs table metadata without having model instances
3. **Simplicity**: No need to pass metadata everywhere — it's available to all ORM functions

**Tradeoffs:**
- Thread safety requires a mutex around `g_meta_registry`
- Static allocation limit (32 tables by default)
- No per-connection schema isolation

### 2.2 Column Type Mapping

```
C Type          │ ORMColumnType    │ SQL Type
────────────────┼──────────────────┼────────────
int             │ ORM_TYPE_INT     │ INTEGER
int64_t         │ ORM_TYPE_INT64   │ BIGINT
float           │ ORM_TYPE_FLOAT   │ REAL
double          │ ORM_TYPE_DOUBLE  │ DOUBLE PRECISION
char[N]         │ ORM_TYPE_STRING  │ VARCHAR(N)
bool            │ ORM_TYPE_BOOL    │ BOOLEAN
char* (large)   │ ORM_TYPE_TEXT    │ TEXT
```

The mapping is used by `orm_column_type_string()` during SQL generation.

### 2.3 Query Builder SQL Generation Pipeline

```
ORMQuery struct → orm_query_generate() → SQL string → SQL backend

Input:
  table     = "users"
  columns   = ["id", "name"]
  conditions = [{col:"age", op:GT, val:"25", is_or:false},
                {col:"active", op:EQ, val:"true", is_or:false}]
  joins     = [{type:LEFT, table_b:"orders", col_a:"id", col_b:"user_id"}]
  order_by  = "name"
  order_dir = ASC
  limit_val = 10

↓ orm_query_generate()

SELECT id, name FROM users
  LEFT JOIN orders ON users.id = orders.user_id
  WHERE age > '25' AND active = 'true'
  ORDER BY name ASC LIMIT 10 OFFSET 0
```

### 2.4 Condition Logical Grouping

The OR condition flag (`is_or`) on each condition enables mixed AND/OR queries without full predicate trees:

```
Condition array: [A AND] [B OR] [C OR] [D AND]

Generated: WHERE A AND (B OR C) AND D
```

However, this linear model cannot express arbitrary boolean trees like `(A AND B) OR (C AND D)`. For such cases, raw SQL or a subquery builder would be needed.

### 2.5 ActiveRecord Lifecycle

```
[New Object]                [Existing Object]
     │                              │
     │ orm_save() → INSERT          │ orm_find() → SELECT
     │                              │
     v                              v
[Model.is_new = false] ←──── [Model populated with data]
     │                              │
     │ orm_save() → UPDATE          │ orm_save() → UPDATE
     │                              │
     v                              v
[Model persisted] ←───────── [Model persisted]
     │
     │ orm_delete() → DELETE
     │
     v
[Record deleted from database]
```

### 2.6 Schema Definition Pattern

```c
typedef struct {
    int    id;
    char   name[64];
    int    age;
    char   email[128];
} User;

static ORMColumnDef user_columns[] = {
    {"id",    ORM_TYPE_INT,    0,   true,  true,  false, ""},
    {"name",  ORM_TYPE_STRING, 64,  false, false, false, ""},
    {"age",   ORM_TYPE_INT,    0,   false, false, true,  "0"},
    {"email", ORM_TYPE_STRING, 128, false, false, false, ""},
};

// ColumnDef: {name, type, length, primary_key, auto_increment, nullable, default}
```

The column definitions are manually kept in sync with the struct layout. A production system would use macros or code generation to ensure consistency.

## 3. Cross-Cutting Concerns

### 3.1 Integration: DI → ORM

The DI container can manage ORM metadata registration and database connections:

```c
// Database connection is a singleton
di_register(&c, "db_conn", "DBConnection", SINGLETON, db_factory, NULL, 0);

// Repository depends on database
const char *deps[] = {"db_conn"};
di_register(&c, "user_repo", "UserRepository", SINGLETON, repo_factory, deps, 1);
```

### 3.2 Integration: Validator → MVC

The validator is used within MVC controller actions to validate model data before processing:

```c
static int create_action(MVCModel *model, MVCView *view, void *context) {
    Validator *v = (Validator *)context;
    if (validator_validate(v, "email", mvc_model_get(model, "email")) > 0) {
        // validation failed — return error view
    }
    // proceed with business logic
}
```

### 3.3 Integration: Serializer → MVC

JSON responses are built by serializing domain objects through the serializer:

```c
UserData user = {.id = 1, .name = "Alice", .email = "alice@example.com", .age = 30};
char json[SER_MAX_JSON_SIZE];
ser_to_json(&user_serializer, &user, json, sizeof(json));
// json = {"id":1,"name":"Alice","email":"alice@example.com","age":30}
```

## 4. Design Tradeoffs and Limitations

| Decision | Benefit | Limitation |
|---|---|---|
| Static array storage (not dynamic) | No heap allocation for registries | Fixed maximum sizes |
| Global metadata registry | Simple API | Not thread-safe without mutex |
| Linear condition model | Easy to use | Cannot express complex boolean trees |
| Explicit factory functions | Full control over construction | More boilerplate than auto-wiring |
| No circular dependency detection | Simpler code | Fails with infinite recursion |
| String-based dependency specification | Readable | No compile-time checking |
| C99 with libc only | Maximum portability | No reflection, no generics |

## References

- Gamma, Helm, Johnson, Vlissides (1994). "Design Patterns: Elements of Reusable Object-Oriented Software"
- Fowler, M. (2004). "Inversion of Control Containers and the Dependency Injection pattern"
- Seemann, M. (2011). "Dependency Injection in .NET" — Chapters 4-7: DI Patterns
- Fowler, M. (2002). "Patterns of Enterprise Application Architecture"
- Martin, R. C. (2003). "Agile Software Development: Principles, Patterns, and Practices"
- CMU 17-514: Software Architecture — Lecture notes on component-based design
