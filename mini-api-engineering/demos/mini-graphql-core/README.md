# mini-graphql-core — GraphQL 核心深度解析

> 深入理解 GraphQL 引擎核心：Schema 定义、类型系统、查询解析、Mutation、解析器、SDL、内省机制

## 目录 Table of Contents

1. [GraphQL 概述](#1-graphql-概述)
2. [类型系统 Type System](#2-类型系统-type-system)
3. [Schema 构建 Schema Definition](#3-schema-构建-schema-definition)
4. [查询解析 Query Parsing](#4-查询解析-query-parsing)
5. [Mutation 变更操作](#5-mutation-变更操作)
6. [解析器 Resolvers](#6-解析器-resolvers)
7. [SDL — Schema Definition Language](#7-sdl--schema-definition-language)
8. [内省 Introspection](#8-内省-introspection)
9. [查询验证与错误处理](#9-查询验证与错误处理)
10. [执行模型 Execution Model](#10-执行模型-execution-model)
11. [GraphQL vs REST 对比](#11-graphql-vs-rest-对比)
12. [最佳实践与反模式](#12-最佳实践与反模式)

---

## 1. GraphQL 概述

GraphQL 由 Facebook 于 2012 年开发，2015 年开源，是一种用于 API 的查询语言和运行时。它允许客户端精确指定所需的数据，解决了 REST API 中常见的过度获取 (Over-fetching) 和不足获取 (Under-fetching) 问题。

### 1.1 核心理念

```
传统 REST                          GraphQL
┌─────────┐                        ┌─────────┐
│ GET     │  → /users/42           │ query { │
│ /users/42│  → /users/42/orders    │  user(id:42) { │
│ GET     │  → /users/42/posts     │   name │
│ /orders  │    多次请求             │   orders { id total } │
│ GET     │                        │   posts { title } │
│ /posts   │                        │  } │
└─────────┘                        │ } │
                                   └─────────┘
                                   ← 单次请求，精确数据
```

### 1.2 GraphQL 三大操作类型

```graphql
# Query  — 读取数据（只读）
query {
  user(id: 42) { name email }
}

# Mutation — 修改数据（创建、更新、删除）
mutation {
  createUser(name: "Eve", email: "eve@example.com") {
    id
    name
  }
}

# Subscription — 实时订阅（WebSocket，本库暂不完整支持）
subscription {
  userCreated { id name }
}
```

### 1.3 本库模块 `graphql_engine.h` 架构

```
用户请求 (GraphQL Query String)
    │
    ▼
┌─────────────────┐
│  gql_parse_query │  → 词法/语法分析 → gql_parsed_query_t
└────────┬────────┘
         │
         ▼
┌───────────────────┐
│  gql_validate_query│  → Schema 校验（类型、字段存在性）
└────────┬──────────┘
         │
         ▼
┌─────────────────┐
│  gql_execute     │  → 递归调用 gql_resolver_fn
└────────┬────────┘
         │
         ▼
    JSON 响应
```

---

## 2. 类型系统 Type System

### 2.1 标量类型 (Scalar Types)

GraphQL 内置五种标量类型：

| 标量 | 含义 | JSON 对应 | 示例 |
|---|---|---|---|
| **String** | UTF-8 字符串 | `string` | `"Hello, World!"` |
| **Int** | 32 位有符号整数 | `number` | `42`, `-7` |
| **Float** | 双精度浮点数 | `number` | `3.14`, `-0.5` |
| **Boolean** | 布尔值 | `boolean` | `true`, `false` |
| **ID** | 唯一标识符（序列化为字符串） | `string` | `"abc123"`, `"42"` |

### 2.2 本库标量类型枚举

```c
typedef enum {
    GQL_TYPE_STRING,
    GQL_TYPE_INT,
    GQL_TYPE_FLOAT,
    GQL_TYPE_BOOLEAN,
    GQL_TYPE_ID,
    GQL_TYPE_OBJECT,        // 自定义对象类型
    GQL_TYPE_LIST,          // 列表类型 [Type]
    GQL_TYPE_NON_NULL,      // 非空类型 Type!
    GQL_TYPE_ENUM,          // 枚举类型
    GQL_TYPE_INPUT_OBJECT,  // 输入对象类型
    GQL_TYPE_SCALAR         // 自定义标量
} gql_scalar_t;
```

### 2.3 对象类型 (Object Types)

对象类型是 GraphQL 的核心——由字段集合组成：

```graphql
type User {
  id: ID!           # 非空 ID
  name: String!     # 非空字符串
  email: String     # 可选的字符串
  age: Int          # 可选的整数
  posts: [Post]     # Post 列表
  role: Role        # 枚举
}
```

**C 代码对应：**

```c
gql_schema_add_type(&e.schema, "User");
gql_schema_add_field(&e.schema, "User", "id",    GQL_TYPE_ID,     "ID",     false, true);
gql_schema_add_field(&e.schema, "User", "name",  GQL_TYPE_STRING, "String", false, true);
gql_schema_add_field(&e.schema, "User", "email", GQL_TYPE_STRING, "String", false, false);
gql_schema_add_field(&e.schema, "User", "age",   GQL_TYPE_INT,    "Int",    false, false);
gql_schema_add_field(&e.schema, "User", "posts", GQL_TYPE_OBJECT, "Post",   true,  false);
```

### 2.4 非空和列表修饰符

```graphql
String          # 可空字符串
String!         # 非空字符串（不能返回 null）
[String]        # 字符串列表（列表本身可 null，元素可 null）
[String!]       # 非空字符串列表（元素非空，列表可 null）
[String]!       # 非空列表（列表非 null，元素可 null）
[String!]!      # 非空列表且元素非空
```

**C 代码中的表示：**

```c
// name: String!  → is_non_null=true
gql_schema_add_field(&e.schema, "User", "name", GQL_TYPE_STRING, "String", false, true);

// posts: [Post]  → is_list=true
gql_schema_add_field(&e.schema, "User", "posts", GQL_TYPE_OBJECT, "Post", true, false);
```

### 2.5 枚举类型 (Enum Types)

```graphql
enum Role {
  ADMIN
  USER
  GUEST
}
```

```c
gql_schema_add_type(&e.schema, "Role");
gql_schema_add_enum_value(&e.schema, "Role", "ADMIN");
gql_schema_add_enum_value(&e.schema, "Role", "USER");
gql_schema_add_enum_value(&e.schema, "Role", "GUEST");
```

### 2.6 字段参数 (Field Arguments)

```graphql
type Query {
  user(id: ID!): User        # 必填参数
  search(query: String!): [Post]  # 必填参数
  posts(limit: Int = 10, offset: Int = 0): [Post]  # 可选参数带默认值
}
```

```c
gql_schema_add_field_arg(&e.schema, "Query", "user", "id", "ID", true);
gql_schema_add_field_arg(&e.schema, "Query", "search", "query", "String", true);
```

---

## 3. Schema 构建 Schema Definition

### 3.1 Schema 结构

```c
typedef struct {
    gql_type_t     types[GQL_MAX_TYPES];        // 类型定义
    int32_t        type_count;
    gql_resolver_t resolvers[GQL_MAX_RESOLVERS]; // 解析器
    int32_t        resolver_count;
    char           query_type[GQL_MAX_NAME_LEN];        // "Query"
    char           mutation_type[GQL_MAX_NAME_LEN];     // "Mutation"
    char           subscription_type[GQL_MAX_NAME_LEN]; // "Subscription"
} gql_schema_t;
```

### 3.2 逐步构建 Schema

```c
gql_engine_t e;
gql_engine_init(&e);

// 1. 定义对象类型
gql_schema_add_type(&e.schema, "User");
gql_schema_add_field(&e.schema, "User", "id",   GQL_TYPE_ID,     "ID",     false, true);
gql_schema_add_field(&e.schema, "User", "name", GQL_TYPE_STRING, "String", false, true);

gql_schema_add_type(&e.schema, "Post");
gql_schema_add_field(&e.schema, "Post", "title", GQL_TYPE_STRING, "String", false, true);
gql_schema_add_field(&e.schema, "Post", "content", GQL_TYPE_STRING, "String", false, false);

// 2. 定义 Query 根类型
gql_schema_add_type(&e.schema, "Query");
gql_schema_add_field(&e.schema, "Query", "hello", GQL_TYPE_STRING, "String", false, false);
gql_schema_add_field(&e.schema, "Query", "user",  GQL_TYPE_OBJECT, "User",   false, false);
gql_schema_add_field_arg(&e.schema, "Query", "user", "id", "ID", true);

// 3. 设置根操作类型
gql_schema_set_query_type(&e.schema, "Query");

// 4. 注册解析器
gql_schema_add_resolver(&e.schema, "Query", "hello", resolve_hello, NULL);
gql_schema_add_resolver(&e.schema, "Query", "user",  resolve_user,  NULL);
```

### 3.3 Schema 设计原则

```
1. 根 Query 类型应有一个直观的命名（通常就叫 "Query"）
2. 每个对象类型的字段应语义内聚
3. 列表字段尽量提供过滤、分页参数
4. 枚举明确定义业务状态
5. 避免深度嵌套（影响查询性能）
6. 字段命名使用 camelCase
```

---

## 4. 查询解析 Query Parsing

### 4.1 查询结构

```graphql
query GetUserProfile {        # 操作名（可选）
  user(id: 42) {              # 带参数的字段
    name                      # 叶子字段
    email
    posts(limit: 5) {         # 嵌套字段，带参数
      title
      published
    }
  }
}
```

### 4.2 解析结果结构

```c
typedef struct {
    gql_operation_t   operation;          // QUERY / MUTATION / SUBSCRIPTION
    char              operation_name[128]; // "GetUserProfile"
    char              target_field[128];   // "user"
    gql_selection_t   selections[128];    // 选择的字段
    int32_t           selection_count;
    gql_argument_t    args[16];           // 参数
    int32_t           arg_count;
} gql_parsed_query_t;
```

### 4.3 简单查询解析

```c
gql_engine_t e;
gql_engine_init(&e);
const char* query = "{ hello }";
bool ok = gql_parse_query(&e, query);
// ok == true
// e.parsed.selection_count == 1
// e.parsed.selections[0].field_name == "hello"
```

### 4.4 带参数的查询解析

```c
const char* query = "{ user(id: 42) { name email } }";
bool ok = gql_parse_query(&e, query);
// e.parsed.target_field == "user"
// e.parsed.arg_count == 1
// e.parsed.args[0].name == "id", value == "42"
// e.parsed.selection_count == 2
// e.parsed.selections[0].field_name == "name"
// e.parsed.selections[1].field_name == "email"
```

### 4.5 解析步骤

```
输入: "{ user(id: 42) { name email } }"
  │
  ├── Step 1: 识别操作类型（没有前缀 → 默认 QUERY）
  │
  ├── Step 2: 提取根字段 "user" 和参数 "id" = "42"
  │
  ├── Step 3: 提取选择集 { name email }
  │    ├── "name" → is_leaf = true
  │    └── "email" → is_leaf = true
  │
  └── 输出: gql_parsed_query_t
```

### 4.6 嵌套查询

```graphql
{
  user(id: 42) {
    name           # 第一层选择
    posts {        # 第二层嵌套
      title
      author {     # 第三层嵌套
        name
      }
    }
  }
}
```

本库的解析器会标记 leaf 字段，执行引擎递归处理嵌套对象。

---

## 5. Mutation 变更操作

### 5.1 Mutation 与 Query 的区别

| 特性 | Query | Mutation |
|---|---|---|
| 语义 | 读取数据 | 修改数据 |
| 执行顺序 | 并行 | **严格串行** |
| 副作用 | 无 | 有（创建、更新、删除） |
| 缓存 | 可缓存 | 不可缓存 |
| 关键词 | `query` | `mutation` |

### 5.2 Mutation 查询示例

```graphql
mutation {
  createUser(name: "Eve", email: "eve@example.com") {
    id
    name
    email
  }
}
```

### 5.3 Mutation 解析

```c
const char* query = "mutation { createUser(name: \"Eve\") { id name } }";
bool ok = gql_parse_query(&e, query);
// e.parsed.operation == GQL_OP_MUTATION
// e.parsed.target_field == "createUser"
// e.parsed.args[0] == { name="name", value="Eve" }
```

### 5.4 典型 Mutation 模式

```graphql
# CRUD 模式
type Mutation {
  createUser(input: CreateUserInput!): User!
  updateUser(id: ID!, input: UpdateUserInput!): User!
  deleteUser(id: ID!): Boolean!
}

# 业务操作模式
type Mutation {
  placeOrder(cartId: ID!, paymentMethod: String!): Order!
  cancelOrder(orderId: ID!, reason: String): Order!
  addToCart(productId: ID!, quantity: Int!): Cart!
}
```

### 5.5 Mutation 最佳实践

```
1. 使用动词命名：create, update, delete, cancel, place
2. 返回操作后的对象（而非仅返回 ID）
3. 使用 Input 对象封装多个参数
4. Mutation 之间串行执行（保证数据一致性）
5. 在 Schema 中明确定义 Mutation 根类型
```

---

## 6. 解析器 Resolvers

### 6.1 解析器函数签名

```c
typedef void* (*gql_resolver_fn)(void* parent, void* args, void* context);

// parent  — 父级解析器的返回值
// args    — 字段参数（结构体）
// context — 请求级别上下文（如 DB 连接、认证信息）
// 返回值   — 字段值（字符串指针、结构体指针等）
```

### 6.2 注册解析器

```c
// 注册一个解析器到特定类型的特定字段
gql_schema_add_resolver(&e.schema, "Query", "hello", resolve_hello, NULL);
gql_schema_add_resolver(&e.schema, "Query", "user",  resolve_user,  NULL);
gql_schema_add_resolver(&e.schema, "User",  "name",  resolve_user_name, NULL);
```

### 6.3 简单解析器实现

```c
static void* resolve_hello(void* parent, void* args, void* context) {
    (void)parent;
    (void)args;
    (void)context;
    printf("  [resolver] hello() called\n");
    return (void*)"Hello, World!";
}

static void* resolve_user_name(void* parent, void* args, void* context) {
    (void)parent;
    (void)args;
    (void)context;
    // parent 指向父解析器返回的用户结构体
    printf("  [resolver] User.name called\n");
    return (void*)"Alice";
}
```

### 6.4 解析器执行链

```
查询: { user { name } }
  │
  ├── resolve_user(parent=NULL, args=NULL, context=...)
  │         → 返回 user_data
  │
  └── resolve_user_name(parent=user_data, args=NULL, context=...)
            → 返回 "Alice"
```

### 6.5 解析器设计原则

```
1. 每个字段一个解析器（单一职责）
2. 解析器应纯净（无副作用，尤其对于 Query）
3. 避免 N+1 查询问题（使用 DataLoader 模式）
4. 解析器错误应返回 null 而非抛出异常
5. 解析器应通过 context 获取共享资源（而非全局变量）
```

---

## 7. SDL — Schema Definition Language

SDL 是描述 GraphQL Schema 的领域特定语言，既可人工编写，也可由工具生成。

### 7.1 SDL 语法

```graphql
# 标量字段
type User {
  id: ID!
  name: String!
  email: String
  age: Int
  isActive: Boolean
}

# 列表字段
type Post {
  title: String!
  tags: [String!]!
}

# 枚举类型
enum Status {
  ACTIVE
  INACTIVE
  SUSPENDED
}

# 带参数的字段
type Query {
  hello: String
  user(id: ID!): User
  search(query: String!, limit: Int = 10): [Post]
}

# 根类型声明
schema {
  query: Query
  mutation: Mutation
}
```

### 7.2 解析 SDL

```c
gql_engine_t e;
gql_engine_init(&e);

const char* sdl =
    "type Query {\n"
    "  hello: String\n"
    "  user(id: ID!): User\n"
    "}\n"
    "type User {\n"
    "  id: ID!\n"
    "  name: String!\n"
    "  email: String\n"
    "}\n";

bool ok = gql_parse_schema_sdl(&e, sdl);
// ok == true → Schema 从 SDL 构建完毕

// 遍历生成的类型
for (int32_t i = 0; i < e.schema.type_count; i++) {
    gql_type_t* t = &e.schema.types[i];
    printf("type %s (%d fields)\n", t->name, t->field_count);
}
```

### 7.3 SDL 与程序化构建的对比

| 特性 | SDL 解析 | 程序化构建 (C API) |
|---|---|---|
| 可读性 | 高，人类友好 | 较低，需理解 C 结构体 |
| 灵活性 | 静态描述 | 动态构建、条件添加 |
| 工具支持 | 大量生态工具 | 需自行实现 |
| 适合场景 | 服务端定义 | 嵌入式/生成代码 |

---

## 8. 内省 Introspection

内省是 GraphQL 允许客户端查询 Schema 自身的能力，是 GraphiQL 等工具的基础。

### 8.1 内省查询示例

```graphql
# 查询所有类型
{
  __schema {
    types {
      name
      kind
      fields {
        name
        type {
          name
          kind
        }
      }
    }
    queryType { name }
  }
}

# 查询特定类型
{
  __type(name: "User") {
    name
    fields {
      name
      type {
        name
        kind
        ofType { name }
      }
    }
  }
}
```

### 8.2 内省保留类型

```
__Schema      — Schema 本身
__Type        — 类型描述
__Field       — 字段描述
__InputValue  — 参数描述
__EnumValue   — 枚举值描述
__Directive   — 指令描述
```

### 8.3 本库内省 API

```c
// 获取整个 Schema 的内省信息
char buf[2048];
gql_introspect_schema(&e, buf, sizeof(buf));
printf("Introspection:\n%s\n", buf);

// 获取特定类型的内省信息
char type_buf[1024];
gql_introspect_type(&e, "User", type_buf, sizeof(type_buf));
printf("Type introspection:\n%s\n", type_buf);
```

### 8.4 内省输出格式

```json
{
  "__schema": {
    "types": [
      {
        "name": "Query",
        "kind": "OBJECT",
        "fields": [
          {
            "name": "hello",
            "type": { "name": "String", "kind": "SCALAR" }
          },
          {
            "name": "user",
            "type": { "name": "User", "kind": "OBJECT" }
          }
        ]
      },
      {
        "name": "String",
        "kind": "SCALAR"
      }
    ],
    "queryType": { "name": "Query" }
  }
}
```

### 8.5 内省最佳实践

```
1. 生产环境可限制或禁用内省（安全考虑）
2. 内省输出供工具链使用（代码生成、文档生成）
3. 使用内省实现客户端 Schema 验证
4. 内省是构建 GraphQL IDE（如 GraphiQL）的基础
```

---

## 9. 查询验证与错误处理

### 9.1 验证流程

```c
// 1. 解析查询
bool parsed = gql_parse_query(&e, query);

// 2. 验证查询（检查类型、字段、参数合法性）
bool valid = gql_validate_query(&e);

// 3. 获取错误信息
int32_t count = gql_error_count(&e);
for (int32_t i = 0; i < count; i++) {
    printf("Error %d: %s\n", i, gql_error_message(&e, i));
}
```

### 9.2 错误结构

```c
typedef struct {
    char message[512];  // 错误描述
    int  line;          // 行号
    int  column;        // 列号
} gql_error_t;
```

### 9.3 常见验证错误

| 错误类型 | 描述 | 示例 |
|---|---|---|
| **字段不存在** | 查询中引用了 Schema 未定义的字段 | `user { nonexistent }` |
| **类型不匹配** | 参数类型与 Schema 定义不一致 | `user(id: "abc")` 期望 ID |
| **缺少必填参数** | 未提供必填的 `!` 参数 | `user()` 缺少 id |
| **不支持的操作** | Schema 未定义 Mutation 但查询中使用了 | `mutation { ... }` 但 schema 无 Mutation 类型 |
| **循环引用** | Schema 中存在类型循环依赖（可能导致无限递归） | A → B → A |

### 9.4 错误处理原则

```
1. 验证失败不应导致整个查询失败（只标记错误字段为 null）
2. 提供精确的错误位置（行号、列号）
3. 错误消息应清晰描述问题及修复建议
4. 支持多个错误同时返回
```

---

## 10. 执行模型 Execution Model

### 10.1 递归执行

```
gql_execute(&e, NULL)
  │
  ├── 遍历 parsed.selections[]
  │   ├── 查找对应字段的 gql_resolver_fn
  │   ├── 调用 resolver(parent, args, context)
  │   └── 如果字段是对象类型 → 递归执行子选择集
  │
  └── 返回结果树
```

### 10.2 执行特点

```
Query:
  解析阶段: 自上而下 (Top-down)
  执行阶段: 自下而上 (Bottom-up)，但 Query 的字段可并行执行

Mutation:
  解析阶段: 自上而下
  执行阶段: 严格串行，按查询中出现的顺序依次执行
```

### 10.3 解析器返回值

```c
// 标量字段：返回字符串指针
return (void*)"hello";

// 数字字段：返回结构体指针
int64_t* id_val = malloc(sizeof(int64_t));
*id_val = 42;
return (void*)id_val;

// 对象字段：返回结构体指针（供子解析器使用）
user_t* user = get_user_from_db();
return (void*)user;
```

---

## 11. GraphQL vs REST 对比

| 维度 | REST | GraphQL |
|---|---|---|
| **数据获取** | 每个资源一个端点 | 单一端点 `/graphql` |
| **过度获取** | 常返回多余字段 | 客户端精确指定字段 |
| **不足获取** | 需要多次请求 | 单次请求获取所有数据 |
| **版本控制** | URI 版本（`/v1`, `/v2`） | Schema 演进（不破坏性变更） |
| **缓存** | HTTP 缓存开箱即用 | 需要自定义缓存层（如 APQ） |
| **学习曲线** | 低 | 中高 |
| **工具链** | 成熟（Postman, curl） | GraphiQL, Apollo, Relay |
| **性能** | 简单请求更轻量 | 复杂查询减少网络往返 |
| **类型安全** | 需借助 OpenAPI/JSON Schema | Schema 天生强类型 |
| **文件上传** | 原生 multipart/form-data | 需使用 Upload 标量或扩展 |
| **适用场景** | CRUD、微服务、公开 API | 复杂数据图、移动端、多变 UI |

### 11.1 选择建议

```
选择 REST 当:
  ✓ API 简单、CRUD 为主
  ✓ 需要 HTTP 缓存
  ✓ 公开给第三方，需 HTTP 状态码语义
  ✓ 团队不熟悉 GraphQL

选择 GraphQL 当:
  ✓ 数据关系复杂（图状结构）
  ✓ 多种客户端（Web/iOS/Android）需求不同
  ✓ 需要减少请求次数（移动网络）
  ✓ 前端团队需要灵活的数据查询
```

---

## 12. 最佳实践与反模式

### 12.1 Schema 设计最佳实践

| 实践 | 说明 |
|---|---|
| **使用 connection 模式分页** | 而非简单列表，提供 `edges { node, cursor }` 和 `pageInfo` |
| **Input 对象** | 当字段参数超过 3 个时封装为 Input Object |
| **全局 ID** | 使用 `base64(Type:Id)` 格式的全局唯一 ID |
| **错误作为数据** | 用 Union 类型 `type Result = User | Error` 而非顶层 errors |
| **nullable 优先** | 先设计可空字段，必要时再加 `!` |
| **Schema 描述** | 为类型、字段、参数添加 `"""描述"""` |
| **枚举代替魔法字符串** | 使用枚举类型而非 String 表示选项 |

### 12.2 常见反模式

| 反模式 | 问题 | 改进方案 |
|---|---|---|
| **万能 Query** | `query(fields: [String!]!)` 失去类型安全 | 明确定义每个查询字段 |
| **深层嵌套** | 查询深度过深，性能差 | 设置查询深度限制 |
| **大杂烩 Schema** | 所有类型放在一个文件 | 模块化 Schema（按领域拆分） |
| **忽略 N+1** | 列表查询中每个元素触发子查询 | 使用 DataLoader 批量加载 |
| **Mutation 返回 null** | 客户端无法确认操作结果 | Mutation 应返回操作后的对象 |
| **String 滥用** | 日期、货币、百分比都用 String | 自定义标量类型 |
| **无查询复杂度限制** | 恶意查询耗尽服务器资源 | 设置复杂度上限 |

### 12.3 安全实践

```
1. 设置查询深度限制（如深度 ≤ 5）
2. 设置查询复杂度上限（如每个字段 1 分，上限 100 分）
3. 禁用生产环境的内省（可选）
4. 超时保护（每个查询设置执行时间上限）
5. 限制请求体大小（如 10KB）
6. 对列表字段强制分页（限制 max limit）
```

---

## 运行演示

```bash
# 编译 GraphQL 演示程序
make demo_graphql_query

# 运行
bin/demo_graphql_query

# 演示内容:
#   - 简单查询解析: "{ hello }"
#   - 带参数查询: "{ user(id: 42) { name email } }"
#   - Schema 构建 (User, Post, Query, Role)
#   - 解析器注册与执行
#   - SDL 解析
#   - Schema 内省
#   - Mutation 解析
```

---

## 参考资料 References

- [GraphQL Spec (October 2021)](https://spec.graphql.org/)
- [GraphQL Foundation](https://graphql.org/)
- [Apollo GraphQL](https://www.apollographql.com/)
- [GraphQL Type System](https://graphql.org/learn/schema/)
- [GraphQL Best Practices](https://graphql.org/learn/best-practices/)
- [The Guild — GraphQL Tools](https://the-guild.dev/graphql)
- [Production Ready GraphQL — Marc-Andre Giroux](https://book.graphqlsyntax.com/)
- [GraphQL vs REST — Phil Sturgeon](https://apisyouwonthate.com/)

## 本库相关文件

- `include/graphql_engine.h` — GraphQL 引擎核心 API
- `src/graphql_engine.c` — 查询解析、Schema 构建、SDL 解析、内省实现
- `examples/demo_graphql_query.c` — 完整可运行的 GraphQL 演示
- `docs/demo_graphql_query.md` — GraphQL 演示文档
- `demos/mini-restful-design/README.md` — RESTful API 深度解析
