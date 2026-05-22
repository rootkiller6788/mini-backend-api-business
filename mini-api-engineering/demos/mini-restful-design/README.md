# mini-restful-design — RESTful API 设计深度解析

> 深入理解 REST 架构风格：资源建模、HTTP 动词、状态码、HATEOAS、分页模式、路由设计

## 目录 Table of Contents

1. [REST 架构风格概述](#1-rest-架构风格概述)
2. [资源建模 Resource Modeling](#2-资源建模-resource-modeling)
3. [HTTP 方法详解 HTTP Methods](#3-http-方法详解-http-methods)
4. [HTTP 状态码指南 Status Codes](#4-http-状态码指南-status-codes)
5. [HATEOAS 超媒体约束](#5-hateoas-超媒体约束)
6. [分页模式 Pagination Patterns](#6-分页模式-pagination-patterns)
7. [路由设计 Router Design](#7-路由设计-router-design)
8. [请求头与内容协商](#8-请求头与内容协商)
9. [错误响应设计 Error Responses](#9-错误响应设计-error-responses)
10. [API 设计模式与反模式](#10-api-设计模式与反模式)
11. [实战案例：Users CRUD](#11-实战案例users-crud)
12. [实战案例：HATEOAS 订单资源](#12-实战案例hateoas-订单资源)

---

## 1. REST 架构风格概述

REST (Representational State Transfer) 由 Roy Fielding 于 2000 年在博士论文中提出，是当今 Web API 设计最主流的架构风格。

### 1.1 REST 六大约束

| 约束 (Constraint) | 含义 | 实践体现 |
|---|---|---|
| **客户端-服务器 (Client-Server)** | 关注点分离，UI 与数据存储解耦 | 前端 SPA ↔ 后端 API |
| **无状态 (Stateless)** | 每个请求包含所有必要信息，服务器不保存客户端上下文 | JWT Token 每请求携带 |
| **可缓存 (Cacheable)** | 响应必须标记是否可缓存 | Cache-Control, ETag |
| **统一接口 (Uniform Interface)** | 资源标识、自描述消息、HATEOAS | URI + HTTP 方法 + 媒体类型 |
| **分层系统 (Layered System)** | 中间层（代理、网关）透明 | Nginx → API Gateway → 服务 |
| **按需代码 Code-on-Demand** (可选) | 服务器可下发可执行代码 | 极少使用 |

### 1.2 Richardson 成熟度模型

```
Level 0 — 单一 URI，单一方法（RPC over HTTP）
Level 1 — 多个 URI，单一方法（资源）
Level 2 — 多个 URI，多个 HTTP 方法（动词）
Level 3 — HATEOAS（超媒体控制）
```

本库 `rest_design.h` 支持 Level 3 成熟度模型。

### 1.3 REST 核心理念

```
资源 (Resource)   → 一切都是资源，用 URI 标识
表现 (Representation) → 服务器返回资源的多种表示（JSON/XML/HTML）
动词 (Verb)       → 使用标准 HTTP 方法操作资源
状态 (State)      → 客户端持有状态，通过表现转移
```

---

## 2. 资源建模 Resource Modeling

### 2.1 资源类型

| 类型 | URI 模式 | 示例 |
|---|---|---|
| **集合资源 (Collection)** | `/{resource}` | `/users`, `/orders` |
| **单体资源 (Singleton)** | `/{resource}/{id}` | `/users/42`, `/orders/1001` |
| **子资源 (Sub-resource)** | `/{resource}/{id}/{sub}` | `/users/42/orders` |
| **动作资源 (Action)** | `/{resource}/{id}/{action}` | `/orders/1001/cancel` |
| **关联资源 (Association)** | `/{A}/{id}/{B}` | `/orgs/1/members` |

### 2.2 资源命名规范

```
✅ 正确做法                       ❌ 错误做法
/users                          /getUsers, /userList
/users/42                       /users?id=42
/users/42/orders                /getUserOrders?userId=42
/orders/1001/cancel             /cancelOrder/1001
/articles?tag=rust              /articles/tagged/rust
```

**核心原则：**
- URI 命名的是资源（名词），不是操作（动词）
- 使用复数名词表示集合
- 使用连字符 `-` 而非下划线 `_`
- 全部小写
- 不使用文件扩展名
- 嵌套深度不超过 3 层

### 2.3 资源关系建模

```c
// 使用 rest_resource_add_link 建立资源间超媒体关系
rest_resource_add_link(&order, "cancel",  "/api/v1/orders/1001/cancel",  "POST");
rest_resource_add_link(&order, "invoice", "/api/v1/invoices/5001",      "GET");
rest_resource_add_link(&order, "track",   "/api/v1/orders/1001/track",  "GET");
rest_resource_add_link(&order, "return",  "/api/v1/returns",            "POST");
rest_resource_add_link(&order, "self",    "/api/v1/orders/1001",        "GET");
```

### 2.4 资源路径参数

```
/users/{userId}/orders/{orderId}
       ↑                ↑
   路径参数          路径参数

// 使用 rest_resource_add_path_param 绑定参数
rest_resource_add_path_param(&get_user, "id", "42");
```

---

## 3. HTTP 方法详解 HTTP Methods

### 3.1 标准方法速查

| 方法 | 语义 | 幂等 | 安全 | 请求体 | 响应体 |
|---|---|---|---|---|---|
| **GET** | 获取资源表现 | ✓ | ✓ | 无 | 有 |
| **POST** | 创建新资源 | ✗ | ✗ | 有 | 有 |
| **PUT** | 完整替换资源 | ✓ | ✗ | 有 | 可选 |
| **PATCH** | 部分更新资源 | ✗ | ✗ | 有 | 可选 |
| **DELETE** | 删除资源 | ✓ | ✗ | 无 | 可选 |
| **HEAD** | 获取响应头（无响应体） | ✓ | ✓ | 无 | 无 |
| **OPTIONS** | 获取可用方法 | ✓ | ✓ | 无 | 可选 |

### 3.2 方法选择决策树

```
操作是读取数据？
  ├── 是 → GET
  └── 否 → 操作是创建新资源？
            ├── 是 → POST
            └── 否 → 操作是完整替换？
                      ├── 是 → PUT
                      └── 否 → 操作是部分更新？
                                ├── 是 → PATCH
                                └── 否 → 操作是删除？
                                          └── 是 → DELETE
```

### 3.3 POST vs PUT vs PATCH

```json
// POST — 创建新资源（服务器分配 ID）
POST /users
{ "name": "Alice", "email": "alice@example.com" }
→ 201 Created, Location: /users/101

// PUT — 完整替换（客户端指定 ID，幂等）
PUT /users/101
{ "name": "Alice Updated", "email": "new@example.com" }
→ 200 OK

// PATCH — 部分更新（仅发送变更字段）
PATCH /users/101
{ "email": "new@example.com" }
→ 200 OK
```

### 3.4 幂等性 Idempotency

| 方法 | 幂等？ | 多次相同请求的结果 |
|---|---|---|
| GET | ✓ | 相同响应 |
| PUT | ✓ | 最终状态相同 |
| DELETE | ✓ | 首次 204，后续 404（但资源状态一致） |
| POST | ✗ | 可能创建多个资源 |
| PATCH | ✗ | 多次应用同一补丁可能产生不同结果 |

---

## 4. HTTP 状态码指南 Status Codes

### 4.1 状态码分类

```
1xx — 信息 (Informational)    — 请求已接收，继续处理
2xx — 成功 (Success)          — 请求成功
3xx — 重定向 (Redirection)    — 需要进一步操作
4xx — 客户端错误 (Client Error) — 请求有语法错误或无法完成
5xx — 服务器错误 (Server Error) — 服务器处理请求失败
```

### 4.2 常用状态码详解

#### 2xx 成功类

| 状态码 | 含义 | 使用场景 |
|---|---|---|
| **200 OK** | 请求成功 | GET、PUT、PATCH 成功返回 |
| **201 Created** | 资源创建成功 | POST 成功，附带 Location 头 |
| **202 Accepted** | 请求已接受，异步处理中 | 异步任务提交 |
| **204 No Content** | 成功但无响应体 | DELETE 成功 |

#### 3xx 重定向类

| 状态码 | 含义 | 使用场景 |
|---|---|---|
| **301 Moved Permanently** | 资源永久移动 | API 版本迁移（旧 URI → 新 URI） |
| **302 Found** | 临时重定向 | 临时维护页面 |
| **304 Not Modified** | 资源未修改 | 条件 GET（ETag / If-None-Match） |

#### 4xx 客户端错误类

| 状态码 | 含义 | 使用场景 |
|---|---|---|
| **400 Bad Request** | 请求格式错误 | JSON 解析失败、参数缺失 |
| **401 Unauthorized** | 未认证 | Token 缺失或过期 |
| **403 Forbidden** | 无权限 | 权限不足（即使已认证） |
| **404 Not Found** | 资源不存在 | ID 找不到 |
| **405 Method Not Allowed** | 方法不允许 | 对只读资源使用 POST |
| **409 Conflict** | 资源冲突 | 重复创建、乐观锁冲突 |
| **410 Gone** | 资源永久删除 | 已弃用的 API |
| **415 Unsupported Media Type** | 不支持的媒体类型 | Content-Type 不是 application/json |
| **422 Unprocessable Entity** | 语义错误 | 格式正确但业务校验失败 |
| **429 Too Many Requests** | 请求频率过高 | 触发速率限制 |

#### 5xx 服务器错误类

| 状态码 | 含义 | 使用场景 |
|---|---|---|
| **500 Internal Server Error** | 服务器内部错误 | 未预期的异常 |
| **502 Bad Gateway** | 网关错误 | 上游服务不可用 |
| **503 Service Unavailable** | 服务不可用 | 维护模式、过载保护 |
| **504 Gateway Timeout** | 网关超时 | 上游服务响应超时 |

### 4.3 常见错误：滥用 200 OK

```json
// ❌ 错误：所有响应都用 200，状态码在 body 里
HTTP/1.1 200 OK
{ "code": 401, "message": "Unauthorized" }

// ✅ 正确：使用真实的 HTTP 状态码
HTTP/1.1 401 Unauthorized
{ "error": { "code": "AUTH_FAILED", "message": "Token expired" } }
```

---

## 5. HATEOAS 超媒体约束

HATEOAS (Hypermedia As The Engine Of Application State) 是 Richardson 成熟度模型的最高级别 (Level 3)。其核心思想是：客户端应通过服务器返回的超链接来驱动应用状态转换，而非硬编码 URI。

### 5.1 HATEOAS 的价值

```
传统 REST (Level 2)              HATEOAS (Level 3)
├── 客户端硬编码 URI               ├── 客户端发现 URI
├── 服务端 URI 变更 → 客户端崩溃    ├── 服务端 URI 变更 → 客户端自适应
├── 客户端需知道状态转换规则        ├── 服务端通过链接告知可用操作
└── 耦合度高                       └── 松耦合
```

### 5.2 链接关系 (Link Relation)

| rel | 含义 | 使用场景 |
|---|---|---|
| **self** | 当前资源自身 | 每个资源必须包含 |
| **next** | 下一页 | 分页列表 |
| **prev** / **previous** | 上一页 | 分页列表 |
| **first** | 第一页 | 分页列表 |
| **last** | 最后一页 | 分页列表 |
| **create** | 创建新资源 | 集合资源 |
| **update** | 更新资源 | 单体资源 |
| **delete** | 删除资源 | 单体资源 |
| **cancel** | 取消操作 | 订单、任务 |
| **approve** | 审批 | 工作流资源 |
| **parent** | 父资源 | 子资源 |
| **collection** | 所属集合 | 单体资源 |
| **alternate** | 替代表示 | 不同格式 |

### 5.3 HATEOAS 响应格式

```json
{
  "id": 1001,
  "status": "shipped",
  "total": 99.95,
  "items": [
    { "sku": "ABC-123", "qty": 2 },
    { "sku": "XYZ-789", "qty": 1 }
  ],
  "_links": {
    "self":    { "href": "/api/v1/orders/1001",      "method": "GET" },
    "cancel":  { "href": "/api/v1/orders/1001/cancel","method": "POST" },
    "invoice": { "href": "/api/v1/invoices/5001",    "method": "GET" },
    "track":   { "href": "/api/v1/orders/1001/track","method": "GET" },
    "return":  { "href": "/api/v1/returns",          "method": "POST" }
  }
}
```

### 5.4 条件链接

并非所有链接都始终可用——应根据资源状态动态展示：

```c
// 只有状态为 "pending" 的订单才能取消
if (strcmp(order_status, "pending") == 0) {
    rest_resource_add_link(&order, "cancel", "/api/v1/orders/1001/cancel", "POST");
}

// 已发货的订单可以跟踪
if (strcmp(order_status, "shipped") == 0) {
    rest_resource_add_link(&order, "track", "/api/v1/orders/1001/track", "GET");
}
```

---

## 6. 分页模式 Pagination Patterns

### 6.1 三种主流分页策略

| 策略 | 请求参数 | 响应包含 | 适用场景 |
|---|---|---|---|
| **基于页码 (Page-based)** | `page=2&page_size=20` | page, total_pages, total_items | 传统 Web 应用，支持跳页 |
| **基于偏移 (Offset-based)** | `offset=20&limit=10` | has_more, next_offset | SQL 友好，简单直观 |
| **基于游标 (Cursor-based)** | `cursor=abc123&limit=10` | has_more, next_cursor | 实时数据流、大数据集 |

### 6.2 基于页码的分页 (Page-based) — 本库实现

```c
rest_pagination_t p;
rest_pagination_init(&p, page=2, page_size=20, total_items=100);
```

**响应结构：**

```json
{
  "data": [ /* ... */ ],
  "pagination": {
    "page": 2,
    "page_size": 20,
    "total_items": 100,
    "total_pages": 5,
    "has_next": true,
    "has_prev": true
  },
  "_links": {
    "self":     "/api/v1/users?page=2&page_size=20",
    "first":    "/api/v1/users?page=1&page_size=20",
    "prev":     "/api/v1/users?page=1&page_size=20",
    "next":     "/api/v1/users?page=3&page_size=20",
    "last":     "/api/v1/users?page=5&page_size=20"
  }
}
```

### 6.3 基于游标的分页 (Cursor-based)

```json
// 请求
GET /api/v1/events?cursor=evt_789&limit=50

// 响应
{
  "data": [ /* ... */ ],
  "pagination": {
    "next_cursor": "evt_839",
    "has_more": true
  }
}
```

**优点：**
- 数据新增/删除不影响分页稳定性
- 数据库利用索引（`WHERE id > cursor`）
- 适合无限滚动列表

**缺点：**
- 不支持直接跳页
- 无法知道总数和总页数

### 6.4 分页最佳实践

```
1. 始终设置默认分页值（page=1, page_size=20）
2. 设置 page_size 上限（如 100），防止客户端请求过大
3. 总数为 0 时，应考虑是否省略分页元数据
4. 对客户端透明：无论是否分页，响应格式保持一致
5. HATEOAS 链接中包含完整的分页参数
6. 首尾页链接应在所有分页响应中提供
```

### 6.5 分页边界情况

| 情况 | page | 响应 |
|---|---|---|
| 请求第 1 页，有数据 | 1 | `has_next=true`, `has_prev=false` |
| 请求最后一页 | N | `has_next=false`, `has_prev=true` |
| 请求超过总页数 | > N | 返回最后一页或空数组 + 400 |
| 仅 1 页数据 | 1 | `has_next=false`, `has_prev=false` |
| 无数据 | 1 | 空数组, `total_items=0`, `total_pages=0` |

---

## 7. 路由设计 Router Design

### 7.1 路由结构

```c
rest_router_t router;
rest_router_init(&router, "/api", 1);  // base_path="/api", version=1
```

### 7.2 URL 组成

```
https://api.example.com/api/v1/users/42?include=orders
\___/   \_______________/ \___________/ \__/ \____________/
 协议        主机名+端口      Base Path     资源    查询参数
                            (含版本)
```

### 7.3 路由注册与解析

```c
rest_router_register(&router, &resource);
const char* response = rest_router_resolve(&router, "/api/v1/users/42", REST_GET);
```

### 7.4 多版本路由

```
API v1: /api/v1/users          → 旧实现
API v2: /api/v2/users          → 新实现（不兼容变更）
API v1: /api/v1/users/42       → 旧实现（已弃用，带 Sunset 头）
```

版本共存期间，两个版本同时提供服务，v1 逐渐下线。详见 `api_version.h`。

---

## 8. 请求头与内容协商

### 8.1 核心 HTTP 头

| 请求头 | 含义 | 示例值 |
|---|---|---|
| **Accept** | 客户端期望的响应格式 | `application/json` |
| **Content-Type** | 请求体的媒体类型 | `application/json; charset=utf-8` |
| **Authorization** | 认证凭据 | `Bearer eyJhbG...` |
| **Accept-Version** | API 版本（Header 策略） | `v1`, `v2` |
| **If-None-Match** | 条件请求（ETag） | `"abc123"` |
| **Accept-Encoding** | 压缩支持 | `gzip, deflate, br` |

### 8.2 内容协商流程

```
客户端发送 Accept: application/json
    ↓
服务器检查支持的类型
    ├── 支持 → 响应 Content-Type: application/json + 数据
    └── 不支持 → 406 Not Acceptable

客户端发送 Accept: application/xml
服务器仅支持 JSON
    ↓
HTTP/1.1 406 Not Acceptable
{
  "error": {
    "code": "MEDIA_NOT_ACCEPTABLE",
    "supported_types": ["application/json"]
  }
}
```

### 8.3 自定义头

```c
rest_resource_add_header(&resource, "X-Request-Id", "req_abc123");
rest_resource_add_header(&resource, "X-RateLimit-Remaining", "99");
rest_resource_add_header(&resource, "Content-Security-Policy", "default-src 'none'");
```

---

## 9. 错误响应设计 Error Responses

### 9.1 统一错误格式

```json
{
  "error": {
    "code": "RESOURCE_NOT_FOUND",
    "message": "User with id '999' was not found.",
    "details": [
      {
        "field": "id",
        "reason": "not_found",
        "message": "No user exists with this identifier."
      }
    ],
    "request_id": "req_abc123",
    "documentation_url": "https://docs.example.com/errors/RESOURCE_NOT_FOUND"
  }
}
```

### 9.2 错误码设计

```
命名规范：大写下划线，语义化

RESOURCE_NOT_FOUND      → 资源不存在
VALIDATION_ERROR        → 参数校验失败
AUTHENTICATION_FAILED   → 认证失败
PERMISSION_DENIED       → 权限不足
RATE_LIMIT_EXCEEDED     → 超过速率限制
CONCURRENCY_CONFLICT    → 并发冲突
INTERNAL_ERROR          → 内部错误
SERVICE_UNAVAILABLE     → 服务不可用
```

### 9.3 本库中的状态码工具函数

```c
const char* rest_status_string(rest_status_t status);   // 返回 "200_OK"
const char* rest_status_reason(rest_status_t status);     // 返回 "OK", "Not Found" 等
```

---

## 10. API 设计模式与反模式

### 10.1 推荐模式

| 模式 | 描述 | 示例 |
|---|---|---|
| **CRUD 标准化** | 集合资源提供 GET/POST，单体资源提供 GET/PUT/PATCH/DELETE | `/users`, `/users/{id}` |
| **复合资源** | 使用 `?embed=` 包含嵌套资源 | `GET /users/42?embed=orders` |
| **稀疏字段集** | 使用 `?fields=` 选择字段 | `GET /users?fields=id,name,email` |
| **批量操作** | POST 到 `/resource/batch` | `POST /users/batch` |
| **长时操作** | 返回 202 + `Location: /tasks/{id}` | 异步导出、批量处理 |

### 10.2 常见反模式

| 反模式 | 错误做法 | 正确做法 |
|---|---|---|
| **动词化 URI** | `/getUser`, `/createOrder` | `GET /users/42`, `POST /orders` |
| **深层嵌套** | `/orgs/1/depts/2/users/3/roles/4` | 扁平化或提供查询参数 `/roles?user=3` |
| **万能 POST** | 所有操作用 POST | 正确使用 GET/POST/PUT/PATCH/DELETE |
| **状态码误用** | 错误时仍返回 200 | 使用正确的 4xx / 5xx 状态码 |
| **泄露内部实现** | `/api/getUserFromMySQLById` | `/api/users/42` |
| **文件扩展名** | `/users.json`, `/users.xml` | 使用 Accept 头做内容协商 |
| **大小写混用** | `/getUserList` | `/users` |

---

## 11. 实战案例：Users CRUD

### 11.1 完整端点设计

```
GET    /api/v1/users           → 用户列表（分页，可筛选）
GET    /api/v1/users/{id}      → 单个用户详情（含 HATEOAS 链接）
POST   /api/v1/users           → 创建新用户
PUT    /api/v1/users/{id}      → 完整更新用户
PATCH  /api/v1/users/{id}      → 部分更新用户
DELETE /api/v1/users/{id}      → 删除用户
```

### 11.2 完整请求/响应示例

```
# 1. 获取用户列表
GET /api/v1/users?page=1&page_size=2 HTTP/1.1
Accept: application/json

HTTP/1.1 200 OK
Content-Type: application/json
{
  "data": [
    { "id": 1, "name": "Alice", "email": "alice@example.com" },
    { "id": 2, "name": "Bob",   "email": "bob@example.com" }
  ],
  "pagination": { "page": 1, "page_size": 2, "total_items": 42, "total_pages": 21 },
  "_links": {
    "self":  { "href": "/api/v1/users?page=1&page_size=2", "method": "GET" },
    "next":  { "href": "/api/v1/users?page=2&page_size=2", "method": "GET" },
    "last":  { "href": "/api/v1/users?page=21&page_size=2","method": "GET" }
  }
}

# 2. 获取单个用户
GET /api/v1/users/42 HTTP/1.1
Accept: application/json

HTTP/1.1 200 OK
Content-Type: application/json
{
  "id": 42,
  "name": "Charlie",
  "email": "charlie@example.com",
  "_links": {
    "self":   { "href": "/api/v1/users/42",        "method": "GET" },
    "update": { "href": "/api/v1/users/42",        "method": "PUT" },
    "delete": { "href": "/api/v1/users/42",        "method": "DELETE" },
    "orders": { "href": "/api/v1/users/42/orders", "method": "GET" }
  }
}

# 3. 创建用户
POST /api/v1/users HTTP/1.1
Content-Type: application/json
{ "name": "Diana", "email": "diana@example.com" }

HTTP/1.1 201 Created
Location: /api/v1/users/43
Content-Type: application/json
{
  "id": 43,
  "name": "Diana",
  "email": "diana@example.com"
}

# 4. 部分更新用户
PATCH /api/v1/users/43 HTTP/1.1
Content-Type: application/json
{ "email": "diana.new@example.com" }

HTTP/1.1 200 OK
Content-Type: application/json
{
  "id": 43,
  "name": "Diana",
  "email": "diana.new@example.com"
}

# 5. 删除用户
DELETE /api/v1/users/43 HTTP/1.1

HTTP/1.1 204 No Content
```

### 11.3 C 代码实现概览

```c
// 用户列表 GET /users
rest_resource_t get_users;
rest_resource_init(&get_users, "list_users", "/api/v1/users", REST_GET);
rest_resource_set_status(&get_users, REST_200_OK);
rest_resource_set_body(&get_users, "[{\"id\":1,\"name\":\"Alice\"},...]");
rest_resource_set_content_type(&get_users, "application/json");
rest_pagination_init(&get_users.pagination, 1, 20, 42);
rest_resource_add_link(&get_users, "self",   "/api/v1/users?page=1&page_size=20", "GET");
rest_resource_add_link(&get_users, "create", "/api/v1/users", "POST");

// 单个用户 GET /users/{id}
rest_resource_t get_user;
rest_resource_init(&get_user, "get_user", "/api/v1/users/{id}", REST_GET);
rest_resource_add_path_param(&get_user, "id", "42");
rest_resource_set_body(&get_user, "{\"id\":42,\"name\":\"Charlie\",...}");
rest_resource_add_link(&get_user, "self",   "/api/v1/users/42", "GET");
rest_resource_add_link(&get_user, "update", "/api/v1/users/42", "PUT");
rest_resource_add_link(&get_user, "delete", "/api/v1/users/42", "DELETE");

// 创建用户 POST /users
rest_resource_t create_user;
rest_resource_init(&create_user, "create_user", "/api/v1/users", REST_POST);
rest_resource_set_status(&create_user, REST_201_CREATED);

// 删除用户 DELETE /users/{id}
rest_resource_t delete_user;
rest_resource_init(&delete_user, "delete_user", "/api/v1/users/43", REST_DELETE);
rest_resource_set_status(&delete_user, REST_204_NO_CONTENT);
```

---

## 12. 实战案例：HATEOAS 订单资源

### 12.1 订单状态机

```
                     ┌─────────┐
          POST /orders│ pending │
              ───────→│         │
                     └────┬────┘
                          │ confirm
                          ▼
                     ┌─────────┐
                     │confirmed│
                     └────┬────┘
                          │ ship
                          ▼
                     ┌─────────┐
                     │ shipped │
                     └────┬────┘
                    ┌─────┘  └─────┐
                    ▼              ▼
              ┌─────────┐   ┌─────────┐
              │delivered│   │ returned │
              └─────────┘   └─────────┘
```

### 12.2 各状态下的 HATEOAS 链接

| 订单状态 | 可用操作（链接） |
|---|---|
| **pending** | self, cancel, invoice |
| **confirmed** | self, cancel, invoice |
| **shipped** | self, invoice, track, return |
| **delivered** | self, invoice, return |
| **returned** | self, invoice |
| **cancelled** | self |

### 12.3 已发货订单的完整 HATEOAS 响应

```json
{
  "id": 1001,
  "status": "shipped",
  "total": 99.95,
  "currency": "USD",
  "items": [
    { "sku": "ABC-123", "qty": 2, "unit_price": 29.99 },
    { "sku": "XYZ-789", "qty": 1, "unit_price": 39.97 }
  ],
  "shipping_address": {
    "street": "123 Main St",
    "city": "San Francisco",
    "zip": "94105"
  },
  "created_at": "2024-01-15T10:30:00Z",
  "shipped_at": "2024-01-16T14:22:00Z",
  "_links": {
    "self":    { "href": "/api/v1/orders/1001",        "method": "GET" },
    "invoice": { "href": "/api/v1/invoices/5001",     "method": "GET" },
    "track":   { "href": "/api/v1/orders/1001/track", "method": "GET" },
    "return":  { "href": "/api/v1/returns",           "method": "POST" },
    "items":   { "href": "/api/v1/orders/1001/items", "method": "GET" }
  }
}
```

### 12.4 C 代码

```c
rest_resource_t order;
rest_resource_init(&order, "order", "/api/v1/orders/1001", REST_GET);
rest_resource_set_body(&order,
    "{\"id\":1001,\"status\":\"shipped\",\"total\":99.95,"
    "\"items\":[{\"sku\":\"ABC-123\",\"qty\":2},"
    "{\"sku\":\"XYZ-789\",\"qty\":1}]}");
rest_resource_add_link(&order, "self",    "/api/v1/orders/1001",       "GET");
rest_resource_add_link(&order, "cancel",  "/api/v1/orders/1001/cancel","POST");
rest_resource_add_link(&order, "invoice", "/api/v1/invoices/5001",    "GET");
rest_resource_add_link(&order, "track",   "/api/v1/orders/1001/track","GET");
rest_resource_add_link(&order, "return",  "/api/v1/returns",          "POST");
```

---

## 参考资料 References

- [RFC 7231 — HTTP/1.1 Semantics and Content](https://datatracker.ietf.org/doc/html/rfc7231)
- [Roy Fielding — Architectural Styles and the Design of Network-based Software Architectures](https://www.ics.uci.edu/~fielding/pubs/dissertation/top.htm)
- [RESTful API Design — Microsoft REST API Guidelines](https://github.com/microsoft/api-guidelines)
- [REST API Design Rulebook — Mark Masse](https://www.oreilly.com/library/view/rest-api-design/9781449317904/)
- [Zalando RESTful API Guidelines](https://opensource.zalando.com/restful-api-guidelines/)
- [RFC 7807 — Problem Details for HTTP APIs](https://datatracker.ietf.org/doc/html/rfc7807)
- [RFC 8288 — Web Linking](https://datatracker.ietf.org/doc/html/rfc8288)
- [HATEOAS — Wikipedia](https://en.wikipedia.org/wiki/HATEOAS)

## 本库相关文件

- `include/rest_design.h` — REST 资源设计核心 API
- `src/rest_design.c` — REST 路由、分页、链接实现
- `examples/demo_rest_crud.c` — 完整可运行的 CRUD 演示
- `docs/api-design-principles.md` — API 设计原则总览
- `demos/mini-graphql-core/README.md` — GraphQL 深度解析
