# API 设计原则 API Design Principles

## 目录 Table of Contents

1. [概述 Overview](#1-概述-overview)
2. [命名规范 Naming Conventions](#2-命名规范-naming-conventions)
3. [版本策略 Versioning Strategy](#3-版本策略-versioning-strategy)
4. [错误处理 Error Handling](#4-错误处理-error-handling)
5. [安全设计 Security Design](#5-安全设计-security-design)
6. [分页设计 Pagination Design](#6-分页设计-pagination-design)
7. [文档与规范 Documentation](#7-文档与规范-documentation)
8. [性能优化 Performance](#8-性能优化-performance)
9. [设计检查清单 Design Checklist](#9-设计检查清单-design-checklist)

---

## 1. 概述 Overview

API (Application Programming Interface) 设计是软件工程中的基石。一个优秀的 API 设计应当具备以下特质：

- **一致性 (Consistency)**：统一的命名、参数顺序、错误格式
- **可预测性 (Predictability)**：行为符合直觉，不产生意外
- **简洁性 (Simplicity)**：最小的认知负担，易于理解和记忆
- **健壮性 (Robustness)**：对异常输入有明确的处理策略
- **可演化性 (Evolvability)**：支持在不破坏现有客户端的情况下演进

### 设计第一性原则 Design Principles

```
1. API 是产品，不是功能                   —— API-as-a-Product
2. 客户端永远是对的（客户端体验优先）      —— Client-First Design
3. 向后兼容是铁律                        —— Backward Compatibility
4. 少即是多                              —— Less is More
5. 显式优于隐式                          —— Explicit over Implicit
6. 约定优于配置                          —— Convention over Configuration
7. 关注点分离                            —— Separation of Concerns
```

### 该库支持的 API 范式

| 范式 | 库模块 | 适用场景 |
|------|--------|---------|
| RESTful | `rest_design.h` | CRUD 资源操作，微服务通信 |
| GraphQL | `graphql_engine.h` | 复杂数据获取，前端灵活性要求高 |
| gRPC | `grpc_sim.h` | 服务间高性能通信，流式处理 |
| OpenAPI | `openapi_builder.h` | API 文档自动生成，SDK 生成 |

---

## 2. 命名规范 Naming Conventions

### 2.1 URI 设计

| 规则 | 正确示例 | 错误示例 |
|------|---------|---------|
| 使用名词复数 | `/users` | `/getUser`, `/userList` |
| 小写字母 + 连字符 | `/order-items` | `/orderItems`, `/Order_Items` |
| 不使用文件扩展名 | `/users/42` | `/users/42.json` |
| 集合用复数，单例用单数 | `/users/{id}/settings` | `/user/{id}/settings` |
| 不超过 3 层嵌套 | `/orgs/1/users/2/roles` | `/orgs/1/groups/2/users/3/roles` |
| 动作用 POST + 动词 | `POST /orders/42/cancel` | `GET /cancelOrder/42` |

### 2.2 字段命名

- JSON 字段使用 `camelCase`：`createdAt`, `updatedAt`, `userId`
- 布尔字段使用 `is` / `has` 前缀：`isActive`, `hasChildren`
- 日期时间使用 ISO 8601 格式：`2024-01-15T10:30:00Z`
- 枚举值使用大写蛇形：`PENDING`, `IN_PROGRESS`, `COMPLETED`

### 2.3 查询参数命名

- 分页：`page`, `page_size` 或 `offset`, `limit`
- 排序：`sort` + 可选 `order` (`asc` / `desc`)
- 过滤：`filter[field]` 或 `field` 直接作为参数
- 搜索：`q` 或 `search`

---

## 3. 版本策略 Versioning Strategy

本库 `api_version.h` 支持四种版本策略：

### 3.1 URI 路径版本 (推荐)

```
GET /api/v1/users
GET /api/v2/users
```

**优点**：最简单、最可见、易于路由
**缺点**：需要 URI 重写

### 3.2 HTTP Header 版本

```
GET /api/users
Accept-Version: v1
```

**优点**：URI 保持干净
**缺点**：不易调试、缓存复杂

### 3.3 查询参数版本

```
GET /api/users?version=1
```

**优点**：简单直接
**缺点**：污染查询参数

### 3.4 弃用与日落 Deprecation & Sunset

```
// 响应头示例
Deprecation: true; Sunset=2025-06-30
Sunset: Wed, 30 Jun 2025 23:59:59 GMT
Link: </api/v2/users>; rel="successor-version"
```

```c
// C 代码示例：设置弃用
av_router_set_deprecation(&router, "v1", AV_MAJOR,
    "2025-06-30", "v1 is deprecated, please migrate to v2");
```

---

## 4. 错误处理 Error Handling

### 4.1 HTTP 状态码使用指南

| 状态码 | 使用场景 |
|--------|---------|
| **400** Bad Request | 请求格式错误、参数校验失败 |
| **401** Unauthorized | 缺少认证凭据或凭据无效 |
| **403** Forbidden | 已认证但无权限访问资源 |
| **404** Not Found | 请求的资源不存在 |
| **405** Method Not Allowed | HTTP 方法不被支持 |
| **409** Conflict | 资源状态冲突（如重复创建） |
| **422** Unprocessable Entity | 语义错误（格式正确但内容无效） |
| **429** Too Many Requests | 速率限制触发 |
| **500** Internal Server Error | 未预期的服务器错误 |
| **503** Service Unavailable | 服务暂时不可用（维护/过载） |

### 4.2 标准错误响应格式

```json
{
  "error": {
    "code": "VALIDATION_ERROR",
    "message": "The 'email' field is required.",
    "details": [
      {
        "field": "email",
        "reason": "required",
        "message": "Email address must not be empty."
      }
    ],
    "request_id": "req_abc123",
    "documentation_url": "https://docs.example.com/errors/VALIDATION_ERROR"
  }
}
```

---

## 5. 安全设计 Security Design

### 5.1 认证与授权

| 方案 | 适用场景 | OpenAPI 配置 |
|------|---------|-------------|
| API Key | 简单的服务间调用 | `type: apiKey` |
| Bearer JWT | 用户认证 | `type: http, scheme: bearer` |
| OAuth 2.0 | 第三方授权 | `type: oauth2` |
| mTLS | 零信任网络安全 | `type: mutualTLS` |

### 5.2 安全最佳实践

- **始终使用 HTTPS**：禁用 HTTP 端点
- **输入验证**：对所有输入进行长度、类型、范围校验
- **输出编码**：防止 XSS，对 JSON 输出进行转义
- **速率限制**：每个 API Key / IP 应有速率上限
- **最小权限原则**：只暴露必要的数据字段
- **CORS 正确配置**：不乱用 `Access-Control-Allow-Origin: *`
- **安全头设置**：Content-Security-Policy, X-Content-Type-Options, X-Frame-Options

---

## 6. 分页设计 Pagination Design

### 6.1 分页策略对比

| 策略 | 描述 | 优点 | 缺点 |
|------|------|------|------|
| **Offset-based** | `?offset=20&limit=10` | 简单，支持跳页 | 数据变动时不稳定 |
| **Cursor-based** | `?cursor=abc&limit=10` | 稳定，高性能 | 不支持跳页 |
| **Page-based** | `?page=2&page_size=10` | 最直观 | 数据变动时重复/遗漏 |
| **Keyset-based** | `?after_id=20&limit=10` | 大数据集最优 | 客户端实现复杂 |

### 6.2 分页响应

```json
{
  "data": [...],
  "pagination": {
    "page": 2,
    "page_size": 20,
    "total_items": 100,
    "total_pages": 5,
    "has_next": true,
    "has_previous": true
  },
  "_links": {
    "self": "/api/v1/users?page=2&page_size=20",
    "first": "/api/v1/users?page=1&page_size=20",
    "previous": "/api/v1/users?page=1&page_size=20",
    "next": "/api/v1/users?page=3&page_size=20",
    "last": "/api/v1/users?page=5&page_size=20"
  }
}
```

---

## 7. 文档与规范 Documentation

### 7.1 OpenAPI 3.0 规范

OpenAPI 规范（原 Swagger）是 REST API 的事实标准描述语言。本库的 `openapi_builder.h` 支持程序化构建完整的 OpenAPI 3.0 规范并导出为 `swagger.json`。

```c
oa_spec_t spec;
oa_spec_init(&spec, "My API", "1.0.0", "Description");

oa_operation_t* op = oa_spec_add_operation(&spec, "/users", OA_GET, "listUsers", "List all users");
oa_operation_add_response(op, "200", "A list of users", "application/json", "#/components/schemas/UserList");

char json[OA_SPEC_BUF_LEN];
oa_spec_to_json(&spec, json, sizeof(json));
// 输出完整的 OpenAPI 3.0 JSON 规范
```

### 7.2 API 文档应包含

- 端点列表（路径 + HTTP 方法）
- 请求参数（类型、必填、默认值、示例）
- 请求体 Schema
- 响应 Schema（每个状态码）
- 认证方式
- 错误码说明
- 速率限制
- 变更日志 (Changelog)

---

## 8. 性能优化 Performance

### 8.1 通用优化策略

| 策略 | 说明 |
|------|------|
| **字段过滤** | 支持 `?fields=id,name` 只返回需要的字段 |
| **资源嵌入** | 支持 `?embed=author` 避免 N+1 查询 |
| **条件请求** | 支持 ETag / If-None-Match 减少数据传输 |
| **压缩** | 启用 Gzip / Brotli 压缩响应体 |
| **批量操作** | 提供批量创建/更新端点 |
| **缓存头** | 正确设置 Cache-Control / Expires |
| **连接复用** | 使用 HTTP/2 或 gRPC 多路复用 |

### 8.2 GraphQL 性能要点

- 使用 DataLoader 模式避免 N+1 查询问题
- 设置查询深度限制防止递归查询攻击
- 设置查询复杂度上限防止计算密集型查询
- 对高频查询实施持久化查询 (Persisted Queries)

---

## 9. 设计检查清单 Design Checklist

### 新 API 上线前检查

- [ ] URI 设计符合命名规范
- [ ] 所有端点有对应的 OpenAPI 文档
- [ ] HTTP 状态码使用正确
- [ ] 错误响应格式统一
- [ ] 认证/鉴权正确实现
- [ ] 输入验证覆盖所有参数
- [ ] 分页参数有默认值和上限
- [ ] 速率限制配置到位
- [ ] CORS 头正确配置
- [ ] 敏感数据不泄露（脱敏输出）
- [ ] API 版本策略已确定
- [ ] 弃用策略有明确时间表
- [ ] HTTPS 强制启用
- [ ] 性能测试完成
- [ ] 变更日志已更新

### 持续维护检查

- [ ] 弃用 API 的 Sunset 日期不应晚于当前日期
- [ ] 安全漏洞扫描每周执行
- [ ] API 文档与实现保持一致
- [ ] SDK 生成与 API 同步
- [ ] 客户端使用数据被监控
