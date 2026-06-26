#ifndef CONSISTENT_HASH_H
#define CONSISTENT_HASH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CH_MAX_NODE_LEN         128
#define CH_DEFAULT_VIRTUAL_NODES 150
#define CH_MAX_VIRTUAL_NODES    4096

/**
 * Consistent Hash Ring (L5: Algorithm)
 *
 * Implements Karger et al. "Consistent Hashing and Random Trees"
 * (STOC 1997). Uses virtual nodes for even key distribution.
 *
 * Key property: when a node is added/removed, only K/N keys
 * need to be remapped (where K=total keys, N=total nodes).
 * This is a substantial improvement over mod-N hashing
 * where all keys would need remapping.
 *
 * Complexity:
 *   Lookup: O(log V) where V = nodes * virtual_nodes
 *   Add/Remove node: O(V * log V)
 */
typedef struct ch_ring ch_ring_t;

/**
 * Node representation on the hash ring.
 */
typedef struct {
    char            name[CH_MAX_NODE_LEN];
    uint32_t        hash;
    int             is_virtual;
    int             physical_index;
} ch_node_t;

ch_ring_t  *ch_ring_create(int virtual_nodes_per_physical);
void        ch_ring_destroy(ch_ring_t *ring);

int         ch_ring_add_node(ch_ring_t *ring, const char *node_name);
int         ch_ring_remove_node(ch_ring_t *ring, const char *node_name);

/**
 * Find the node responsible for a given key.
 * Returns the node_name in out_node (must be at least CH_MAX_NODE_LEN).
 * Returns 0 on success, -1 if ring is empty.
 */
int         ch_ring_get_node(ch_ring_t *ring, const char *key,
                             char *out_node);

/**
 * Get N distinct responsible nodes for replication (N <= replica_count).
 * Useful for quorum writes with replication factor > 1.
 * Returns the number of nodes found (may be less than req_n if ring has fewer nodes).
 */
int         ch_ring_get_nodes(ch_ring_t *ring, const char *key,
                              int req_n, char out_nodes[][CH_MAX_NODE_LEN]);

int         ch_ring_size(ch_ring_t *ring);
int         ch_ring_virtual_size(ch_ring_t *ring);

/**
 * Get all physical node names.
 * Returns count of physical nodes.
 */
int         ch_ring_list_nodes(ch_ring_t *ring,
                               char nodes[][CH_MAX_NODE_LEN], int max_count);

/**
 * Rebalance: redistribute all virtual nodes. Used after topology changes.
 */
void        ch_ring_rebalance(ch_ring_t *ring);

#ifdef __cplusplus
}
#endif

#endif
