# API.md — mini-web-server API Reference

## http_core.h — HTTP 核心

### 类型

| 类型 | 说明 |
|------|------|
| `HttpMethod` | 枚举: `HTTP_GET`, `HTTP_POST`, `HTTP_PUT`, `HTTP_DELETE`, `HTTP_HEAD`, `HTTP_OPTIONS`, `HTTP_PATCH`, `HTTP_UNKNOWN` |
| `HttpHeader` | 头键值对: `char name[128]`, `char value[4096]` |
| `HttpRequest` | 请求: method, path, query_string, headers[], header_count, body, body_len |
| `HttpResponse` | 响应: status_code, headers[], header_count, body, body_len, headers_sent |

### 常量 (HTTP 状态码)

```
HTTP_STATUS_OK                200
HTTP_STATUS_CREATED           201
HTTP_STATUS_NO_CONTENT        204
HTTP_STATUS_PARTIAL_CONTENT   206
HTTP_STATUS_MOVED_PERMANENTLY 301
HTTP_STATUS_FOUND             302
HTTP_STATUS_NOT_MODIFIED      304
HTTP_STATUS_BAD_REQUEST       400
HTTP_STATUS_UNAUTHORIZED      401
HTTP_STATUS_FORBIDDEN         403
HTTP_STATUS_NOT_FOUND         404
HTTP_STATUS_METHOD_NOT_ALLOWED 405
HTTP_STATUS_REQUEST_TIMEOUT   408
HTTP_STATUS_CONFLICT          409
HTTP_STATUS_GONE              410
HTTP_STATUS_PAYLOAD_TOO_LARGE 413
HTTP_STATUS_URI_TOO_LONG      414
HTTP_STATUS_UNSUPPORTED_MEDIA 415
HTTP_STATUS_RANGE_NOT_SATISFIABLE 416
HTTP_STATUS_TOO_MANY_REQUESTS 429
HTTP_STATUS_INTERNAL_ERROR    500
HTTP_STATUS_NOT_IMPLEMENTED   501
HTTP_STATUS_BAD_GATEWAY       502
HTTP_STATUS_SERVICE_UNAVAILABLE 503
HTTP_STATUS_GATEWAY_TIMEOUT   504
```

### 函数

| 函数 | 签名 | 说明 |
|------|------|------|
| `http_method_str` | `const char *(HttpMethod)` | 方法枚举 → 字符串 |
| `http_method_from_str` | `HttpMethod(const char *)` | 字符串 → 方法枚举 |
| `http_status_text` | `const char *(int)` | 状态码 → 文本 |
| `http_status_message` | `const char *(int)` | 同上别名 |
| `http_request_init` | `void(HttpRequest *)` | 初始化请求结构 |
| `http_request_free` | `void(HttpRequest *)` | 释放请求内存 |
| `http_parse_request_line` | `bool(const char *, HttpRequest *)` | 解析请求行 |
| `http_parse_header` | `bool(const char *, HttpRequest *)` | 解析单个头 |
| `http_request_get_header` | `const char *(const HttpRequest *, const char *)` | 查找头值 |
| `http_response_init` | `void(HttpResponse *)` | 初始化响应 (默认 200) |
| `http_response_free` | `void(HttpResponse *)` | 释放响应内存 |
| `http_response_set_status` | `void(HttpResponse *, int)` | 设置状态码 |
| `http_response_add_header` | `void(HttpResponse *, const char *, const char *)` | 添加响应头 |
| `http_response_set_body` | `void(HttpResponse *, const char *, size_t)` | 设置二进制响应体 |
| `http_response_set_body_str` | `void(HttpResponse *, const char *)` | 设置字符串响应体 |
| `http_serialize_response` | `int(const HttpResponse *, char *, size_t)` | 序列化为 HTTP 文本 |
| `http_parse_query_string` | `bool(const char *, char *, size_t, char *, size_t, const char *)` | 解析查询参数 |
| `http_url_decode` | `bool(const char *, char *, size_t)` | URL 百分号解码 |

---

## router.h — 路由器

### 类型

| 类型 | 说明 |
|------|------|
| `RouteNode` | Trie 树节点: segment, children[], is_endpoint, handler, is_wildcard, is_param, param_name |
| `Router` | 路由器: root (RouteNode*) |
| `RouteParam` | 路径参数: name[64], value[256] |
| `RouteHandler` | `bool (*)(const HttpRequest*, HttpResponse*, const RouteParam*, int)` |

### 函数

| 函数 | 签名 | 说明 |
|------|------|------|
| `router_create` | `Router *(void)` | 创建路由器 |
| `router_destroy` | `void(Router *)` | 销毁路由器 |
| `router_add` | `bool(Router *, HttpMethod, const char *pattern, RouteHandler)` | 添加路由 |
| `router_dispatch` | `bool(const Router *, HttpMethod, const char *, const HttpRequest *, HttpResponse *)` | 分发请求 |
| `route_node_create` | `RouteNode *(const char *)` | 创建 trie 节点 |
| `route_node_destroy` | `void(RouteNode *)` | 销毁 trie 节点 |

### 路由模式语法

| 语法 | 示例 | 匹配 |
|------|------|------|
| 静态段 | `/api/users` | `/api/users` |
| 路径参数 `:name` | `/api/users/:id` | `/api/users/42` → params[0]={id,42} |
| 通配符 `*` | `/static/*` | `/static/css/app.css` |

### 使用示例

```c
Router *r = router_create();

bool get_user(const HttpRequest *req, HttpResponse *res,
              const RouteParam *params, int count) {
    printf("User ID: %s\n", params[0].value);
    http_response_set_body_str(res, "{\"id\":42}");
    return true;
}

router_add(r, HTTP_GET, "/api/users/:id", get_user);

HttpRequest req;
HttpResponse res;
http_request_init(&req);
http_response_init(&res);
http_parse_request_line("GET /api/users/42 HTTP/1.1", &req);

router_dispatch(r, HTTP_GET, "/api/users/42", &req, &res);
// get_user called with params[0] = {.name="id", .value="42"}

router_destroy(r);
```

---

## middleware.h — 中间件

### 类型

| 类型 | 说明 |
|------|------|
| `MiddlewareResult` | 枚举: `MIDDLEWARE_NEXT`, `MIDDLEWARE_STOP`, `MIDDLEWARE_ERROR` |
| `MiddlewareFn` | `MiddlewareResult (*)(HttpRequest*, HttpResponse*, MiddlewareContext*)` |
| `MiddlewareContext` | 上下文: user_data, depth, log_buf |
| `MiddlewareChain` | 链: entries[16], contexts[16], count |

### 函数

| 函数 | 签名 | 说明 |
|------|------|------|
| `middleware_chain_init` | `void(MiddlewareChain *)` | 初始化链 |
| `middleware_chain_add` | `bool(MiddlewareChain *, MiddlewareFn, void *)` | 添加中间件 |
| `middleware_chain_execute` | `bool(MiddlewareChain *, HttpRequest *, HttpResponse *, MiddlewareContext *)` | 执行链 |
| `middleware_chain_clear` | `void(MiddlewareChain *)` | 清空链 |
| `middleware_logger` | `MiddlewareResult(...)` | 访问日志 |
| `middleware_cors` | `MiddlewareResult(...)` | CORS 处理 |
| `middleware_compress_gzip` | `MiddlewareResult(...)` | Gzip 模拟 |
| `middleware_auth_bearer` | `MiddlewareResult(...)` | Bearer Token 认证 |
| `middleware_rate_limit` | `MiddlewareResult(...)` | 速率限制 |
| `middleware_body_parser` | `MiddlewareResult(...)` | 请求体解析 |
| `middleware_cors_set_origin` | `void(const char *)` | 设置 CORS origin |
| `middleware_cors_set_methods` | `void(const char *)` | 设置 CORS methods |
| `middleware_cors_set_headers` | `void(const char *)` | 设置 CORS headers |

---

## static_serve.h — 静态文件服务

### 类型

| 类型 | 说明 |
|------|------|
| `MimeEntry` | MIME 映射: extension[16], mime_type[64] |
| `StaticConfig` | 配置: root_dir, mime_table[], mime_count, enable_caching, enable_range, directory_listing, index_file |
| `FileCacheInfo` | 缓存信息: etag, last_modified, file_size, mtime |

### 函数

| 函数 | 签名 | 说明 |
|------|------|------|
| `static_config_init` | `void(StaticConfig *, const char *root)` | 初始化配置 |
| `static_config_add_mime` | `void(StaticConfig *, const char *, const char *)` | 添加 MIME 类型 |
| `static_config_load_default_mimes` | `void(StaticConfig *)` | 加载 26 种默认 MIME |
| `static_get_mime_type` | `const char *(const StaticConfig *, const char *)` | 根据扩展名获取 MIME |
| `static_serve_file` | `bool(const StaticConfig *, const char *, const HttpRequest *, HttpResponse *)` | 服务静态文件 |
| `static_serve_range` | `bool(const StaticConfig *, const char *, const HttpRequest *, HttpResponse *, int64_t, int64_t)` | 服务 Range 请求 |
| `static_build_etag` | `void(const char *, time_t, uint64_t, char *, size_t)` | 生成 ETag |
| `static_build_cache_headers` | `void(const FileCacheInfo *, HttpResponse *)` | 添加缓存响应头 |
| `static_check_not_modified` | `bool(const HttpRequest *, const FileCacheInfo *)` | 检查 304 条件 |

---

## cgi_handler.h — CGI/FastCGI

### 类型

| 类型 | 说明 |
|------|------|
| `CgiEnvVar` | 环境变量: key[128], value[4096] |
| `CgiConfig` | 配置: script_path, env_vars[], timeout, pass_headers, pass_body |
| `CgiResult` | 结果: exit_code, stdout/stdin data/length, timed_out |
| `FcgiHeader` | FastCGI 记录头: version, type, request_id, content_length, padding_length |
| `FcgiBeginRequestBody` | FastCGI 请求体: role, flags |
| `FcgiEndRequestBody` | FastCGI 响应: app_status, protocol_status |
| `FcgiRecordType` | 枚举: 1-11 种记录类型 |
| `FcgiRole` | 枚举: RESPONDER, AUTHORIZER, FILTER |

### 函数

| 函数 | 签名 | 说明 |
|------|------|------|
| `cgi_config_init` | `void(CgiConfig *, const char *)` | 初始化 CGI 配置 |
| `cgi_config_add_env` | `void(CgiConfig *, const char *, const char *)` | 添加环境变量 |
| `cgi_config_set_request_env` | `void(CgiConfig *, const HttpRequest *)` | 从请求设置标准 CGI 环境 |
| `cgi_execute` | `bool(const CgiConfig *, const HttpRequest *, CgiResult *)` | 执行 CGI 脚本 |
| `cgi_parse_status_line` | `bool(const char *, size_t, int *, char *, size_t)` | 解析 CGI 状态行 |
| `cgi_parse_headers` | `void(const CgiResult *, HttpResponse *)` | 解析 CGI 响应头 |
| `cgi_result_to_response` | `bool(const CgiResult *, HttpResponse *)` | 转换 CGI 结果为 HTTP 响应 |
| `fcgi_build_header` | `void(uint8_t, uint16_t, uint16_t, uint8_t, FcgiHeader *)` | 构建 FastCGI 头 |
| `fcgi_build_begin_request` | `void(uint16_t, uint8_t, FcgiBeginRequestBody *)` | 构建 FastCGI 请求 |
| `fcgi_parse_header` | `bool(const uint8_t *, size_t, FcgiHeader *)` | 解析 FastCGI 头 |
| `fcgi_parse_end_request` | `bool(const uint8_t *, FcgiEndRequestBody *)` | 解析 FastCGI 响应 |

### CGI 环境变量 (cgi_config_set_request_env 设置)

```
REQUEST_METHOD    CONTENT_TYPE      HTTP_HOST
REQUEST_URI       CONTENT_LENGTH    HTTP_USER_AGENT
QUERY_STRING      GATEWAY_INTERFACE SERVER_PROTOCOL  SERVER_SOFTWARE
```
