# demo_graphql_query — GraphQL 查询引擎演示

## 概述 Overview

`demo_graphql_query` 是一个 C 语言程序，演示如何使用 `mini-api-engineering` 库中的 `graphql_engine` 模块来构建一个轻量级的 GraphQL 查询引擎。该演示涵盖 GraphQL Schema 定义、SDL (Schema Definition Language) 解析、查询解析、字段选择、参数提取、解析器 (Resolver) 注册与执行、以及自省 (Introspection) 查询。

## 架构理念 Architecture Philosophy

GraphQL 是 Facebook 开发的一种 API 查询语言，核心思想是让客户端精确指定所需的数据结构，避免 REST 常见的「过度获取」(over-fetching) 和「获取不足」(under-fetching) 问题。

本库的 `graphql_engine` 模块提供以下核心能力：
- **Schema 定义**：类型系统 (Object / Scalar / Enum / List / NonNull)
- **SDL 解析**：从 Schema Definition Language 文本构建类型系统
- **查询解析**：支持 query / mutation / subscription 操作，嵌套字段选择，参数绑定
- **解析器映射**：类型 + 字段维度的解析器注册表
- **查询执行**：递归遍历选择集，调用对应解析器
- **自省 (Introspection)**：实现 `__schema` 和 `__type` 查询，输出 JSON 格式

## 程序结构 Program Structure

```
demo_graphql_query.c
├── resolve_hello()          解析器：返回问候语
├── resolve_user()           解析器：返回用户对象
├── resolve_user_name()      解析器：返回用户名
├── resolve_search()         解析器：搜索功能
├── demo_parse_simple_query()   简单查询解析演示
├── demo_parse_with_args()      带参数查询解析
├── demo_schema_building()      Schema 构建演示
├── demo_resolvers()            解析器注册与执行
├── demo_sdl_parsing()          SDL 文本解析
├── demo_introspection()        自省查询
├── demo_mutation_parsing()     Mutation 解析
└── main()                      入口函数
```

## 编译与运行 Build & Run

### 前提条件 Prerequisites

- C99 兼容的编译器
- GNU Make 或兼容构建工具

### 构建 Build

```bash
# 进入项目根目录
cd mini-api-engineering

# 编译所有示例
make

# 仅编译 GraphQL 演示
gcc -std=c99 -Wall -Wextra -I include -o build/demo_graphql_query \
    examples/demo_graphql_query.c src/graphql_engine.c
```

### 运行 Run

```bash
./build/demo_graphql_query
```

### 预期输出片段 Expected Output

```
=== mini-api-engineering: GraphQL Query Demo ===

--- Demo: Parse Simple Query ---
Input: { hello }
Parsed: SUCCESS
Selections: 1
  field: hello

--- Demo: Schema Building ---
Schema types: 4
  Query (3 fields)
    user: User
    hello: String
    search: Post[]
  Post (3 fields)
    title: String!
    published: Boolean
    content: String
  User (5 fields)
    age: Int
    name: String!
    email: String
    id: ID!
    posts: Post[]
  Role [ENUM] (3 fields)
    - ADMIN
    - USER
    - GUEST

--- Demo: SDL Parsing ---
SDL parsed: OK
  type Query (2 fields)
  type User (3 fields)
  type Status (0 fields) [ENUM]

--- Demo: Schema Introspection ---
Introspection result:
{"__schema":{"queryType":{"name":"Query"},"types":[...]}}
```

## 核心概念详解 Core Concepts

### 1. GraphQL Schema 与类型系统

GraphQL 的类型系统由以下构件组成：

| 类型 | 说明 | C 表示 |
|------|------|--------|
| `String` | UTF-8 字符串 | `GQL_TYPE_STRING` |
| `Int`    | 32 位有符号整数 | `GQL_TYPE_INT` |
| `Float`  | 双精度浮点数 | `GQL_TYPE_FLOAT` |
| `Boolean`| true / false | `GQL_TYPE_BOOLEAN` |
| `ID`     | 唯一标识符 | `GQL_TYPE_ID` |
| `Enum`   | 枚举值集合 | `GQL_TYPE_ENUM` |
| `Type[]` | 列表包装 | `GQL_TYPE_LIST` |
| `Type!`  | 非空包装 | `GQL_TYPE_NON_NULL` |
| `Input`  | 输入对象 | `GQL_TYPE_INPUT_OBJECT` |

```c
// 定义类型
gql_schema_add_type(&e.schema, "User");
gql_schema_add_field(&e.schema, "User", "id",    GQL_TYPE_ID,    "ID",     false, true);
gql_schema_add_field(&e.schema, "User", "name",  GQL_TYPE_STRING,"String", false, true);
gql_schema_add_field(&e.schema, "User", "email", GQL_TYPE_STRING,"String", false, false);
gql_schema_add_field(&e.schema, "User", "posts", GQL_TYPE_OBJECT,"Post",   true,  false);

// 定义枚举
gql_schema_add_type(&e.schema, "Role");
gql_schema_add_enum_value(&e.schema, "Role", "ADMIN");
gql_schema_add_enum_value(&e.schema, "Role", "USER");
gql_schema_add_enum_value(&e.schema, "Role", "GUEST");
```

### 2. SDL (Schema Definition Language)

GraphQL 使用声明式的 Schema 定义语言来描述 API 的能力面。本库支持从 SDL 文本构建完整类型系统。

```graphql
type Query {
  hello: String
  user(id: ID!): User
  search(query: String!): [Post]
}

type User {
  id: ID!
  name: String!
  email: String
  posts: [Post]
}

type Post {
  title: String!
  content: String
  published: Boolean
}

enum Role {
  ADMIN
  USER
  GUEST
}
```

```c
// 从 SDL 字符串构建 Schema
const char* sdl = "type Query { hello: String }";
bool ok = gql_parse_schema_sdl(&e, sdl);
```

### 3. 查询解析 Query Parsing

本库的查询解析器支持：

- **操作类型**：query / mutation / subscription
- **具名操作**：`query GetUser { ... }`
- **字段选择**：嵌套的字段集 `{ user { name email posts { title } } }`
- **参数绑定**：`user(id: 42)` 或 `search(query: "hello world")`
- **叶节点标记**：区分标量叶字段与对象嵌套字段

```c
// 解析带参数的查询
const char* query = "{ user(id: 42) { name email posts { title } } }";
gql_parse_query(&e, query);

// 访问解析结果
printf("Target: %s\n", e.parsed.target_field);
printf("Args: %d\n", e.parsed.arg_count);
printf("Fields: %d\n", e.parsed.selection_count);
```

### 4. 解析器映射 Resolver Map

解析器 (Resolver) 是一个函数指针，在查询执行时被调用以获取字段的实际数据。解析器按 `[类型, 字段]` 维度注册。

```c
// 定义解析器函数
void* resolve_user_name(void* parent, void* args, void* context) {
    return (void*)"Alice";
}

// 注册解析器
gql_schema_add_resolver(&e.schema, "Query", "hello", resolve_hello, NULL);
gql_schema_add_resolver(&e.schema, "User",  "name",  resolve_user_name, NULL);

// 执行查询
gql_parse_query(&e, "{ hello user { name } }");
gql_execute(&e, NULL);
```

### 5. 自省 Introspection

GraphQL 的内省系统允许客户端在运行时查询 Schema 的元数据。本库实现了 `__schema` 和 `__type` 查询。

```c
// 获取完整 Schema
char buf[2048];
gql_introspect_schema(&e, buf, sizeof(buf));
printf("%s\n", buf);
// 输出: {"__schema":{"queryType":{"name":"Query"},"types":[...]}}

// 获取特定类型信息
gql_introspect_type(&e, "User", buf, sizeof(buf));
printf("%s\n", buf);
// 输出: {"name":"User","fields":[{"name":"id","type":"ID!"},...]}
```

### 6. GraphQL vs REST 快速对比

| 维度 | REST | GraphQL |
|------|------|---------|
| 端点数量 | 多个端点（/users, /posts） | 单一端点（/graphql） |
| 数据获取 | 服务端决定返回什么 | 客户端精确指定所需字段 |
| 版本管理 | URI 路径或 Header 版本化 | Schema 演化（字段 @deprecated） |
| 类型系统 | 隐式（由文档定义） | 显式（强制类型 + Schema） |
| 缓存策略 | HTTP 缓存（ETag, Cache-Control） | 应用层缓存（Apollo / Relay Store） |
| 错误处理 | HTTP 状态码 | `errors` 数组（全部 200 OK） |
| 批量请求 | 多个 HTTP 请求 | 单次查询获取关联数据 |
| 学习曲线 | 低（HTTP 常识） | 中（需理解类型系统） |

## API 参考 API Reference

详见头文件 `include/graphql_engine.h`：

| 函数 | 说明 |
|------|------|
| `gql_engine_init()` | 初始化 GraphQL 引擎 |
| `gql_schema_init()` | 初始化 Schema |
| `gql_schema_add_type()` | 添加类型定义 |
| `gql_schema_add_field()` | 为类型添加字段 |
| `gql_schema_add_field_arg()` | 为字段添加参数 |
| `gql_schema_add_enum_value()` | 添加枚举值 |
| `gql_schema_set_query_type()` | 设置 Query 类型 |
| `gql_schema_add_resolver()` | 注册解析器 |
| `gql_parse_query()` | 解析 GraphQL 查询 |
| `gql_parse_schema_sdl()` | 从 SDL 解析 Schema |
| `gql_validate_query()` | 验证查询 |
| `gql_execute()` | 执行查询 |
| `gql_introspect_schema()` | Schema 自省 |
| `gql_introspect_type()` | 类型自省 |
| `gql_find_type()` | 按名称查找类型 |
| `gql_error_count()` | 错误数量 |
| `gql_error_message()` | 错误消息 |

## 相关资源 References

- [GraphQL Specification (October 2021)](https://spec.graphql.org/)
- [GraphQL 入门指南](https://graphql.org/learn/)
- [Apollo Server 文档](https://www.apollographql.com/docs/apollo-server/)
- [Relay 文档](https://relay.dev/)
- [GraphQL Schemas and Types](https://graphql.org/learn/schema/)
- [Introduction to GraphQL (GitHub)](https://docs.github.com/en/graphql/guides/introduction-to-graphql)
