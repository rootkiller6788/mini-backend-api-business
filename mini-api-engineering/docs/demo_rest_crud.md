# demo_rest_crud — REST CRUD 演示

## 概述 Overview

`demo_rest_crud` 是一个 C 语言程序，演示如何使用 `mini-api-engineering` 库中的 `rest_design` 模块来构建符合 REST 架构风格的 HTTP API。该演示涵盖 RESTful 资源定义、CRUD 操作（Create / Read / Update / Delete）、HTTP 状态码、HATEOAS 超媒体链接、分页以及错误响应模式。

## 架构理念 Architecture Philosophy

REST (Representational State Transfer) 是一种面向资源的架构风格。核心思想是将一切抽象为「资源」(Resource)，通过统一的接口（HTTP 方法）对资源进行操作。资源的标识符是 URI，资源的表示（Representation）通过 JSON/XML 等格式传输。

本库的 `rest_design` 模块提供以下核心能力：
- **资源建模**：将业务实体映射为 REST 资源，绑定 HTTP 方法和 URI 路径
- **状态码映射**：完整映射 20+ 常用 HTTP 状态码
- **HATEOAS 超链接**：在响应中嵌入关系链接，驱动客户端状态转移
- **分页支持**：标准化分页参数和 Link 头
- **路由注册**：支持 URI 路径版本化 (`/api/v1/`)

## 程序结构 Program Structure

```
demo_rest_crud.c
├── print_pagination()    打印分页信息
├── print_resource()       打印资源详情
├── demo_users_crud()      用户 CRUD 完整示例
├── demo_error_responses()  HTTP 状态码展示
├── demo_hateoas()         HATEOAS 链接演示
├── demo_router()          路由与版本化
└── main()                 入口函数
```

## 编译与运行 Build & Run

### 前提条件 Prerequisites

- C99 兼容的编译器（GCC ≥ 4.8, Clang ≥ 3.3, MSVC ≥ 2013）
- GNU Make 或兼容的构建工具

### 构建 Build

```bash
# 进入项目根目录
cd mini-api-engineering

# 编译所有示例
make

# 仅编译 REST CRUD 演示
gcc -std=c99 -Wall -Wextra -I include -o build/demo_rest_crud \
    examples/demo_rest_crud.c src/rest_design.c
```

### 运行 Run

```bash
./build/demo_rest_crud
```

### 预期输出 Expected Output

```
=== mini-api-engineering: REST CRUD Demo ===

--- Demo: Users CRUD (RESTful) ---

=== REST Resource ===
Name    : list_users
Path    : /api/v1/users
Method  : GET
Status  : 200 OK
Content : application/json
Body    : [{"id":1,"name":"Alice"},{"id":2,"name":"Bob"}]
Page    : 1 / 3
Items   : 42 total (page size 20)
Has prev: no, Has next: yes
...

--- Demo: Error Responses ---
  200 OK                         -> OK
  400 Bad Request                -> Bad Request
  401 Unauthorized               -> Unauthorized
  403 Forbidden                  -> Forbidden
  404 Not Found                  -> Not Found
  ...

--- Demo: HATEOAS Order Resource ---
=== REST Resource ===
Name    : order
Path    : /api/v1/orders/1001
Method  : GET
Links   :
  [self]    GET    /api/v1/orders/1001
  [cancel]  POST   /api/v1/orders/1001/cancel
  [invoice] GET    /api/v1/invoices/5001
  [track]   GET    /api/v1/orders/1001/track
  [return]  POST   /api/v1/returns
```

## 核心概念详解 Core Concepts

### 1. 资源与 URI 设计 Resource & URI Design

RESTful API 的核心是名词化的资源 URI。设计原则：

| 操作 | HTTP 方法 | URI 模式 | 示例 |
|------|----------|---------|------|
| 列表查询 | GET | `/resource` | `GET /api/v1/users` |
| 单个查询 | GET | `/resource/{id}` | `GET /api/v1/users/42` |
| 创建资源 | POST | `/resource` | `POST /api/v1/users` |
| 完整更新 | PUT | `/resource/{id}` | `PUT /api/v1/users/42` |
| 部分更新 | PATCH | `/resource/{id}` | `PATCH /api/v1/users/42` |
| 删除资源 | DELETE | `/resource/{id}` | `DELETE /api/v1/users/42` |
| 子资源 | GET | `/resource/{id}/sub` | `GET /api/v1/users/42/orders` |
| 动作/操作 | POST | `/resource/{id}/action` | `POST /api/v1/orders/1001/cancel` |

```c
// 定义资源
rest_resource_t get_user;
rest_resource_init(&get_user, "get_user", "/api/v1/users/{id}", REST_GET);
rest_resource_add_path_param(&get_user, "id", "42");

// 设置响应体
rest_resource_set_body(&get_user,
    "{\"id\":42,\"name\":\"Charlie\",\"email\":\"charlie@example.com\"}");
rest_resource_set_status(&get_user, REST_200_OK);
```

### 2. HTTP 状态码 HTTP Status Codes

本库完整实现了 20+ 常用状态码，按类别组织：

| 类别 | 范围 | 示例 |
|------|------|------|
| 2xx Success | 200-299 | 200 OK, 201 Created, 204 No Content |
| 3xx Redirection | 300-399 | 301 Moved, 302 Found, 304 Not Modified |
| 4xx Client Error | 400-499 | 400 Bad Request, 401 Unauthorized, 403 Forbidden, 404 Not Found, 422 Unprocessable |
| 5xx Server Error | 500-599 | 500 Internal Error, 502 Bad Gateway, 503 Unavailable |

```c
// 设置不同的状态码
rest_resource_set_status(&create_user, REST_201_CREATED);
rest_resource_set_status(&delete_user, REST_204_NO_CONTENT);
rest_resource_set_status(&error,     REST_422_UNPROCESSABLE_ENTITY);

// 获取状态码的字符串表示
const char* str = rest_status_string(REST_404_NOT_FOUND);     // "404 Not Found"
const char* reason = rest_status_reason(REST_201_CREATED);     // "Created"
```

### 3. HATEOAS 超媒体链接

HATEOAS (Hypermedia as the Engine of Application State) 是 REST 成熟度模型 Level 3 的核心要求。通过在响应中嵌入可用的链接，客户端可以动态发现下一步可执行的操作。

```c
// 为订单资源添加 HATEOAS 链接
rest_resource_add_link(&order, "self",    "/api/v1/orders/1001",       "GET");
rest_resource_add_link(&order, "cancel",  "/api/v1/orders/1001/cancel","POST");
rest_resource_add_link(&order, "invoice", "/api/v1/invoices/5001",     "GET");
rest_resource_add_link(&order, "track",   "/api/v1/orders/1001/track", "GET");
rest_resource_add_link(&order, "return",  "/api/v1/returns",           "POST");
```

典型响应中包含的链接关系 (rel)：
- `self` — 当前资源自身
- `create` / `update` / `delete` — 标准 CRUD 操作
- `prev` / `next` / `first` / `last` — 分页导航
- 业务语义链接 — 如 `cancel`, `approve`, `ship`, `pay`

### 4. 分页 Pagination

分页使用标准的 `page` + `page_size` 查询参数，并在响应中通过 `rest_pagination_t` 提供完整的导航信息。

```c
rest_pagination_t p;
rest_pagination_init(&p, 1, 20, 42);  // page=1, page_size=20, total=42

// p.page = 1
// p.page_size = 20
// p.total_pages = 3
// p.has_next = true
// p.next_link = "?page=2&page_size=20"
// p.prev_link = "?page=0&page_size=20"
// p.first_link = "?page=1&page_size=20"
// p.last_link = "?page=3&page_size=20"
```

### 5. RESTful API 设计检查清单

- [ ] URI 使用名词复数（`/users` 而非 `/getUsers`）
- [ ] 使用标准 HTTP 方法（GET / POST / PUT / PATCH / DELETE）
- [ ] 使用正确的 HTTP 状态码传达结果
- [ ] ID 作为路径参数而非查询参数（`/users/42` 而非 `/users?id=42`）
- [ ] 资源嵌套不超过 2 层（`/users/42/orders` 而非 `/users/42/orders/1001/items/5`）
- [ ] 请求体和响应体使用 JSON 格式
- [ ] Content-Type 头设为 `application/json`
- [ ] 列表接口支持分页
- [ ] 重要操作提供 HATEOAS 链接
- [ ] 版本号放入 URI 路径（`/api/v1/`）

## API 参考 API Reference

详见头文件 `include/rest_design.h`：

| 函数 | 说明 |
|------|------|
| `rest_resource_init()` | 初始化 REST 资源 |
| `rest_resource_set_status()` | 设置 HTTP 状态码 |
| `rest_resource_set_body()` | 设置响应体 |
| `rest_resource_set_content_type()` | 设置 Content-Type |
| `rest_resource_add_link()` | 添加 HATEOAS 链接 |
| `rest_resource_add_header()` | 添加响应头 |
| `rest_resource_add_path_param()` | 添加路径参数 |
| `rest_pagination_init()` | 初始化分页 |
| `rest_router_init()` | 初始化路由 |
| `rest_router_register()` | 注册资源到路由 |
| `rest_method_string()` | 获取 HTTP 方法字符串 |
| `rest_status_string()` | 获取状态码字符串 |
| `rest_status_reason()` | 获取状态码原因短语 |

## 相关资源 References

- [RFC 7231 — HTTP Semantics & Content](https://tools.ietf.org/html/rfc7231)
- [Roy Fielding's REST Dissertation](https://www.ics.uci.edu/~fielding/pubs/dissertation/top.htm)
- [Richardson Maturity Model](https://martinfowler.com/articles/richardsonMaturityModel.html)
- [REST API Design Rulebook (O'Reilly)](https://www.oreilly.com/library/view/rest-api-design/)
- [Microsoft REST API Guidelines](https://github.com/microsoft/api-guidelines)
- [Zalando RESTful API Guidelines](https://opensource.zalando.com/restful-api-guidelines/)
