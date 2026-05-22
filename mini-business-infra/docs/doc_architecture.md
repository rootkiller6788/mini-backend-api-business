# mini-business-infra 架构文档

## 总体架构

```
+---------------------------------------------------------------+
|                    mini-business-infra                         |
+---------------------------------------------------------------+
|  gateway_routing  |  config_center  |  service_registry        |
|  (API 网关路由)     |  (配置中心)      |  (服务注册与发现)          |
+---------------------------------------------------------------+
|  message_queue    |  distributed_cache                         |
|  (消息队列)        |  (分布式缓存)                               |
+---------------------------------------------------------------+
```

## 模块设计

### 1. 消息队列 (message_queue)

核心数据结构：`mq_broker_t` 管理 exchanges、queues 和 bindings。

```
Publisher → Exchange (direct/topic/fanout) → [Binding] → Queue → Consumer
                                                    ↓
                                            Dead Letter Exchange
                                                    ↓
                                              Dead Letter Queue
```

- **Exchange 类型**: Direct (精确匹配 routing key), Topic (通配符匹配), Fanout (广播)
- **队列特性**: durable (持久化标记), TTL (消息超时), DLX (死信交换机)
- **消息确认**: Publisher Confirm (发布确认), Consumer Ack/Nack/Requeue
- **重试机制**: 指数退避重试, 达到最大重试次数后进入 DLQ

### 2. 分布式缓存 (distributed_cache)

```
Client → cache_get() → [命中] → 返回缓存值
                     → [未命中] → backend_read() → 写入缓存 → 返回
                     → [即将过期] → 概率性提前刷新 (防缓存击穿)
```

- **驱逐策略**: LRU (最近最少使用), LFU (最不经常使用), TTL (超时驱逐)
- **缓存击穿防护**: 概率性提前重算 (`recompute_threshold` 控制触发时机)
- **写策略**: Write-through (同步写后端), Write-behind (异步写), Write-around (绕过)
- **失效模式**: Delete (删除), Update (更新), Version (版本控制)

### 3. 配置中心 (config_center)

```
Config Admin → cc_config_put() → 版本历史 → 通知订阅者 (hot-reload)
                                      ↓
                               gray_release → 按 IP 灰度发布
```

- **存储模型**: namespace → group → key → value
- **热加载**: 订阅者通过 `cc_subscribe()` 注册, 配置变更时自动回调
- **版本控制**: 每次修改生成版本记录, 支持历史查询和回滚
- **加密**: XOR 加密存储敏感配置值
- **灰度发布**: 按实例 IP 白名单推送灰度配置

### 4. 服务注册 (service_registry)

```
Service Instance → sr_register() → [heartbeat] → TTL Watchdog
                                                   ↓
                                              mark DOWN
Client → sr_discover() → [LB: RR/Weighted/Random] → instance
```

- **注册**: 服务实例提供 name, host, port, metadata, health_url
- **心跳 + TTL**: 定期心跳维持 UP 状态, 超时标记 DOWN
- **负载均衡**: Round Robin, Weighted, Random
- **健康检查**: 主动检查 + 状态变更回调

### 5. 网关路由 (gateway_routing)

```
Request → gw_forward() → Route Match → Auth Check → Rate Limit → Transform → Upstream
                                                                                  ↓
Response ← gw_transform_resp() ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ←
```

- **路由匹配**: path + method → upstream service, 优先级排序
- **负载均衡**: Weighted RR, Round Robin, Least Connections
- **限流**: 令牌桶算法, 按 path + client IP 维度
- **鉴权**: 可注入 auth_check 回调
- **变换**: 请求/响应变换钩子

## 数据流

```
[外部请求] → Gateway → [鉴权] → [限流] → [路由] → [负载均衡]
                ↓                                    ↓
         Config Center ← [热加载]              Service Registry
                ↓                                    ↓
         [灰度发布]                            [健康检查]
```

## 线程安全

当前实现为单线程模型。多线程场景需外部加锁保护。
