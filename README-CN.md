# Mini Backend API Business（迷你后端接口业务）

**从零开始、零依赖的 C 语言实现**，涵盖后端服务、API 工程、认证授权、业务架构和基础设施模式。每个模块以教学级精度建模真实后端系统行为 — 从 HTTP 服务器和 REST/GraphQL API 到 OAuth2 认证、DDD/CQRS 业务架构、消息队列和任务调度系统。

## 模块总览

| 模块 | 主题 | 参考标准 |
|--------|--------|----------------|
| [mini-web-server](mini-web-server/) | HTTP/1.1 服务器、中间件链、路由（Trie/Radix）、静态文件服务、CGI/FastCGI 仿真 | NGINX 内部原理, Express.js |
| [mini-api-engineering](mini-api-engineering/) | RESTful API 设计、GraphQL Schema/查询解析器、gRPC 仿真、OpenAPI/Swagger 规范构建器、版本管理 | REST, GraphQL Spec, gRPC |
| [mini-backend-framework](mini-backend-framework/) | DI/IoC 容器、ORM（ActiveRecord/DataMapper）、MVC 模式、输入验证、序列化 | Spring, Django, Rails |
| [mini-auth-security](mini-auth-security/) | OAuth2（授权码、客户端凭证）、JWT 签名/验证、RBAC/ABAC、SSO 模型、速率限制 | OAuth2 RFC 6749, JWT RFC 7519 |
| [mini-business-arch](mini-business-arch/) | DDD（实体/值对象/聚合/仓储）、CQRS 命令查询分离、事件溯源、Saga 模式 | Eric Evans DDD, Martin Fowler |
| [mini-business-infra](mini-business-infra/) | 消息队列（发布订阅、死信）、分布式缓存（LRU、TTL）、配置中心（热重载）、服务注册 | RabbitMQ, Redis, Nacos |
| [mini-job-system](mini-job-system/) | Cron 调度器、延迟任务队列、工作队列（Worker Pool）、任务重试退避、任务进度追踪 | Celery, Sidekiq, Quartz |

## 设计理念

- **零外部依赖** — 纯 C（C99/C11），仅使用 `libc` 和 `libm`
- **模块自包含** — 每个目录自带 `Makefile`、`include/`、`src/`、`examples/`、`demos/`、`tests/`
- **用户态后端仿真** — 对后端服务、认证流程和业务架构的教学级建模
- **理论到代码的映射** — 每个模块包含 `docs/` 目录，内有参考规范对齐说明
- **实用演示程序** — HTTP 服务器、OAuth2 认证服务器、消息队列引擎、任务调度器等

## 构建方式

每个模块相互独立。进入模块目录后运行：

```bash
cd mini-web-server
make all    # 构建全部
make test   # 运行测试
```

需要 **GCC** 和 **GNU Make**。

## 项目结构

```
mini-backend-api-business/
├── mini-web-server/             # Web 服务器
├── mini-api-engineering/        # API 工程
├── mini-backend-framework/      # 后端框架
├── mini-auth-security/          # 认证与安全
├── mini-business-arch/          # 业务架构（DDD/CQRS）
├── mini-business-infra/         # 业务基础设施
└── mini-job-system/             # 任务调度系统
```

## 许可证

MIT
