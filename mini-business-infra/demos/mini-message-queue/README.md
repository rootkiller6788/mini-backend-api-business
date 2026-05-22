# mini-message-queue — 消息队列深度剖析

## 概述

`mini-message-queue` 是 `mini-business-infra` 中消息队列模块的深度指南。该模块实现了一个基于 C99 标准库的内存内消息代理（message broker），核心抽象为 AMQP 0-9-1 模型的子集，涵盖交换器类型、队列属性、消息生命周期、消费者确认以及死信队列处理。

## 核心架构

```
+----------+     publish      +-------------+     route      +----------+
| Producer | ---------------> |  Exchange   | -------------> |  Queue   |
+----------+                  +-------------+                +----------+
                                   |                               |
                          direct / topic / fanout           consume | push
                                                             +----------+
                                                             | Consumer |
                                                             +----------+
                                                                  |
                                                            ack / nack / requeue
```

### 组件职责

| 组件 | 头文件定义 | 说明 |
|------|-----------|------|
| `mq_broker_t` | `message_queue.h` | 消息代理主实例，管理所有交换器、队列和消费者 |
| `mq_channel_t` | `message_queue.h` | 虚拟连接通道，承载收发操作和确认语义 |
| `mq_message_t` | `message_queue.h` | 消息体（body + 元数据），最大 payload 64K |
| `mq_exchange_declare_t` | `message_queue.h` | 交换器定义（名称、类型、持久化、自动删除） |
| `mq_queue_declare_t` | `message_queue.h` | 队列定义（持久化、TTL、最大长度、DLX 路由） |

---

## 交换器类型

`mini-message-queue` 支持三种交换器类型（`mq_exchange_type_t` 枚举），与 RabbitMQ 语义一致。

### 1. Direct Exchange (`MQ_EXCHANGE_DIRECT`)

**基本规则：** 消息路由到 `routing_key` 与队列绑定的 `routing_key` **完全相等** 的队列。

**绑定示例：**
```
Exchange: "order_exchange" (direct)
  Queue "order.created"  <-- routing_key: "order.created"
  Queue "order.paid"     <-- routing_key: "order.paid"
  Queue "order.shipped"  <-- routing_key: "order.shipped"
```

**路由行为：**
| 发布 routing_key | 路由到 |
|-------------------|--------|
| `"order.created"` | `order.created` |
| `"order.paid"`    | `order.paid` |
| `"order.unknown"` | (无队列，消息丢弃) |

**适用场景：**
- 精确路由：不同业务事件发往不同消费者
- RPC 调用：请求-响应模式下的单向路由
- 工作队列：多消费者竞争消费同一队列

```c
mq_exchange_declare_t exch = {
    .exchange_name = "order_exchange",
    .type          = MQ_EXCHANGE_DIRECT,
    .durable       = 1,
    .auto_delete   = 0
};
mq_exchange_declare(ch, &exch);

mq_queue_bind(ch, "order.created", "order_exchange", "order.created");
mq_queue_bind(ch, "order.paid",    "order_exchange", "order.paid");
```

---

### 2. Topic Exchange (`MQ_EXCHANGE_TOPIC`)

**基本规则：** 消息路由到绑定的 `routing_key` **模式匹配** 发布 `routing_key` 的队列。`routing_key` 由点号 `.` 分隔的单词组成。

**通配符：**
| 符号 | 含义 | 示例 |
|------|------|------|
| `*` | 匹配恰好一个单词 | `order.*.paid` 匹配 `order.new.paid` |
| `#` | 匹配零或多个单词 | `order.#` 匹配 `order.new.paid.done` |

**绑定示例：**
```
Exchange: "event_bus" (topic)
  Queue "logs.all"      <-- routing_key: "#"
  Queue "logs.error"    <-- routing_key: "*.error"
  Queue "logs.order"    <-- routing_key: "order.#"
  Queue "logs.specific" <-- routing_key: "order.*.paid"
```

**路由测试矩阵：**
| 发布 routing_key | `#` | `*.error` | `order.#` | `order.*.paid` |
|-------------------|-----|-----------|-----------|-----------------|
| `"order.new.paid"` | ✓ | | ✓ | ✓ |
| `"payment.error"`  | ✓ | ✓ | | |
| `"order.error"`    | ✓ | ✓ | ✓ | |
| `"sys.info"`       | ✓ | | | |

**适用场景：**
- 事件总线：灵活的多消费者订阅体系
- 日志聚合：按级别/模块分发日志
- 领域事件：跨微服务的事件路由

```c
mq_exchange_declare_t exch = {
    .exchange_name = "event_bus",
    .type          = MQ_EXCHANGE_TOPIC,
    .durable       = 1,
    .auto_delete   = 0
};
mq_exchange_declare(ch, &exch);

mq_queue_bind(ch, "logs.error", "event_bus", "*.error");
mq_queue_bind(ch, "logs.order", "event_bus", "order.#");
```

---

### 3. Fanout Exchange (`MQ_EXCHANGE_FANOUT`)

**基本规则：** 消息**广播**到所有绑定的队列，**忽略 `routing_key`**。

**绑定示例：**
```
Exchange: "broadcast" (fanout)
  Queue "service_a"  <-- routing_key: (忽略)
  Queue "service_b"  <-- routing_key: (忽略)
  Queue "service_c"  <-- routing_key: (忽略)
```

**路由行为：** 任意消息发布到 `"broadcast"` 交换器后，三个队列全部收到相同副本。

**适用场景：**
- 配置广播：所有服务节点接收同一份配置变更
- 缓存失效：所有缓存节点失效同一个 key
- 通知推送：实时通知广播到所有在线用户

```c
mq_exchange_declare_t exch = {
    .exchange_name = "broadcast",
    .type          = MQ_EXCHANGE_FANOUT,
    .durable       = 0,
    .auto_delete   = 1
};
mq_exchange_declare(ch, &exch);

mq_queue_bind(ch, "service_a", "broadcast", "");
mq_queue_bind(ch, "service_b", "broadcast", "");
```

---

### 交换器对比总结

| 特性 | Direct | Topic | Fanout |
|------|--------|-------|--------|
| 路由依据 | routing_key 精确匹配 | routing_key 模式匹配 | 无条件广播 |
| routing_key 有效性 | 必需 | 必需（支持通配） | 忽略 |
| 路由灵活性 | 低 | 高 | 无 |
| 性能开销 | 低 | 中（模式解析） | 低 |
| 典型场景 | 工作队列 / RPC | 事件总线 | 广播 / 通知 |

---

## 队列属性

### 1. 持久化 (Durable)

```c
typedef struct {
    int durable;  // 1 = 持久化, 0 = 非持久化
    // ...
} mq_queue_declare_t;
```

| `durable` | 行为 |
|-----------|------|
| `1` | 代理重启后队列和消息不丢失 |
| `0` | 代理重启后队列消失，消息丢失 |

**注意：** 本实现中 `durable` 作用于内存数据结构的生命周期。在持久化后端扩展中，`durable=1` 的队列会写入磁盘日志。

---

### 2. 消息 TTL (Time-To-Live)

队列级 TTL 和消息级 TTL 协同工作。实际 TTL 取两者最小值：

```c
// 队列级 TTL — 队列中消息的统一过期时间
typedef struct {
    int32_t message_ttl_seconds;  // 0 = 不限
} mq_queue_declare_t;

// 消息级 TTL — 单条消息的过期时间
typedef struct {
    int32_t ttl_seconds;  // 0 = 不限（使用队列 TTL）
} mq_message_t;
```

**TTL 过期处理流程：**
```
消息入队 → 标记 TTL = min(msg.ttl, queue.ttl)
        ↓
    代理定期扫描
        ↓
   TTL 到期 → 若配置了 DLX，转发到死信队列
           → 若未配置 DLX，消息被丢弃
```

---

### 3. 死信交换器 (Dead Letter Exchange, DLX)

当消息无法正常消费时，可将其路由到死信交换器（DLX）绑定的死信队列（DLQ），实现**消息不丢失 + 异常追踪**。

**死信产生条件：**
1. 消息 TTL 过期
2. 队列长度达到上限 (`max_length`)
3. 消费者 NACK 且不 requeue（`MQ_ACK_NACK`）
4. 消费重试次数耗尽（`max_retries` 耗尽）

```c
mq_queue_declare_t decl = {
    .queue_name              = "order.processing",
    .durable                 = 1,
    .message_ttl_seconds     = 3600,
    .max_length              = 10000,
    .dead_letter_exchange    = "dlx_exchange",       // DLX 名称
    .dead_letter_routing_key = "order.dead"          // 进入 DLQ 的 routing key
};
mq_queue_declare(ch, &decl);

mq_queue_declare_t dlq = {
    .queue_name = "order.dead_letter_queue",
    .durable    = 1
};
mq_queue_declare(ch, &dlq);
mq_queue_bind(ch, "order.dead_letter_queue", "dlx_exchange", "order.dead");
```

**死信处理流程：**
```
1. 原始消息在 "order.processing" 中被标记为死信
2. 代理自动将消息发布到 "dlx_exchange"
3. 通过 routing_key "order.dead" 路由到 "order.dead_letter_queue"
4. 死信消费者读取、分析失败原因、记录日志、决定是否重试
```

---

### 4. 队列长度限制

```c
typedef struct {
    int32_t max_length;  // 0 = 不限
} mq_queue_declare_t;
```

当队列消息数达到 `max_length`：
- 最早的消息被**丢弃或转发到 DLX**（取决于 DLX 是否配置）
- 从队列头部移除消息（FIFO 溢出策略）

---

## 消息生命周期

一条消息从产生到死亡的完整状态机：

```
     [ Producer 创建 mq_message_t ]
                    |
                    v
     [ mq_basic_publish() ] --- exchange 为 ""? ---> 直接路由到队列
                    |
          exchange 非空
                    |
                    v
     [ 交换器路由 (direct / topic / fanout) ]
                    |
                    v
     +---------- [ 队列缓冲 ] <----------+
     |              |                     |
     |      TTL 定时扫描                   |
     |              |                     |
     |     +-- TTL 到期? --+              |
     |     |                |             |
     |    yes              no             |
     |     |                |             |
     |  [ DLX 路由 ]   [ 等待消费 ]        |
     |     |                |             |
     |     v                v             |
     | [ 死信队列 ]    [ mq_consume_callback ]     |
     |                    |                     |
     |              +--- ack? ---+              |
     |              |             |              |
     |           MQ_ACK_SUCCESS  MQ_ACK_NACK     |
     |              |             |              |
     |         [ 消息删除 ]    +-- requeue? --+  |
     |                        |               |  |
     |                       yes             no  |
     |                        |               |  |
     |                   [ 重新入队 ]    [ DLX 路由 ]  (如果 retry_count < max_retries)
     |                        |               |
     +------------------------+               |
                                              v
                                        [ 死信队列 ]
```

### 消息结构详解

```c
typedef struct {
    char     exchange_name[MQ_MAX_KEY_LEN];        // 来源交换器名称
    char     routing_key[MQ_MAX_ROUTING_KEY_LEN];  // 发布时的 routing key
    uint8_t *body;                                  // 消息体 (heap 分配)
    size_t   body_len;                              // body 字节数
    char     message_id[37];                        // UUID v4 (36 char + '\0')
    int64_t  timestamp_ms;                          // 发布时间戳
    int32_t  ttl_seconds;                           // 单条消息 TTL
    int32_t  retry_count;                           // 已重试次数
    int32_t  max_retries;                           // 最大重试次数 (默认 MQ_MAX_RETRY_COUNT=10)
    int32_t  backoff_ms;                            // 重试退避时间 (默认 MQ_DEFAULT_BACKOFF_MS=100)
} mq_message_t;
```

**关键字段说明：**

- **`message_id`**：全局唯一标识符（UUID v4 格式），用于发布确认（publish confirm）和消费者 ack 的去重追溯。
- **`retry_count` vs `max_retries`**：消费者 `MQ_ACK_NACK` 后若请求 requeue，`retry_count++`。当 `retry_count >= max_retries` 时，消息不再重新入队，而是送入 DLX。
- **`backoff_ms`**：指数退避的基础毫秒数。重试延迟 = `backoff_ms * 2^(retry_count - 1)`。

---

## 消费者确认 (Ack/Nack/Requeue)

消费者通过 `mq_basic_ack()` 确认消息处理结果，三态语义：

```c
typedef enum {
    MQ_ACK_SUCCESS = 0,  // 消费成功，消息从队列中删除
    MQ_ACK_NACK    = 1,  // 消费失败，不删除消息
    MQ_ACK_REQUEUE = 2   // 消费失败，消息立即重新入队
} mq_ack_status_t;
```

### 三态行为详解

| 状态 | 消息去向 | retry_count | 适用场景 |
|------|----------|-------------|----------|
| `MQ_ACK_SUCCESS` | 永久删除 | N/A | 正常处理完成 |
| `MQ_ACK_NACK` | 留在队列 / DLX | +1 (若 requeue) | 临时错误期望稍后重试 |
| `MQ_ACK_REQUEUE` | 重新入队（队尾） | +1 | 需要重新排队处理 |

### 重试与死信联动

当 `retry_count >= max_retries` 时：
1. 消息标记为死信
2. 路由到 `dead_letter_exchange`（如果配置）
3. 无 DLX 配置则消息被丢弃

```c
int my_consumer_callback(mq_message_t *msg, void *user_data) {
    (void)user_data;

    if (process_message(msg) == 0) {
        return MQ_ACK_SUCCESS;  // 成功 → 消息删除
    }

    // 临时错误 → 重试
    if (msg->retry_count < msg->max_retries) {
        printf("Retry %d/%d for msg %s\n",
               msg->retry_count + 1, msg->max_retries, msg->message_id);
        return MQ_ACK_REQUEUE;
    }

    // 超过最大重试 → NACK（进入 DLX）
    printf("Message %s exhausted retries, routing to DLX\n", msg->message_id);
    return MQ_ACK_NACK;
}
```

### 发布确认 (Publisher Confirm)

生产者可开启确认模式，确保消息成功投递：

```c
mq_set_confirm_mode(ch, MQ_CONFIRM_ON);

void on_confirm(const char *message_id, int success, void *user_data) {
    if (success) {
        printf("Message %s confirmed by broker\n", message_id);
    } else {
        printf("Message %s failed to deliver\n", message_id);
    }
}
mq_set_confirm_callback(ch, on_confirm, NULL);
```

**确认语义：**
- `MQ_CONFIRM_ON`：每条消息发布后等待代理 ack
- `MQ_CONFIRM_OFF`：fire-and-forget 模式

---

## 死信队列处理

### 完整处理流程

```
+------------------+
| order.processing |  正常业务队列
+--------+---------+
         |
    [ 死信条件触发 ]
         |
         v
+--------+---------+
|  dlx_exchange    |  死信交换器 (通常为 direct 或 topic)
+--------+---------+
         |
    routing: "order.dead"
         |
         v
+--------+---------+
| order.dead_queue |  死信队列 (DLQ)
+--------+---------+
         |
    [ 死信消费者 ]
         |
    +----+----+
    |         |
  记录      人工
  日志      处理
```

### 死信消费者实现

```c
int dlq_consumer_callback(mq_message_t *msg, void *user_data) {
    FILE *log = (FILE *)user_data;

    // 记录死信详情
    fprintf(log, "DLQ: id=%s exchange=%s key=%s retry=%d/%d time=%lld body_len=%zu\n",
            msg->message_id,
            msg->exchange_name,
            msg->routing_key,
            msg->retry_count,
            msg->max_retries,
            (long long)msg->timestamp_ms,
            msg->body_len);

    // 死信处理策略：
    // 1. 持久化到数据库供人工排查
    // 2. 发送告警通知
    // 3. 定时批处理重新发布回原队列

    // 死信确认后删除
    return MQ_ACK_SUCCESS;
}
```

### 死信队列最佳实践

| 实践 | 说明 |
|------|------|
| 独立 DLQ | 每种业务队列配独立 DLQ，避免死信混杂 |
| 监控告警 | DLQ 堆积超过阈值应触发告警 |
| 保留时效 | DLQ 消息保留 7-30 天供回溯，超期归档 |
| 重放机制 | 提供死信消息批处理重放功能 |
| 元数据保留 | 保留原 exchange / routing_key / 重试次数 便于溯源 |

---

## API 快速参考

```
mq_broker_create()
    mq_broker_start(broker, addr, port)
        mq_broker_open_channel(broker)
            mq_exchange_declare(ch, decl)
            mq_queue_declare(ch, decl)
            mq_queue_bind(ch, queue, exchange, routing_key)
            mq_queue_unbind(ch, queue, exchange, routing_key)
            mq_basic_publish(ch, exchange, routing_key, msg)
            mq_basic_consume(ch, queue, callback, user_data)
            mq_basic_cancel(ch, queue)
            mq_basic_ack(ch, message_id, status)
            mq_set_confirm_mode(ch, mode)
            mq_set_confirm_callback(ch, cb, user_data)
            mq_queue_purge(ch, queue)
            mq_queue_delete(ch, queue)
            mq_exchange_delete(ch, exchange)
        mq_queue_message_count(broker, queue)
    mq_broker_stop(broker)
mq_broker_destroy(broker)

mq_message_init(msg)
mq_message_free(msg)
```

---

## 性能特征

| 指标 | 数值 |
|------|------|
| 最大队列数 | 256 (`MQ_MAX_QUEUES`) |
| 最大消费者数 | 1024 (`MQ_MAX_CONSUMERS`) |
| 消息体最大长度 | 64 KB (`MQ_MAX_BODY_LEN`) |
| 路由键最大长度 | 128 B (`MQ_MAX_ROUTING_KEY_LEN`) |
| 默认最大重试 | 10 次 (`MQ_MAX_RETRY_COUNT`) |
| 默认退避时间 | 100 ms (`MQ_DEFAULT_BACKOFF_MS`) |
| 实现语言 | C99，无外部依赖 |
| 内存模型 | 全内存代理，无持久化后端（durable 控制数据结构生命周期） |

---

## 相关资源

- 头文件：`include/message_queue.h`
- 源文件：`src/message_queue.c`
- 示例：`examples/`
- 主文档：`docs/`
- 根文档：`../README.md`
