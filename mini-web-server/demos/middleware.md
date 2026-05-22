# demo/middleware.md — Middleware System 中间件系统

> 本文档详细描述 mini-web-server 的中间件系统设计与使用方式。
> 覆盖: 中间件概念、链模型、内置中间件、自定义中间件、执行流程、最佳实践。

---

## 1. 中间件概念 Middleware Concept

### 1.1 什么是中间件

中间件是在 **HTTP 请求到达业务 Handler 之前** 和 **Handler 返回响应之后**
执行的可组合函数。中间件形成一个链, 每个中间件都可以:

1. **修改请求** (如: 解析 body, 添加认证信息)
2. **修改响应** (如: 添加 CORS 头, 压缩响应体)
3. **短路请求** (如: 认证失败直接返回 401)
4. **传递控制权** 给下一个中间件

### 1.2 洋葱模型 (Onion Model)

```
        ┌──────────────────────────────────┐
        │     Middleware 1 (Logger)        │
        │  ┌────────────────────────────┐  │
        │  │   Middleware 2 (CORS)      │  │
        │  │  ┌──────────────────────┐  │  │
        │  │  │ Middleware 3 (Auth)  │  │  │
        │  │  │  ┌──────────────┐   │  │  │
        │  │  │  │   Handler    │   │  │  │
        │  │  │  └──────────────┘   │  │  │
        │  │  └──────────────────────┘  │  │
        │  └────────────────────────────┘  │
        └──────────────────────────────────┘

        请求方向 →───→───→───→───→───→───→
        ←───←───←───←───←───←───←─── 响应方向
```

---

## 2. 核心数据结构

### 2.1 MiddlewareFn 函数签名

```c
typedef MiddlewareResult (*MiddlewareFn)(HttpRequest  *req,
                                          HttpResponse *res,
                                          MiddlewareContext *ctx);
```

### 2.2 MiddlewareResult 返回值

| 值 | 含义 | 后续行为 |
|----|------|---------|
| `MIDDLEWARE_NEXT` | 继续执行 | 调用链中下一个中间件 |
| `MIDDLEWARE_STOP` | 停止执行 | 不调用后续中间件, 直接返回当前响应 |
| `MIDDLEWARE_ERROR` | 发生错误 | 链执行中断, `chain_execute` 返回 `false` |

### 2.3 MiddlewareContext

```c
struct MiddlewareContext {
    void *user_data;    // 用户自定义数据指针
    int  depth;         // 当前执行的深度 (0-based)
    char log_buf[];     // 日志缓冲区
};
```

### 2.4 MiddlewareChain

```c
typedef struct {
    MiddlewareFn entries[MIDDLEWARE_MAX_CHAIN];  // 函数指针数组
    void        *contexts[MIDDLEWARE_MAX_CHAIN]; // 各中间件上下文
    int          count;                          // 链中中间件数量
} MiddlewareChain;
```

最大链长度: 16 (可调整 `MIDDLEWARE_MAX_CHAIN`)

---

## 3. 链生命周期 Chain Lifecycle

### 3.1 创建链

```c
MiddlewareChain chain;
middleware_chain_init(&chain);
```

### 3.2 添加中间件

```c
middleware_chain_add(&chain, middleware_logger,       NULL);
middleware_chain_add(&chain, middleware_cors,         NULL);
middleware_chain_add(&chain, middleware_auth_bearer,  NULL);
middleware_chain_add(&chain, middleware_compress_gzip, NULL);
```

添加顺序 = 执行顺序。

### 3.3 执行链

```c
MiddlewareContext ctx = {0};
bool ok = middleware_chain_execute(&chain, &req, &res, &ctx);
```

### 3.4 清除链

```c
middleware_chain_clear(&chain);
```

---

## 4. 内置中间件 Built-in Middleware

### 4.1 middleware_logger — 访问日志

```
功能: 记录 [时间] IP METHOD PATH 格式的访问日志
输出: ctx->log_buf 缓冲区
返回值: 永远返回 MIDDLEWARE_NEXT
```

配置: 无 (开箱即用)

### 4.2 middleware_cors — 跨域资源共享

```
功能:
- 添加 CORS 响应头
- 处理 OPTIONS 预检请求 (返回 204)
- 支持自定义 origin / methods / headers

返回值:
  - OPTIONS 请求 → MIDDLEWARE_STOP (短路)
  - 其他请求     → MIDDLEWARE_NEXT
```

**自定义配置**:

```c
middleware_cors_set_origin("https://myapp.example.com");
middleware_cors_set_methods("GET, POST, PUT");
middleware_cors_set_headers("Content-Type, X-API-Key");
```

**默认配置**:

| 设置 | 默认值 |
|------|--------|
| Origin | `*` (允许所有) |
| Methods | `GET, POST, PUT, DELETE, OPTIONS` |
| Headers | `Content-Type, Authorization` |

**响应头**:

```
Access-Control-Allow-Origin: https://example.com
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type, Authorization
Vary: Origin
```

### 4.3 middleware_compress_gzip — Gzip 压缩 (模拟)

```
功能:
- 检查 Accept-Encoding 头是否包含 "gzip"
- 如支持, 添加 Content-Encoding: gzip 响应头
- 添加 Vary: Accept-Encoding 响应头

返回值: 永远返回 MIDDLEWARE_NEXT
```

**注意**: 该中间件模拟压缩语义, 不实际压缩响应体。
实际压缩可在响应序列化阶段通过 zlib 实现。

### 4.4 middleware_auth_bearer — Bearer Token 认证

```
功能:
- 检查 Authorization 头是否以 "Bearer " 开头
- 无效/缺失 Token → 返回 401 + WWW-Authenticate 头

返回值:
  - Token 存在且格式正确 → MIDDLEWARE_NEXT
  - 否则 → MIDDLEWARE_STOP
```

**扩展点**: 可在此中间件中调用外部认证服务验证 JWT。

### 4.5 middleware_rate_limit — 速率限制

```
功能:
- 滑动窗口计数器 (60 秒窗口)
- 超过 100 请求返回 429 Too Many Requests
- 添加 X-RateLimit-Remaining 头
- 添加 Retry-After 头

返回值:
  - 未超过限制 → MIDDLEWARE_NEXT
  - 超过限制   → MIDDLEWARE_STOP
```

**注意**: 当前使用全局静态变量, 生产环境需改为 per-IP 或 Redis 计数器。

### 4.6 middleware_body_parser — 请求体解析

```
功能:
- 读取 Content-Length 头, 验证大小
- 检测 JSON Content-Type, 标记 X-Body-Parsed 响应头

返回值:
  - Content-Length 合法 → MIDDLEWARE_NEXT
  - 否则 → MIDDLEWARE_ERROR
```

---

## 5. 自定义中间件 Custom Middleware

### 5.1 基本结构

```c
static MiddlewareResult my_middleware(HttpRequest *req,
                                       HttpResponse *res,
                                       MiddlewareContext *ctx) {
    /* ── 前置逻辑 ── */
    const char *val = http_request_get_header(req, "X-Custom");
    if (!val) {
        http_response_set_status(res, HTTP_STATUS_BAD_REQUEST);
        http_response_set_body_str(res, "{\"error\":\"missing X-Custom\"}");
        return MIDDLEWARE_STOP;
    }

    /* ── 调用下一个中间件 (隐式通过返回 NEXT) ── */
    // ... 如果需要记录开始时间:
    // clock_t start = clock();

    /* 返回 NEXT 后, handler 执行, 然后... */

    /* ── 后置逻辑 (如需要, 可在返回前修改响应) ── */
    http_response_add_header(res, "X-Custom-Processed", "true");
    return MIDDLEWARE_NEXT;
}
```

### 5.2 带上下文的中间件

```c
typedef struct {
    int threshold;
    int counter;
} RateLimitCtx;

static MiddlewareResult per_ip_limit(HttpRequest *req,
                                      HttpResponse *res,
                                      MiddlewareContext *ctx) {
    RateLimitCtx *my_ctx = (RateLimitCtx *)ctx->user_data;
    my_ctx->counter++;

    if (my_ctx->counter > my_ctx->threshold) {
        http_response_set_status(res, HTTP_STATUS_TOO_MANY_REQUESTS);
        return MIDDLEWARE_STOP;
    }
    return MIDDLEWARE_NEXT;
}

/* 使用 */
RateLimitCtx rl_ctx = { .threshold = 50, .counter = 0 };
MiddlewareChain chain;
middleware_chain_init(&chain);
middleware_chain_add(&chain, per_ip_limit, &rl_ctx);
```

### 5.3 请求修改中间件

```c
/* 将 X-Forwarded-For 写入自定义头 */
static MiddlewareResult proxy_fixup(HttpRequest *req,
                                     HttpResponse *res,
                                     MiddlewareContext *ctx) {
    (void)res;
    (void)ctx;
    const char *xff = http_request_get_header(req, "X-Forwarded-For");
    if (xff) {
        http_response_add_header(res, "X-Real-IP", xff);
    }
    return MIDDLEWARE_NEXT;
}
```

### 5.4 响应修改中间件

```c
/* 在所有 JSON 响应中添加包装 */
static MiddlewareResult json_envelope(HttpRequest *req,
                                       HttpResponse *res,
                                       MiddlewareContext *ctx) {
    (void)req;
    (void)ctx;
    if (res->body && res->status_code < 400) {
        char *new_body = malloc(res->body_len + 64);
        snprintf(new_body, res->body_len + 64,
                 "{\"code\":0,\"data\":%.*s}",
                 (int)res->body_len, res->body);
        free(res->body);
        res->body = new_body;
        res->body_len = strlen(new_body);
    }
    return MIDDLEWARE_NEXT;
}
```

---

## 6. 执行流程详解

### 6.1 正常流程

```
chain_execute(chain, req, res, ctx):
  for i in 0 .. chain.count-1:
    result = chain.entries[i](req, res, ctx)
    if result == NEXT:     continue
    if result == STOP:     return true
    if result == ERROR:    return false
  return true
```

### 6.2 短路流程 (以 CORS preflight 为例)

```
Request:  OPTIONS /api/data
  ├─ [0] middleware_logger     → NEXT
  ├─ [1] middleware_cors       → detects OPTIONS → STOP
  │     (设置 204 No Content, 添加 CORS 头)
  └─ chain_execute 返回 true
  │
  └─ 不再执行 [2] middleware_auth_bearer
      不再执行 [3] middleware_compress_gzip
      不进入 Router
```

### 6.3 错误流程 (以认证失败为例)

```
Request:  GET /api/users  (无 Authorization)
  ├─ [0] middleware_logger       → NEXT
  ├─ [1] middleware_cors         → NEXT
  ├─ [2] middleware_auth_bearer  → 无 Bearer token → STOP
  │     (设置 401, 添加 WWW-Authenticate)
  └─ chain_execute 返回 true
  │
  └─ Router 不执行
```

---

## 7. 典型链组合 Common Chains

### 7.1 REST API 服务

```c
middleware_chain_add(&chain, middleware_logger,      NULL);
middleware_chain_add(&chain, middleware_cors,        NULL);
middleware_chain_add(&chain, middleware_body_parser, NULL);
middleware_chain_add(&chain, middleware_auth_bearer, NULL);
middleware_chain_add(&chain, middleware_rate_limit,  NULL);
```

### 7.2 静态文件服务

```c
middleware_chain_add(&chain, middleware_logger,       NULL);
middleware_chain_add(&chain, middleware_compress_gzip, NULL);
// 不需要 auth, body_parser, rate_limit
```

### 7.3 公开 API (无认证)

```c
middleware_chain_add(&chain, middleware_logger, NULL);
middleware_chain_add(&chain, middleware_cors,   NULL);
middleware_chain_add(&chain, middleware_rate_limit, NULL);
```

---

## 8. 最佳实践 Best Practices

1. **Logger 放在第一位**: 确保所有请求都被记录 (包括被后续中间件短路的)

2. **CORS 放在早期**: 在认证之前处理 OPTIONS 预检, 避免不必要的认证检查

3. **Auth 放在业务逻辑前**: 认证中间件应在业务 Handler 之前短路无效请求

4. **Rate Limit 灵活放置**:
   - 在 Auth 前: 防止暴力破解 (但可能误伤合法用户)
   - 在 Auth 后: 按用户限流更精确

5. **避免中间件膨胀**: 每个中间件应只做一件事, 复杂度通过组合而非单一大函数实现

6. **中间件不持有状态**: 状态应通过 `ctx->user_data` 传入, 保持无状态性

7. **错误信息标准化**: 短路时返回统一 JSON 错误格式:
   ```json
   {"error": "unauthorized", "code": 401}
   ```

8. **响应头使用标准字段**: 
   - 速率限制 → `X-RateLimit-*`
   - 请求追踪 → `X-Request-Id`
   - 响应时间 → `X-Response-Time`

---

## 9. 中间件性能开销

| 中间件 | CPU 开销 | 内存开销 | 说明 |
|--------|---------|---------|------|
| logger | ~1us | 1KB | strftime + sprintf |
| cors | ~0.5us | 0 | 仅字符串比较 |
| compress | ~0.5us | 0 | 仅 header 检查 |
| auth | ~1us | 0 | 字符串前缀匹配 |
| rate_limit | ~1us | ~16B | 计数器操作 |
| body_parser | ~2us | 0 | Content-Length 解析 |

单次请求全部中间件 ≈ **5-6 微秒**。

---

## 10. 调试中间件链

### 10.1 启用调试输出

```c
MiddlewareResult custom_debug(HttpRequest *req, HttpResponse *res,
                               MiddlewareContext *ctx) {
    fprintf(stderr, "[DEBUG:%d] %s %s → status=%d\n",
            ctx->depth, http_method_str(req->method),
            req->path, res->status_code);
    return MIDDLEWARE_NEXT;
}
```

### 10.2 检查响应头

执行链后打印所有响应头以验证中间件效果:

```c
for (int i = 0; i < res.header_count; i++) {
    printf("  %s: %s\n", res.headers[i].name, res.headers[i].value);
}
```

---

> 版本: 0.1.0 | 最后更新: 2024-05-06
