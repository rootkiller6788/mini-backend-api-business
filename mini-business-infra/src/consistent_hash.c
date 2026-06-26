#include "consistent_hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Consistent Hashing Ring Implementation
 *
 * Reference: Karger et al., "Consistent Hashing and Random Trees"
 *            STOC 1997, pp. 654-663.
 *
 * Theorem: For N nodes each with V virtual replicas,
 * the standard deviation of key distribution is O(1/sqrt(V)).
 * With V=150, max deviation is typically less than 10% of mean.
 *
 * L4 (CAP Theorem): Consistent hashing minimizes data movement
 * on node changes, trading off perfect uniformity for
 * operational simplicity in partition-tolerant systems.
 */

typedef struct {
    uint32_t hash;
    int      physical_idx;
} ch_vnode_t;

struct ch_ring {
    int         virtual_count;
    int         physical_capacity;
    int         physical_count;
    char      **physical_names;
    ch_vnode_t *vnodes;
    int         vnode_count;
    int         vnode_capacity;
};

/* MurmurHash3 32-bit finalizer */
static uint32_t ch_mix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6bU;
    h ^= h >> 13;
    h *= 0xc2b2ae35U;
    h ^= h >> 16;
    return h;
}

/* FNV-1a hash for key to uint32 */
static uint32_t ch_hash_key(const char *key) {
    uint32_t h = 0x811c9dc5U;
    while (*key) {
        h ^= (uint32_t)(unsigned char)*key;
        h *= 0x01000193U;
        key++;
    }
    return ch_mix32(h);
}

/* derive virtual node hash from base hash + replica index */
static uint32_t ch_hash_vnode(uint32_t base, int replica) {
    uint32_t h = base;
    h ^= (uint32_t)replica * 0x9e3779b9U;
    return ch_mix32(h);
}

static int ch_vnode_cmp(const void *a, const void *b) {
    uint32_t ha = ((const ch_vnode_t *)a)->hash;
    uint32_t hb = ((const ch_vnode_t *)b)->hash;
    return (ha > hb) ? 1 : (ha < hb) ? -1 : 0;
}

/* binary search for the first vnode with hash at or above target */
static int ch_find_vnode(const ch_ring_t *ring, uint32_t hash) {
    if (ring->vnode_count == 0) return -1;
    int lo = 0, hi = ring->vnode_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (ring->vnodes[mid].hash < hash) lo = mid + 1;
        else hi = mid - 1;
    }
    return lo >= ring->vnode_count ? 0 : lo;
}

ch_ring_t *ch_ring_create(int virtual_nodes_per_physical) {
    ch_ring_t *r = (ch_ring_t *)calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->virtual_count = virtual_nodes_per_physical > 0
        ? virtual_nodes_per_physical : CH_DEFAULT_VIRTUAL_NODES;
    if (r->virtual_count > CH_MAX_VIRTUAL_NODES)
        r->virtual_count = CH_MAX_VIRTUAL_NODES;
    r->physical_capacity = 16;
    r->physical_names = (char **)calloc((size_t)r->physical_capacity, sizeof(char *));
    r->vnode_capacity = r->physical_capacity * r->virtual_count;
    r->vnodes = (ch_vnode_t *)calloc((size_t)r->vnode_capacity, sizeof(ch_vnode_t));
    if (!r->physical_names || !r->vnodes) {
        free(r->physical_names); free(r->vnodes); free(r); return NULL;
    }
    return r;
}

void ch_ring_destroy(ch_ring_t *ring) {
    if (!ring) return;
    for (int i = 0; i < ring->physical_count; i++) free(ring->physical_names[i]);
    free(ring->physical_names);
    free(ring->vnodes);
    free(ring);
}

int ch_ring_add_node(ch_ring_t *ring, const char *node_name) {
    if (!ring || !node_name) return -1;
    for (int i = 0; i < ring->physical_count; i++)
        if (strcmp(ring->physical_names[i], node_name) == 0) return 0;

    if (ring->physical_count >= ring->physical_capacity) {
        int new_cap = ring->physical_capacity * 2;
        char **new_names = (char **)realloc(ring->physical_names,
            (size_t)new_cap * sizeof(char *));
        if (!new_names) return -1;
        ring->physical_names = new_names;
        ring->physical_capacity = new_cap;
    }

    int idx = ring->physical_count;
    ring->physical_names[idx] = strdup(node_name);
    if (!ring->physical_names[idx]) return -1;
    ring->physical_count++;

    uint32_t base_hash = ch_hash_key(node_name);
    int needed = ring->vnode_count + ring->virtual_count;
    if (needed > ring->vnode_capacity) {
        ring->vnode_capacity = needed * 2;
        ch_vnode_t *new_v = (ch_vnode_t *)realloc(ring->vnodes,
            (size_t)ring->vnode_capacity * sizeof(ch_vnode_t));
        if (!new_v) return -1;
        ring->vnodes = new_v;
    }

    for (int v = 0; v < ring->virtual_count; v++) {
        ring->vnodes[ring->vnode_count].hash = ch_hash_vnode(base_hash, v);
        ring->vnodes[ring->vnode_count].physical_idx = idx;
        ring->vnode_count++;
    }

    qsort(ring->vnodes, (size_t)ring->vnode_count, sizeof(ch_vnode_t), ch_vnode_cmp);
    return 0;
}

int ch_ring_remove_node(ch_ring_t *ring, const char *node_name) {
    if (!ring || !node_name) return -1;
    int target_idx = -1;
    for (int i = 0; i < ring->physical_count; i++) {
        if (strcmp(ring->physical_names[i], node_name) == 0)
            { target_idx = i; break; }
    }
    if (target_idx < 0) return -1;

    free(ring->physical_names[target_idx]);
    for (int i = target_idx; i < ring->physical_count - 1; i++)
        ring->physical_names[i] = ring->physical_names[i + 1];
    ring->physical_count--;

    int w = 0;
    for (int i = 0; i < ring->vnode_count; i++) {
        if (ring->vnodes[i].physical_idx == target_idx) continue;
        if (ring->vnodes[i].physical_idx > target_idx)
            ring->vnodes[i].physical_idx--;
        ring->vnodes[w++] = ring->vnodes[i];
    }
    ring->vnode_count = w;

    qsort(ring->vnodes, (size_t)ring->vnode_count, sizeof(ch_vnode_t), ch_vnode_cmp);
    return 0;
}

int ch_ring_get_node(ch_ring_t *ring, const char *key, char *out_node) {
    if (!ring || !key || !out_node) return -1;
    if (ring->vnode_count == 0) return -1;
    uint32_t hash = ch_hash_key(key);
    int idx = ch_find_vnode(ring, hash);
    int pi = ring->vnodes[idx].physical_idx;
    strncpy(out_node, ring->physical_names[pi], CH_MAX_NODE_LEN - 1);
    out_node[CH_MAX_NODE_LEN - 1] = '\0';
    return 0;
}

int ch_ring_get_nodes(ch_ring_t *ring, const char *key,
                       int req_n, char out_nodes[][CH_MAX_NODE_LEN]) {
    if (!ring || !key || !out_nodes || req_n <= 0) return -1;
    if (ring->vnode_count == 0) return 0;
    if (req_n > ring->physical_count) req_n = ring->physical_count;

    uint32_t hash = ch_hash_key(key);
    int start = ch_find_vnode(ring, hash);

    int *seen = (int *)calloc((size_t)ring->physical_count, sizeof(int));
    if (!seen) return -1;
    int found = 0;

    for (int i = 0; i < ring->vnode_count && found < req_n; i++) {
        int vi = (start + i) % ring->vnode_count;
        int pi = ring->vnodes[vi].physical_idx;
        if (!seen[pi]) {
            seen[pi] = 1;
            strncpy(out_nodes[found], ring->physical_names[pi], CH_MAX_NODE_LEN - 1);
            out_nodes[found][CH_MAX_NODE_LEN - 1] = '\0';
            found++;
        }
    }

    free(seen);
    return found;
}

int ch_ring_size(ch_ring_t *ring) {
    return ring ? ring->physical_count : 0;
}

int ch_ring_virtual_size(ch_ring_t *ring) {
    return ring ? ring->vnode_count : 0;
}

int ch_ring_list_nodes(ch_ring_t *ring, char nodes[][CH_MAX_NODE_LEN],
                        int max_count) {
    if (!ring || !nodes) return -1;
    int n = ring->physical_count < max_count ? ring->physical_count : max_count;
    for (int i = 0; i < n; i++) {
        strncpy(nodes[i], ring->physical_names[i], CH_MAX_NODE_LEN - 1);
        nodes[i][CH_MAX_NODE_LEN - 1] = '\0';
    }
    return n;
}

void ch_ring_rebalance(ch_ring_t *ring) {
    if (!ring || ring->physical_count == 0) return;
    ring->vnode_count = 0;
    for (int i = 0; i < ring->physical_count; i++) {
        uint32_t base = ch_hash_key(ring->physical_names[i]);
        int needed = ring->vnode_count + ring->virtual_count;
        if (needed > ring->vnode_capacity) {
            ring->vnode_capacity = needed * 2;
            ring->vnodes = (ch_vnode_t *)realloc(ring->vnodes,
                (size_t)ring->vnode_capacity * sizeof(ch_vnode_t));
        }
        for (int v = 0; v < ring->virtual_count; v++) {
            ring->vnodes[ring->vnode_count].hash = ch_hash_vnode(base, v);
            ring->vnodes[ring->vnode_count].physical_idx = i;
            ring->vnode_count++;
        }
    }
    qsort(ring->vnodes, (size_t)ring->vnode_count, sizeof(ch_vnode_t), ch_vnode_cmp);
}
