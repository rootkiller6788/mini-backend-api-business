#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "config_center.h"
#include "distributed_cache.h"
#include "gateway_routing.h"
#include "message_queue.h"
#include "service_registry.h"
#include "circuit_breaker.h"
#include "consistent_hash.h"
#include "bloom_filter.h"

static int run = 0, pass = 0;
static void ok(int c, const char *n) {
    run++; if (c) { pass++; printf("  PASS: %s\n", n); }
    else printf("  FAIL: %s\n", n);
}

int main(void) {
    srand((unsigned)time(NULL));
    printf("=== mini-business-infra Test Suite ===\n");
    int cnt;

    printf("[config_center]\n");
    cc_config_center_t *c = cc_center_create();
    ok(c != NULL, "cc_create");
    ok(cc_config_put(c, "app", "db", "host", "localhost") == 0, "cc_put");
    cc_config_entry_t e;
    memset(&e, 0, sizeof(e));
    ok(cc_config_get(c, "app", "db", "host", &e) == 0, "cc_get");
    ok(strcmp(e.value, "localhost") == 0, "cc_get_val");
    ok(e.version == 1, "cc_ver");
    ok(cc_config_put(c, "app", "db", "host", "v2") == 0, "cc_put2");
    ok(cc_config_delete(c, "app", "db", "host") == 0, "cc_del");
    memset(&e, 0, sizeof(e));
    ok(cc_config_get(c, "app", "db", "host", &e) != 0, "cc_del_verify");
    cc_config_put(c, "ns_a", "g1", "k1", "v1");
    cc_config_put(c, "ns_a", "g1", "k2", "v2");
    cc_config_put(c, "ns_b", "g1", "k1", "v1");
    char diff[10][128];
    ok(cc_config_diff(c, "ns_a", "g1", "ns_b", "g1", diff, &cnt) == 0, "cc_diff");
    ok(cnt == 1, "cc_diff_cnt");
    char nss[10][64]; char grps[10][64]; char keys[10][128];
    ok(cc_namespace_list(c, nss, &cnt) == 0, "cc_ns");
    ok(cc_group_list(c, "ns_a", grps, &cnt) == 0, "cc_grp");
    ok(cc_key_list(c, "ns_a", "g1", keys, &cnt) == 0, "cc_key");
    cc_center_destroy(c);

    printf("[distributed_cache]\n");
    dc_cache_config_t dcfg = { .max_entries = 50 };
    dc_cache_t *dc = dc_cache_create(&dcfg);
    ok(dc != NULL, "dc_create");
    uint8_t v1[] = "hello";
    ok(dc_cache_put(dc, "k1", v1, 5, 60) == 0, "dc_put");
    uint8_t *vo = NULL; size_t vl = 0;
    ok(dc_cache_get(dc, "k1", &vo, &vl) == 0, "dc_get");
    ok(vl == 5 && memcmp(vo, "hello", 5) == 0, "dc_val");
    free(vo);
    ok(dc_cache_exists(dc, "k1") == 1, "dc_ex");
    ok(dc_cache_exists(dc, "nx") == 0, "dc_nx");
    ok(dc_cache_delete(dc, "k1") == 0, "dc_rm");
    ok(dc_cache_exists(dc, "k1") == 0, "dc_rmv");
    uint8_t vx[] = "x";
    dc_cache_put(dc, "kk", vx, 1, 60);
    ok(dc_cache_size(dc) > 0, "dc_sz");
    ok(dc_cache_flush(dc) == 0, "dc_flush");
    ok(dc_cache_size(dc) == 0, "dc_flushed");
    dc_cache_destroy(dc);

    printf("[gateway_routing]\n");
    gw_gateway_t *gw = gw_gateway_create();
    ok(gw != NULL, "gw_create");
    gw_route_rule_t rule = {0};
    rule.method = GW_METHOD_GET;
    strcpy(rule.path, "/api/users");
    strcpy(rule.service_name, "user-svc");
    rule.priority = 10;
    rule.enabled = 1;
    ok(gw_route_add(gw, &rule) == 0, "gw_add");
    gw_route_rule_t *fr = gw_route_find(gw, GW_METHOD_GET, "/api/users");
    ok(fr != NULL, "gw_find");
    ok(strcmp(fr->service_name, "user-svc") == 0, "gw_svc");
    gw_route_rule_t rules[10];
    ok(gw_route_list(gw, rules, &cnt) == 0, "gw_list");
    ok(cnt == 1, "gw_cnt");
    ok(gw_route_remove(gw, GW_METHOD_GET, "/api/users") == 0, "gw_rm");
    ok(gw_rate_limiter_allow(gw, "/api/test", "127.0.0.1") == 1, "gw_rl");
    gw_request_t req; gw_request_init(&req);
    gw_response_t resp; gw_response_init(&resp);
    gw_request_free(&req); gw_response_free(&resp);
    gw_gateway_destroy(gw);

    printf("[message_queue]\n");
    mq_broker_t *b = mq_broker_create();
    ok(b != NULL, "mq_br");
    mq_channel_t *ch = mq_broker_open_channel(b);
    ok(ch != NULL, "mq_ch");
    mq_exchange_declare_t exd = {0};
    strcpy(exd.exchange_name, "test.ex");
    exd.type = MQ_EXCHANGE_DIRECT;
    ok(mq_exchange_declare(ch, &exd) == 0, "mq_ex");
    mq_queue_declare_t qd = {0};
    strcpy(qd.queue_name, "test.q");
    ok(mq_queue_declare(ch, &qd) == 0, "mq_q");
    ok(mq_queue_bind(ch, "test.q", "test.ex", "rk") == 0, "mq_bind");
    mq_message_t msg;
    mq_message_init(&msg);
    uint8_t body[] = "testmsg";
    msg.body = malloc(8);
    memcpy(msg.body, body, 8);
    msg.body_len = 8;
    ok(mq_basic_publish(ch, "test.ex", "rk", &msg) == 0, "mq_pub");
    ok(mq_queue_message_count(b, "test.q") == 1, "mq_cnt");
    mq_message_free(&msg);
    ok(mq_queue_purge(ch, "test.q") == 0, "mq_purge");
    ok(mq_queue_message_count(b, "test.q") == 0, "mq_pgd");
    mq_broker_destroy(b);

    printf("[topic_exchange]\n");
    mq_broker_t *b2 = mq_broker_create();
    mq_channel_t *ch2 = mq_broker_open_channel(b2);
    mq_exchange_declare_t tex = {0};
    strcpy(tex.exchange_name, "topic.ex");
    tex.type = MQ_EXCHANGE_TOPIC;
    mq_exchange_declare(ch2, &tex);
    mq_queue_declare_t tqd = {0};
    strcpy(tqd.queue_name, "topic.q");
    mq_queue_declare(ch2, &tqd);
    mq_queue_bind(ch2, "topic.q", "topic.ex", "order.*.paid");
    mq_message_t tmsg;
    mq_message_init(&tmsg);
    tmsg.body = malloc(4);
    memcpy(tmsg.body, "xyz", 3);
    tmsg.body_len = 3;
    ok(mq_basic_publish_topic(ch2, "topic.ex", "order.123.paid", &tmsg) == 0, "mq_topic");
    ok(mq_queue_message_count(b2, "topic.q") == 1, "mq_tpc");
    mq_message_free(&tmsg);
    mq_broker_destroy(b2);

    printf("[message_dedup]\n");
    mq_broker_t *b3 = mq_broker_create();
    mq_channel_t *ch3 = mq_broker_open_channel(b3);
    mq_exchange_declare_t dex = {0};
    strcpy(dex.exchange_name, "dedup.ex");
    dex.type = MQ_EXCHANGE_DIRECT;
    mq_exchange_declare(ch3, &dex);
    mq_queue_declare_t dqd = {0};
    strcpy(dqd.queue_name, "dedup.q");
    mq_queue_declare(ch3, &dqd);
    mq_queue_bind(ch3, "dedup.q", "dedup.ex", "dd");
    mq_message_t dmsg;
    mq_message_init(&dmsg);
    dmsg.body = malloc(6);
    memcpy(dmsg.body, "uniq", 4);
    dmsg.body_len = 4;
    mq_basic_publish_dedup(ch3, "dedup.ex", "dd", &dmsg);
    mq_basic_publish_dedup(ch3, "dedup.ex", "dd", &dmsg);
    ok(mq_queue_message_count(b3, "dedup.q") == 1, "mq_dedup");
    mq_message_free(&dmsg);
    mq_broker_destroy(b3);

    printf("[service_registry]\n");
    sr_registry_t *sr = sr_registry_create();
    ok(sr != NULL, "sr_create");
    sr_instance_t inst = {0};
    strcpy(inst.service_name, "api-gw");
    strcpy(inst.host, "10.0.0.1");
    strcpy(inst.instance_id, "i1");
    inst.port = 8080;
    inst.weight = 10;
    ok(sr_register(sr, &inst) == 0, "sr_reg");
    ok(sr_service_count(sr) == 1, "sr_cnt");
    sr_instance_t *sinst = NULL;
    ok(sr_discover(sr, "api-gw", &sinst, &cnt) == 0, "sr_disc");
    ok(cnt == 1 && sinst[0].port == 8080, "sr_dv");
    sr_instance_t one;
    ok(sr_lookup_one(sr, "api-gw", SR_LB_ROUND_ROBIN, &one) == 0, "sr_lkp");
    ok(sr_heartbeat(sr, "api-gw", "i1") == 0, "sr_hb");
    sr_dependency_add(sr, "order-svc", "payment-svc");
    sr_instance_t d2 = {0};
    strcpy(d2.service_name, "payment-svc");
    strcpy(d2.host, "10.0.0.2");
    strcpy(d2.instance_id, "p1");
    d2.port = 9001; d2.weight = 1;
    sr_register(sr, &d2);
    ok(sr_dependency_check(sr, "order-svc") >= 0, "sr_dep");
    sr_registry_destroy(sr);

    printf("[circuit_breaker]\n");
    cb_config_t ccfg = {0};
    strcpy(ccfg.name, "test-cb");
    ccfg.failure_threshold = 3;
    ccfg.success_threshold = 2;
    ccfg.timeout_ms = 1000;
    cb_circuit_breaker_t *cb = cb_create(&ccfg);
    ok(cb != NULL, "cb_create");
    ok(cb_get_state(cb) == CB_CLOSED, "cb_st");
    cb_destroy(cb);

    printf("[consistent_hash]\n");
    ch_ring_t *ring = ch_ring_create(10);
    ok(ring != NULL, "ch_create");
    ok(ch_ring_add_node(ring, "n1") == 0, "ch_a1");
    ok(ch_ring_add_node(ring, "n2") == 0, "ch_a2");
    ok(ch_ring_add_node(ring, "n3") == 0, "ch_a3");
    ok(ch_ring_size(ring) == 3, "ch_sz");
    char node[CH_MAX_NODE_LEN];
    ok(ch_ring_get_node(ring, "key-1", node) == 0, "ch_get");
    ok(strlen(node) > 0, "ch_gv");
    char nodes[5][CH_MAX_NODE_LEN];
    ok(ch_ring_get_nodes(ring, "key-x", 3, nodes) == 3, "ch_gn");
    ok(ch_ring_remove_node(ring, "n3") == 0, "ch_rm");
    ok(ch_ring_size(ring) == 2, "ch_s2");
    ch_ring_destroy(ring);

    printf("[bloom_filter]\n");
    bf_bloom_filter_t *bf = bf_create(1000, 0.01);
    ok(bf != NULL, "bf_create");
    ok(bf_add_string(bf, "apple") == 0, "bf_add");
    ok(bf_contains_string(bf, "apple") == 1, "bf_in");
    ok(bf_contains_string(bf, "banana") == 0, "bf_neg");
    ok(bf_element_count(bf) == 1, "bf_cnt");
    bf_clear(bf);
    ok(bf_contains_string(bf, "apple") == 0, "bf_clr");
    double fp = bf_current_fp_rate(bf);
    ok(fp >= 0.0 && fp <= 1.0, "bf_fp");
    bf_destroy(bf);

    printf("\n=== %d/%d passed ===\n", pass, run);
    return pass == run ? 0 : 1;
}
