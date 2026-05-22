# mini-api-engineering — API工程 (C 语言实现)

> 参考 REST, GraphQL Spec, gRPC, OpenAPI 3.0

## 模块概览 Module Overview

| 模块 (Module) | 头文件 (Header) | 源文件 (Source) | 功能简介 |
|---|---|---|---|
| **rest_design** | `include/rest_design.h` | `src/rest_design.c` | RESTful 资源建模、路由、分页、HATEOAS 链接 |
| **graphql_engine** | `include/graphql_engine.h` | `src/graphql_engine.c` | GraphQL Schema 构建、查询解析、SDL 解析、内省 |
| **grpc_sim** | `include/grpc_sim.h` | `src/grpc_sim.c` | gRPC 协议模拟：Proto 定义、流式 RPC、HTTP/2 帧 |
| **openapi_builder** | `include/openapi_builder.h` | `src/openapi_builder.c` | OpenAPI 3.0 规范构建器，生成 swagger.json |
| **api_version** | `include/api_version.h` | `src/api_version.c` | API 版本策略：URI/Header/Query、弃用与日落管理 |

## 目录结构 Directory Tree

```
mini-api-engineering/
├── README.md
├── Makefile
├── include/
│   ├── rest_design.h
│   ├── graphql_engine.h
│   ├── grpc_sim.h
│   ├── openapi_builder.h
│   └── api_version.h
├── src/
│   ├── rest_design.c
│   ├── graphql_engine.c
│   ├── grpc_sim.c
│   ├── openapi_builder.c
│   └── api_version.c
├── examples/
│   ├── demo_rest_crud.c
│   ├── demo_graphql_query.c
│   └── demo_openapi_spec.c
├── demos/
│   ├── mini-restful-design/
│   │   └── README.md
│   └── mini-graphql-core/
│       └── README.md
├── docs/
│   ├── api-design-principles.md
│   ├── demo_rest_crud.md
│   └── demo_graphql_query.md
├── bin/                  (build output)
├── benches/
└── tests/
```

## 构建与测试 Build & Test

```bash
# 构建所有演示程序
make all

# 运行测试（执行所有演示程序）
make test

# 单独构建某个目标
make demo_rest_crud
make demo_graphql_query
make demo_openapi_spec

# 清理构建产物
make clean
```
