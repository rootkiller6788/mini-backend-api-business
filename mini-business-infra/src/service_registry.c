#include "service_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct sr_svc_entry {
    sr_instance_t *instances;
    int            count;
    int            cap;
} sr_svc_entry_t;

struct sr_registry {
    sr_svc_entry_t        services[SR_MAX_INSTANCES / 8];
    char                  service_names[SR_MAX_INSTANCES / 8][SR_MAX_NAME_LEN];
    int                   service_count;
    sr_health_check_callback health_cb;
    void                 *health_user_data;
    int                   watchdog_running;
    sr_instance_t        *lb_buffer;
    int                   lb_buffer_len;
};

static sr_svc_entry_t *sr_find_service(sr_registry_t *registry, const char *name) {
    for (int i = 0; i < registry->service_count; i++) {
        if (strcmp(registry->service_names[i], name) == 0)
            return &registry->services[i];
    }
    return NULL;
}

static sr_instance_t *sr_find_instance(sr_svc_entry_t *svc, const char *instance_id) {
    for (int i = 0; i < svc->count; i++) {
        if (strcmp(svc->instances[i].instance_id, instance_id) == 0)
            return &svc->instances[i];
    }
    return NULL;
}

sr_registry_t *sr_registry_create(void) {
    sr_registry_t *r = (sr_registry_t *)calloc(1, sizeof(sr_registry_t));
    if (!r) return NULL;
    r->lb_buffer_len = 0;
    return r;
}

void sr_registry_destroy(sr_registry_t *registry) {
    if (!registry) return;
    for (int i = 0; i < registry->service_count; i++) {
        free(registry->services[i].instances);
    }
    free(registry->lb_buffer);
    free(registry);
}

int sr_register(sr_registry_t *registry, sr_instance_t *instance) {
    if (!registry || !instance) return -1;
    sr_svc_entry_t *svc = sr_find_service(registry, instance->service_name);
    if (!svc) {
        if (registry->service_count >= SR_MAX_INSTANCES / 8) return -1;
        svc = &registry->services[registry->service_count];
        strncpy(registry->service_names[registry->service_count], instance->service_name, SR_MAX_NAME_LEN - 1);
        svc->cap = 16;
        svc->instances = (sr_instance_t *)calloc((size_t)svc->cap, sizeof(sr_instance_t));
        if (!svc->instances) return -1;
        registry->service_count++;
    }
    if (svc->count >= SR_MAX_INSTANCES) return -1;
    if (svc->count >= svc->cap) {
        svc->cap *= 2;
        svc->instances = (sr_instance_t *)realloc(svc->instances, (size_t)svc->cap * sizeof(sr_instance_t));
        if (!svc->instances) return -1;
    }
    sr_instance_t *inst = &svc->instances[svc->count];
    memcpy(inst, instance, sizeof(sr_instance_t));
    inst->register_time = (int64_t)time(NULL);
    inst->last_heartbeat = inst->register_time;
    if (inst->heartbeat_interval == 0) inst->heartbeat_interval = SR_DEFAULT_HEARTBEAT;
    if (inst->ttl_seconds == 0) inst->ttl_seconds = SR_DEFAULT_TTL;
    inst->status = SR_STATUS_UP;
    if (strlen(instance->instance_id) == 0) {
        snprintf(inst->instance_id, 37, "%04x%04x-%04x-%04x-%04x-%04x%04x%04x",
                 rand() & 0xffff, rand() & 0xffff, rand() & 0xffff,
                 rand() & 0xffff, rand() & 0xffff, rand() & 0xffff,
                 rand() & 0xffff, rand() & 0xffff);
    }
    svc->count++;
    return 0;
}

int sr_deregister(sr_registry_t *registry, const char *service_name,
                  const char *instance_id) {
    if (!registry || !service_name || !instance_id) return -1;
    sr_svc_entry_t *svc = sr_find_service(registry, service_name);
    if (!svc) return -1;
    for (int i = 0; i < svc->count; i++) {
        if (strcmp(svc->instances[i].instance_id, instance_id) == 0) {
            for (int j = i; j < svc->count - 1; j++)
                svc->instances[j] = svc->instances[j + 1];
            svc->count--;
            return 0;
        }
    }
    return -1;
}

int sr_heartbeat(sr_registry_t *registry, const char *service_name,
                 const char *instance_id) {
    if (!registry || !service_name || !instance_id) return -1;
    sr_svc_entry_t *svc = sr_find_service(registry, service_name);
    if (!svc) return -1;
    sr_instance_t *inst = sr_find_instance(svc, instance_id);
    if (!inst) return -1;
    inst->last_heartbeat = (int64_t)time(NULL);
    if (inst->status == SR_STATUS_DOWN) inst->status = SR_STATUS_UP;
    return 0;
}

int sr_set_status(sr_registry_t *registry, const char *service_name,
                  const char *instance_id, sr_instance_status_t status) {
    if (!registry || !service_name || !instance_id) return -1;
    sr_svc_entry_t *svc = sr_find_service(registry, service_name);
    if (!svc) return -1;
    sr_instance_t *inst = sr_find_instance(svc, instance_id);
    if (!inst) return -1;
    sr_instance_status_t old = inst->status;
    inst->status = status;
    if (old != status && registry->health_cb) {
        registry->health_cb(inst, status, registry->health_user_data);
    }
    return 0;
}

int sr_discover(sr_registry_t *registry, const char *service_name,
                sr_instance_t **instances, int *count) {
    if (!registry || !service_name || !instances || !count) return -1;
    sr_svc_entry_t *svc = sr_find_service(registry, service_name);
    if (!svc) { *count = 0; return -1; }
    *instances = svc->instances;
    *count = svc->count;
    return 0;
}

int sr_discover_healthy(sr_registry_t *registry, const char *service_name,
                        sr_instance_t **instances, int *count) {
    if (!registry || !service_name || !instances || !count) return -1;
    sr_svc_entry_t *svc = sr_find_service(registry, service_name);
    if (!svc) { *count = 0; return -1; }
    int h = 0;
    for (int i = 0; i < svc->count; i++) {
        if (svc->instances[i].status == SR_STATUS_UP) h++;
    }
    if (h == 0) { *count = 0; return -1; }
    sr_instance_t *out = (sr_instance_t *)malloc((size_t)h * sizeof(sr_instance_t));
    if (!out) return -1;
    h = 0;
    for (int i = 0; i < svc->count; i++) {
        if (svc->instances[i].status == SR_STATUS_UP)
            out[h++] = svc->instances[i];
    }
    *instances = out;
    *count = h;
    return 0;
}

int sr_lookup_one(sr_registry_t *registry, const char *service_name,
                  sr_lb_strategy_t strategy, sr_instance_t *instance) {
    if (!registry || !service_name || !instance) return -1;
    sr_svc_entry_t *svc = sr_find_service(registry, service_name);
    if (!svc || svc->count == 0) return -1;
    int healthy_count = 0;
    for (int i = 0; i < svc->count; i++)
        if (svc->instances[i].status == SR_STATUS_UP) healthy_count++;
    if (healthy_count == 0) return -1;
    if (strategy == SR_LB_ROUND_ROBIN) {
        static int rr_counter = 0;
        int start = rr_counter % svc->count;
        for (int i = 0; i < svc->count; i++) {
            int idx = (start + i) % svc->count;
            if (svc->instances[idx].status == SR_STATUS_UP) {
                *instance = svc->instances[idx];
                rr_counter = idx + 1;
                return 0;
            }
        }
    } else if (strategy == SR_LB_WEIGHTED) {
        int total_weight = 0;
        for (int i = 0; i < svc->count; i++)
            if (svc->instances[i].status == SR_STATUS_UP)
                total_weight += svc->instances[i].weight;
        if (total_weight == 0) return -1;
        int r = rand() % total_weight;
        int cumulative = 0;
        for (int i = 0; i < svc->count; i++) {
            if (svc->instances[i].status == SR_STATUS_UP) {
                cumulative += svc->instances[i].weight;
                if (r < cumulative) { *instance = svc->instances[i]; return 0; }
            }
        }
    } else {
        for (int i = 0; i < svc->count; i++)
            if (svc->instances[i].status == SR_STATUS_UP)
                { *instance = svc->instances[i]; return 0; }
    }
    return -1;
}

int sr_health_check(sr_registry_t *registry) {
    if (!registry) return -1;
    int64_t now = (int64_t)time(NULL);
    for (int i = 0; i < registry->service_count; i++) {
        sr_svc_entry_t *svc = &registry->services[i];
        for (int j = 0; j < svc->count; j++) {
            sr_instance_t *inst = &svc->instances[j];
            if (inst->status == SR_STATUS_UP) {
                if (now - inst->last_heartbeat > inst->ttl_seconds) {
                    inst->status = SR_STATUS_DOWN;
                    if (registry->health_cb) {
                        registry->health_cb(inst, SR_STATUS_DOWN, registry->health_user_data);
                    }
                }
            }
        }
    }
    return 0;
}

int sr_health_check_instance(sr_registry_t *registry, const char *service_name,
                             const char *instance_id) {
    if (!registry || !service_name || !instance_id) return -1;
    sr_svc_entry_t *svc = sr_find_service(registry, service_name);
    if (!svc) return -1;
    sr_instance_t *inst = sr_find_instance(svc, instance_id);
    if (!inst) return -1;
    if (inst->status == SR_STATUS_UP) {
        inst->status = SR_STATUS_DOWN;
        if (registry->health_cb)
            registry->health_cb(inst, SR_STATUS_DOWN, registry->health_user_data);
    }
    return 0;
}

int sr_set_health_callback(sr_registry_t *registry, sr_health_check_callback cb,
                           void *user_data) {
    if (!registry) return -1;
    registry->health_cb = cb;
    registry->health_user_data = user_data;
    return 0;
}

int sr_service_list(sr_registry_t *registry, char names[][SR_MAX_NAME_LEN], int *count) {
    if (!registry || !names || !count) return -1;
    for (int i = 0; i < registry->service_count; i++)
        strncpy(names[i], registry->service_names[i], SR_MAX_NAME_LEN - 1);
    *count = registry->service_count;
    return 0;
}

int sr_service_count(sr_registry_t *registry) {
    return registry ? registry->service_count : 0;
}

void sr_ttl_watchdog_start(sr_registry_t *registry) {
    if (registry) registry->watchdog_running = 1;
}

void sr_ttl_watchdog_stop(sr_registry_t *registry) {
    if (registry) registry->watchdog_running = 0;
}

/* === L4: Quorum-based Health Check (CAP Theorem Application) ===
 *
 * In distributed service registries (e.g., Consul, Eureka),
 * health status is determined by consensus among multiple
 * observers. A single observer may falsely declare a healthy
 * instance as DOWN (network partition between observer and
 * instance, but instance can still serve other clients).
 *
 * Quorum health check: an instance is DOWN only if
 * Q observers agree, where Q > N/2 (majority).
 *
 * This prevents split-brain: with N=3, Q=2 observers must
 * agree, tolerating 1 faulty observer.
 *
 * Theorem (L4): With N observers and Q > N/2 quorum,
 * at most floor((N-1)/2) observers can be faulty without
 * causing false positives.
 */

typedef struct {
    char     instance_id[37];
    int      observer_count;
    int      down_votes;
    int64_t  vote_reset_time;
} sr_quorum_state_t;

/**
 * Quorum health check: collect votes from multiple health check
 * endpoints. An instance transitions DOWN only when
 * down_votes >= quorum_threshold.
 *
 * L5: The quorum check is equivalent to an (N,Q) threshold
 * function — a fundamental building block of Byzantine fault
 * tolerance (Lamport, Shostak, Pease, 1982).
 */
int sr_health_check_quorum(sr_registry_t *registry, const char *service_name,
                            const char *instance_id, int observer_count,
                            int quorum_threshold) {
    if (!registry || !service_name || !instance_id) return -1;
    if (observer_count <= 0) observer_count = 3;
    if (quorum_threshold <= 0) quorum_threshold = observer_count / 2 + 1;

    sr_svc_entry_t *svc = sr_find_service(registry, service_name);
    if (!svc) return -1;
    sr_instance_t *inst = sr_find_instance(svc, instance_id);
    if (!inst) return -1;

    /* Simulate multiple observer health checks.
     * In production, each observer independently probes the instance.
     * Here we model variance: each observer has independent
     * probability of detecting the true health state. */
    int64_t now = (int64_t)time(NULL);
    int down_votes = 0;

    for (int i = 0; i < observer_count; i++) {
        /* Each observer independently evaluates health */
        int elapsed = (int)(now - inst->last_heartbeat);
        /* Add observer-specific jitter: +/- 20% of TTL */
        int jitter = (inst->ttl_seconds * (rand() % 40 - 20)) / 100;
        int effective_ttl = inst->ttl_seconds + jitter;

        if (elapsed > effective_ttl) {
            down_votes++;
        }
    }

    if (down_votes >= quorum_threshold) {
        sr_set_status(registry, service_name, instance_id, SR_STATUS_DOWN);
    }
    return down_votes;
}

/**
 * L7: Service dependency graph.
 *
 * In microservice architectures, services depend on each other
 * (e.g., order-service depends on payment-service and inventory-service).
 * When a dependency goes down, dependent services should be
 * marked as DEGRADED rather than UP.
 *
 * This implements a simple adjacency-list dependency tracker
 * using the service registry's existing storage.
 */

typedef struct sr_dep {
    char  from_svc[SR_MAX_NAME_LEN];
    char  to_svc[SR_MAX_NAME_LEN];
    sr_dependency_status_t status;
} sr_dependency_t;

static sr_dependency_t sr_dependencies[1024];
static int sr_dep_count = 0;

/**
 * Declare that 'from_svc' depends on 'to_svc'.
 * If 'to_svc' has no healthy instances, 'from_svc' is DEGRADED.
 */
int sr_dependency_add(sr_registry_t *registry, const char *from_svc,
                       const char *to_svc) {
    if (!registry || !from_svc || !to_svc) return -1;
    if (sr_dep_count >= 1024) return -1;
    /* dedup */
    for (int i = 0; i < sr_dep_count; i++) {
        if (strcmp(sr_dependencies[i].from_svc, from_svc) == 0 &&
            strcmp(sr_dependencies[i].to_svc, to_svc) == 0) return 0;
    }
    strncpy(sr_dependencies[sr_dep_count].from_svc, from_svc, SR_MAX_NAME_LEN - 1);
    strncpy(sr_dependencies[sr_dep_count].to_svc, to_svc, SR_MAX_NAME_LEN - 1);
    sr_dependencies[sr_dep_count].status = SR_DEP_OK;
    sr_dep_count++;
    return 0;
}

/**
 * Check the dependency health of a service.
 * If any dependency has no healthy instances, the service's
 * own health status should be considered degraded.
 *
 * Returns: number of unhealthy dependencies.
 */
int sr_dependency_check(sr_registry_t *registry, const char *service_name) {
    if (!registry || !service_name) return -1;
    int unhealthy = 0;
    for (int i = 0; i < sr_dep_count; i++) {
        if (strcmp(sr_dependencies[i].from_svc, service_name) != 0) continue;

        sr_instance_t *instances = NULL;
        int count = 0;
        sr_discover_healthy(registry, sr_dependencies[i].to_svc,
                             &instances, &count);
        if (count == 0) {
            sr_dependencies[i].status = SR_DEP_DOWN;
            unhealthy++;
        } else {
            sr_dependencies[i].status = SR_DEP_OK;
        }
        free(instances);
    }
    return unhealthy;
}

/**
 * L8: Rendezvous Hashing (Highest Random Weight) for service selection.
 *
 * Unlike consistent hashing (ring-based), rendezvous hashing
 * (Thaler & Ravishankar, 1996) assigns each key to the node
 * with the highest weighted hash. This provides:
 *   - Minimal disruption: only keys assigned to removed node move
 *   - Even distribution without virtual nodes
 *   - Weighted selection trivially
 *
 * Complexity: O(N * K) for N nodes and K hash functions,
 * vs O(log N) for ring-based consistent hashing.
 *
 * Rendezvous is used by: Microsoft Azure Storage, Apache Zookeeper
 * (for leader election), and Twitter's Manhattan KV store.
 */

static uint64_t sr_rendezvous_hash(const char *key, const char *node) {
    /* combine key and node then hash */
    char buf[SR_MAX_NAME_LEN + 256];
    snprintf(buf, sizeof(buf), "%s:%s", key, node);
    /* FNV-1a 64-bit */
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const char *p = buf; *p; p++) {
        h ^= (uint64_t)(unsigned char)*p;
        h *= 0x100000001b3ULL;
    }
    return h;
}

int sr_lookup_rendezvous(sr_registry_t *registry, const char *service_name,
                          const char *key, sr_instance_t *instance) {
    if (!registry || !service_name || !key || !instance) return -1;
    sr_svc_entry_t *svc = sr_find_service(registry, service_name);
    if (!svc || svc->count == 0) return -1;

    uint64_t best_hash = 0;
    int best_idx = -1;

    for (int i = 0; i < svc->count; i++) {
        if (svc->instances[i].status != SR_STATUS_UP) continue;
        uint64_t h = sr_rendezvous_hash(key, svc->instances[i].instance_id);
        /* weight adjustment: multiply hash by weight */
        h = h * (uint64_t)svc->instances[i].weight;
        if (h > best_hash || best_idx < 0) {
            best_hash = h;
            best_idx = i;
        }
    }

    if (best_idx < 0) return -1;
    *instance = svc->instances[best_idx];
    return 0;
}
