#include "message_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int consumer_callback(mq_message_t *msg, void *user_data) {
    const char *name = (const char *)user_data;
    printf("[%s] Received: %s (body=%.*s)\n", name, msg->message_id,
           (int)msg->body_len, (const char *)msg->body);
    return MQ_ACK_SUCCESS;
}

static void confirm_callback(const char *msg_id, int success, void *user_data) {
    (void)user_data;
    printf("[confirm] %s %s\n", msg_id, success ? "ACK" : "NACK");
}

int main(void) {
    mq_broker_t *broker = mq_broker_create();
    if (!broker) { fprintf(stderr, "Failed to create broker\n"); return 1; }
    mq_broker_start(broker, "0.0.0.0", 5672);

    mq_channel_t *ch1 = mq_broker_open_channel(broker);
    mq_channel_t *ch2 = mq_broker_open_channel(broker);

    mq_exchange_declare_t ex_decl = {0};
    strcpy(ex_decl.exchange_name, "orders");
    ex_decl.type = MQ_EXCHANGE_DIRECT;
    ex_decl.durable = 1;
    mq_exchange_declare(ch1, &ex_decl);

    mq_queue_declare_t q_decl = {0};
    strcpy(q_decl.queue_name, "order.process");
    q_decl.durable = 1;
    q_decl.message_ttl_seconds = 60;
    strcpy(q_decl.dead_letter_exchange, "orders");
    strcpy(q_decl.dead_letter_routing_key, "order.dlq");
    mq_queue_declare(ch1, &q_decl);

    mq_queue_declare_t dlq_decl = {0};
    strcpy(dlq_decl.queue_name, "order.dlq");
    dlq_decl.durable = 1;
    mq_queue_declare(ch1, &dlq_decl);

    mq_queue_bind(ch1, "order.process", "orders", "order.created");
    mq_queue_bind(ch1, "order.dlq", "orders", "order.dlq");

    mq_set_confirm_mode(ch1, MQ_CONFIRM_ON);
    mq_set_confirm_callback(ch1, confirm_callback, NULL);

    mq_message_t msg;
    mq_message_init(&msg);
    const char *payload = "{\"order_id\":123,\"amount\":99.90}";
    msg.body = (uint8_t *)malloc(strlen(payload) + 1);
    msg.body_len = strlen(payload);
    memcpy(msg.body, payload, msg.body_len);
    msg.ttl_seconds = 60;
    mq_basic_publish(ch1, "orders", "order.created", &msg);
    mq_message_free(&msg);

    mq_basic_consume(ch2, "order.process", consumer_callback, "worker-1");

    printf("Queue 'order.process' message count: %lld\n",
           (long long)mq_queue_message_count(broker, "order.process"));

    mq_queue_delete(ch1, "order.process");
    mq_queue_delete(ch1, "order.dlq");
    mq_exchange_delete(ch1, "orders");

    free(ch1);
    free(ch2);
    mq_broker_stop(broker);
    mq_broker_destroy(broker);

    printf("Message broker example completed.\n");
    return 0;
}
