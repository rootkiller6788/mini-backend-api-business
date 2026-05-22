# ARCHITECTURE.md — mini-web-server Architecture

## 项目概述

mini-web-server 是一个用 C99 编写的轻量级 HTTP/1.1 Web 服务器库。设计目标: 模块化、可嵌入、零依赖 (仅 C 标准库 + POSIX)。

## 目录结构

```
mini-web-server/
├── include/            # 头文件 (5)
│   ├── http_core.h     # HTTP 请求/响应解析、状态码
│   ├── router.h        # Trie 树 URL 路由器
│   ├── middleware.h     # 中间件链 (洋葱模型)
│   ├── static_serve.h   # 静态文件服务、MIME、缓存
│   └── cgi_handler.h    # CGI/FastCGI 进程管理
├── src/                # 源文件 (5)
│   ├── http_core.c     # HTTP 解析实现
│   ├── router.c        # 路由实现
│   ├── middleware.c     # 中间件实现
│   ├── static_serve.c   # 静态文件实现
│   └── cgi_handler.c    # CGI 实现
├── examples/           # 示例程序 (3)
│   ├── http_parse_demo.c
│   ├── middleware_chain_demo.c
│   └── static_server_demo.c
├── demos/              # 演示文档 (2)
│   ├── request_lifecycle.md
│   └── middleware.md
├── docs/               # 项目文档 (2)
│   ├── ARCHITECTURE.md
│   └── API.md
├── Makefile
└── README.md
```

## 模块依赖关系

```
http_core.h  ◄──────────────────────────────┐
     ▲                                      │
     │  (基础类型: HttpRequest, HttpResponse) │
     │                                      │
     ├──── router.h ──── uses ───────────────┤
     ├──── middleware.h ─ uses ──────────────┤
     ├──── static_serve.h ─ uses ────────────┤
     └──── cgi_handler.h ─ uses ────────────┘

No circular dependencies. All modules depend only on http_core.h.
```

## 设计原则

1. **C99 兼容** — 无 GNU 扩展, 纯标准 C + POSIX
2. **头文件守卫** — 全部使用 `#ifndef` 守卫
3. **命名规范**:
   - 函数: `snake_case`
   - 类型: `PascalCase`
   - 常量: `UPPER_SNAKE_CASE`
4. **零外部依赖** — 仅 `libc` + `libm` + POSIX (`unistd.h`, `sys/wait.h`)
5. **模块化** — 每个 .h/.c 对独立可测

## 关键技术决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 路由结构 | Trie 树 | O(n) 查找, n=路径深度; 比正则快 10-100x |
| 中间件模型 | 洋葱模型 | 业界标准, 易于理解和组合 |
| 静态文件 | 读入内存 | 简化实现; 大文件可改用 sendfile() |
| CGI | fork + pipe | 标准 CGI 实现; FastCGI 提供 header 构建工具 |
| 内存管理 | 手动 malloc/free | C99 无 RAII; 配套 _init/_free 函数 |
| 字符串 | 固定缓冲区 | 避免动态分配复杂度, 设置合理上限 |

## 扩展点

- **TLS/HTTPS**: 包装 OpenSSL 在 accept 后
- **WebSocket**: 添加 upgrade 处理中间件
- **模板引擎**: 在 handler 中调用模板库
- **Session**: 添加 session 中间件 (cookie-based)
- **数据库连接池**: 在模块初始化时创建

## 构建

```bash
make        # 编译库和示例
make clean  # 清理
make all    # 编译所有目标
```

## 版本

0.1.0 — 初始版本
