#include "job_dag.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * L3 Engineering Structure: DAG stored as adjacency list.
 * Each node maintains in_edges[] and out_edges[] as index arrays.
 *
 * L4 Theorem: Kahn's algorithm proves correct topological ordering.
 * A DAG has a topological ordering iff it has no directed cycles.
 * Topological sort uniquely determines a partial order of job execution.
 *
 * L5 Algorithm implementations:
 *   - kahn() : BFS-based, handles all nodes with indegree 0 simultaneously
 *   - dfs_topo() : DFS post-order, recursive
 *   - has_cycle() : DFS back-edge detection using color marking (WHITE/GRAY/BLACK)
 */

struct jdag_t {
    jdag_node_t nodes[JDAG_MAX_NODES];
    int         node_count;
    int         max_nodes;
    int         edge_count;
};

jdag_t *jdag_create(int max_nodes)
{
    jdag_t *dag = (jdag_t *)calloc(1, sizeof(*dag));
    if (!dag) return NULL;
    dag->max_nodes = (max_nodes > 0 && max_nodes <= JDAG_MAX_NODES)
                     ? max_nodes : JDAG_MAX_NODES;
    dag->node_count = 0;
    dag->edge_count = 0;
    return dag;
}

void jdag_destroy(jdag_t *dag)
{
    if (dag) free(dag);
}

int jdag_add_node(jdag_t *dag, uint64_t id, const char *name, int priority)
{
    if (!dag) return -1;
    if (dag->node_count >= dag->max_nodes) return -2;

    /* Check for duplicate ID */
    int i;
    for (i = 0; i < dag->node_count; i++)
        if (dag->nodes[i].id == id)
            return -3;

    jdag_node_t *node = &dag->nodes[dag->node_count];
    memset(node, 0, sizeof(*node));
    node->id       = id;
    node->priority = priority;
    node->state    = JDAG_NODE_PENDING;
    if (name)
        strncpy(node->name, name, JDAG_MAX_NAME - 1);
    else
        snprintf(node->name, JDAG_MAX_NAME, "node_%llu", (unsigned long long)id);
    node->name[JDAG_MAX_NAME - 1] = '\0';

    dag->node_count++;
    return dag->node_count - 1;
}

int jdag_find_node(const jdag_t *dag, uint64_t id)
{
    if (!dag) return -1;
    int i;
    for (i = 0; i < dag->node_count; i++)
        if (dag->nodes[i].id == id)
            return i;
    return -1;
}

int jdag_remove_node(jdag_t *dag, uint64_t id)
{
    int idx = jdag_find_node(dag, id);
    if (idx < 0) return -1;

    /* Remove all edges involving this node */
    int i;
    for (i = 0; i < dag->node_count; i++) {
        if (i == idx) continue;
        jdag_remove_edge(dag, dag->nodes[i].id, id);
        jdag_remove_edge(dag, id, dag->nodes[i].id);
    }

    /* Compact the array */
    dag->nodes[idx] = dag->nodes[dag->node_count - 1];
    dag->node_count--;

    /* Fix up edge references (index shifted) */
    for (i = 0; i < dag->node_count; i++) {
        int j;
        for (j = 0; j < dag->nodes[i].in_count; j++) {
            if (dag->nodes[i].in_edges[j] == dag->node_count)
                dag->nodes[i].in_edges[j] = idx;
        }
        for (j = 0; j < dag->nodes[i].out_count; j++) {
            if (dag->nodes[i].out_edges[j] == dag->node_count)
                dag->nodes[i].out_edges[j] = idx;
        }
    }

    return 0;
}

int jdag_add_edge(jdag_t *dag, uint64_t from_id, uint64_t to_id)
{
    if (!dag) return -1;

    int fi = jdag_find_node(dag, from_id);
    int ti = jdag_find_node(dag, to_id);
    if (fi < 0 || ti < 0) return -2;
    if (fi == ti) return -3;

    /* Check for duplicate edge */
    int i;
    for (i = 0; i < dag->nodes[fi].out_count; i++)
        if (dag->nodes[fi].out_edges[i] == ti)
            return -4;

    /* Safety: check if this would create a cycle */
    if (jdag_would_create_cycle(dag, from_id, to_id))
        return -5;

    if (dag->nodes[fi].out_count >= JDAG_MAX_NODES) return -6;
    if (dag->nodes[ti].in_count >= JDAG_MAX_NODES) return -6;

    dag->nodes[fi].out_edges[dag->nodes[fi].out_count++] = ti;
    dag->nodes[ti].in_edges[dag->nodes[ti].in_count++]   = fi;

    dag->nodes[fi].outdegree++;
    dag->nodes[ti].indegree++;
    dag->edge_count++;
    return 0;
}

int jdag_remove_edge(jdag_t *dag, uint64_t from_id, uint64_t to_id)
{
    if (!dag) return -1;

    int fi = jdag_find_node(dag, from_id);
    int ti = jdag_find_node(dag, to_id);
    if (fi < 0 || ti < 0) return -2;

    int i;
    for (i = 0; i < dag->nodes[fi].out_count; i++) {
        if (dag->nodes[fi].out_edges[i] == ti) {
            dag->nodes[fi].out_edges[i] =
                dag->nodes[fi].out_edges[dag->nodes[fi].out_count - 1];
            dag->nodes[fi].out_count--;
            dag->nodes[fi].outdegree--;
            break;
        }
    }

    for (i = 0; i < dag->nodes[ti].in_count; i++) {
        if (dag->nodes[ti].in_edges[i] == fi) {
            dag->nodes[ti].in_edges[i] =
                dag->nodes[ti].in_edges[dag->nodes[ti].in_count - 1];
            dag->nodes[ti].in_count--;
            dag->nodes[ti].indegree--;
            break;
        }
    }

    dag->edge_count--;
    return 0;
}

/*
 * Kahn's Topological Sort (L5 Algorithm)
 * Algorithm: BFS-based, O(V+E) time, O(V) space.
 *
 * Correctness (L4 Theorem): At each iteration, we remove a node with
 * indegree 0. Since the graph has no cycles, there is always at least one
 * such node. The order of removal gives a valid topological ordering.
 *
 * Steps:
 *   1. Compute indegree for all nodes
 *   2. Enqueue all nodes with indegree 0 into a FIFO queue
 *   3. While queue not empty:
 *      a. Dequeue node, append to result
 *      b. For each neighbor, decrement indegree
 *      c. If indegree becomes 0, enqueue it
 *   4. If not all nodes visited: graph has cycle
 *
 * Reference: Kahn, A.B. (1962), Communications of the ACM, 5(11):558-562.
 */
int jdag_topological_sort_kahn(const jdag_t *dag, uint64_t *result, int max)
{
    if (!dag || !result || max < dag->node_count) return -1;

    int  indegree[JDAG_MAX_NODES];
    int  queue[JDAG_MAX_NODES];
    int  qhead = 0, qtail = 0;
    int  visited = 0;
    int  i;

    /* Step 1: Compute indegree */
    for (i = 0; i < dag->node_count; i++)
        indegree[i] = dag->nodes[i].indegree;

    /* Step 2: Enqueue nodes with indegree 0 */
    for (i = 0; i < dag->node_count; i++)
        if (indegree[i] == 0)
            queue[qtail++] = i;

    /* Step 3: BFS processing */
    while (qhead < qtail) {
        int u = queue[qhead++];
        result[visited++] = dag->nodes[u].id;

        for (i = 0; i < dag->nodes[u].out_count; i++) {
            int v = dag->nodes[u].out_edges[i];
            indegree[v]--;
            if (indegree[v] == 0)
                queue[qtail++] = v;
        }
    }

    /* Step 4: Cycle check */
    if (visited != dag->node_count)
        return -2; /* graph has a cycle */

    return visited;
}

/*
 * DFS-based Topological Sort (L5 Algorithm)
 * Uses post-order traversal with color marking.
 * WHITE(0)=unvisited, GRAY(1)=in-progress, BLACK(2)=done.
 *
 * Tarjan's variation: push nodes onto result after all children processed.
 * O(V+E) time.
 */
static void dfs_visit(const jdag_t *dag, int u, int *color,
                      uint64_t *result, int *pos)
{
    int i;
    color[u] = 1; /* GRAY: in progress */
    for (i = 0; i < dag->nodes[u].out_count; i++) {
        int v = dag->nodes[u].out_edges[i];
        if (color[v] == 0)
            dfs_visit(dag, v, color, result, pos);
    }
    color[u] = 2; /* BLACK: done */
    result[--(*pos)] = dag->nodes[u].id;
}

int jdag_topological_sort_dfs(const jdag_t *dag, uint64_t *result, int max)
{
    if (!dag || !result || max < dag->node_count) return -1;

    int color[JDAG_MAX_NODES] = {0};
    int pos = dag->node_count;
    int i;

    for (i = 0; i < dag->node_count; i++)
        if (color[i] == 0)
            dfs_visit(dag, i, color, result, &pos);

    return dag->node_count;
}

/*
 * Cycle Detection via DFS back-edge (L5 Algorithm)
 * WHITE(0)=unvisited, GRAY(1)=on current path, BLACK(2)=fully processed.
 *
 * L4 Theorem: A directed graph contains a cycle iff a DFS discovers
 * a back edge (edge from current node to a GRAY node).
 *
 * Proof: A back edge u->v means v is an ancestor of u in the DFS tree,
 * forming a directed cycle u -> ... -> v -> ... -> u.
 */
static int dfs_cycle_detect(const jdag_t *dag, int u, int *color)
{
    int i;
    color[u] = 1; /* GRAY */
    for (i = 0; i < dag->nodes[u].out_count; i++) {
        int v = dag->nodes[u].out_edges[i];
        if (color[v] == 1)
            return 1; /* back edge → cycle */
        if (color[v] == 0)
            if (dfs_cycle_detect(dag, v, color))
                return 1;
    }
    color[u] = 2; /* BLACK */
    return 0;
}

int jdag_has_cycle(const jdag_t *dag)
{
    if (!dag || dag->node_count == 0) return 0;
    int color[JDAG_MAX_NODES] = {0};
    int i;
    for (i = 0; i < dag->node_count; i++)
        if (color[i] == 0)
            if (dfs_cycle_detect(dag, i, color))
                return 1;
    return 0;
}

int jdag_get_ready_nodes(const jdag_t *dag, uint64_t *result, int max)
{
    if (!dag || !result) return -1;
    int count = 0;
    int i;
    for (i = 0; i < dag->node_count && count < max; i++) {
        if (dag->nodes[i].state == JDAG_NODE_PENDING &&
            dag->nodes[i].indegree == 0) {
            result[count++] = dag->nodes[i].id;
        }
    }
    return count;
}

int jdag_mark_completed(jdag_t *dag, uint64_t id)
{
    int idx = jdag_find_node(dag, id);
    if (idx < 0) return -1;

    jdag_node_t *node = &dag->nodes[idx];
    node->state = JDAG_NODE_COMPLETED;
    node->completed_at = time(NULL);

    /* Decrement indegree of all successors */
    int i;
    for (i = 0; i < node->out_count; i++) {
        int succ = node->out_edges[i];
        if (dag->nodes[succ].indegree > 0)
            dag->nodes[succ].indegree--;
    }
    return 0;
}

int jdag_mark_failed(jdag_t *dag, uint64_t id)
{
    int idx = jdag_find_node(dag, id);
    if (idx < 0) return -1;
    dag->nodes[idx].state = JDAG_NODE_FAILED;

    /* Mark all downstream nodes as skipped (cascading failure) */
    int i;
    for (i = 0; i < dag->nodes[idx].out_count; i++) {
        int succ = dag->nodes[idx].out_edges[i];
        if (dag->nodes[succ].state == JDAG_NODE_PENDING)
            dag->nodes[succ].state = JDAG_NODE_SKIPPED;
    }
    return 0;
}

int jdag_node_count(const jdag_t *dag)
{
    return dag ? dag->node_count : 0;
}

int jdag_edge_count(const jdag_t *dag)
{
    return dag ? dag->edge_count : 0;
}

int jdag_get_all_nodes(const jdag_t *dag, jdag_node_t *out, int max)
{
    if (!dag || !out) return 0;
    int n = dag->node_count < max ? dag->node_count : max;
    memcpy(out, dag->nodes, n * sizeof(jdag_node_t));
    return n;
}

/*
 * Critical Path Length (L5 Algorithm)
 * The longest path from any source to any sink in the DAG.
 * Determines the minimum make-span (parallel execution lower bound).
 *
 * Algorithm: DP with topological order.
 *   dist[v] = max(dist[v], dist[u] + 1) for each edge u -> v
 *   Critical path length = max(dist[*])
 *
 * L4 Theorem: The critical path length is a lower bound on the makespan
 * of any scheduling of the DAG with unlimited parallelism.
 * (Related to Brent's theorem for parallel scheduling)
 *
 * O(V+E) time, O(V) space.
 */
int jdag_critical_path_length(const jdag_t *dag)
{
    if (!dag || dag->node_count == 0) return 0;

    int  dist[JDAG_MAX_NODES] = {0};
    int  indegree[JDAG_MAX_NODES];
    int  queue[JDAG_MAX_NODES];
    int  qhead = 0, qtail = 0;
    int  i, max_dist = 0;

    for (i = 0; i < dag->node_count; i++)
        indegree[i] = dag->nodes[i].indegree;

    /* Initialize sources */
    for (i = 0; i < dag->node_count; i++)
        if (indegree[i] == 0) {
            queue[qtail++] = i;
            dist[i] = 1;
        }

    /* DP with topological order */
    while (qhead < qtail) {
        int u = queue[qhead++];
        if (dist[u] > max_dist) max_dist = dist[u];
        for (i = 0; i < dag->nodes[u].out_count; i++) {
            int v = dag->nodes[u].out_edges[i];
            if (dist[u] + 1 > dist[v])
                dist[v] = dist[u] + 1;
            indegree[v]--;
            if (indegree[v] == 0)
                queue[qtail++] = v;
        }
    }

    return max_dist;
}

/*
 * Level-order BFS grouping
 * Groups nodes by their maximum distance from any source.
 * Produces level_boundaries array where [0..level_boundaries[0]) is level 0,
 * [level_boundaries[0]..level_boundaries[1]) is level 1, etc.
 *
 * Useful for parallel execution: all nodes in the same level can run
 * concurrently.
 */
int jdag_get_levels(const jdag_t *dag, uint64_t *result, int max,
                    int *level_boundaries)
{
    if (!dag || !result || max < dag->node_count) return -1;

    int  dist[JDAG_MAX_NODES] = {0};
    int  indegree[JDAG_MAX_NODES];
    int  queue[JDAG_MAX_NODES];
    int  qhead = 0, qtail = 0;
    int  i, max_level = 0;

    for (i = 0; i < dag->node_count; i++)
        indegree[i] = dag->nodes[i].indegree;

    for (i = 0; i < dag->node_count; i++)
        if (indegree[i] == 0) {
            queue[qtail++] = i;
            dist[i] = 0;
        }

    while (qhead < qtail) {
        int u = queue[qhead++];
        if (dist[u] > max_level) max_level = dist[u];
        for (i = 0; i < dag->nodes[u].out_count; i++) {
            int v = dag->nodes[u].out_edges[i];
            if (dist[u] + 1 > dist[v])
                dist[v] = dist[u] + 1;
            indegree[v]--;
            if (indegree[v] == 0)
                queue[qtail++] = v;
        }
    }

    /* Count nodes per level */
    int level_counts[JDAG_MAX_NODES] = {0};
    for (i = 0; i < dag->node_count; i++)
        level_counts[dist[i]]++;

    /* Build boundaries */
    int pos = 0;
    for (i = 0; i <= max_level; i++) {
        level_boundaries[i] = pos + level_counts[i];
        pos = level_boundaries[i];
    }

    /* Place nodes into result by level */
    int next_pos[JDAG_MAX_NODES];
    level_boundaries[0] = level_counts[0];
    for (i = 1; i <= max_level; i++)
        level_boundaries[i] = level_boundaries[i - 1] + level_counts[i];

    pos = 0;
    for (i = 0; i <= max_level; i++) {
        next_pos[i] = (i == 0) ? 0 : level_boundaries[i - 1];
    }

    for (i = 0; i < dag->node_count; i++) {
        int lev = dist[i];
        result[next_pos[lev]++] = dag->nodes[i].id;
    }

    return max_level + 1; /* number of levels */
}

/*
 * Cycle safety check: would adding edge (from_id -> to_id) create a cycle?
 * Uses DFS reachability: if DFS can reach from_id from to_id, adding
 * the edge would create a cycle.
 *
 * O(V+E) time. This is the standard "add edge safely" pattern used in
 * build systems (e.g., Make, Bazel) and workflow engines (e.g., Airflow).
 */
int jdag_would_create_cycle(const jdag_t *dag, uint64_t from_id, uint64_t to_id)
{
    if (!dag) return 0;
    int from_idx = jdag_find_node(dag, from_id);
    int to_idx   = jdag_find_node(dag, to_id);
    if (from_idx < 0 || to_idx < 0) return 0;
    if (from_idx == to_idx) return 1;

    /* BFS from to_idx: if from_idx is reachable, cycle would exist */
    int visited[JDAG_MAX_NODES] = {0};
    int queue[JDAG_MAX_NODES];
    int qhead = 0, qtail = 0;

    queue[qtail++] = to_idx;
    visited[to_idx] = 1;

    while (qhead < qtail) {
        int u = queue[qhead++];
        int i;
        for (i = 0; i < dag->nodes[u].out_count; i++) {
            int v = dag->nodes[u].out_edges[i];
            if (v == from_idx) return 1;
            if (!visited[v]) {
                visited[v] = 1;
                queue[qtail++] = v;
            }
        }
    }
    return 0;
}
