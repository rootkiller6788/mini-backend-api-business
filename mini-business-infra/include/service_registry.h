#ifndef SERVICE_REGISTRY_H
#define SERVICE_REGISTRY_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SR_MAX_NAME_LEN      64
#define SR_MAX_HOST_LEN      128
#define SR_MAX_URL_LEN       256
#define SR_MAX_META_LEN      1024
#define SR_MAX_INSTANCES     4096
#define SR_DEFAULT_HEARTBEAT 30
#define SR_DEFAULT_TTL       90

typedef enum {
    SR_STATUS_UP     = 0,
    SR_STATUS_DOWN   = 1,
    SR_STATUS_UNKNOWN = 2
} sr_instance_status_t;

typedef enum {
    SR_LB_ROUND_ROBIN = 0,
    SR_LB_RANDOM      = 1,
    SR_LB_WEIGHTED    = 2
} sr_lb_strategy_t;

typedef struct {
    char      service_name[SR_MAX_NAME_LEN];
    char      instance_id[37];
    char      host[SR_MAX_HOST_LEN];
    uint16_t  port;
    char      health_url[SR_MAX_URL_LEN];
    char      metadata[SR_MAX_META_LEN];
    int32_t   weight;
    int64_t   register_time;
    int64_t   last_heartbeat;
    int32_t   heartbeat_interval;
    int32_t   ttl_seconds;
    sr_instance_status_t status;
} sr_instance_t;

typedef void (*sr_health_check_callback)(const sr_instance_t *inst, sr_instance_status_t new_status, void *user_data);

typedef struct sr_registry sr_registry_t;

sr_registry_t *sr_registry_create(void);
void           sr_registry_destroy(sr_registry_t *registry);

int            sr_register(sr_registry_t *registry, sr_instance_t *instance);
int            sr_deregister(sr_registry_t *registry, const char *service_name,
                             const char *instance_id);
int            sr_heartbeat(sr_registry_t *registry, const char *service_name,
                            const char *instance_id);
int            sr_set_status(sr_registry_t *registry, const char *service_name,
                             const char *instance_id, sr_instance_status_t status);

int            sr_discover(sr_registry_t *registry, const char *service_name,
                           sr_instance_t **instances, int *count);
int            sr_discover_healthy(sr_registry_t *registry, const char *service_name,
                                   sr_instance_t **instances, int *count);
int            sr_lookup_one(sr_registry_t *registry, const char *service_name,
                             sr_lb_strategy_t strategy, sr_instance_t *instance);

int            sr_health_check(sr_registry_t *registry);
int            sr_health_check_instance(sr_registry_t *registry, const char *service_name,
                                        const char *instance_id);
int            sr_set_health_callback(sr_registry_t *registry, sr_health_check_callback cb,
                                      void *user_data);

int            sr_service_list(sr_registry_t *registry, char names[][SR_MAX_NAME_LEN], int *count);
int            sr_service_count(sr_registry_t *registry);

void           sr_ttl_watchdog_start(sr_registry_t *registry);
void           sr_ttl_watchdog_stop(sr_registry_t *registry);

/* L4: Quorum-based health check */
int            sr_health_check_quorum(sr_registry_t *registry, const char *service_name,
                                       const char *instance_id, int observer_count,
                                       int quorum_threshold);

/* L7: Service dependency graph */
typedef enum {
    SR_DEP_OK       = 0,
    SR_DEP_DEGRADED = 1,
    SR_DEP_DOWN     = 2
} sr_dependency_status_t;

int            sr_dependency_add(sr_registry_t *registry, const char *from_svc,
                                  const char *to_svc);
int            sr_dependency_check(sr_registry_t *registry, const char *service_name);

/* L8: Rendezvous hashing (Highest Random Weight) */
int            sr_lookup_rendezvous(sr_registry_t *registry, const char *service_name,
                                     const char *key, sr_instance_t *instance);

#ifdef __cplusplus
}
#endif

#endif
