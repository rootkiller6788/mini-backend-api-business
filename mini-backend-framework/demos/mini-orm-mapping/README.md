# ORM Mapping — Object-Relational Mapping with Query Builder

## Overview

The ORM (Object-Relational Mapping) module bridges the gap between C structs and tabular data storage. It implements the ActiveRecord pattern for basic CRUD operations and a fluent query builder for constructing SQL queries programmatically. Designed for embedded and systems programming contexts where a full SQL database may not be practical, the ORM generates SQL that can be executed against any SQL-compliant backend.

The module supports column type definitions (INT, BIGINT, REAL, DOUBLE, VARCHAR, BOOLEAN, TEXT), primary key identification, auto-increment, nullable constraints, and default values. The query builder handles SELECT, WHERE with multiple operators, OR conditions, JOINs (INNER, LEFT, RIGHT), ORDER BY, and LIMIT/OFFSET.

## Theory

### Object-Relational Impedance Mismatch

The fundamental problem ORMs solve is the mismatch between the relational model (tables, rows, columns, foreign keys) and the object model (structs, pointers, hierarchies, composition):

```
Relational Model          │    C Object Model
──────────────────────────┼──────────────────────────
Table: users              │    struct User {
  id    INTEGER PK        │      int    id;
  name  VARCHAR(64)       │      char   name[64];
  email VARCHAR(128)      │      char   email[128];
                          │    };
                          │
Table: orders             │    struct Order {
  id      INTEGER PK      │      int    id;
  user_id INTEGER FK      │      int    user_id;
  amount  DOUBLE          │      double amount;
                          │    };
```

The ORM handles:
1. **Type Mapping**: SQL types ↔ C types (VARCHAR → char[], INTEGER → int)
2. **Column Mapping**: Table columns ↔ struct fields
3. **Key Management**: PRIMARY KEY, FOREIGN KEY, AUTO_INCREMENT
4. **Relationship Navigation**: JOIN generation across multiple tables

### ActiveRecord Pattern vs Data Mapper

#### ActiveRecord

```
Model = Table Row

user = User.find(1)       // Load row with id=1 into User struct
user.name = "Alice"       // Modify field
user.save()               // Persist back to database
user.delete()             // Remove from database
```

Each model instance wraps a database row. The model knows how to persist itself. **Pros**: Simple, intuitive, works well for CRUD-heavy applications. **Cons**: Tight coupling between domain logic and persistence.

#### Data Mapper

```
User user = {...};
UserMapper mapper;
mapper.insert(user);        // Separate mapper handles persistence
User loaded = mapper.find(1);
```

Domain objects are persistence-ignorant. A separate mapper handles all database operations. **Pros**: Clean separation, domain objects are pure. **Cons**: More code, more indirection.

This implementation uses the ActiveRecord pattern for simplicity. The model struct carries its own `ORMModel` metadata.

### Query Builder Design

The Query Builder uses a fluent interface to construct SQL strings:

```
ORMQuery q;
orm_query_init(&q, "users");
orm_query_where(&q, "age", ORM_OP_GT, "18");
orm_query_where(&q, "active", ORM_OP_EQ, "true");
orm_query_order(&q, "name", ORM_ORDER_ASC);
orm_query_limit(&q, 10, 0);
orm_query_generate(&q, sql, 4096);

// Produces:
// SELECT * FROM users WHERE age > '18' AND active = 'true'
// ORDER BY name ASC LIMIT 10 OFFSET 0
```

#### Supported SQL Operators

| ORM Operator | SQL Equivalent | Example |
|---|---|---|
| `ORM_OP_EQ` | `=` | `WHERE id = '5'` |
| `ORM_OP_NE` | `!=` | `WHERE status != 'deleted'` |
| `ORM_OP_LT` | `<` | `WHERE age < '30'` |
| `ORM_OP_LE` | `<=` | `WHERE price <= '100'` |
| `ORM_OP_GT` | `>` | `WHERE score > '80'` |
| `ORM_OP_GE` | `>=` | `WHERE rating >= '4'` |
| `ORM_OP_LIKE` | `LIKE` | `WHERE name LIKE '%Alice%'` |
| `ORM_OP_IN` | `IN` | `WHERE id IN (1,2,3)` |
| `ORM_OP_IS_NULL` | `IS NULL` | `WHERE deleted_at IS NULL` |

#### Logical Operators

By default, chained `orm_query_where()` calls produce `AND` conditions:

```
orm_query_where(&q, "age", ORM_OP_GT, "18");
orm_query_where(&q, "active", ORM_OP_EQ, "true");
// → age > '18' AND active = 'true'
```

Use `orm_query_or_where()` for `OR`:

```
orm_query_where(&q, "name", ORM_OP_LIKE, "%Alice%");
orm_query_or_where(&q, "email", ORM_OP_LIKE, "%@example.com");
// → name LIKE '%Alice%' OR email LIKE '%@example.com'
```

#### Join Types

| Type | Enum | SQL |
|---|---|---|
| Inner Join | `ORM_JOIN_INNER` | `INNER JOIN orders ON users.id = orders.user_id` |
| Left Join | `ORM_JOIN_LEFT` | `LEFT JOIN orders ON users.id = orders.user_id` |
| Right Join | `ORM_JOIN_RIGHT` | `RIGHT JOIN orders ON users.id = orders.user_id` |

### Table Metadata

Each table is defined with a schema of columns:

```c
ORMColumnDef user_columns[] = {
    {"id",     ORM_TYPE_INT,    0,   true,  true,  false, ""},
    {"name",   ORM_TYPE_STRING, 64,  false, false, false, ""},
    {"age",    ORM_TYPE_INT,    0,   false, false, true,  "0"},
    {"email",  ORM_TYPE_STRING, 128, false, false, false, ""},
    {"salary", ORM_TYPE_DOUBLE, 0,   false, false, true,  "0.0"},
    {"active", ORM_TYPE_BOOL,   0,   false, false, true,  "true"},
};

// ColumnDef fields: name, type, length, primary_key, auto_increment, nullable, default_value
```

This metadata is stored in a global registry (`g_meta_registry[]`) and used by the query builder and ActiveRecord operations to understand table structure.

### Generated SQL Examples

| Operation | Generated SQL |
|---|---|
| Find by PK | `SELECT * FROM users WHERE id = 1` |
| Find by field | `SELECT * FROM users WHERE email = 'alice@example.com'` |
| Insert | `INSERT INTO users (name, age, email) VALUES ('Alice', 30, 'alice@example.com')` |
| Update | `UPDATE users SET name='Alice', age=31 WHERE id=1` |
| Delete by PK | `DELETE FROM users WHERE id=1` |

## Implementation

### Header: `include/orm_core.h`

```c
typedef enum {
    ORM_TYPE_INT, ORM_TYPE_INT64, ORM_TYPE_FLOAT, ORM_TYPE_DOUBLE,
    ORM_TYPE_STRING, ORM_TYPE_BOOL, ORM_TYPE_TEXT
} ORMColumnType;

typedef enum {
    ORM_OP_EQ, ORM_OP_NE, ORM_OP_LT, ORM_OP_LE, ORM_OP_GT,
    ORM_OP_GE, ORM_OP_LIKE, ORM_OP_IN, ORM_OP_IS_NULL
} ORMOp;

typedef enum { ORM_JOIN_INNER, ORM_JOIN_LEFT, ORM_JOIN_RIGHT } ORMJoinType;
typedef enum { ORM_ORDER_ASC, ORM_ORDER_DESC } ORMOrderDir;

typedef struct {
    ORMColumnDef  columns[ORM_MAX_COLUMNS];
    int           column_count;
    size_t        struct_size;
    int           pk_index;
} ORMMeta;

typedef struct {
    char          table[ORM_MAX_TABLE_NAME];
    bool          select_all;
    char          columns[ORM_MAX_COLUMNS][ORM_MAX_COLUMN_NAME];
    int           column_count;
    ORMCondition  conditions[ORM_MAX_COLUMNS];
    int           cond_count;
    ORMJoin       joins[ORM_MAX_JOINS];
    int           join_count;
    char          order_by[ORM_MAX_COLUMN_NAME];
    ORMOrderDir   order_dir;
    int           limit_val;
    int           offset_val;
} ORMQuery;
```

### Source: `src/orm_core.c`

The source provides:
- `orm_define()`: Registers a table schema in the global meta registry
- `orm_save()`: Saves (inserts or updates) a model instance
- `orm_find()`: Finds a record by primary key
- `orm_find_by()`: Finds a record by a specific column value
- `orm_delete()` / `orm_delete_by()`: Deletes records
- `orm_query_init()`: Initializes a new query builder for a given table
- `orm_query_select()`: Specifies which columns to SELECT
- `orm_query_where()` / `orm_query_or_where()`: Adds WHERE conditions
- `orm_query_join()`: Adds JOIN clauses
- `orm_query_order()`: Adds ORDER BY clause
- `orm_query_limit()`: Adds LIMIT/OFFSET
- `orm_query_generate()`: Generates the final SQL string

## Demo: `examples/orm_demo.c`

### Expected Output

```
=== ORM Core Demo: ActiveRecord + Query Builder ===

[1] ORM initialized
    Defined table 'users' with 6 columns

[2] ActiveRecord: Save
    Saved User id=1 name=Alice age=30 salary=75000.00 active=true

[3] ActiveRecord: Find
    Found User by primary key id=1

[4] ActiveRecord: Find By
    Found User by email='alice@example.com'

[5] ActiveRecord: Delete
    Deleted User with id=1

[6] Query Builder: Basic SELECT
    SQL: SELECT * FROM users

[7] Query Builder: SELECT with columns
    SQL: SELECT id, name, email FROM users

[8] Query Builder: WHERE conditions
    SQL: SELECT * FROM users WHERE age > '25' AND active = 'true'

[9] Query Builder: OR WHERE
    SQL: SELECT * FROM users WHERE name LIKE '%Alice%' OR email LIKE '%@example.com'

[10] Query Builder: JOIN
    SQL: SELECT * FROM users INNER JOIN orders ON users.id = orders.user_id

[11] Query Builder: ORDER BY + LIMIT
    SQL: SELECT * FROM users WHERE active = 'true' ORDER BY salary DESC LIMIT 10 OFFSET 0

[12] Query Builder: IS NULL
    SQL: SELECT * FROM users WHERE email IS NULL

[13] Query Builder: Complex query
    SQL: SELECT * FROM users LEFT JOIN departments ON users.dept_id = departments.id
    WHERE age >= '18' AND age <= '65' ORDER BY name ASC LIMIT 50 OFFSET 100

=== ORM Core Demo Complete ===
```

## Building

```bash
make orm_demo
./bin/orm_demo
```

## References

- Fowler, M. (2002). "Patterns of Enterprise Application Architecture" — Chapters 10-12: Data Source Architectural Patterns
- Ambler, S. W. (2000). "The Design of a Robust Persistence Layer for Relational Databases"
- Korth, Silberschatz, Sudarshan. "Database System Concepts" — Chapter 4-5: SQL and Relational Algebra
- Hecht, R. & Jablonski, S. (2011). "NoSQL Evaluation: A Use Case Oriented Survey"
- Chen, P. P. (1976). "The Entity-Relationship Model — Toward a Unified View of Data" (TODS)
- Codd, E. F. (1970). "A Relational Model of Data for Large Shared Data Banks" (CACM)
