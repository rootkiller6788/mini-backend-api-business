# mini-business-infra — 业务基础设施 (C 语言实现)

## 概述

`mini-business-infra` 是一个纯 C99 实现的轻量级业务基础设施库，覆盖分布式系统中常见的五类中间件组件：消息队列、分布式缓存、配置中心、服务注册与发现、API 网关路由。

## 模块一览

| 模块 | 头文件 | 源文件 | 说明 |
|------|--------|--------|------|
| 消息队列 | `include/message_queue.h` | `src/message_queue.c` | 内存内消息代理 (direct/topic/fanout) |
| 分布式缓存 | `include/distributed_cache.h` | `src/distributed_cache.c` | 支持 LRU/LFU/TTL 的分布式 KV 缓存 |
| 配置中心 | `include/config_center.h` | `src/config_center.c` | KV 配置存储, 热加载, 灰度发布 |
| 服务注册 | `include/service_registry.h` | `src/service_registry.c` | 服务实例注册, 心跳, 健康检查 |
| 网关路由 | `include/gateway_routing.h` | `src/gateway_routing.c` | 路由表, 限流, 负载均衡, 鉴权 |

## 快速开始

### 构建

```bash
make
```

### 运行示例

```bash
make examples
bin/example_broker
bin/example_cache
bin/example_config
```

### 运行演示

```bash
make demos
bin/demo_full_stack
bin/demo_integration
```

## 目录结构

```
mini-business-infra/
├── include/          # 头文件
├── src/              # 源文件
├── examples/         # 独立示例
├── demos/            # 综合演示
├── docs/             # 文档
├── benches/          # 性能基准
├── tests/            # 单元测试
├── Makefile
└── README.md
```

## 编译要求

- C99 编译器 (GCC 或 Clang)
- GNU Make
- 无外部依赖 (纯标准库)

## 许可证

MIT
