#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "job_dag.h"

static int tr, tp, tf;
#define T(n) do{tr++;printf("  TEST %s... ",n);}while(0)
#define P() do{tp++;printf("PASS\n");}while(0)
#define F(m) do{tf++;printf("FAIL: %s\n",m);return;}while(0)
#define C(c,m) do{if(!(c)){F(m);return;}}while(0)

static void test_create_empty_dag(void)
{
    T("create empty DAG");
    jdag_t *dag = jdag_create(16);
    C(dag != NULL, "create failed");
    C(jdag_node_count(dag) == 0, "empty node count");
    C(jdag_edge_count(dag) == 0, "empty edge count");
    C(jdag_has_cycle(dag) == 0, "empty DAG has no cycles");
    jdag_destroy(dag);
    P();
}

static void test_add_nodes(void)
{
    T("add nodes");
    jdag_t *dag = jdag_create(16);
    int i0 = jdag_add_node(dag, 1, "node1", 0);
    int i1 = jdag_add_node(dag, 2, "node2", 1);
    int i2 = jdag_add_node(dag, 3, "node3", 0);
    C(i0 >= 0 && i1 >= 0 && i2 >= 0, "nodes added");
    C(jdag_node_count(dag) == 3, "node count is 3");
    jdag_destroy(dag);
    P();
}

static void test_add_edge(void)
{
    T("add edges");
    jdag_t *dag = jdag_create(16);
    jdag_add_node(dag, 1, "A", 0);
    jdag_add_node(dag, 2, "B", 0);
    jdag_add_node(dag, 3, "C", 0);

    int r1 = jdag_add_edge(dag, 1, 2);
    int r2 = jdag_add_edge(dag, 1, 3);
    int r3 = jdag_add_edge(dag, 2, 3);
    C(r1 == 0 && r2 == 0 && r3 == 0, "edges added");
    C(jdag_edge_count(dag) == 3, "edge count is 3");
    jdag_destroy(dag);
    P();
}

static void test_topological_sort_kahn(void)
{
    T("Kahn topological sort");
    jdag_t *dag = jdag_create(16);
    jdag_add_node(dag, 10, "A", 0);
    jdag_add_node(dag, 20, "B", 0);
    jdag_add_node(dag, 30, "C", 0);
    jdag_add_edge(dag, 10, 20);
    jdag_add_edge(dag, 20, 30);

    uint64_t result[16];
    int n = jdag_topological_sort_kahn(dag, result, 16);
    C(n == 3, "should sort all 3 nodes");
    C(result[0] == 10, "first should be A");
    C(result[1] == 20, "second should be B");
    C(result[2] == 30, "third should be C");
    jdag_destroy(dag);
    P();
}

static void test_dfs_topo_sort(void)
{
    T("DFS topological sort");
    jdag_t *dag = jdag_create(16);
    jdag_add_node(dag, 1, "A", 0);
    jdag_add_node(dag, 2, "B", 0);
    jdag_add_edge(dag, 1, 2);

    uint64_t result[16];
    int n = jdag_topological_sort_dfs(dag, result, 16);
    C(n == 2, "should sort 2 nodes");
    C(result[0] == 1, "A before B");
    C(result[1] == 2, "B after A");
    jdag_destroy(dag);
    P();
}

static void test_cycle_detection(void)
{
    T("cycle detection");
    jdag_t *dag = jdag_create(16);
    jdag_add_node(dag, 1, "A", 0);
    jdag_add_node(dag, 2, "B", 0);
    jdag_add_node(dag, 3, "C", 0);
    jdag_add_edge(dag, 1, 2);
    jdag_add_edge(dag, 2, 3);
    /* Try to add edge that would create cycle */
    int r = jdag_add_edge(dag, 3, 1);
    C(r == -5, "should reject cycle-creating edge");
    C(jdag_has_cycle(dag) == 0, "DAG should still be acyclic");
    jdag_destroy(dag);
    P();
}

static void test_critical_path(void)
{
    T("critical path length");
    jdag_t *dag = jdag_create(16);
    jdag_add_node(dag, 1, "A", 0);
    jdag_add_node(dag, 2, "B", 0);
    jdag_add_node(dag, 3, "C", 0);
    jdag_add_node(dag, 4, "D", 0);
    jdag_add_edge(dag, 1, 2);
    jdag_add_edge(dag, 2, 3);
    jdag_add_edge(dag, 1, 4);
    jdag_add_edge(dag, 4, 3);

    int cpl = jdag_critical_path_length(dag);
    C(cpl == 3, "critical path should be 3 (1->2->3 or 1->4->3)");
    jdag_destroy(dag);
    P();
}

static void test_mark_completed(void)
{
    T("mark completed reduces indegree");
    jdag_t *dag = jdag_create(16);
    jdag_add_node(dag, 1, "A", 0);
    jdag_add_node(dag, 2, "B", 0);
    jdag_add_edge(dag, 1, 2);

    uint64_t ready[16];
    int rc = jdag_get_ready_nodes(dag, ready, 16);
    C(rc == 1 && ready[0] == 1, "only A is ready");

    jdag_mark_completed(dag, 1);
    rc = jdag_get_ready_nodes(dag, ready, 16);
    C(rc == 1 && ready[0] == 2, "B becomes ready after A completes");
    jdag_destroy(dag);
    P();
}

int main(void)
{
    tr = tp = tf = 0;
    printf("=== Job DAG Tests ===\n\n");
    test_create_empty_dag();
    test_add_nodes();
    test_add_edge();
    test_topological_sort_kahn();
    test_dfs_topo_sort();
    test_cycle_detection();
    test_critical_path();
    test_mark_completed();
    printf("\n=== Results: %d run, %d passed, %d failed ===\n", tr, tp, tf);
    return tf > 0 ? 1 : 0;
}
