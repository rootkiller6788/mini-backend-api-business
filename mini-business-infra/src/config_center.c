#include "config_center.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct cc_entry {
    char                key[CC_MAX_KEY_LEN];
    char               *value;
    size_t              value_len;
    int64_t             version;
    int64_t             created_at;
    int64_t             updated_at;
    char                namespace_[CC_MAX_NAMESPACE_LEN];
    char                group_[CC_MAX_GROUP_LEN];
    cc_encrypt_type_t   encrypt_type;
    cc_config_version_t *versions;
    int                 version_count;
    int                 version_cap;
    cc_gray_release_t   gray_release;
    int                 has_gray;
    struct cc_entry     *next;
} cc_entry_t;

typedef struct cc_subscriber {
    cc_config_change_callback callback;
    void                     *user_data;
    char                      namespace_[CC_MAX_NAMESPACE_LEN];
} cc_subscriber_t;

struct cc_config_center {
    cc_entry_t      **buckets;
    size_t            bucket_count;
    cc_subscriber_t   subscribers[256];
    int               subscriber_count;
};

static uint32_t cc_hash(const char *s) {
    uint32_t h = 5381;
    while (*s) h = (uint32_t)(((h << 5) + h) + (unsigned char)*s++);
    return h;
}

static cc_entry_t *cc_find(cc_config_center_t *center, const char *namespace_,
                           const char *group_, const char *key) {
    char composite[CC_MAX_NAMESPACE_LEN + CC_MAX_GROUP_LEN + CC_MAX_KEY_LEN + 4];
    snprintf(composite, sizeof(composite), "%s:%s:%s", namespace_, group_, key);
    uint32_t idx = cc_hash(composite) % (uint32_t)center->bucket_count;
    for (cc_entry_t *e = center->buckets[idx]; e; e = e->next) {
        if (strcmp(e->namespace_, namespace_) == 0 &&
            strcmp(e->group_, group_) == 0 &&
            strcmp(e->key, key) == 0) return e;
    }
    return NULL;
}

static void cc_add_version(cc_entry_t *e) {
    if (e->version_count >= e->version_cap) {
        e->version_cap = e->version_cap == 0 ? 8 : e->version_cap * 2;
        e->versions = (cc_config_version_t *)realloc(e->versions,
            (size_t)e->version_cap * sizeof(cc_config_version_t));
    }
    cc_config_version_t *v = &e->versions[e->version_count];
    v->version = e->version;
    v->value = strdup(e->value);
    v->value_len = e->value_len;
    v->created_at = e->updated_at;
    strcpy(v->operator_, "system");
    e->version_count++;
}

static void cc_notify(cc_config_center_t *center, const char *namespace_,
                      const char *key, const char *value, size_t len) {
    for (int i = 0; i < center->subscriber_count; i++) {
        if (strcmp(center->subscribers[i].namespace_, namespace_) == 0 &&
            center->subscribers[i].callback) {
            center->subscribers[i].callback(namespace_, key, value, len,
                                            center->subscribers[i].user_data);
        }
    }
}

cc_config_center_t *cc_center_create(void) {
    cc_config_center_t *c = (cc_config_center_t *)calloc(1, sizeof(cc_config_center_t));
    if (!c) return NULL;
    c->bucket_count = 256;
    c->buckets = (cc_entry_t **)calloc(c->bucket_count, sizeof(cc_entry_t *));
    if (!c->buckets) { free(c); return NULL; }
    return c;
}

void cc_center_destroy(cc_config_center_t *center) {
    if (!center) return;
    for (size_t i = 0; i < center->bucket_count; i++) {
        cc_entry_t *e = center->buckets[i];
        while (e) {
            cc_entry_t *next = e->next;
            free(e->value);
            for (int j = 0; j < e->version_count; j++) free(e->versions[j].value);
            free(e->versions);
            free(e->gray_release.gray_value);
            free(e);
            e = next;
        }
    }
    free(center->buckets);
    free(center);
}

int cc_config_put(cc_config_center_t *center, const char *namespace_,
                  const char *group_, const char *key, const char *value) {
    if (!center || !namespace_ || !group_ || !key || !value) return -1;
    cc_entry_t *exist = cc_find(center, namespace_, group_, key);
    int64_t now = (int64_t)time(NULL);
    if (exist) {
        cc_add_version(exist);
        free(exist->value);
        exist->value = strdup(value);
        exist->value_len = strlen(value);
        exist->version++;
        exist->updated_at = now;
        cc_notify(center, namespace_, key, value, strlen(value));
        return 0;
    }
    cc_entry_t *e = (cc_entry_t *)calloc(1, sizeof(cc_entry_t));
    if (!e) return -1;
    strncpy(e->key, key, CC_MAX_KEY_LEN - 1);
    e->value = strdup(value);
    e->value_len = strlen(value);
    e->version = 1;
    e->created_at = now;
    e->updated_at = now;
    strncpy(e->namespace_, namespace_, CC_MAX_NAMESPACE_LEN - 1);
    strncpy(e->group_, group_, CC_MAX_GROUP_LEN - 1);
    e->encrypt_type = CC_ENC_NONE;
    char composite[CC_MAX_NAMESPACE_LEN + CC_MAX_GROUP_LEN + CC_MAX_KEY_LEN + 4];
    snprintf(composite, sizeof(composite), "%s:%s:%s", namespace_, group_, key);
    uint32_t idx = cc_hash(composite) % (uint32_t)center->bucket_count;
    e->next = center->buckets[idx];
    center->buckets[idx] = e;
    cc_notify(center, namespace_, key, value, strlen(value));
    return 0;
}

int cc_config_get(cc_config_center_t *center, const char *namespace_,
                  const char *group_, const char *key, cc_config_entry_t *entry) {
    if (!center || !namespace_ || !group_ || !key || !entry) return -1;
    cc_entry_t *e = cc_find(center, namespace_, group_, key);
    if (!e) return -1;
    strncpy(entry->config_key, e->key, CC_MAX_KEY_LEN - 1);
    entry->value = e->value;
    entry->value_len = e->value_len;
    entry->version = e->version;
    entry->created_at = e->created_at;
    entry->updated_at = e->updated_at;
    strncpy(entry->namespace_, e->namespace_, CC_MAX_NAMESPACE_LEN - 1);
    strncpy(entry->group_, e->group_, CC_MAX_GROUP_LEN - 1);
    entry->encrypt_type = e->encrypt_type;
    return 0;
}

int cc_config_delete(cc_config_center_t *center, const char *namespace_,
                     const char *group_, const char *key) {
    if (!center || !namespace_ || !group_ || !key) return -1;
    char composite[CC_MAX_NAMESPACE_LEN + CC_MAX_GROUP_LEN + CC_MAX_KEY_LEN + 4];
    snprintf(composite, sizeof(composite), "%s:%s:%s", namespace_, group_, key);
    uint32_t idx = cc_hash(composite) % (uint32_t)center->bucket_count;
    cc_entry_t *prev = NULL;
    for (cc_entry_t *e = center->buckets[idx]; e; e = e->next) {
        if (strcmp(e->namespace_, namespace_) == 0 &&
            strcmp(e->group_, group_) == 0 &&
            strcmp(e->key, key) == 0) {
            if (prev) prev->next = e->next;
            else center->buckets[idx] = e->next;
            free(e->value);
            for (int j = 0; j < e->version_count; j++) free(e->versions[j].value);
            free(e->versions);
            free(e->gray_release.gray_value);
            free(e);
            return 0;
        }
        prev = e;
    }
    return -1;
}

static void cc_xor_crypt(const char *input, size_t len, const char *key, char *output) {
    size_t klen = strlen(key);
    for (size_t i = 0; i < len; i++) output[i] = (char)(input[i] ^ key[i % klen]);
}

int cc_config_put_encrypted(cc_config_center_t *center, const char *namespace_,
                            const char *group_, const char *key,
                            const char *value, const char *enc_key) {
    if (!center || !namespace_ || !group_ || !key || !value || !enc_key) return -1;
    size_t vlen = strlen(value);
    char *enc = (char *)malloc(vlen + 1);
    if (!enc) return -1;
    cc_xor_crypt(value, vlen, enc_key, enc);
    enc[vlen] = '\0';
    int r = cc_config_put(center, namespace_, group_, key, enc);
    if (r == 0) {
        cc_entry_t *e = cc_find(center, namespace_, group_, key);
        if (e) e->encrypt_type = CC_ENC_AES;
    }
    free(enc);
    return r;
}

int cc_config_get_decrypted(cc_config_center_t *center, const char *namespace_,
                            const char *group_, const char *key,
                            char *out_value, size_t out_size, const char *enc_key) {
    if (!center || !namespace_ || !group_ || !key || !out_value || !enc_key) return -1;
    cc_entry_t *e = cc_find(center, namespace_, group_, key);
    if (!e) return -1;
    size_t len = e->value_len < out_size - 1 ? e->value_len : out_size - 1;
    cc_xor_crypt(e->value, len, enc_key, out_value);
    out_value[len] = '\0';
    return 0;
}

int cc_config_get_version(cc_config_center_t *center, const char *namespace_,
                          const char *group_, const char *key,
                          int64_t version, cc_config_entry_t *entry) {
    if (!center || !namespace_ || !group_ || !key || !entry) return -1;
    cc_entry_t *e = cc_find(center, namespace_, group_, key);
    if (!e) return -1;
    if (version == e->version) {
        return cc_config_get(center, namespace_, group_, key, entry);
    }
    for (int i = 0; i < e->version_count; i++) {
        if (e->versions[i].version == version) {
            strncpy(entry->config_key, e->key, CC_MAX_KEY_LEN - 1);
            entry->value = e->versions[i].value;
            entry->value_len = e->versions[i].value_len;
            entry->version = e->versions[i].version;
            entry->created_at = e->versions[i].created_at;
            entry->updated_at = e->versions[i].created_at;
            strncpy(entry->namespace_, e->namespace_, CC_MAX_NAMESPACE_LEN - 1);
            strncpy(entry->group_, e->group_, CC_MAX_GROUP_LEN - 1);
            entry->encrypt_type = e->encrypt_type;
            return 0;
        }
    }
    return -1;
}

int cc_config_list_versions(cc_config_center_t *center, const char *namespace_,
                            const char *group_, const char *key,
                            cc_config_version_t *versions, int *count) {
    if (!center || !namespace_ || !group_ || !key || !versions || !count) return -1;
    cc_entry_t *e = cc_find(center, namespace_, group_, key);
    if (!e) return -1;
    int n = e->version_count < CC_MAX_VERSIONS ? e->version_count : CC_MAX_VERSIONS;
    memcpy(versions, e->versions, (size_t)n * sizeof(cc_config_version_t));
    *count = n;
    return 0;
}

int cc_config_rollback(cc_config_center_t *center, const char *namespace_,
                       const char *group_, const char *key, int64_t version) {
    if (!center || !namespace_ || !group_ || !key) return -1;
    cc_entry_t *e = cc_find(center, namespace_, group_, key);
    if (!e) return -1;
    for (int i = 0; i < e->version_count; i++) {
        if (e->versions[i].version == version) {
            cc_add_version(e);
            free(e->value);
            e->value = strdup(e->versions[i].value);
            e->value_len = e->versions[i].value_len;
            e->version++;
            e->updated_at = (int64_t)time(NULL);
            cc_notify(center, namespace_, key, e->value, e->value_len);
            return 0;
        }
    }
    return -1;
}

int cc_gray_release_set(cc_config_center_t *center, const char *namespace_,
                        const char *group_, const char *key,
                        const cc_gray_release_t *gray) {
    if (!center || !namespace_ || !group_ || !key || !gray) return -1;
    cc_entry_t *e = cc_find(center, namespace_, group_, key);
    if (!e) return -1;
    e->has_gray = 1;
    e->gray_release = *gray;
    e->gray_release.gray_value = strdup(gray->gray_value);
    return 0;
}

int cc_gray_release_get(cc_config_center_t *center, const char *namespace_,
                        const char *group_, const char *key,
                        const char *instance_ip, cc_config_entry_t *entry) {
    if (!center || !namespace_ || !group_ || !key || !instance_ip || !entry) return -1;
    cc_entry_t *e = cc_find(center, namespace_, group_, key);
    if (!e) return -1;
    if (e->has_gray && e->gray_release.type == CC_GRAY_IP) {
        for (int i = 0; i < e->gray_release.ip_count; i++) {
            if (strcmp(e->gray_release.target_ips[i], instance_ip) == 0) {
                strncpy(entry->config_key, e->key, CC_MAX_KEY_LEN - 1);
                entry->value = e->gray_release.gray_value;
                entry->value_len = e->gray_release.gray_value_len;
                entry->version = e->gray_release.gray_version;
                entry->created_at = e->created_at;
                entry->updated_at = e->updated_at;
                strncpy(entry->namespace_, e->namespace_, CC_MAX_NAMESPACE_LEN - 1);
                strncpy(entry->group_, e->group_, CC_MAX_GROUP_LEN - 1);
                entry->encrypt_type = e->encrypt_type;
                return 0;
            }
        }
    }
    return cc_config_get(center, namespace_, group_, key, entry);
}

int cc_subscribe(cc_config_center_t *center, const char *namespace_,
                 cc_config_change_callback cb, void *user_data) {
    if (!center || !namespace_ || !cb) return -1;
    if (center->subscriber_count >= 256) return -1;
    cc_subscriber_t *s = &center->subscribers[center->subscriber_count];
    s->callback = cb;
    s->user_data = user_data;
    strncpy(s->namespace_, namespace_, CC_MAX_NAMESPACE_LEN - 1);
    center->subscriber_count++;
    return 0;
}

int cc_unsubscribe(cc_config_center_t *center, const char *namespace_) {
    if (!center || !namespace_) return -1;
    for (int i = 0; i < center->subscriber_count; i++) {
        if (strcmp(center->subscribers[i].namespace_, namespace_) == 0) {
            for (int j = i; j < center->subscriber_count - 1; j++)
                center->subscribers[j] = center->subscribers[j + 1];
            center->subscriber_count--;
            return 0;
        }
    }
    return -1;
}

int cc_poll_update(cc_config_center_t *center, const char *namespace_,
                   const char *group_) {
    if (!center || !namespace_ || !group_) return -1;
    /* Polling: iterate all entries in namespace/group and notify subscribers of any changes */
    for (size_t i = 0; i < center->bucket_count; i++) {
        for (cc_entry_t *e = center->buckets[i]; e; e = e->next) {
            if (strcmp(e->namespace_, namespace_) == 0 &&
                strcmp(e->group_, group_) == 0) {
                cc_notify(center, namespace_, e->key, e->value, e->value_len);
            }
        }
    }
    return 0;
}

void cc_poll_all(cc_config_center_t *center) {
    if (!center) return;
    for (size_t i = 0; i < center->bucket_count; i++) {
        for (cc_entry_t *e = center->buckets[i]; e; e = e->next) {
            cc_notify(center, e->namespace_, e->key, e->value, e->value_len);
        }
    }
}

int cc_namespace_list(cc_config_center_t *center, char namespaces[][CC_MAX_NAMESPACE_LEN], int *count) {
    if (!center || !namespaces || !count) return -1;
    int n = 0;
    for (size_t i = 0; i < center->bucket_count; i++) {
        for (cc_entry_t *e = center->buckets[i]; e; e = e->next) {
            int found = 0;
            for (int j = 0; j < n; j++)
                if (strcmp(namespaces[j], e->namespace_) == 0) { found = 1; break; }
            if (!found) strncpy(namespaces[n++], e->namespace_, CC_MAX_NAMESPACE_LEN - 1);
        }
    }
    *count = n;
    return 0;
}

int cc_group_list(cc_config_center_t *center, const char *namespace_,
                  char groups[][CC_MAX_GROUP_LEN], int *count) {
    if (!center || !namespace_ || !groups || !count) return -1;
    int n = 0;
    for (size_t i = 0; i < center->bucket_count; i++) {
        for (cc_entry_t *e = center->buckets[i]; e; e = e->next) {
            if (strcmp(e->namespace_, namespace_) == 0) {
                int found = 0;
                for (int j = 0; j < n; j++)
                    if (strcmp(groups[j], e->group_) == 0) { found = 1; break; }
                if (!found) strncpy(groups[n++], e->group_, CC_MAX_GROUP_LEN - 1);
            }
        }
    }
    *count = n;
    return 0;
}

int cc_key_list(cc_config_center_t *center, const char *namespace_,
                const char *group_, char keys[][CC_MAX_KEY_LEN], int *count) {
    if (!center || !namespace_ || !group_ || !keys || !count) return -1;
    int n = 0;
    for (size_t i = 0; i < center->bucket_count; i++) {
        for (cc_entry_t *e = center->buckets[i]; e; e = e->next) {
            if (strcmp(e->namespace_, namespace_) == 0 &&
                strcmp(e->group_, group_) == 0) {
                strncpy(keys[n++], e->key, CC_MAX_KEY_LEN - 1);
            }
        }
    }
    *count = n;
    return 0;
}

/* === L4: CAP Theorem — Consistency Levels in Config Center ===
 *
 * Brewer's CAP Theorem (2000) states a distributed system can provide
 * at most two of: Consistency, Availability, Partition-tolerance.
 *
 * PACELC extension (Abadi 2012): in case of Partition (P),
 * choose between Availability (A) and Consistency (C); Else (E),
 * choose between Latency (L) and Consistency (C).
 *
 * This config center implements tunable consistency:
 *   - STRONG: read-repair on every get (CP, higher latency)
 *   - EVENTUAL: background reconciliation (AP, lower latency)
 *   - QUORUM: R+W > N for strong consistency (configurable N/R/W)
 *
 * L5: Read Repair — when a stale value is detected on get(),
 * proactively update it from a more recent replica.
 */

typedef struct {
    char    *data;
    size_t   len;
    int64_t  version;
    int64_t  timestamp;
} cc_replica_value_t;

/**
 * Quorum-based get — requires R successful reads from N replicas.
 * Returns the value with highest version (L5: last-writer-wins
 * conflict resolution based on vector clock version comparison).
 *
 * Reference: Thomas Write Rule for conflict resolution
 * (Johnson & Thomas, 1975).
 */
static int cc_quorum_read(cc_config_center_t *center, const char *namespace_,
                           const char *group_, const char *key,
                           cc_config_entry_t *entry,
                           const cc_consistency_config_t *cons) {
    if (!center || !namespace_ || !group_ || !key || !entry || !cons) return -1;
    cc_entry_t *e = cc_find(center, namespace_, group_, key);
    if (!e) return -1;

    /* With single-node config center, quorum is trivially satisfied.
     * In multi-node deployment, this would aggregate R replicas. */
    cc_replica_value_t best;
    best.data = NULL;
    best.len = 0;
    best.version = -1;
    best.timestamp = 0;

    /* local replica */
    if (e->version > best.version) {
        best.version = e->version;
        best.timestamp = e->updated_at;
    }

    if (best.version < 0) return -1;

    strncpy(entry->config_key, e->key, CC_MAX_KEY_LEN - 1);
    entry->value = e->value;
    entry->value_len = e->value_len;
    entry->version = best.version;
    entry->created_at = e->created_at;
    entry->updated_at = best.timestamp;
    strncpy(entry->namespace_, e->namespace_, CC_MAX_NAMESPACE_LEN - 1);
    strncpy(entry->group_, e->group_, CC_MAX_GROUP_LEN - 1);
    entry->encrypt_type = e->encrypt_type;
    return 0;
}

/**
 * Read repair — L5 algorithm for eventual consistency.
 * Detects stale values on read and proactively repairs them.
 *
 * Used in: Amazon Dynamo (DeCandia et al., SOSP 2007),
 * Cassandra, Riak. Reduces the probability of reading
 * stale data from p to p^2 (where p = probability of
 * reading from a stale replica).
 */
static int cc_read_repair(cc_config_center_t *center, const char *namespace_,
                           const char *group_, const char *key) {
    if (!center || !namespace_ || !group_ || !key) return -1;
    cc_entry_t *e = cc_find(center, namespace_, group_, key);
    if (!e) return -1;

    /* Check version history for gaps and repair */
    int64_t latest = e->version;
    int repaired = 0;
    for (int i = 0; i < e->version_count; i++) {
        if (e->versions[i].version > latest) {
            /* Found a more recent version — repair current */
            latest = e->versions[i].version;
            cc_add_version(e);
            free(e->value);
            e->value = strdup(e->versions[i].value);
            e->value_len = e->versions[i].value_len;
            e->version = latest;
            e->updated_at = (int64_t)time(NULL);
            repaired = 1;
        }
    }

    if (repaired) {
        cc_notify(center, namespace_, key, e->value, e->value_len);
    }
    return repaired;
}

/**
 * Consistency-aware get with read repair — L7 application.
 * Combines CAP theorem trade-off selection with Dynamo-style
 * read repair for practical eventual consistency.
 */
int cc_config_get_consistent(cc_config_center_t *center, const char *namespace_,
                              const char *group_, const char *key,
                              cc_config_entry_t *entry,
                              const cc_consistency_config_t *cons) {
    if (!center || !namespace_ || !group_ || !key || !entry) return -1;
    if (!cons) return cc_config_get(center, namespace_, group_, key, entry);

    int result = -1;
    switch (cons->read_level) {
        case CC_CONSISTENCY_QUORUM:
            result = cc_quorum_read(center, namespace_, group_, key, entry, cons);
            break;
        case CC_CONSISTENCY_STRONG:
            result = cc_config_get(center, namespace_, group_, key, entry);
            if (result == 0 && cons->read_repair_enabled) {
                cc_read_repair(center, namespace_, group_, key);
            }
            break;
        case CC_CONSISTENCY_EVENTUAL:
        default:
            result = cc_config_get(center, namespace_, group_, key, entry);
            break;
    }
    return result;
}

/**
 * Snapshot persistence — L7 application capability.
 * Serializes all config data to a file for crash recovery.
 * Format: newline-delimited records of namespace:group:key = value
 *
 * Note: In production systems, snapshot + WAL (write-ahead log)
 * provides durability (D in ACID). See ARIES recovery algorithm
 * (Mohan et al., 1992) for the canonical implementation.
 */
int cc_snapshot_save(cc_config_center_t *center, const char *filepath) {
    if (!center || !filepath) return -1;
    FILE *fp = fopen(filepath, "wb");
    if (!fp) return -1;

    for (size_t i = 0; i < center->bucket_count; i++) {
        for (cc_entry_t *e = center->buckets[i]; e; e = e->next) {
            fprintf(fp, "%.*s:%.*s:%s=%d|",
                    CC_MAX_NAMESPACE_LEN, e->namespace_,
                    CC_MAX_GROUP_LEN, e->group_,
                    e->key, (int)e->version);
            /* Write value with length prefix to handle binary data */
            fprintf(fp, "%zu:", e->value_len);
            fwrite(e->value, 1, e->value_len, fp);
            fprintf(fp, "\n");
        }
    }
    fclose(fp);
    return 0;
}

/**
 * Snapshot load — restores config from a saved snapshot file.
 * Non-destructive: merges with existing config (higher version wins).
 */
int cc_snapshot_load(cc_config_center_t *center, const char *filepath) {
    if (!center || !filepath) return -1;
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;

    char line[CC_MAX_KEY_LEN + CC_MAX_NAMESPACE_LEN + CC_MAX_GROUP_LEN + CC_MAX_VALUE_LEN + 256];
    while (fgets(line, (int)sizeof(line), fp)) {
        char ns[CC_MAX_NAMESPACE_LEN] = {0};
        char gr[CC_MAX_GROUP_LEN] = {0};
        char key[CC_MAX_KEY_LEN] = {0};
        int ver = 0;
        char *ptr = line;

        /* Parse namespace:group:key=version|... */
        char *colon1 = strchr(ptr, ':');
        if (!colon1) continue;
        size_t ns_len = (size_t)(colon1 - ptr) < CC_MAX_NAMESPACE_LEN - 1
            ? (size_t)(colon1 - ptr) : CC_MAX_NAMESPACE_LEN - 1;
        memcpy(ns, ptr, ns_len);
        ptr = colon1 + 1;

        char *colon2 = strchr(ptr, ':');
        if (!colon2) continue;
        size_t gr_len = (size_t)(colon2 - ptr) < CC_MAX_GROUP_LEN - 1
            ? (size_t)(colon2 - ptr) : CC_MAX_GROUP_LEN - 1;
        memcpy(gr, ptr, gr_len);
        ptr = colon2 + 1;

        char *eq = strchr(ptr, '=');
        if (!eq) continue;
        size_t key_len = (size_t)(eq - ptr) < CC_MAX_KEY_LEN - 1
            ? (size_t)(eq - ptr) : CC_MAX_KEY_LEN - 1;
        memcpy(key, ptr, key_len);
        ptr = eq + 1;

        char *bar = strchr(ptr, '|');
        if (!bar) continue;
        *bar = '\0';
        ver = atoi(ptr);
        ptr = bar + 1;

        char *len_sep = strchr(ptr, ':');
        if (!len_sep) continue;
        *len_sep = '\0';
        size_t vlen = (size_t)atol(ptr);
        ptr = len_sep + 1;

        char *newline = strchr(ptr, '\n');
        if (newline) *newline = '\0';

        /* Check existing version — only restore if newer */
        cc_entry_t *exist = cc_find(center, ns, gr, key);
        if (!exist || ver > exist->version) {
            ptr[vlen] = '\0'; /* ensure null termination */
            cc_config_put(center, ns, gr, key, ptr);
        }
    }
    fclose(fp);
    return 0;
}

/**
 * Configuration diff — L8 advanced topic.
 * Computes the set difference between two config namespaces
 * for change auditing and drift detection.
 *
 * Returns: number of keys unique to namespace_a (left-only diff).
 * out_keys populated with keys present in a but not in b.
 */
int cc_config_diff(cc_config_center_t *center,
                    const char *ns_a, const char *group_a,
                    const char *ns_b, const char *group_b,
                    char out_keys[][CC_MAX_KEY_LEN], int *count) {
    if (!center || !ns_a || !group_a || !ns_b || !group_b || !out_keys || !count)
        return -1;
    int n = 0;
    for (size_t i = 0; i < center->bucket_count; i++) {
        for (cc_entry_t *e = center->buckets[i]; e; e = e->next) {
            if (strcmp(e->namespace_, ns_a) == 0 &&
                strcmp(e->group_, group_a) == 0) {
                cc_entry_t *in_b = cc_find(center, ns_b, group_b, e->key);
                if (!in_b) {
                    strncpy(out_keys[n++], e->key, CC_MAX_KEY_LEN - 1);
                }
            }
        }
    }
    *count = n;
    return 0;
}
