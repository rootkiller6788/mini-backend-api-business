#include "message_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void mq_gen_uuid(char buf[37]) {
    static int seeded = 0;
    if (!seeded) { srand((unsigned)time(NULL)); seeded = 1; }
    snprintf(buf, 37, "%08x-%04x-%04x-%04x-%04x%08x",
             rand(), rand() & 0xffff, (rand() & 0x0fff) | 0x4000,
             (rand() & 0x3fff) | 0x8000, rand() & 0xffff, rand());
}

typedef struct mq_queue {
    char    name[MQ_MAX_KEY_LEN];
    int     durable;
    int32_t message_ttl;
    int32_t max_length;
    char    dlx[MQ_MAX_KEY_LEN];
    char    dlx_rk[MQ_MAX_ROUTING_KEY_LEN];
    mq_message_t **messages;
    int     count;
    int     cap;
    mq_consume_callback consumer_cb;
    void   *consumer_data;
} mq_queue_t;

typedef struct mq_binding {
    char    exchange[MQ_MAX_KEY_LEN];
    char    queue[MQ_MAX_KEY_LEN];
    char    routing_key[MQ_MAX_ROUTING_KEY_LEN];
} mq_binding_t;

typedef struct mq_exchange {
    char               name[MQ_MAX_KEY_LEN];
    mq_exchange_type_t type;
    int                durable;
    int                auto_delete;
} mq_exchange_t;

struct mq_channel {
    mq_broker_t        *broker;
    mq_confirm_mode_t   confirm_mode;
    mq_confirm_callback confirm_cb;
    void               *confirm_user_data;
};

struct mq_broker {
    mq_exchange_t  exchanges[MQ_MAX_QUEUES];
    int            exchange_count;
    mq_queue_t     queues[MQ_MAX_QUEUES];
    int            queue_count;
    mq_binding_t   bindings[MQ_MAX_QUEUES * 4];
    int            binding_count;
    int            running;
};

mq_broker_t *mq_broker_create(void) {
    mq_broker_t *b = (mq_broker_t *)calloc(1, sizeof(mq_broker_t));
    if (!b) return NULL;
    mq_exchange_declare_t de = {0};
    strcpy(de.exchange_name, "amq.default");
    de.type = MQ_EXCHANGE_DIRECT;
    de.durable = 1;
    de.auto_delete = 0;
    b->exchanges[b->exchange_count] = (mq_exchange_t){{0}};
    strcpy(b->exchanges[b->exchange_count].name, de.exchange_name);
    b->exchanges[b->exchange_count].type = de.type;
    b->exchanges[b->exchange_count].durable = de.durable;
    b->exchange_count++;
    return b;
}

void mq_broker_destroy(mq_broker_t *broker) {
    if (!broker) return;
    for (int i = 0; i < broker->queue_count; i++) {
        for (int j = 0; j < broker->queues[i].count; j++) {
            mq_message_free(broker->queues[i].messages[j]);
            free(broker->queues[i].messages[j]);
        }
        free(broker->queues[i].messages);
    }
    free(broker);
}

int mq_broker_start(mq_broker_t *broker, const char *bind_addr, uint16_t port) {
    (void)bind_addr; (void)port;
    if (!broker) return -1;
    broker->running = 1;
    return 0;
}

void mq_broker_stop(mq_broker_t *broker) {
    if (broker) broker->running = 0;
}

mq_channel_t *mq_broker_open_channel(mq_broker_t *broker) {
    if (!broker) return NULL;
    mq_channel_t *ch = (mq_channel_t *)calloc(1, sizeof(mq_channel_t));
    if (!ch) return NULL;
    ch->broker = broker;
    return ch;
}

int mq_exchange_declare(mq_channel_t *ch, const mq_exchange_declare_t *decl) {
    if (!ch || !decl) return -1;
    mq_broker_t *b = ch->broker;
    for (int i = 0; i < b->exchange_count; i++) {
        if (strcmp(b->exchanges[i].name, decl->exchange_name) == 0) return 0;
    }
    if (b->exchange_count >= MQ_MAX_QUEUES) return -1;
    mq_exchange_t *e = &b->exchanges[b->exchange_count];
    strcpy(e->name, decl->exchange_name);
    e->type = decl->type;
    e->durable = decl->durable;
    e->auto_delete = decl->auto_delete;
    b->exchange_count++;
    return 0;
}

int mq_queue_declare(mq_channel_t *ch, const mq_queue_declare_t *decl) {
    if (!ch || !decl) return -1;
    mq_broker_t *b = ch->broker;
    for (int i = 0; i < b->queue_count; i++) {
        if (strcmp(b->queues[i].name, decl->queue_name) == 0) return 0;
    }
    if (b->queue_count >= MQ_MAX_QUEUES) return -1;
    mq_queue_t *q = &b->queues[b->queue_count];
    strcpy(q->name, decl->queue_name);
    q->durable = decl->durable;
    q->message_ttl = decl->message_ttl_seconds;
    q->max_length = decl->max_length;
    strcpy(q->dlx, decl->dead_letter_exchange);
    strcpy(q->dlx_rk, decl->dead_letter_routing_key);
    q->cap = 64;
    q->messages = (mq_message_t **)calloc((size_t)q->cap, sizeof(mq_message_t *));
    b->queue_count++;
    return 0;
}

int mq_queue_bind(mq_channel_t *ch, const char *queue, const char *exchange, const char *routing_key) {
    if (!ch || !queue || !exchange) return -1;
    mq_broker_t *b = ch->broker;
    if (b->binding_count >= MQ_MAX_QUEUES * 4) return -1;
    mq_binding_t *bd = &b->bindings[b->binding_count];
    strcpy(bd->exchange, exchange);
    strcpy(bd->queue, queue);
    strcpy(bd->routing_key, routing_key ? routing_key : "");
    b->binding_count++;
    return 0;
}

int mq_queue_unbind(mq_channel_t *ch, const char *queue, const char *exchange, const char *routing_key) {
    if (!ch || !queue || !exchange) return -1;
    mq_broker_t *b = ch->broker;
    for (int i = 0; i < b->binding_count; i++) {
        if (strcmp(b->bindings[i].queue, queue) == 0 &&
            strcmp(b->bindings[i].exchange, exchange) == 0 &&
            (routing_key == NULL || strcmp(b->bindings[i].routing_key, routing_key) == 0)) {
            for (int j = i; j < b->binding_count - 1; j++) b->bindings[j] = b->bindings[j + 1];
            b->binding_count--;
            return 0;
        }
    }
    return -1;
}

int mq_basic_publish(mq_channel_t *ch, const char *exchange, const char *routing_key,
                     mq_message_t *msg) {
    if (!ch || !exchange || !msg) return -1;
    mq_broker_t *b = ch->broker;
    int delivered = 0;
    for (int i = 0; i < b->binding_count; i++) {
        if (strcmp(b->bindings[i].exchange, exchange) != 0) continue;
        if (routing_key && strcmp(b->bindings[i].routing_key, routing_key) != 0) {
            mq_exchange_t *ex = NULL;
            for (int j = 0; j < b->exchange_count; j++) {
                if (strcmp(b->exchanges[j].name, exchange) == 0) { ex = &b->exchanges[j]; break; }
            }
            if (!ex || ex->type == MQ_EXCHANGE_DIRECT) continue;
            if (ex->type == MQ_EXCHANGE_FANOUT) { /* fanout ignores routing key */ }
        }
        for (int j = 0; j < b->queue_count; j++) {
            if (strcmp(b->queues[j].name, b->bindings[i].queue) == 0) {
                if (b->queues[j].max_length > 0 && b->queues[j].count >= b->queues[j].max_length) {
                    if (strlen(b->queues[j].dlx) > 0) {
                        mq_message_t *dl_msg = (mq_message_t *)malloc(sizeof(mq_message_t));
                        memcpy(dl_msg, msg, sizeof(mq_message_t));
                        dl_msg->body = (uint8_t *)malloc(msg->body_len);
                        memcpy(dl_msg->body, msg->body, msg->body_len);
                        mq_basic_publish(ch, b->queues[j].dlx, b->queues[j].dlx_rk, dl_msg);
                    }
                    continue;
                }
                mq_queue_t *q = &b->queues[j];
                if (q->count >= q->cap) { q->cap *= 2; q->messages = (mq_message_t **)realloc(q->messages, (size_t)q->cap * sizeof(mq_message_t *)); }
                q->messages[q->count] = (mq_message_t *)malloc(sizeof(mq_message_t));
                memcpy(q->messages[q->count], msg, sizeof(mq_message_t));
                q->messages[q->count]->body = (uint8_t *)malloc(msg->body_len);
                memcpy(q->messages[q->count]->body, msg->body, msg->body_len);
                q->count++;
                delivered++;
            }
        }
    }
    if (delivered > 0 && ch->confirm_cb) {
        ch->confirm_cb(msg->message_id, 1, ch->confirm_user_data);
    }
    return delivered > 0 ? 0 : -1;
}

int mq_basic_consume(mq_channel_t *ch, const char *queue, mq_consume_callback cb, void *user_data) {
    if (!ch || !queue || !cb) return -1;
    mq_broker_t *b = ch->broker;
    for (int i = 0; i < b->queue_count; i++) {
        if (strcmp(b->queues[i].name, queue) == 0) {
            b->queues[i].consumer_cb = cb;
            b->queues[i].consumer_data = user_data;
            for (int j = 0; j < b->queues[i].count; j++) {
                mq_message_t *msg = b->queues[i].messages[j];
                int64_t now_ms = (int64_t)(time(NULL) * 1000);
                if (msg->ttl_seconds > 0 && now_ms - msg->timestamp_ms > (int64_t)msg->ttl_seconds * 1000) {
                    if (strlen(b->queues[i].dlx) > 0) {
                        mq_basic_publish(ch, b->queues[i].dlx, b->queues[i].dlx_rk, msg);
                    }
                    mq_message_free(msg);
                    free(msg);
                    b->queues[i].messages[j] = NULL;
                    continue;
                }
                int ack = cb(msg, b->queues[i].consumer_data);
                if (ack == MQ_ACK_SUCCESS) {
                    mq_message_free(msg);
                    free(msg);
                    b->queues[i].messages[j] = NULL;
                } else if (ack == MQ_ACK_NACK) {
                    if (msg->retry_count < msg->max_retries) {
                        msg->retry_count++;
                        msg->backoff_ms *= 2;
                    } else if (strlen(b->queues[i].dlx) > 0) {
                        mq_basic_publish(ch, b->queues[i].dlx, b->queues[i].dlx_rk, msg);
                    }
                    mq_message_free(msg);
                    free(msg);
                    b->queues[i].messages[j] = NULL;
                }
            }
            int w = 0;
            for (int j = 0; j < b->queues[i].count; j++) {
                if (b->queues[i].messages[j] != NULL) b->queues[i].messages[w++] = b->queues[i].messages[j];
            }
            b->queues[i].count = w;
            return 0;
        }
    }
    return -1;
}

int mq_basic_cancel(mq_channel_t *ch, const char *queue) {
    if (!ch || !queue) return -1;
    mq_broker_t *b = ch->broker;
    for (int i = 0; i < b->queue_count; i++) {
        if (strcmp(b->queues[i].name, queue) == 0) {
            b->queues[i].consumer_cb = NULL;
            b->queues[i].consumer_data = NULL;
            return 0;
        }
    }
    return -1;
}

int mq_basic_ack(mq_channel_t *ch, const char *message_id, mq_ack_status_t status) {
    (void)ch; (void)message_id; (void)status;
    return 0;
}

int mq_set_confirm_mode(mq_channel_t *ch, mq_confirm_mode_t mode) {
    if (!ch) return -1;
    ch->confirm_mode = mode;
    return 0;
}

int mq_set_confirm_callback(mq_channel_t *ch, mq_confirm_callback cb, void *user_data) {
    if (!ch) return -1;
    ch->confirm_cb = cb;
    ch->confirm_user_data = user_data;
    return 0;
}

int mq_queue_purge(mq_channel_t *ch, const char *queue) {
    if (!ch || !queue) return -1;
    mq_broker_t *b = ch->broker;
    for (int i = 0; i < b->queue_count; i++) {
        if (strcmp(b->queues[i].name, queue) == 0) {
            for (int j = 0; j < b->queues[i].count; j++) {
                mq_message_free(b->queues[i].messages[j]);
                free(b->queues[i].messages[j]);
            }
            b->queues[i].count = 0;
            return 0;
        }
    }
    return -1;
}

int mq_queue_delete(mq_channel_t *ch, const char *queue) {
    if (!ch || !queue) return -1;
    mq_broker_t *b = ch->broker;
    for (int i = 0; i < b->queue_count; i++) {
        if (strcmp(b->queues[i].name, queue) == 0) {
            for (int j = 0; j < b->queues[i].count; j++) {
                mq_message_free(b->queues[i].messages[j]);
                free(b->queues[i].messages[j]);
            }
            free(b->queues[i].messages);
            for (int j = i; j < b->queue_count - 1; j++) b->queues[j] = b->queues[j + 1];
            b->queue_count--;
            return 0;
        }
    }
    return -1;
}

int mq_exchange_delete(mq_channel_t *ch, const char *exchange) {
    if (!ch || !exchange) return -1;
    mq_broker_t *b = ch->broker;
    for (int i = 0; i < b->exchange_count; i++) {
        if (strcmp(b->exchanges[i].name, exchange) == 0) {
            for (int j = i; j < b->exchange_count - 1; j++) b->exchanges[j] = b->exchanges[j + 1];
            b->exchange_count--;
            return 0;
        }
    }
    return -1;
}

int64_t mq_queue_message_count(mq_broker_t *broker, const char *queue) {
    if (!broker || !queue) return -1;
    for (int i = 0; i < broker->queue_count; i++) {
        if (strcmp(broker->queues[i].name, queue) == 0) return broker->queues[i].count;
    }
    return -1;
}

void mq_message_init(mq_message_t *msg) {
    if (!msg) return;
    memset(msg, 0, sizeof(mq_message_t));
    msg->timestamp_ms = (int64_t)(time(NULL) * 1000);
    mq_gen_uuid(msg->message_id);
    msg->max_retries = MQ_MAX_RETRY_COUNT;
    msg->backoff_ms = MQ_DEFAULT_BACKOFF_MS;
}

void mq_message_free(mq_message_t *msg) {
    if (!msg) return;
    free(msg->body);
    msg->body = NULL;
}

/* === L5: Priority Queue (Binary Heap) for Message Ordering ===
 *
 * Implements a min-heap prioritized by an int32_t priority field.
 * Supports O(log n) push/pop with level-order array storage.
 *
 * Theorem (L4): A binary heap of n elements has height floor(log2 n).
 * Heapify operation operates in O(n) time for building the heap
 * from an unordered array (Floyd, 1964).
 *
 * Reference: Williams, "Algorithm 232: Heapsort" (CACM 1964).
 */

typedef struct {
    int32_t  priority;
    int      msg_index;  /* index into queue's messages array */
} mq_prio_entry_t;

typedef struct {
    mq_prio_entry_t *heap;
    int              size;
    int              capacity;
} mq_priority_queue_t;

static mq_priority_queue_t *mq_pq_create(int capacity) {
    mq_priority_queue_t *pq = (mq_priority_queue_t *)calloc(1, sizeof(*pq));
    if (!pq) return NULL;
    pq->capacity = capacity > 0 ? capacity : 64;
    pq->heap = (mq_prio_entry_t *)calloc((size_t)pq->capacity, sizeof(mq_prio_entry_t));
    if (!pq->heap) { free(pq); return NULL; }
    return pq;
}

static void mq_pq_destroy(mq_priority_queue_t *pq) {
    if (!pq) return;
    free(pq->heap);
    free(pq);
}

/* sift-up: bubble element at idx up to correct position */
static void mq_pq_sift_up(mq_priority_queue_t *pq, int idx) {
    while (idx > 0) {
        int parent = (idx - 1) / 2;
        if (pq->heap[idx].priority >= pq->heap[parent].priority) break;
        /* swap */
        mq_prio_entry_t tmp = pq->heap[idx];
        pq->heap[idx] = pq->heap[parent];
        pq->heap[parent] = tmp;
        idx = parent;
    }
}

/* sift-down: bubble element at idx down to correct position */
static void mq_pq_sift_down(mq_priority_queue_t *pq, int idx) {
    while (1) {
        int left = 2 * idx + 1;
        int right = 2 * idx + 2;
        int smallest = idx;
        if (left < pq->size && pq->heap[left].priority < pq->heap[smallest].priority)
            smallest = left;
        if (right < pq->size && pq->heap[right].priority < pq->heap[smallest].priority)
            smallest = right;
        if (smallest == idx) break;
        mq_prio_entry_t tmp = pq->heap[idx];
        pq->heap[idx] = pq->heap[smallest];
        pq->heap[smallest] = tmp;
        idx = smallest;
    }
}

static int mq_pq_push(mq_priority_queue_t *pq, int32_t priority, int msg_index) {
    if (!pq) return -1;
    if (pq->size >= pq->capacity) {
        pq->capacity *= 2;
        mq_prio_entry_t *new_heap = (mq_prio_entry_t *)realloc(
            pq->heap, (size_t)pq->capacity * sizeof(mq_prio_entry_t));
        if (!new_heap) return -1;
        pq->heap = new_heap;
    }
    pq->heap[pq->size].priority = priority;
    pq->heap[pq->size].msg_index = msg_index;
    mq_pq_sift_up(pq, pq->size);
    pq->size++;
    return 0;
}

static int mq_pq_pop(mq_priority_queue_t *pq, int32_t *priority, int *msg_index) {
    if (!pq || pq->size == 0) return -1;
    if (priority) *priority = pq->heap[0].priority;
    if (msg_index) *msg_index = pq->heap[0].msg_index;
    pq->size--;
    if (pq->size > 0) {
        pq->heap[0] = pq->heap[pq->size];
        mq_pq_sift_down(pq, 0);
    }
    return 0;
}

/**
 * L7: Priority-based message consumption.
 * Messages are delivered in priority order (lowest value = highest priority).
 * Useful for: emergency messages, system commands vs data messages.
 */
int mq_basic_consume_priority(mq_channel_t *ch, const char *queue,
                               int32_t *priorities, int msg_count,
                               mq_consume_callback cb, void *user_data) {
    if (!ch || !queue || !cb) return -1;
    mq_broker_t *b = ch->broker;
    for (int i = 0; i < b->queue_count; i++) {
        if (strcmp(b->queues[i].name, queue) == 0) {
            if (b->queues[i].count == 0) return 0;

            mq_priority_queue_t *pq = mq_pq_create(b->queues[i].count);
            if (!pq) return -1;

            /* insert all messages into priority queue */
            for (int j = 0; j < b->queues[i].count; j++) {
                int32_t prio = (priorities && j < msg_count) ? priorities[j] : 0;
                mq_pq_push(pq, prio, j);
            }

            /* consume in priority order */
            int32_t prio;
            int idx;
            while (mq_pq_pop(pq, &prio, &idx) == 0) {
                mq_message_t *msg = b->queues[i].messages[idx];
                if (msg) {
                    cb(msg, user_data);
                }
            }

            /* cleanup all messages after consumption */
            for (int j = 0; j < b->queues[i].count; j++) {
                mq_message_free(b->queues[i].messages[j]);
                free(b->queues[i].messages[j]);
            }
            b->queues[i].count = 0;
            mq_pq_destroy(pq);
            return 0;
        }
    }
    return -1;
}

/**
 * L7: Message deduplication using sliding window.
 *
 * Problem (L6): At-least-once delivery can cause duplicate messages.
 * Solution: Track recently seen message IDs in a ring buffer.
 * When a duplicate is detected, it is ACKed but not delivered.
 *
 * Window size = 10000 (covers high-throughput scenarios).
 * For production, this should use a Bloom filter (see bloom_filter.h)
 * to reduce memory from O(window) to O(1) with configurable FP rate.
 */
#define MQ_DEDUP_WINDOW 10000

typedef struct {
    char    message_id[37];
    int64_t seen_at;
} mq_dedup_entry_t;

typedef struct {
    mq_dedup_entry_t entries[MQ_DEDUP_WINDOW];
    int              pos;
    int              filled;
} mq_dedup_tracker_t;

static mq_dedup_tracker_t mq_dedup_tracker;

static int mq_is_duplicate(const char *message_id) {
    for (int i = 0; i < (mq_dedup_tracker.filled ? MQ_DEDUP_WINDOW : mq_dedup_tracker.pos); i++) {
        if (strcmp(mq_dedup_tracker.entries[i].message_id, message_id) == 0) {
            return 1;
        }
    }
    return 0;
}

static void mq_dedup_record(const char *message_id) {
    strncpy(mq_dedup_tracker.entries[mq_dedup_tracker.pos].message_id,
            message_id, 36);
    mq_dedup_tracker.entries[mq_dedup_tracker.pos].message_id[36] = '\0';
    mq_dedup_tracker.entries[mq_dedup_tracker.pos].seen_at = (int64_t)time(NULL);
    mq_dedup_tracker.pos++;
    if (mq_dedup_tracker.pos >= MQ_DEDUP_WINDOW) {
        mq_dedup_tracker.pos = 0;
        mq_dedup_tracker.filled = 1;
    }
}

/**
 * Publish with deduplication guarantee.
 * If the message_id was already seen in the dedup window, silently
 * succeed without re-delivering (idempotent publish).
 *
 * L4: Idempotency is a key property in distributed systems.
 * f(f(x)) = f(x). A publish operation that is idempotent can be
 * safely retried without causing duplicate side effects.
 */
int mq_basic_publish_dedup(mq_channel_t *ch, const char *exchange,
                            const char *routing_key, mq_message_t *msg) {
    if (!ch || !exchange || !msg) return -1;
    if (mq_is_duplicate(msg->message_id)) {
        /* Duplicate detected — ack but don't re-publish */
        if (ch->confirm_cb) {
            ch->confirm_cb(msg->message_id, 1, ch->confirm_user_data);
        }
        return 0;
    }
    mq_dedup_record(msg->message_id);
    return mq_basic_publish(ch, exchange, routing_key, msg);
}

/**
 * L8: Topic pattern matching for fanout routing.
 *
 * AMQP 0-9-1 topic exchange pattern matching:
 *   * (star) matches exactly one word
 *   # (hash) matches zero or more words
 *
 * Words are delimited by '.' in the routing key.
 *
 * Examples:
 *   "order.*.paid"  matches "order.123.paid", "order.456.paid"
 *   "order.#"       matches "order.123.paid", "order.anything.here"
 *   "*.critical.*"  matches "payment.critical.alert"
 */
static int mq_topic_match(const char *pattern, const char *routing_key) {
    if (!pattern || !routing_key) return 0;
    while (*pattern && *routing_key) {
        if (*pattern == '#') {
            pattern++;
            if (*pattern == '\0') return 1; /* # at end matches everything */
            /* consume characters until we find a match for the rest of pattern */
            while (*routing_key) {
                if (mq_topic_match(pattern, routing_key)) return 1;
                routing_key++;
            }
            return 0;
        } else if (*pattern == '*') {
            pattern++;
            /* consume one word from routing_key */
            while (*routing_key && *routing_key != '.') routing_key++;
            /* also consume the separator from both pattern and routing_key */
            if (*routing_key == '.' && *pattern == '.') {
                routing_key++;
                pattern++;
            }
        } else {
            if (*pattern != *routing_key) return 0;
            pattern++;
            routing_key++;
        }
    }
    /* both must be at end for exact match (or pattern is # which already matched) */
    if (*pattern == '#') pattern++;
    if (*pattern == '\0' && *routing_key == '\0') return 1;
    return 0;
}

/**
 * Publish with topic-aware routing.
 * Walks all bindings and matches routing_key against topic patterns.
 * This is the canonical AMQP topic exchange routing algorithm.
 */
int mq_basic_publish_topic(mq_channel_t *ch, const char *exchange,
                            const char *routing_key, mq_message_t *msg) {
    if (!ch || !exchange || !routing_key || !msg) return -1;
    mq_broker_t *b = ch->broker;
    int delivered = 0;

    /* find exchange */
    mq_exchange_t *ex = NULL;
    for (int j = 0; j < b->exchange_count; j++) {
        if (strcmp(b->exchanges[j].name, exchange) == 0) {
            ex = &b->exchanges[j]; break;
        }
    }
    if (!ex) return -1;

    for (int i = 0; i < b->binding_count; i++) {
        if (strcmp(b->bindings[i].exchange, exchange) != 0) continue;

        int route_match = 0;
        if (ex->type == MQ_EXCHANGE_FANOUT) {
            route_match = 1;
        } else if (ex->type == MQ_EXCHANGE_TOPIC) {
            route_match = mq_topic_match(b->bindings[i].routing_key, routing_key);
        } else {
            /* DIRECT: exact routing key match */
            route_match = (strcmp(b->bindings[i].routing_key, routing_key) == 0);
        }

        if (!route_match) continue;

        /* deliver to queue */
        for (int j = 0; j < b->queue_count; j++) {
            if (strcmp(b->queues[j].name, b->bindings[i].queue) == 0) {
                /* skip queue overflow check for topic — always deliver */
                mq_queue_t *q = &b->queues[j];
                if (q->count >= q->cap) {
                    q->cap = q->cap > 0 ? q->cap * 2 : 64;
                    q->messages = (mq_message_t **)realloc(q->messages,
                        (size_t)q->cap * sizeof(mq_message_t *));
                }
                q->messages[q->count] = (mq_message_t *)malloc(sizeof(mq_message_t));
                memcpy(q->messages[q->count], msg, sizeof(mq_message_t));
                q->messages[q->count]->body = (uint8_t *)malloc(msg->body_len);
                memcpy(q->messages[q->count]->body, msg->body, msg->body_len);
                q->count++;
                delivered++;
            }
        }
    }

    if (delivered > 0 && ch->confirm_cb) {
        ch->confirm_cb(msg->message_id, 1, ch->confirm_user_data);
    }
    return delivered > 0 ? 0 : -1;
}
