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
