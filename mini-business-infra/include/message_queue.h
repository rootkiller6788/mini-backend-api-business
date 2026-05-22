#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MQ_MAX_KEY_LEN         64
#define MQ_MAX_ROUTING_KEY_LEN 128
#define MQ_MAX_BODY_LEN        65536
#define MQ_MAX_QUEUES          256
#define MQ_MAX_CONSUMERS       1024
#define MQ_DEFAULT_TTL_SEC     0
#define MQ_MAX_RETRY_COUNT     10
#define MQ_DEFAULT_BACKOFF_MS  100

typedef enum {
    MQ_EXCHANGE_DIRECT = 0,
    MQ_EXCHANGE_TOPIC  = 1,
    MQ_EXCHANGE_FANOUT = 2
} mq_exchange_type_t;

typedef enum {
    MQ_CONFIRM_OFF     = 0,
    MQ_CONFIRM_ON      = 1
} mq_confirm_mode_t;

typedef enum {
    MQ_ACK_SUCCESS   = 0,
    MQ_ACK_NACK      = 1,
    MQ_ACK_REQUEUE   = 2
} mq_ack_status_t;

typedef struct {
    char     exchange_name[MQ_MAX_KEY_LEN];
    char     routing_key[MQ_MAX_ROUTING_KEY_LEN];
    uint8_t *body;
    size_t   body_len;
    char     message_id[37];
    int64_t  timestamp_ms;
    int32_t  ttl_seconds;
    int32_t  retry_count;
    int32_t  max_retries;
    int32_t  backoff_ms;
} mq_message_t;

typedef struct {
    char        queue_name[MQ_MAX_KEY_LEN];
    int         durable;
    int32_t     message_ttl_seconds;
    int32_t     max_length;
    char        dead_letter_exchange[MQ_MAX_KEY_LEN];
    char        dead_letter_routing_key[MQ_MAX_ROUTING_KEY_LEN];
} mq_queue_declare_t;

typedef struct {
    char                exchange_name[MQ_MAX_KEY_LEN];
    mq_exchange_type_t  type;
    int                 durable;
    int                 auto_delete;
} mq_exchange_declare_t;

typedef int  (*mq_consume_callback)(mq_message_t *msg, void *user_data);
typedef void (*mq_confirm_callback)(const char *message_id, int success, void *user_data);

typedef struct mq_broker mq_broker_t;
typedef struct mq_channel mq_channel_t;

mq_broker_t    *mq_broker_create(void);
void            mq_broker_destroy(mq_broker_t *broker);
int             mq_broker_start(mq_broker_t *broker, const char *bind_addr, uint16_t port);
void            mq_broker_stop(mq_broker_t *broker);
mq_channel_t   *mq_broker_open_channel(mq_broker_t *broker);

int             mq_exchange_declare(mq_channel_t *ch, const mq_exchange_declare_t *decl);
int             mq_queue_declare(mq_channel_t *ch, const mq_queue_declare_t *decl);
int             mq_queue_bind(mq_channel_t *ch, const char *queue, const char *exchange, const char *routing_key);
int             mq_queue_unbind(mq_channel_t *ch, const char *queue, const char *exchange, const char *routing_key);

int             mq_basic_publish(mq_channel_t *ch, const char *exchange, const char *routing_key,
                                 mq_message_t *msg);
int             mq_basic_consume(mq_channel_t *ch, const char *queue, mq_consume_callback cb, void *user_data);
int             mq_basic_cancel(mq_channel_t *ch, const char *queue);
int             mq_basic_ack(mq_channel_t *ch, const char *message_id, mq_ack_status_t status);

int             mq_set_confirm_mode(mq_channel_t *ch, mq_confirm_mode_t mode);
int             mq_set_confirm_callback(mq_channel_t *ch, mq_confirm_callback cb, void *user_data);

int             mq_queue_purge(mq_channel_t *ch, const char *queue);
int             mq_queue_delete(mq_channel_t *ch, const char *queue);
int             mq_exchange_delete(mq_channel_t *ch, const char *exchange);
int64_t         mq_queue_message_count(mq_broker_t *broker, const char *queue);

void            mq_message_init(mq_message_t *msg);
void            mq_message_free(mq_message_t *msg);

#ifdef __cplusplus
}
#endif

#endif
