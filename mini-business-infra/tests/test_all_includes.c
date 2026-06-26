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

int main(void) {
    printf("hello with all includes\n");
    cc_config_center_t *c = cc_center_create();
    printf("cc_center_create OK\n");
    cc_config_put(c, "app", "db", "k", "v");
    printf("cc_put OK\n");
    cc_center_destroy(c);
    printf("all done\n");
    return 0;
}
