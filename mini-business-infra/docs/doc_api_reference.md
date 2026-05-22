# mini-business-infra API 参考

## message_queue

### 生命周期

| 函数 | 说明 |
|------|------|
| `mq_broker_create()` | 创建消息代理 |
| `mq_broker_destroy(broker)` | 销毁代理及所有资源 |
| `mq_broker_start(broker, addr, port)` | 启动代理 |
| `mq_broker_stop(broker)` | 停止代理 |
| `mq_broker_open_channel(broker)` | 打开信道 |

### 拓扑管理

| 函数 | 说明 |
|------|------|
| `mq_exchange_declare(ch, decl)` | 声明交换机 (direct/topic/fanout) |
| `mq_queue_declare(ch, decl)` | 声明队列 (含 durable, TTL, DLX) |
| `mq_queue_bind(ch, q, ex, rk)` | 绑定队列到交换机 |
| `mq_queue_unbind(ch, q, ex, rk)` | 解绑 |

### 消息收发

| 函数 | 说明 |
|------|------|
| `mq_basic_publish(ch, ex, rk, msg)` | 发布消息 |
| `mq_basic_consume(ch, q, cb, data)` | 消费消息 |
| `mq_basic_cancel(ch, q)` | 取消消费 |
| `mq_basic_ack(ch, msg_id, status)` | 确认消息 (ACK/NACK/REQUEUE) |

### 发布确认

| 函数 | 说明 |
|------|------|
| `mq_set_confirm_mode(ch, mode)` | 设置确认模式 |
| `mq_set_confirm_callback(ch, cb, data)` | 设置确认回调 |

### 管理

| 函数 | 说明 |
|------|------|
| `mq_queue_purge(ch, q)` | 清空队列 |
| `mq_queue_delete(ch, q)` | 删除队列 |
| `mq_exchange_delete(ch, ex)` | 删除交换机 |
| `mq_queue_message_count(broker, q)` | 获取队列消息数 |

---

## distributed_cache

### 生命周期

| 函数 | 说明 |
|------|------|
| `dc_cache_create(config)` | 创建缓存实例 |
| `dc_cache_destroy(cache)` | 销毁缓存 |

### 配置

```c
typedef struct {
    size_t               max_entries;         // 最大条目数
    size_t               max_memory_bytes;    // 最大内存
    dc_eviction_policy_t eviction_policy;     // 驱逐策略 (LRU/LFU/TTL)
    int                  stampede_protection;  // 缓存击穿防护
    double               recompute_threshold;  // 提前刷新阈值 (0.0~1.0)
    dc_write_strategy_t  write_strategy;      // 写策略
    dc_backend_read_fn   backend_read;        // 后端读取回调
    dc_backend_write_fn  backend_write;       // 后端写入回调
} dc_cache_config_t;
```

### 基本操作

| 函数 | 说明 |
|------|------|
| `dc_cache_put(cache, key, val, len, ttl)` | 写入缓存 |
| `dc_cache_get(cache, key, &val, &len)` | 读取缓存 |
| `dc_cache_delete(cache, key)` | 删除条目 |
| `dc_cache_exists(cache, key)` | 检查存在 |
| `dc_cache_expire(cache, key, ttl)` | 设置 TTL |
| `dc_cache_ttl(cache, key)` | 查询剩余 TTL |

### 缓存失效

| 函数 | 说明 |
|------|------|
| `dc_cache_invalidate(cache, key, mode)` | 失效 (DELETE/UPDATE/VERSION) |

### 批量操作

| 函数 | 说明 |
|------|------|
| `dc_cache_batch_get(cache, keys, n, vals, lens)` | 批量读取 |
| `dc_cache_batch_put(cache, keys, vals, lens, ttls, n)` | 批量写入 |

### 统计与管理

| 函数 | 说明 |
|------|------|
| `dc_cache_flush(cache)` | 清空缓存 |
| `dc_cache_size(cache)` | 条目数 |
| `dc_cache_memory_used(cache)` | 内存占用 |
| `dc_cache_hit_count(cache)` | 命中次数 |
| `dc_cache_miss_count(cache)` | 未命中次数 |
| `dc_cache_hit_rate(cache)` | 命中率 |
| `dc_cache_compact(cache)` | 压缩 (清理过期) |

---

## config_center

### 生命周期

| 函数 | 说明 |
|------|------|
| `cc_center_create()` | 创建配置中心 |
| `cc_center_destroy(center)` | 销毁配置中心 |

### 配置 CURD

| 函数 | 说明 |
|------|------|
| `cc_config_put(center, ns, grp, key, val)` | 写入配置 |
| `cc_config_get(center, ns, grp, key, &entry)` | 读取配置 |
| `cc_config_delete(center, ns, grp, key)` | 删除配置 |

### 加密配置

| 函数 | 说明 |
|------|------|
| `cc_config_put_encrypted(center, ns, grp, key, val, enc_key)` | 加密写入 |
| `cc_config_get_decrypted(center, ns, grp, key, out, size, enc_key)` | 解密读取 |

### 版本管理

| 函数 | 说明 |
|------|------|
| `cc_config_get_version(center, ns, grp, key, ver, &entry)` | 按版本读取 |
| `cc_config_list_versions(center, ns, grp, key, vers, &cnt)` | 列出历史版本 |
| `cc_config_rollback(center, ns, grp, key, ver)` | 回滚到指定版本 |

### 灰度发布

| 函数 | 说明 |
|------|------|
| `cc_gray_release_set(center, ns, grp, key, &gray)` | 设置灰度规则 |
| `cc_gray_release_get(center, ns, grp, key, ip, &entry)` | 灰度取值 |

### 订阅与轮询

| 函数 | 说明 |
|------|------|
| `cc_subscribe(center, ns, cb, data)` | 订阅配置变更 |
| `cc_unsubscribe(center, ns)` | 取消订阅 |
| `cc_poll_update(center, ns, grp)` | 触发轮询更新 |
| `cc_poll_all(center)` | 触发全量轮询 |

---

## service_registry

### 生命周期

| 函数 | 说明 |
|------|------|
| `sr_registry_create()` | 创建注册中心 |
| `sr_registry_destroy(registry)` | 销毁 |

### 注册与发现

| 函数 | 说明 |
|------|------|
| `sr_register(registry, &inst)` | 注册服务实例 |
| `sr_deregister(registry, svc, inst_id)` | 注销实例 |
| `sr_discover(registry, svc, &list, &cnt)` | 发现所有实例 |
| `sr_discover_healthy(registry, svc, &list, &cnt)` | 发现健康实例 |
| `sr_lookup_one(registry, svc, strategy, &inst)` | 负载均衡选取一个 |

### 健康管理

| 函数 | 说明 |
|------|------|
| `sr_heartbeat(registry, svc, inst_id)` | 发送心跳 |
| `sr_set_status(registry, svc, inst_id, status)` | 设置状态 |
| `sr_health_check(registry)` | 执行健康检查 |
| `sr_set_health_callback(registry, cb, data)` | 健康变更回调 |
| `sr_ttl_watchdog_start(registry)` | 启动 TTL 看门狗 |
| `sr_ttl_watchdog_stop(registry)` | 停止 TTL 看门狗 |

---

## gateway_routing

### 生命周期

| 函数 | 说明 |
|------|------|
| `gw_gateway_create()` | 创建网关 |
| `gw_gateway_destroy(gateway)` | 销毁网关 |

### 路由管理

| 函数 | 说明 |
|------|------|
| `gw_route_add(gateway, &rule)` | 添加路由规则 |
| `gw_route_remove(gateway, method, path)` | 移除路由 |
| `gw_route_update(gateway, &rule)` | 更新路由 |
| `gw_route_find(gateway, method, path)` | 查找匹配路由 |
| `gw_route_list(gateway, rules, &cnt)` | 列出所有路由 |

### 上游管理

| 函数 | 说明 |
|------|------|
| `gw_upstream_add(gateway, svc, &up)` | 添加上游 |
| `gw_upstream_remove(gateway, svc, host, port)` | 移除上游 |
| `gw_upstream_select(gateway, svc, algo)` | 负载均衡选取上游 |

### 请求处理

| 函数 | 说明 |
|------|------|
| `gw_forward(gateway, &req, &resp)` | 转发请求 |
| `gw_aggregate(gateway, reqs, n, &resp)` | 聚合请求 |
| `gw_set_auth_check(gateway, fn, data)` | 设置鉴权回调 |
| `gw_set_req_transform(gateway, fn, data)` | 请求变换 |
| `gw_set_resp_transform(gateway, fn, data)` | 响应变换 |
| `gw_rate_limiter_allow(gateway, path, ip)` | 限流检查 |
| `gw_rate_limiter_reset(gateway, path)` | 重置限流器 |

### 路由规则

```c
typedef struct {
    gw_http_method_t method;           // HTTP 方法
    char             path[256];        // 路径
    char             service_name[64]; // 目标服务
    int              auth_required;    // 是否需要鉴权
    int              rate_limit_per_sec; // 每秒限流
    int32_t          priority;         // 优先级
    int              enabled;          // 是否启用
} gw_route_rule_t;
```
