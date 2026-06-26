#ifndef JOB_DAG_H
#define JOB_DAG_H

#include <stdint.h>
#include <time.h>

/*
 * L2 Core Concept: Directed Acyclic Graph (DAG) for job dependency scheduling.
 * Jobs form vertices; edges represent "must complete before" constraints.
 *
 * L4 Standards/Theorems:
 *   - A DAG has at least one topological ordering iff it contains no cycles.
 *   - Kahn's Theorem: A graph's nodes can be topologically sorted by repeatedly
 *     removing nodes with in-degree 0.
 *   - Cycle detection via DFS back-edge detection: O(V+E) time complexity.
 *
 * L5 Algorithms:
 *   - Kahn's algorithm (BFS-based) for topological sort: O(V+E)
 *   - DFS-based topological sort (Tarjan's alternative): O(V+E)
 *   - DFS back-edge cycle detection
 *
 * Reference:
 *   - MIT 6.006: Introduction to Algorithms, Lecture 14 (Topological Sort)
 *   - CMU 15-210: Parallel DAG scheduling
 *   - Kahn, A.B. (1962) "Topological sorting of large networks"
 */

#define JDAG_MAX_NODES    128
#define JDAG_MAX_EDGES    512
#define JDAG_MAX_NAME      64

/* Job states in DAG execution */
typedef enum {
    JDAG_NODE_PENDING   = 0,
    JDAG_NODE_READY     = 1,
    JDAG_NODE_RUNNING   = 2,
    JDAG_NODE_COMPLETED = 3,
    JDAG_NODE_FAILED    = 4,
    JDAG_NODE_SKIPPED   = 5
} jdag_node_state_t;

/* DAG node representing a single job vertex */
typedef struct jdag_node_t {
    uint64_t          id;
    char              name[JDAG_MAX_NAME];
    jdag_node_state_t state;
    int               indegree;
    int               outdegree;
    int               in_edges[JDAG_MAX_NODES];
    int               in_count;
    int               out_edges[JDAG_MAX_NODES];
    int               out_count;
    int               priority;
    time_t            scheduled_at;
    time_t            started_at;
    time_t            completed_at;
    void             *userdata;
} jdag_node_t;

/* DAG edge: parent -> child dependency */
typedef struct {
    int from;
    int to;
} jdag_edge_t;

/* Opaque DAG structure */
typedef struct jdag_t jdag_t;

/* Callback for executing a node when it becomes ready */
typedef void (*jdag_execute_fn)(jdag_node_t *node, void *ctx);

/* DAG creation and lifecycle */
jdag_t *jdag_create(int max_nodes);
void    jdag_destroy(jdag_t *dag);

/* Node management */
int  jdag_add_node(jdag_t *dag, uint64_t id, const char *name, int priority);
int  jdag_remove_node(jdag_t *dag, uint64_t id);
int  jdag_find_node(const jdag_t *dag, uint64_t id);

/* Edge management (dependency creation) */
int  jdag_add_edge(jdag_t *dag, uint64_t from_id, uint64_t to_id);
int  jdag_remove_edge(jdag_t *dag, uint64_t from_id, uint64_t to_id);

/* Topological sort via Kahn's algorithm (BFS-based, O(V+E)) */
int  jdag_topological_sort_kahn(const jdag_t *dag, uint64_t *result, int max);

/* Topological sort via DFS post-order (O(V+E)) */
int  jdag_topological_sort_dfs(const jdag_t *dag, uint64_t *result, int max);

/* Cycle detection via DFS back-edge detection */
int  jdag_has_cycle(const jdag_t *dag);

/* Find all nodes with indegree 0 (ready to execute) */
int  jdag_get_ready_nodes(const jdag_t *dag, uint64_t *result, int max);

/* Mark a node as completed; decrements successors' indegree */
int  jdag_mark_completed(jdag_t *dag, uint64_t id);
int  jdag_mark_failed(jdag_t *dag, uint64_t id);

/* DAG traversal utilities */
int  jdag_node_count(const jdag_t *dag);
int  jdag_edge_count(const jdag_t *dag);
int  jdag_get_all_nodes(const jdag_t *dag, jdag_node_t *out, int max);

/* Critical path length (longest path in DAG, determines minimum make-span) */
int  jdag_critical_path_length(const jdag_t *dag);

/* Level-order BFS: groups nodes by maximum distance from any source */
int  jdag_get_levels(const jdag_t *dag, uint64_t *result, int max, int *level_boundaries);

/* Detect if adding edge (from->to) would create a cycle (safety check) */
int  jdag_would_create_cycle(const jdag_t *dag, uint64_t from_id, uint64_t to_id);

#endif
