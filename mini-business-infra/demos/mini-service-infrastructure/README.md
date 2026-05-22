# mini-service-infrastructure — 服务基础设施深度剖析

## 概述

`mini-service-infrastructure` 是 `mini-business-infra` 中服务基础设施模块的深度指南，覆盖三大核心组件：**分布式缓存**（`distributed_cache`）、**配置中心**（`config_center`）、**服务注册与发现**（`service_registry`）。它们共同构成微服务架构的基础设施层。

---

## 第一部分：分布式缓存 (`distributed_cache`)

### 架构概览

```
+-----------+       +----------------+       +------------+
|  Client   | ----> |  Cache Node    | ----> |  Backend   |
| (app)     |       | (LRU/TTL/LFU)  |       | (DB/Redis) |
+-----------+       +----------------+       +------------+
                           |
                     stampede protection
                     (recompute_threshold)
```

`distributed_cache` 提供一个带驱逐策略、TTL、缓存击穿保护的本地缓存节点，可对接远端存储后端实现多级缓存。

### 缓存配置

```c
typedef struct {
    size_t               max_entries;          // 最大缓存条目数
    size_t               max_memory_bytes;     // 最大内存占用 (字节)
    dc_eviction_policy_t eviction_policy;       // 驱逐策略
    int                  stampede_protection;   // 击穿保护开关
    double               recompute_threshold;   // 击穿保护阈值
    dc_write_strategy_t  write_strategy;        // 写策略
    dc_backend_read_fn   backend_read;          // 后端读取回调
    dc_backend_write_fn  backend_write;         // 后端写入回调
    void                *backend_ctx;           // 后端上下文
    dc_eviction_callback on_evict;              // 驱逐回调
    void                *evict_user_data;       // 驱逐回调数据
} dc_cache_config_t;
```

---

### 驱逐策略 (`dc_eviction_policy_t`)

| 策略 | 枚举值 | 淘汰逻辑 | 适用场景 |
|------|--------|----------|----------|
| 无驱逐 | `DC_EVICTION_NONE` | 超出 `max_entries` 时 `put` 返回错误 | 固定大小缓存，严格容量控制 |
| LRU | `DC_EVICTION_LRU` | 淘汰最久未访问条目 | 通用热点数据缓存 |
| LFU | `DC_EVICTION_LFU` | 淘汰最少访问频率条目 | 读多写少、有明显热度分布 |
| TTL | `DC_EVICTION_TTL` | 淘汰已过期条目，不考虑容量 | 时效性数据（session、token） |

#### LRU 详解

- **数据结构**：双向链表 + 哈希表索引，O(1) `get`/`put`/`delete`
- **驱逐时机**：`put` 时若条目数达到 `max_entries` 且内存达到 `max_memory_bytes`，驱逐链表尾部
- **访问维护**：每次 `get` 将条目移动到链表头部
- **淘汰回调**：驱逐前触发 `on_evict`，可用于写回后端存储

```c
dc_cache_config_t cfg = {
    .max_entries    = 100000,
    .max_memory_bytes = 256 * 1024 * 1024,  // 256 MB
    .eviction_policy = DC_EVICTION_LRU
};
dc_cache_t *cache = dc_cache_create(&cfg);
```

#### LFU 详解

- 为每个 key 维护访问计数器
- 驱逐时选择计数最小的条目
- LFU 对新热点反应慢但抗扫描干扰好

#### TTL 详解

- 过期数据**惰性删除**（`get` 时检查 + 后台定期扫描）
- `dc_cache_expire()` 可手动更新 TTL
- `dc_cache_ttl()` 返回剩余秒数

---

### TTL 生存时间

```c
// put 时指定 TTL
dc_cache_put(cache, "session:123", session_data, len, 1800);  // 30 分钟过期

// 动态续期 (滑动过期)
dc_cache_expire(cache, "session:123", 1800);  // 重置为 30 分钟

// 查询剩余 TTL
int64_t remaining = dc_cache_ttl(cache, "session:123");
if (remaining <= 0) {
    // 已过期，需重新获取
}

// 检查存在性
int exists = dc_cache_exists(cache, "session:123");
```

**TTL 时钟与精度：**
- 默认为 300 秒 (`DC_DEFAULT_TTL_SEC`)
- 基于系统时间 (`time_t`)，秒级精度
- 过期检查发生在 `get`、`exists`、`ttl` 调用以及后台清理扫描

---

### 缓存击穿防护 (Stampede Protection)

**问题：** 热点数据过期瞬间，大量并发请求同时穿透到后端，造成后端压力骤增甚至雪崩。

**解决方案：**

```c
dc_cache_config_t cfg = {
    .stampede_protection = 1,          // 开启击穿保护
    .recompute_threshold = 0.8         // TTL 剩余 20% 时触发异步刷新
};
```

**工作机制：**

```
时间轴:
  TTL=100s                               TTL=0
  |                                       |
  [===========|-----threshold------|======]
              |                    |
            TTL=80s              TTL=20s
              |                    |
             正常返回缓存       触发异步刷新
              (不发后端)       (先返回旧值，后台重建)
```

| 阶段 | TTL 比例 | 行为 |
|------|----------|------|
| 正常期 | > `recompute_threshold` | 直接返回缓存值 |
| 预热期 | ≤ `recompute_threshold` | 返回旧值 + 异步触发 `backend_read` 重建缓存 |
| 过期期 | ≤ 0 | 同步调用 `backend_read`（首次请求线程重建） |

**击穿保护的关键控制流：**

1. **单线程重建**：使用互斥锁确保同一 key 只有一个线程执行 `backend_read`
2. **旧值降级**：重建期间其他请求返回过期旧值（避免空白 gap）
3. **重建超时**：若 `backend_read` 超时未完成，释放锁允许下一次尝试

---

### 写策略 (`dc_write_strategy_t`)

| 策略 | 枚举值 | 写入路径 | 延迟 | 一致性 |
|------|--------|----------|------|--------|
| Write-Through | `DC_WRITE_THROUGH` | 缓存 + 后端同时写 | 较高 | 强一致 |
| Write-Behind | `DC_WRITE_BEHIND` | 先写缓存，异步批量写后端 | 低 | 最终一致 |
| Write-Around | `DC_WRITE_AROUND` | 直接写后端，读时加载到缓存 | 最低 | 读时一致 |

```c
typedef int (*dc_backend_read_fn)(const char *key, uint8_t **value, size_t *len, void *ctx);
typedef int (*dc_backend_write_fn)(const char *key, const uint8_t *value, size_t len, void *ctx);

// Write-Through 示例
int my_backend_read(const char *key, uint8_t **value, size_t *len, void *ctx) {
    return db_get((db_t *)ctx, key, value, len);  // 从 DB 读取
}
int my_backend_write(const char *key, const uint8_t *value, size_t len, void *ctx) {
    return db_put((db_t *)ctx, key, value, len);  // 同步写 DB
}
```

---

### 缓存失效 (`dc_invalidate_mode_t`)

| 模式 | 枚举值 | 行为 |
|------|--------|------|
| Delete | `DC_INVALIDATE_DELETE` | 直接删除缓存条目 |
| Update | `DC_INVALIDATE_UPDATE` | 刷新（重新从后端加载） |
| Version | `DC_INVALIDATE_VERSION` | 版本号递增使缓存失效 |

**分布式缓存一致性建议：**
- 使用 fanout exchange 广播 `invalidate` 消息到所有缓存节点
- 版本号模式适合部署零停机的灰度发布

### 监控指标

```c
size_t dc_cache_size(cache);         // 当前条目数
size_t dc_cache_memory_used(cache);  // 当前内存占用 (字节)
size_t dc_cache_hit_count(cache);    // 累计命中次数
size_t dc_cache_miss_count(cache);   // 累计未命中次数
double dc_cache_hit_rate(cache);     // 命中率 (0.0 ~ 1.0)
```

---

## 第二部分：配置中心 (`config_center`)

### 架构概览

```
+----------------+     subscribe/poll     +-------------------+
| Config Center  | <------------------->  | Service Instance  |
| (server)       |     hot reload         | (client)          |
+----------------+                        +-------------------+
        |                                         |
   [ version / rollback ]                   [ callback ]
        |                                         |
   [ gray release ]                         [ local cache ]
```

配置中心提供 KV 配置的集中管理，支持命名空间隔离、版本管理、滚动回滚、灰度发布和热加载通知。

### 核心概念

#### 命名空间 (Namespace)

```
namespaces:
  ├── "production"
  │     ├── "database"          (group)
  │     │     ├── "mysql.host"  (key)
  │     │     └── "mysql.port"  (key)
  │     └── "cache"
  │           ├── "redis.host"
  │           └── "redis.ttl"
  └── "staging"
        └── ...
```

- **Namespace** 用于环境隔离 (prod / staging / dev)
- **Group** 用于模块隔离 (database / cache / gateway)
- **Key** 具体配置项

#### 配置条目结构

```c
typedef struct {
    char      config_key[CC_MAX_KEY_LEN];     // 128 字节
    char     *value;                            // 动态分配的值
    size_t    value_len;                        // 值长度
    int64_t   version;                          // 当前版本号
    int64_t   created_at;                       // 创建时间戳
    int64_t   updated_at;                       // 最后更新时间戳
    char      namespace_[CC_MAX_NAMESPACE_LEN]; // 64 字节
    char      group_[CC_MAX_GROUP_LEN];         // 64 字节
    cc_encrypt_type_t encrypt_type;             // 加密类型
} cc_config_entry_t;
```

---

### 热加载 (Hot Reload)

配置中心通过**订阅-通知**模式实现热加载，服务实例无需重启即可感知配置变更。

#### 订阅机制

```c
void on_config_change(const char *namespace, const char *key,
                      const char *new_value, size_t new_len,
                      void *user_data) {
    printf("[hot-reload] %s/%s = %.*s\n", namespace, key, (int)new_len, new_value);

    // 立即应用新配置
    if (strcmp(key, "mysql.pool_size") == 0) {
        int new_pool_size = atoi(new_value);
        resize_connection_pool(new_pool_size);
    }
}

cc_subscribe(center, "production", on_config_change, my_service);
```

#### 轮询模式

对于不支持长连接的场景，可主动拉取变更：

```c
cc_poll_update(center, "production", "database");   // 拉取指定 group 变更
cc_poll_all(center);                                 // 拉取所有订阅的 namespace
```

**热加载流程：**

```
1. 管理员更新配置 (cc_config_put)
2. 版本号 +1，updated_at 更新
3. 中心遍历订阅列表
4. 调用 on_config_change 回调（每个订阅者）
5. 服务实例在回调中应用新配置
```

---

### 版本管理与回滚

#### 版本结构

```c
typedef struct {
    int64_t   version;
    char     *value;
    size_t    value_len;
    int64_t   created_at;
    char      operator_[64];    // 操作人
} cc_config_version_t;
```

#### 版本操作

```c
// 查询历史版本
cc_config_version_t versions[CC_MAX_VERSIONS];
int count = CC_MAX_VERSIONS;
cc_config_list_versions(center, "production", "database", "mysql.host", versions, &count);

for (int i = 0; i < count; i++) {
    printf("v%lld by %s at %lld\n",
           (long long)versions[i].version,
           versions[i].operator_,
           (long long)versions[i].created_at);
}

// 读取指定版本
cc_config_entry_t entry;
cc_config_get_version(center, "production", "database", "mysql.host", 42, &entry);

// 回滚到指定版本
cc_config_rollback(center, "production", "database", "mysql.host", 42);
```

**回滚语义：**
- 回滚本质是创建一个新版本，其值为目标版本的值
- 因此 `cc_config_rollback` 后版本号继续递增（而非重置）
- 回滚操作本身也是版本历史中的一条记录

---

### 灰度发布 (Gray Release)

配置中心支持基于实例 IP 的灰度推送，实现配置的渐进式发布。

```c
typedef struct {
    cc_gray_type_t type;                           // CC_GRAY_IP 或 CC_GRAY_ALL
    char            target_ips[256][CC_MAX_INSTANCE_IP]; // 目标 IP 列表
    int             ip_count;                       // 目标 IP 数量
    char           *gray_value;                     // 灰度配置值
    size_t          gray_value_len;                 // 灰度值长度
    int64_t         gray_version;                   // 灰度版本号
} cc_gray_release_t;
```

**灰度发布流程：**

```
1. 设置灰度规则
   ┌─────────────────────────────────────────┐
   │ cc_gray_release_t gray = {              │
   │     .type      = CC_GRAY_IP,            │
   │     .target_ips = {"10.0.1.5"},         │  ← 仅灰度这台
   │     .ip_count   = 1,                    │
   │     .gray_value = "pool_size=50",       │  ← 新配置值
   │     .gray_version = 1                   │
   │ };                                      │
   │ cc_gray_release_set(center, "prod",     │
   │     "database", "mysql.pool", &gray);   │
   └─────────────────────────────────────────┘

2. 服务端读取配置
   - 实例 10.0.1.5 调用 cc_gray_release_get(ip="10.0.1.5")
     → 返回灰度值 "pool_size=50"
   - 实例 10.0.1.6 调用 cc_gray_release_get(ip="10.0.1.6")
     → 返回原值 "pool_size=20"

3. 验证灰度无异常后，全量发布
   ┌─────────────────────────────────────────┐
   │ cc_config_put(center, "prod",           │
   │     "database", "mysql.pool",           │
   │     "pool_size=50");                    │  ← 全量更新
   │ cc_gray_release_set(center, "prod",     │
   │     "database", "mysql.pool",           │
   │     &(cc_gray_release_t){               │
   │         .type = CC_GRAY_ALL             │  ← 取消灰度
   │     });                                 │
   └─────────────────────────────────────────┘
```

**灰度模式：**
| 模式 | `CC_GRAY_ALL` | `CC_GRAY_IP` |
|------|---------------|--------------|
| 生效范围 | 所有实例 | 指定 IP 列表 |
| 非灰度 IP | 获取灰度值 | 获取原值 |
| 适用场景 | 全量发布 / 取消灰度 | 金丝雀发布 / 蓝绿 |

---

### 加密配置

```c
// 加密写入
cc_config_put_encrypted(center, "production", "database",
                        "mysql.password", "my_secret", "encryption_key_32");

// 解密读取
char decrypted[256];
cc_config_get_decrypted(center, "production", "database",
                        "mysql.password", decrypted, sizeof(decrypted),
                        "encryption_key_32");
```

- 加密算法：AES (`CC_ENC_AES`)
- 密钥管理由上层应用负责，配置中心只存储密文
- 解密在服务端完成，明文不在网络中传输

---

## 第三部分：服务注册与发现 (`service_registry`)

### 架构概览

```
+-----------+  register/heartbeat  +------------+
|  Service  | -------------------> |  Registry  |
|  Instance |                      |  (server)  |
+-----------+                      +------------+
                                       ^
                                       | discover / lookup
+-----------+                          |
|  Client   | ------------------------+
+-----------+
```

### 服务实例模型

```c
typedef struct {
    char      service_name[SR_MAX_NAME_LEN];     // 服务名 (64B)
    char      instance_id[37];                    // UUID 实例 ID
    char      host[SR_MAX_HOST_LEN];              // 主机地址 (128B)
    uint16_t  port;                               // 端口
    char      health_url[SR_MAX_URL_LEN];         // 健康检查 URL (256B)
    char      metadata[SR_MAX_META_LEN];          // 元数据 (1024B)
    int32_t   weight;                             // 权重
    int64_t   register_time;                      // 注册时间
    int64_t   last_heartbeat;                     // 最后心跳
    int32_t   heartbeat_interval;                 // 心跳间隔 (默认 30s)
    int32_t   ttl_seconds;                        // 存活时间 (默认 90s)
    sr_instance_status_t status;                  // 当前状态
} sr_instance_t;
```

---

### 注册流程 (Register)

注册是服务实例加入服务网格的第一步。

```c
sr_instance_t inst = {
    .service_name       = "user-service",
    // instance_id 由 UUID 生成
    .host               = "10.0.0.42",
    .port               = 8080,
    .health_url         = "/health",
    .weight             = 100,
    .heartbeat_interval = SR_DEFAULT_HEARTBEAT,  // 30 秒
    .ttl_seconds        = SR_DEFAULT_TTL,         // 90 秒
    .status             = SR_STATUS_UP
};
snprintf(inst.instance_id, sizeof(inst.instance_id),
         "%s", generate_uuid_v4());

int rc = sr_register(registry, &inst);
```

**注册步骤：**
1. 服务端生成 `instance_id`（UUID v4）
2. 填充实例元数据（host, port, 健康检查 URL, 权重, 元数据）
3. 调用 `sr_register()` 将实例注册到注册中心
4. 注册后实例状态为 `SR_STATUS_UP`
5. 若不及时发送心跳，实例会在 `ttl_seconds` 后被标记为 `SR_STATUS_DOWN`

---

### 心跳机制 (Heartbeat)

心跳是服务存活的持续信号。注册中心通过 TTL 看门狗检测失联实例。

```c
// 服务端：定期发送心跳
while (running) {
    sr_heartbeat(registry, "user-service", instance_id);
    sleep(heartbeat_interval);
}

// 注册中心：启动 TTL 看门狗
sr_ttl_watchdog_start(registry);
```

**TTL 看门狗逻辑：**

```
看门狗线程 (每 5 秒巡检):
  for each instance:
    elapsed = now - instance.last_heartbeat
    if elapsed > instance.ttl_seconds:
      if instance.status == SR_STATUS_UP:
        instance.status = SR_STATUS_DOWN
        触发 health_check_callback
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `heartbeat_interval` | 30s | 客户端发送心跳的间隔 |
| `ttl_seconds` | 90s | 3 倍 heartbeat_interval，防止网络抖动误判 |
| 看门狗巡检间隔 | 5s | 注册中心巡检线程的扫描间隔 |

---

### 服务发现 (Discovery)

#### 全量发现

```c
sr_instance_t *instances = NULL;
int count = 0;

// 发现所有实例（含 UP 和 DOWN）
sr_discover(registry, "user-service", &instances, &count);

// 仅发现健康实例
sr_discover_healthy(registry, "user-service", &instances, &count);

for (int i = 0; i < count; i++) {
    printf("  %s:%u weight=%d status=%d\n",
           instances[i].host, instances[i].port,
           instances[i].weight, instances[i].status);
}
```

#### 负载均衡查找

```c
typedef enum {
    SR_LB_ROUND_ROBIN = 0,  // 轮询
    SR_LB_RANDOM      = 1,  // 随机
    SR_LB_WEIGHTED    = 2   // 加权
} sr_lb_strategy_t;

sr_instance_t inst;
sr_lookup_one(registry, "user-service", SR_LB_WEIGHTED, &inst);
```

| 策略 | 算法 | 适用场景 |
|------|------|----------|
| Round Robin | 顺序循环选择 | 实例性能均匀 |
| Random | 随机选择 | 简单负载分摊 |
| Weighted | 按 weight 概率选择 | 实例性能异构 |

**加权算法示意：**
```
实例 A: weight=100  ─┐
实例 B: weight=50   ─┤  总权重 = 200
实例 C: weight=50   ─┘
→ A 选中概率 50%, B 25%, C 25%
```

---

### 健康检查

#### 主动健康检查

```c
sr_health_check_instance(registry, "user-service", instance_id);

sr_set_health_callback(registry, on_status_change, NULL);
```

健康检查回调：

```c
void on_status_change(const sr_instance_t *inst,
                      sr_instance_status_t new_status,
                      void *user_data) {
    printf("Instance %s/%s status: %d -> %d\n",
           inst->service_name, inst->instance_id,
           inst->status, new_status);

    if (new_status == SR_STATUS_DOWN) {
        send_alert("Service %s instance DOWN: %s:%u",
                   inst->service_name, inst->host, inst->port);
    }
}
```

#### 状态流转

```
         register
         ┌───────┐
         v       │
     SR_STATUS_UP ──────────┐
         │                  │ heartbeat 超时 / 主动 down
         │ heartbeat         v
         │              SR_STATUS_DOWN ────┐
         │                  │              │ 重新上线
         │                  │ heartbeat    │
         │                  v              │
         │              SR_STATUS_UP <─────┘
         │
         └─── deregister ──> [ 从注册表移除 ]
```

---

### 服务管理操作

```c
// 手动设置状态
sr_set_status(registry, "user-service", instance_id, SR_STATUS_DOWN);

// 注销实例
sr_deregister(registry, "user-service", instance_id);

// 查询所有服务名
char names[256][SR_MAX_NAME_LEN];
int count = 256;
sr_service_list(registry, names, &count);

// 服务总数
int total = sr_service_count(registry);

// 控制看门狗
sr_ttl_watchdog_stop(registry);
sr_ttl_watchdog_start(registry);
```

---

## 综合使用场景

### 场景：微服务启动完整流程

```
1. Config Center     → 拉取当前环境配置 (namespace = production)
                      → 订阅配置变更 (subscribe)
                      → 应用配置初始化连接池 / 缓存

2. Service Registry  → register("user-service", host:port, weight)
                      → 启动心跳线程

3. Distributed Cache → 创建缓存实例 (LRU + stampede_protection)
                      → 绑定后端读取回调 (MySQL / Redis)

4. 运行时
   - Config 热更新    → on_config_change → 动态调整参数
   - Cache 击穿保护   → 热点 key 异步刷新
   - Heartbeat 续期   → 每 30s 心跳

5. 关闭
   - deregister       → 从注册中心移除
   - cache_flush      → 写回脏数据到后端
   - unsubscribe      → 清理订阅
```

### 场景：灰度发布完整流程

```
1. 管理员在配置中心设置灰度规则
   - gray_release_set(IP="10.0.1.5", value="pool_size=100")

2. 灰度实例 10.0.1.5
   - poll_update 检测到变更
   - gray_release_get 返回新值
   - 回调中应用 pool_size=100

3. 非灰度实例
   - gray_release_get 返回旧值
   - 继续使用 pool_size=50

4. 观察灰度实例运行正常

5. 全量发布
   - config_put("pool_size=100")
   - 取消灰度规则 (CC_GRAY_ALL)
   - 所有实例收到变更通知
```

---

## 容量与限制

### 分布式缓存

| 指标 | 限制 |
|------|------|
| Key 最大长度 | 128 字节 (`DC_MAX_KEY_LEN`) |
| Value 最大长度 | 4 MB (`DC_MAX_VALUE_LEN`) |
| 默认 TTL | 300 秒 (`DC_DEFAULT_TTL_SEC`) |
| 最大条目数 | 由 `max_entries` 和 `max_memory_bytes` 控制 |

### 配置中心

| 指标 | 限制 |
|------|------|
| Key 最大长度 | 128 字节 (`CC_MAX_KEY_LEN`) |
| Value 最大长度 | 64 KB (`CC_MAX_VALUE_LEN`) |
| Namespace 最大长度 | 64 字节 (`CC_MAX_NAMESPACE_LEN`) |
| Group 最大长度 | 64 字节 (`CC_MAX_GROUP_LEN`) |
| 最大版本数 | 100 (`CC_MAX_VERSIONS`) |

### 服务注册

| 指标 | 限制 |
|------|------|
| 服务名最大长度 | 64 字节 (`SR_MAX_NAME_LEN`) |
| Host 最大长度 | 128 字节 (`SR_MAX_HOST_LEN`) |
| URL 最大长度 | 256 字节 (`SR_MAX_URL_LEN`) |
| 元数据最大长度 | 1024 字节 (`SR_MAX_META_LEN`) |
| 最大实例数 | 4096 (`SR_MAX_INSTANCES`) |
| 默认心跳间隔 | 30 秒 (`SR_DEFAULT_HEARTBEAT`) |
| 默认 TTL | 90 秒 (`SR_DEFAULT_TTL`) |

---

## API 快速参考

### 分布式缓存

```
dc_cache_create(&config)
    ├── dc_cache_put(cache, key, value, len, ttl)
    ├── dc_cache_get(cache, key, &value, &len)
    ├── dc_cache_delete(cache, key)
    ├── dc_cache_exists(cache, key)
    ├── dc_cache_expire(cache, key, ttl)
    ├── dc_cache_ttl(cache, key)
    ├── dc_cache_invalidate(cache, key, mode)
    ├── dc_cache_batch_get(cache, keys, count, values, lens)
    ├── dc_cache_batch_put(cache, keys, values, lens, ttls, count)
    ├── dc_cache_flush(cache)
    ├── dc_cache_size(cache)
    ├── dc_cache_memory_used(cache)
    ├── dc_cache_hit_count(cache)
    ├── dc_cache_miss_count(cache)
    ├── dc_cache_hit_rate(cache)
    └── dc_cache_compact(cache)
dc_cache_destroy(cache)
```

### 配置中心

```
cc_center_create()
    ├── cc_config_put(center, ns, group, key, value)
    ├── cc_config_get(center, ns, group, key, &entry)
    ├── cc_config_delete(center, ns, group, key)
    ├── cc_config_put_encrypted(center, ns, group, key, value, enc_key)
    ├── cc_config_get_decrypted(center, ns, group, key, out, size, enc_key)
    ├── cc_config_get_version(center, ns, group, key, ver, &entry)
    ├── cc_config_list_versions(center, ns, group, key, vers, &count)
    ├── cc_config_rollback(center, ns, group, key, ver)
    ├── cc_gray_release_set(center, ns, group, key, &gray)
    ├── cc_gray_release_get(center, ns, group, key, ip, &entry)
    ├── cc_subscribe(center, ns, cb, user_data)
    ├── cc_unsubscribe(center, ns)
    ├── cc_poll_update(center, ns, group)
    ├── cc_poll_all(center)
    ├── cc_namespace_list(center, namespaces, &count)
    ├── cc_group_list(center, ns, groups, &count)
    └── cc_key_list(center, ns, group, keys, &count)
cc_center_destroy(center)
```

### 服务注册

```
sr_registry_create()
    ├── sr_register(registry, &instance)
    ├── sr_deregister(registry, service_name, instance_id)
    ├── sr_heartbeat(registry, service_name, instance_id)
    ├── sr_set_status(registry, service_name, instance_id, status)
    ├── sr_discover(registry, service_name, &instances, &count)
    ├── sr_discover_healthy(registry, service_name, &instances, &count)
    ├── sr_lookup_one(registry, service_name, strategy, &instance)
    ├── sr_health_check(registry)
    ├── sr_health_check_instance(registry, service_name, instance_id)
    ├── sr_set_health_callback(registry, cb, user_data)
    ├── sr_service_list(registry, names, &count)
    ├── sr_service_count(registry)
    ├── sr_ttl_watchdog_start(registry)
    └── sr_ttl_watchdog_stop(registry)
sr_registry_destroy(registry)
```

---

## 相关资源

- 头文件：`include/distributed_cache.h`, `include/config_center.h`, `include/service_registry.h`
- 源文件：`src/distributed_cache.c`, `src/config_center.c`, `src/service_registry.c`
- 示例：`examples/`
- 主文档：`docs/`
- 根文档：`../README.md`
