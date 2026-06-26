# mini-web-server — Web 服务器 (C 语言实现)

## Module Status: COMPLETE ✅

轻量级 HTTP/1.1 Web 服务器库, 使用 C99 编写。模块化设计, 零外部依赖。

## 特性

- **HTTP/1.1** 请求/响应解析, 完整状态码支持 (30+)
- **Trie 树路由** — 静态段、路径参数 (`:id`)、通配符 (`*`)
- **洋葱模型中间件** — 日志、CORS、压缩、认证、限流、Body 解析
- **静态文件服务** — 26 种 MIME 类型, ETag/Last-Modified 缓存, Range 请求
- **CGI/FastCGI** — fork + pipe 进程管理, 环境变量传递, 超时控制
- **零依赖** — 仅需 C 标准库 + POSIX (`unistd.h`, `sys/wait.h`)
- **可嵌入** — 所有 API 通过头文件暴露, 无全局状态

## 快速开始

```bash
make all
```

```c
#include "http_core.h"
#include "router.h"
#include "middleware.h"

bool hello_handler(const HttpRequest *req, HttpResponse *res,
                   const RouteParam *params, int count) {
    http_response_set_body_str(res, "{\"message\":\"Hello, World!\"}");
    return true;
}

int main(void) {
    Router *router = router_create();
    router_add(router, HTTP_GET, "/api/hello", hello_handler);

    MiddlewareChain chain;
    middleware_chain_init(&chain);
    middleware_chain_add(&chain, middleware_logger, NULL);
    middleware_chain_add(&chain, middleware_cors,   NULL);

    /* Accept loop, parse HTTP, execute chain, dispatch */
    /* → see examples/ for full server integration      */
    return 0;
}
```

## 目录

```
include/    5 个头文件
src/        5 个源文件
examples/   3 个示例程序
demos/      2 个技术文档
docs/       API 参考 + 架构文档
```

## 构建

| 目标 | 命令 |
|------|------|
| 编译所有示例 | `make all` |
| 清理 | `make clean` |
| 编译 + 运行 demo | `make run` |

## 设计理念

| 规范 | 说明 |
|------|------|
| C99 | 纯 ANSI C, 无扩展 |
| 命名 | snake_case 函数, PascalCase 类型, UPPER_SNAKE_CASE 常量 |
| 守卫 | 所有头文件使用 `#ifndef` 守卫 |
| 依赖 | `<stdbool.h>`, `<stddef.h>`, `<stdint.h>` |
| 内存 | 显式 `_init` / `_free` 成对管理 |

## 许可证

MIT
