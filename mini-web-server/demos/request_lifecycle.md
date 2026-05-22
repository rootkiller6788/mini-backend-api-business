# demo/request_lifecycle.md — mini-web-server Request Lifecycle 请求生命周期

> 本文档详细描述 mini-web-server 从接收 TCP 连接到发送 HTTP 响应的完整请求生命周期。
> 覆盖: 连接管理、请求解析、路由分发、中间件链、静态文件、CGI、响应构建。

---

## 1. 概述 Overview

```
Client TCP ──► [Accept] ──► [Read Socket] ──► [Parse HTTP]
                                                    │
            ┌───────────────────────────────────────┘
            ▼
    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
    │ Middleware    │───►│ Router       │───►│ Handler      │
    │ Chain (前置)  │    │ Dispatch     │    │ (业务逻辑)    │
    └──────────────┘    └──────────────┘    └──────────────┘
            │                                       │
            ▼                                       ▼
    ┌──────────────┐                        ┌──────────────┐
    │ Middleware    │◄───────────────────────│ Response     │
    │ Chain (后置)  │                        │ Building     │
    └──────────────┘                        └──────────────┘
            │
            ▼
    [Serialize Response] ──► [Write Socket] ──► [Close / Keep-Alive]
```

---

## 2. 阶段一: 连接建立 Connection Establishment

### 2.1 TCP Accept

```
socket() ──► bind() ──► listen() ──► accept()
```

- 服务器在指定端口监听 (默认 8080)
- `accept()` 返回新的客户端 socket fd
- 每个连接获得唯一的 `connection_id`

### 2.2 连接配置

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `SO_REUSEADDR` | 1 | 快速重启 |
| `TCP_NODELAY` | 1 | 禁用 Nagle 算法 |
| `SO_KEEPALIVE` | 1 | TCP 保活 |
| `SO_RCVTIMEO` | 30s | 读超时 |
| `SO_SNDTIMEO` | 30s | 写超时 |

### 2.3 Keep-Alive

```
HTTP/1.1 默认 Keep-Alive
Connection: keep-alive
Keep-Alive: timeout=5, max=100
```

同一连接可处理多个请求, 直到:
- 客户端发送 `Connection: close`
- 达到 `max` 请求数
- 超时无新请求
- 发生致命错误

---

## 3. 阶段二: 请求读取 Request Reading

### 3.1 读取循环

```
while (1) {
    n = recv(fd, buf + offset, buf_size - offset, 0);
    if (n == 0) → 客户端关闭
    if (n < 0 && errno == EAGAIN) → 等待
    offset += n
    if (headers_complete(buf, offset)) break;
}
```

### 3.2 头结束检测 (headers_complete)

通过查找 `\r\n\r\n` (HTTP 标准) 确定 header 结束位置:

```c
char *end = strstr(buf, "\r\n\r\n");
if (end) {
    header_end = end - buf + 4;
    return true;
}
```

### 3.3 Body 读取

根据 `Content-Length` 或 `Transfer-Encoding: chunked` 读取 body:

**Content-Length 模式**:
```
body = header_end + 剩余的字节
wait until total_received >= header_len + content_length
```

**Chunked 模式**:
```
while (1) {
    read chunk_size line → hex → decimal
    if chunk_size == 0 → done
    read chunk_size bytes
    read trailing CRLF
}
read trailers (optional)
```

---

## 4. 阶段三: HTTP 解析 HTTP Parsing

### 4.1 请求行解析 (http_parse_request_line)

```
GET /api/users?id=42&page=1 HTTP/1.1
│   │                      │
│   │                      └── HTTP 版本
│   └── 路径 + 查询字符串
└── HTTP 方法
```

解析结果填充到 `HttpRequest` 结构体:

```c
typedef struct {
    HttpMethod method;          // HTTP_GET
    char path[HTTP_MAX_PATH];   // "/api/users"
    char query_string[...];     // "id=42&page=1"
    HttpHeader headers[64];     // 请求头数组
    int  header_count;          // 已解析头数量
    char *body;                 // 请求体 (POST/PUT)
    size_t body_len;            // 请求体长度
} HttpRequest;
```

### 4.2 请求头解析 (http_parse_header)

```
Host: localhost:8080\r\n
Content-Type: application/json\r\n
Authorization: Bearer eyJhbG...\r\n
```

每行调用一次 `http_parse_header()`:
1. 查找第一个 `:` 分割 name/value
2. 去除 value 前导空格
3. 去除尾部 `\r\n` 和空格
4. 存入 `headers[]` 数组

### 4.3 请求体解析

根据 `Content-Type` 选择解析策略:

| Content-Type | 处理方式 |
|-------------|---------|
| `application/json` | JSON 解析 (如 jansson) |
| `application/x-www-form-urlencoded` | 表单键值对 |
| `multipart/form-data` | 文件上传解析 |
| `text/plain` | 原始文本 |
| 其他 | raw bytes |

---

## 5. 阶段四: 中间件前置链 Middleware Chain (Before)

### 5.1 中间件链模型

```
Request ──► [MW1.Log] ──► [MW2.CORS] ──► [MW3.Auth] ──► Router
                │               │               │
                │               │               └── 认证检查
                │               └── CORS 预检处理
                └── 访问日志记录
```

### 5.2 三种返回结果

```c
typedef enum {
    MIDDLEWARE_NEXT,   // 继续下一条中间件
    MIDDLEWARE_STOP,   // 停止链, 直接响应 (如 CORS preflight 204)
    MIDDLEWARE_ERROR   // 链中断, 返回 500
} MiddlewareResult;
```

### 5.3 内置中间件

| 中间件 | 功能 | 返回 STOP 条件 |
|--------|------|---------------|
| `middleware_logger` | 记录访问日志 | 永不 |
| `middleware_cors` | CORS 头 + OPTIONS 处理 | `req.method == OPTIONS` |
| `middleware_compress_gzip` | 添加 gzip Content-Encoding | 永不 |
| `middleware_auth_bearer` | Bearer Token 验证 | Token 无效 |
| `middleware_rate_limit` | 速率限制 (100/min) | 超过限制 |
| `middleware_body_parser` | 解析 JSON/form body | Content-Length 无效 |

### 5.4 CORS 预检流程 (Preflight)

```
OPTIONS /api/data HTTP/1.1
Origin: https://example.com
Access-Control-Request-Method: POST
Access-Control-Request-Headers: Authorization

↓ middleware_cors 处理

HTTP/1.1 204 No Content
Access-Control-Allow-Origin: https://example.com
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type, Authorization
Vary: Origin

(链停止, 不进入 router)
```

---

## 6. 阶段五: 路由分发 Router Dispatch

### 6.1 Trie 树路由结构

```
                        /
                       / \
                     api  static
                     /     /   \
                  users  css   js
                  /   \
                :id   *
```

- `:id` — 路径参数, 匹配任意段, 存入 `RouteParam`
- `*` — 通配符, 匹配任意后缀段
- 静态段 — 精确匹配

### 6.2 路由注册示例

```c
/* GET /api/users/:id */
router_add(router, HTTP_GET, "/api/users/:id", handler_get_user);

/* POST /api/users */
router_add(router, HTTP_POST, "/api/users", handler_create_user);

/* GET /static/*   */
router_add(router, HTTP_GET, "/static/*", handler_static);

/* DELETE /api/users/:id */
router_add(router, HTTP_DELETE, "/api/users/:id", handler_delete_user);
```

### 6.3 路径参数提取

请求 `GET /api/users/42` 匹配 `/api/users/:id`:

```c
RouteParam params[] = {
    {.name = "id", .value = "42"}
};
```

Handler 签名:

```c
typedef bool (*RouteHandler)(const HttpRequest *req, HttpResponse *res,
                              const RouteParam *params, int param_count);
```

### 6.4 路由查找算法

```
function router_dispatch(path):
    segments = split(path, '/')
    current  = root
    params   = []

    for each segment in segments:
        for each child in current.children:
            if child matches segment (exact / :param / *wildcard):
                if child is :param → add to params
                current = child
                continue outer loop
        return 404  // no match

    if current.is_endpoint and current.method == request.method:
        return current.handler(request, response, params)
    else:
        return 405  // Method Not Allowed (or 404)
```

---

## 7. 阶段六: 业务处理 Handler (业务逻辑)

Handler 是业务逻辑的核心, 负责:

1. **获取输入**: 从 `HttpRequest` 读取参数
   - URL 路径参数 → `RouteParam[]`
   - Query 参数 → `http_parse_query_string()`
   - 请求体 → `req->body` (JSON/Form)

2. **执行业务**: 数据库查询、计算、外部 API 调用等

3. **构建响应**: 填充 `HttpResponse`
   ```c
   http_response_set_status(&res, 200);
   http_response_add_header(&res, "Content-Type", "application/json");
   http_response_set_body_str(&res, "{\"id\":42,\"name\":\"Alice\"}");
   ```

4. **返回控制权**: `return true` (成功) / `return false` (错误)

### 静态文件 Handler 示例

```c
bool handle_static_file(const HttpRequest *req, HttpResponse *res,
                         const RouteParam *params, int count) {
    const char *file_path = req->path + strlen("/static");
    return static_serve_file(&g_static_cfg, file_path, req, res);
}
```

### CGI Handler 示例

```c
bool handle_cgi_script(const HttpRequest *req, HttpResponse *res,
                        const RouteParam *params, int count) {
    CgiConfig cgi;
    cgi_config_init(&cgi, "/usr/lib/cgi-bin/script.pl");
    cgi_config_set_request_env(&cgi, req);

    CgiResult result;
    cgi_execute(&cgi, req, &result);
    return cgi_result_to_response(&result, res);
}
```

---

## 8. 阶段七: 中间件后置链 Middleware Chain (After)

Handler 执行完成后, 中间件链的后置逻辑执行。在 mini-web-server 中,
中间件的 `MIDDLEWARE_NEXT` 既可作为前置也可作为后置:

```
Handler returns → Middleware chain continues (each MW can modify response)
```

例如 `middleware_compress_gzip`:
- 前置: 检查 `Accept-Encoding` 头
- 后置: (扩展点, 可在此压缩响应体)

---

## 9. 阶段八: 响应构建与发送 Response Serialization

### 9.1 响应结构

```c
typedef struct {
    int status_code;             // 200
    HttpHeader headers[64];      // Content-Type, Content-Length...
    int  header_count;
    char *body;                  // 响应体
    size_t body_len;             // 响应体长度
    bool headers_sent;           // 是否已发送 (流式)
} HttpResponse;
```

### 9.2 序列化 (http_serialize_response)

```
HTTP/1.1 200 OK\r\n
Content-Type: application/json\r\n
Content-Length: 16\r\n
ETag: "abc123"\r\n
\r\n
{"status":"ok"}
```

**注意**: `Content-Length` 由序列化函数自动添加, 不需要手动设置。

### 9.3 Socket 写入

```c
char buf[HTTP_MAX_BUF];
int len = http_serialize_response(&res, buf, sizeof(buf));
send(fd, buf, len, 0);
```

### 9.4 分块传输 (Chunked Transfer)

对于流式响应或大文件:

```
HTTP/1.1 200 OK\r\n
Transfer-Encoding: chunked\r\n
\r\n
1A\r\n
{"data": "chunk 1 here"}\r\n
1A\r\n
{"data": "chunk 2 here"}\r\n
0\r\n
\r\n
```

---

## 10. 错误处理 Error Handling

| 阶段 | 错误 | 响应 | 说明 |
|------|------|------|------|
| Socket Read | timeout / reset | 关闭连接 | 静默关闭 |
| HTTP Parse | 格式错误 | 400 Bad Request | 无效请求行/头 |
| Body Read | Content-Length 不匹配 | 400 / 408 | 请求体不完整 |
| Middleware | Stop | (中间件定义的响应) | 如 401/429 |
| Router | 路径未匹配 | 404 Not Found | 无匹配路由 |
| Router | 方法不匹配 | 405 Method Not Allowed | 路由存在但方法不对 |
| Handler | 返回 false | 500 Internal Error | 业务逻辑异常 |
| Static | 文件不存在 | 404 Not Found | stat() 失败 |
| Static | 权限不足 | 403 Forbidden | 无法读取文件 |
| CGI | 超时 | 504 Gateway Timeout | 脚本执行超时 |
| CGI | 非零退出 | 502 Bad Gateway | 脚本异常退出 |

---

## 11. 性能要点 Performance

1. **零拷贝 (mmap/sendfile)**
   - 大文件使用 `sendfile()` 直接在内核态传输
   - 避免用户态缓冲区拷贝

2. **连接池 (Connection Pool)**
   - Keep-Alive 复用连接减少 TCP 握手
   - 最大空闲连接数可配置

3. **内存管理**
   - `HttpRequest.body` 使用 malloc 动态分配, 用后释放
   - 响应体小对象使用栈缓冲区 (4096 bytes)
   - 大响应用流式写入, 避免全部缓存

4. **Trie 路由**
   - O(n) 查找, n = 路径深度
   - 比正则匹配快 10-100x
   - 支持编译时优化为静态表

---

## 12. 调试与日志 Debug & Logging

```
[2024-05-06 10:00:00] 127.0.0.1 GET /api/users/42 → 200 (3ms)
[2024-05-06 10:00:01] 127.0.0.1 POST /api/users → 201 (12ms)
[2024-05-06 10:00:02] 127.0.0.1 GET /static/style.css → 304 (1ms)
[2024-05-06 10:00:03] 10.0.0.1 GET /admin → 403 (0ms)
```

日志包含: 时间戳、客户端 IP、方法、路径、状态码、响应时间。

---

## 13. 安全考虑 Security

- **路径穿越防护**: 验证 `..` 不会逃出 `root_dir`
- **请求大小限制**: `HTTP_MAX_BODY = 16MB`
- **头大小限制**: `HTTP_MAX_HEADER_VALUE = 4KB`
- **请求超时**: 读超时 30s, CGI 超时 30s
- **HTTP 方法检查**: 仅处理注册的 HTTP 方法
- **输入验证**: URL 解码、查询字符串解析均有边界检查

---

> 版本: 0.1.0 | 最后更新: 2024-05-06
