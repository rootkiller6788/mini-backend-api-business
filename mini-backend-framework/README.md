# mini-backend-framework — 后端框架 (C 语言实现)

> 参考 CMU 17-313 Foundations of Software Engineering, CMU 17-514 Software Architecture, Stanford CS142 Web Applications, MIT 6.031 Software Construction

A lightweight backend framework implemented in pure C99, providing Dependency Injection, ORM with Query Builder, MVC pattern with template rendering, input validation, and JSON serialization. No external dependencies beyond libc and libm. Designed for educational clarity and embedded/constrained environments.

## Module Table — 模块表

| Module | Header | Source | Description |
|--------|--------|--------|-------------|
| **DI Container** | `include/di_container.h` | `src/di_container.c` | Dependency Injection — service registration by name/type, constructor injection, singleton/transient/request scopes, recursive resolution |
| **ORM Core** | `include/orm_core.h` | `src/orm_core.c` | Object-Relational Mapping — ActiveRecord pattern (save/find/delete), fluent query builder (select/where/join/order), table↔struct mapping |
| **MVC Pattern** | `include/mvc_pattern.h` | `src/mvc_pattern.c` | Model-View-Controller — model with field validation, template-based view with variable substitution, controller with route dispatch |
| **Validator** | `include/validator.h` | `src/validator.c` | Input validation — required, min/max length, regex pattern, email format, numeric, integer, min/max value, custom rules with error collection |
| **Serializer** | `include/serializer.h` | `src/serializer.c` | JSON Serialization — struct→JSON, JSON→struct, field mapping with name aliases, ignore fields, custom encoder/decoder, nested objects and arrays |

## Directory Tree — 目录树

```
mini-backend-framework/
├── README.md
├── Makefile
├── include/
│   ├── di_container.h
│   ├── orm_core.h
│   ├── mvc_pattern.h
│   ├── validator.h
│   └── serializer.h
├── src/
│   ├── di_container.c
│   ├── orm_core.c
│   ├── mvc_pattern.c
│   ├── validator.c
│   └── serializer.c
├── examples/
│   ├── di_demo.c
│   ├── orm_demo.c
│   └── mvc_demo.c
├── demos/
│   ├── mini-di-container/
│   │   └── README.md
│   └── mini-orm-mapping/
│       └── README.md
├── docs/
│   ├── course-alignment.md
│   └── di-orm-architecture.md
├── tests/
└── benches/
```

## Build Commands — 构建命令

```bash
# Build all demos
make all

# Build individual demos
make di_demo
make orm_demo
make mvc_demo

# Run all demos
make test

# Clean build artifacts
make clean
```

## Dependencies

- C99 compiler (GCC or Clang)
- libm (math library)
- No other external dependencies

## Key Concepts

### DI Container

- **Service Registration**: Register by unique name and type name, specify scope and factory
- **Constructor Injection**: Dependencies are declared during registration and injected via factory parameters
- **Scopes**: SINGLETON (one per container), TRANSIENT (new per resolve), REQUEST (one per request context)
- **Recursive Resolution**: Depth-first resolution of the full dependency tree on first access
- **Type-Based Lookup**: Resolve services by their registered type name

### ORM Core

- **ActiveRecord**: Model structs carry `ORMModel` metadata; `orm_save()`, `orm_find()`, `orm_delete()` handle persistence
- **Table Metadata**: Column definitions describe type, length, primary key, auto-increment, nullable, defaults
- **Query Builder**: Fluent interface — `orm_query_init()` → `orm_query_where()` → `orm_query_order()` → `orm_query_generate()`
- **SQL Operators**: EQ, NE, LT, LE, GT, GE, LIKE, IN, IS NULL
- **JOIN Support**: INNER JOIN, LEFT JOIN, RIGHT JOIN with ON clause generation
- **Pagination**: ORDER BY ASC/DESC, LIMIT/OFFSET

### MVC Pattern

- **Model**: Named field definitions with required/min/max length/pattern validation, value storage and retrieval
- **View**: Template-based rendering with `{{ variable }}` substitution, variable assignment API
- **Controller**: Route registration by HTTP method + path pattern, dispatch with path parameter matching (`{id}` tokens)
- **Path Matching**: Route patterns with `{param}` placeholders match dynamic URL segments

### Validator

- **Built-in Rules**: required, min_length, max_length, regex, email, numeric, integer, min_value, max_value
- **Custom Rules**: User-defined callback with access to value, parameter, and error output buffer
- **Error Collection**: Accumulates all validation errors with field name and descriptive message
- **Email Validation**: Checks for @ symbol, local part length, domain with dot, no spaces or double-dots
- **Regex Support**: Pre-defined patterns for alphanumeric, alpha-only, and numeric-only strings

### Serializer

- **Field Mapping**: Each struct field maps to a JSON key, with optional name alias (different JSON name)
- **Type Support**: int, int64, float, double, string (char[]), bool, nested objects, arrays
- **Custom Encoding**: Per-field encoder/decoder functions for non-standard serialization
- **Ignore Fields**: Skip specific fields during serialization
- **JSON Utilities**: `ser_json_get_string()`, `ser_json_get_int()`, `ser_json_get_bool()` for parsing
- **String Escaping**: Automatic escaping of quotes, backslashes, newlines, tabs in string values

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                  HTTP Request                            │
│                  GET /users/42                           │
└─────────────────────┬───────────────────────────────────┘
                      │
                      v
┌─────────────────────────────────────────────────────────┐
│  MVC Controller                                         │
│  ┌───────────────────────────────────────────────────┐  │
│  │  Route Dispatch (mvc_dispatch)                     │  │
│  │  /users/{id} → GET → user_get_action               │  │
│  └───────────────────┬───────────────────────────────┘  │
│                      │                                   │
│  ┌───────────────────v───────────────────────────────┐  │
│  │  Model (MVCModel)                                  │  │
│  │  - Extract parameters (id=42)                      │  │
│  │  - Validate with Validator                         │  │
│  └───────────────────┬───────────────────────────────┘  │
│                      │                                   │
│  ┌───────────────────v───────────────────────────────┐  │
│  │  Controller Action                                 │  │
│  │  - Resolve UserService from DI Container           │  │
│  │  - UserService.find(42) → ORM query                │  │
│  │  - Serialize result to JSON                        │  │
│  └───────────────────┬───────────────────────────────┘  │
│                      │                                   │
│  ┌───────────────────v───────────────────────────────┐  │
│  │  View (MVCView)                                    │  │
│  │  - Assign template variables                       │  │
│  │  - Render template (or return JSON)                │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
                      │
                      v
┌─────────────────────────────────────────────────────────┐
│                  HTTP Response                           │
│                  200 OK + JSON body                      │
└─────────────────────────────────────────────────────────┘
```

## Integration Example

```c
// 1. Define data model
typedef struct { int id; char name[64]; } User;

// 2. Configure ORM
ORMColumnDef cols[] = {
    {"id", ORM_TYPE_INT, 0, true, true, false, ""},
    {"name", ORM_TYPE_STRING, 64, false, false, false, ""},
};

// 3. Set up DI container
DIContainer di;
di_init(&di);
di_register(&di, "user_svc", "UserService", DI_SCOPE_SINGLETON,
            user_service_factory, NULL, 0);

// 4. Register MVC routes
MVCController ctrl;
mvc_register_route(&ctrl, MVC_GET, "/users/{id}", "user.show", user_show_action);

// 5. Handle request
MVCModel model;
MVCView view;
mvc_dispatch(&ctrl, MVC_GET, "/users/42", &model, &view, &di);
```
