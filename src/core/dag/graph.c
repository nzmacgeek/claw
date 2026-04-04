#include "dag.h"
#include "config.h"
#include "mem.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * Allocation
 * --------------------------------------------------------------------- */

dag_t *dag_new(void) {
    dag_t *g = mem_calloc(1, sizeof(dag_t));
    g->nodes      = hashmap_new();
    g->topo_order = vector_new();
    g->dirty      = 1;
    return g;
}

void dag_free(dag_t *g) {
    if (!g) return;

    /* Free each node but not the unit structs they reference */
    size_t count = 0;
    char **keys = hashmap_keys(g->nodes, &count);
    for (size_t i = 0; i < count; i++) {
        dag_node_t *n = hashmap_get(g->nodes, keys[i]);
        if (n) {
            free(n->name);
            /* Free string vectors (the strings themselves) */
            for (size_t j = 0; j < vector_length(n->hard_deps); j++)
                free(vector_get(n->hard_deps, j));
            vector_free_shell(n->hard_deps);

            for (size_t j = 0; j < vector_length(n->soft_deps); j++)
                free(vector_get(n->soft_deps, j));
            vector_free_shell(n->soft_deps);

            for (size_t j = 0; j < vector_length(n->dependents); j++)
                free(vector_get(n->dependents, j));
            vector_free_shell(n->dependents);

            free(n);
        }
    }
    free(keys);

    hashmap_free_shell(g->nodes);
    vector_free_shell(g->topo_order);
    free(g);
}

/* -----------------------------------------------------------------------
 * Node management
 * --------------------------------------------------------------------- */

int dag_add_node(dag_t *g, const char *name, unit_type_t type) {
    if (!g || !name) return -1;

    if (hashmap_has(g->nodes, name)) {
        log_debug("dag", "Node already exists: %s", name);
        return -1;
    }

    dag_node_t *n = mem_calloc(1, sizeof(dag_node_t));
    n->name       = mem_strdup(name);
    n->unit_type  = type;
    n->hard_deps  = vector_new();
    n->soft_deps  = vector_new();
    n->dependents = vector_new();
    n->color      = NODE_WHITE;
    n->topo_index = -1;

    hashmap_set(g->nodes, name, n);
    g->dirty = 1;
    return 0;
}

int dag_add_edge(dag_t *g, const char *from, const char *to, dep_type_t dep_type) {
    if (!g || !from || !to) return -1;

    dag_node_t *from_node = hashmap_get(g->nodes, from);
    dag_node_t *to_node   = hashmap_get(g->nodes, to);

    if (!from_node) {
        log_warning("dag", "Edge from unknown node: %s", from);
        return -1;
    }
    if (!to_node) {
        log_warning("dag", "Edge to unknown node: %s", to);
        return -1;
    }

    /* Add to appropriate dependency list (avoid duplicates) */
    vector_t *dep_list = (dep_type == DEP_SOFT) ? from_node->soft_deps
                                                 : from_node->hard_deps;
    int already = 0;
    for (size_t i = 0; i < vector_length(dep_list); i++) {
        if (strcmp(vector_get(dep_list, i), to) == 0) {
            already = 1;
            break;
        }
    }
    if (!already) vector_append(dep_list, mem_strdup(to));

    /* Record reverse edge */
    already = 0;
    for (size_t i = 0; i < vector_length(to_node->dependents); i++) {
        if (strcmp(vector_get(to_node->dependents, i), from) == 0) {
            already = 1;
            break;
        }
    }
    if (!already) vector_append(to_node->dependents, mem_strdup(from));

    g->dirty = 1;
    return 0;
}

dag_node_t *dag_get_node(dag_t *g, const char *name) {
    if (!g || !name) return NULL;
    return hashmap_get(g->nodes, name);
}

/* -----------------------------------------------------------------------
 * Cycle detection — tricolor DFS
 * --------------------------------------------------------------------- */

/* Reset all node colors to WHITE */
static void reset_colors(dag_t *g) {
    size_t count = 0;
    char **keys  = hashmap_keys(g->nodes, &count);
    for (size_t i = 0; i < count; i++) {
        dag_node_t *n = hashmap_get(g->nodes, keys[i]);
        if (n) n->color = NODE_WHITE;
    }
    free(keys);
}

/* Recursive DFS visit — returns 1 if cycle found */
static int dfs_visit(dag_t *g, dag_node_t *node,
                     char **cycle_a, char **cycle_b) {
    node->color = NODE_GRAY;

    /* Visit all hard dependencies */
    for (size_t i = 0; i < vector_length(node->hard_deps); i++) {
        const char *dep_name = vector_get(node->hard_deps, i);
        dag_node_t *dep      = hashmap_get(g->nodes, dep_name);

        if (!dep) continue;  /* Missing deps reported elsewhere */

        if (dep->color == NODE_GRAY) {
            /* Back edge — cycle found */
            if (cycle_a) *cycle_a = mem_strdup(node->name);
            if (cycle_b) *cycle_b = mem_strdup(dep->name);
            return 1;
        }
        if (dep->color == NODE_WHITE) {
            if (dfs_visit(g, dep, cycle_a, cycle_b)) return 1;
        }
    }

    node->color = NODE_BLACK;
    return 0;
}

int dag_detect_cycles(dag_t *g, char **cycle_a, char **cycle_b) {
    if (!g) return -1;

    reset_colors(g);

    size_t count = 0;
    char **keys  = hashmap_keys(g->nodes, &count);

    int found = 0;
    for (size_t i = 0; i < count && !found; i++) {
        dag_node_t *n = hashmap_get(g->nodes, keys[i]);
        if (n && n->color == NODE_WHITE) {
            found = dfs_visit(g, n, cycle_a, cycle_b);
        }
    }

    free(keys);
    return found ? -1 : 0;
}

/* -----------------------------------------------------------------------
 * Topological sort — iterative Kahn's algorithm
 * (avoids deep recursion that could exhaust stack in embedded env)
 * --------------------------------------------------------------------- */

int dag_topo_sort(dag_t *g) {
    if (!g) return -1;

    /* Check for cycles first */
    char *ca = NULL, *cb = NULL;
    if (dag_detect_cycles(g, &ca, &cb) != 0) {
        log_error("dag", "Cycle detected: %s <-> %s", ca, cb);
        free(ca); free(cb);
        return -1;
    }

    /* Build in-degree table */
    size_t  node_count = 0;
    char  **keys       = hashmap_keys(g->nodes, &node_count);
    hashmap_t *indegree = hashmap_with_capacity(node_count * 2);

    for (size_t i = 0; i < node_count; i++) {
        int *deg = mem_calloc(1, sizeof(int));
        *deg = 0;
        hashmap_set(indegree, keys[i], deg);
    }

    for (size_t i = 0; i < node_count; i++) {
        dag_node_t *n = hashmap_get(g->nodes, keys[i]);
        for (size_t j = 0; j < vector_length(n->hard_deps); j++) {
            const char *dep = vector_get(n->hard_deps, j);
            int *deg = hashmap_get(indegree, keys[i]);
            if (deg) (*deg)++;
            (void)dep;
        }
    }

    /* Enqueue nodes with in-degree 0 */
    vector_t *queue  = vector_new();
    vector_t *result = vector_new();

    for (size_t i = 0; i < node_count; i++) {
        int *deg = hashmap_get(indegree, keys[i]);
        if (deg && *deg == 0) {
            vector_append(queue, hashmap_get(g->nodes, keys[i]));
        }
    }

    while (!vector_is_empty(queue)) {
        dag_node_t *n = vector_remove(queue, 0);
        vector_append(result, n);
        n->topo_index = (int)vector_length(result) - 1;

        /* Reduce in-degree of dependents */
        for (size_t i = 0; i < vector_length(n->dependents); i++) {
            const char *dep_name = vector_get(n->dependents, i);
            int *deg = hashmap_get(indegree, dep_name);
            if (deg) {
                (*deg)--;
                if (*deg == 0) {
                    dag_node_t *dep_node = hashmap_get(g->nodes, dep_name);
                    if (dep_node) vector_append(queue, dep_node);
                }
            }
        }
    }

    /* Clean up in-degree map — free the int* values */
    for (size_t i = 0; i < node_count; i++) {
        int *deg = hashmap_remove(indegree, keys[i]);
        free(deg);
    }
    hashmap_free_shell(indegree);
    free(keys);

    /* Replace cached topo_order */
    vector_clear(g->topo_order);
    for (size_t i = 0; i < vector_length(result); i++) {
        vector_append(g->topo_order, vector_get(result, i));
    }
    vector_free_shell(result);
    vector_free_shell(queue);

    g->dirty = 0;
    log_debug("dag", "Topological sort complete: %zu nodes",
              vector_length(g->topo_order));
    return 0;
}

/* -----------------------------------------------------------------------
 * Ready nodes — hard deps all in active_set
 * --------------------------------------------------------------------- */

vector_t *dag_ready_nodes(dag_t *g, hashmap_t *active_set) {
    if (!g || !active_set) return NULL;

    vector_t *ready = vector_new();

    /* Iterate topo_order for stable ordering */
    vector_t *order = g->dirty ? NULL : g->topo_order;

    size_t       node_count = 0;
    char       **keys       = NULL;
    dag_node_t **node_list  = NULL;

    if (order && vector_length(order) > 0) {
        node_count = vector_length(order);
        node_list  = mem_calloc(node_count, sizeof(dag_node_t *));
        for (size_t i = 0; i < node_count; i++)
            node_list[i] = vector_get(order, i);
    } else {
        keys = hashmap_keys(g->nodes, &node_count);
        node_list = mem_calloc(node_count, sizeof(dag_node_t *));
        for (size_t i = 0; i < node_count; i++)
            node_list[i] = hashmap_get(g->nodes, keys[i]);
        free(keys);
    }

    for (size_t i = 0; i < node_count; i++) {
        dag_node_t *n = node_list[i];
        if (!n) continue;

        /* Skip already active nodes */
        if (hashmap_has(active_set, n->name)) continue;

        /* Check all hard deps are active */
        int all_satisfied = 1;
        for (size_t j = 0; j < vector_length(n->hard_deps); j++) {
            const char *dep = vector_get(n->hard_deps, j);
            if (!hashmap_has(active_set, dep)) {
                all_satisfied = 0;
                break;
            }
        }

        if (all_satisfied) vector_append(ready, n);
    }

    free(node_list);
    return ready;
}

/* -----------------------------------------------------------------------
 * Build DAG from a loaded config
 * --------------------------------------------------------------------- */

int dag_build_from_config(dag_t *g, const config_t *cfg) {
    if (!g || !cfg) return -1;

    /* Add all service nodes */
    size_t svc_count = 0;
    char **svc_keys  = hashmap_keys(cfg->services, &svc_count);
    for (size_t i = 0; i < svc_count; i++) {
        dag_add_node(g, svc_keys[i], UNIT_SERVICE);
    }
    free(svc_keys);

    /* Add all target nodes */
    size_t tgt_count = 0;
    char **tgt_keys  = hashmap_keys(cfg->targets, &tgt_count);
    for (size_t i = 0; i < tgt_count; i++) {
        dag_add_node(g, tgt_keys[i], UNIT_TARGET);
    }
    free(tgt_keys);

    /* Wire service edges */
    svc_keys = hashmap_keys(cfg->services, &svc_count);
    for (size_t i = 0; i < svc_count; i++) {
        const service_t *svc = hashmap_get(cfg->services, svc_keys[i]);
        if (!svc) continue;

        for (size_t j = 0; j < vector_length(svc->requires); j++)
            dag_add_edge(g, svc->name, vector_get(svc->requires, j), DEP_HARD);
        for (size_t j = 0; j < vector_length(svc->wants); j++)
            dag_add_edge(g, svc->name, vector_get(svc->wants, j), DEP_SOFT);
        for (size_t j = 0; j < vector_length(svc->after); j++)
            dag_add_edge(g, svc->name, vector_get(svc->after, j), DEP_ORDER);
        /* before: X means X must start after me — add reverse ordering edge */
        for (size_t j = 0; j < vector_length(svc->before); j++)
            dag_add_edge(g, vector_get(svc->before, j), svc->name, DEP_ORDER);
    }
    free(svc_keys);

    /* Wire target edges */
    tgt_keys = hashmap_keys(cfg->targets, &tgt_count);
    for (size_t i = 0; i < tgt_count; i++) {
        const target_t *tgt = hashmap_get(cfg->targets, tgt_keys[i]);
        if (!tgt) continue;

        for (size_t j = 0; j < vector_length(tgt->requires); j++)
            dag_add_edge(g, tgt->name, vector_get(tgt->requires, j), DEP_HARD);
        for (size_t j = 0; j < vector_length(tgt->wants); j++)
            dag_add_edge(g, tgt->name, vector_get(tgt->wants, j), DEP_SOFT);
        for (size_t j = 0; j < vector_length(tgt->after); j++)
            dag_add_edge(g, tgt->name, vector_get(tgt->after, j), DEP_ORDER);
        /* before: X means X starts after this target */
        for (size_t j = 0; j < vector_length(tgt->before); j++)
            dag_add_edge(g, vector_get(tgt->before, j), tgt->name, DEP_ORDER);
    }
    free(tgt_keys);

    log_info("dag", "Graph built: %zu services + %zu targets",
             svc_count, tgt_count);

    return dag_topo_sort(g);
}

/* -----------------------------------------------------------------------
 * Activation order — all transitive deps of a target, in topo order
 * --------------------------------------------------------------------- */

/* BFS to mark all nodes reachable via hard+soft deps from start */
static void collect_deps(dag_t *g, const char *name, hashmap_t *visited) {
    if (!name || hashmap_has(visited, name)) return;

    dag_node_t *n = hashmap_get(g->nodes, name);
    if (!n) return;

    hashmap_set(visited, name, n);

    for (size_t i = 0; i < vector_length(n->hard_deps); i++)
        collect_deps(g, vector_get(n->hard_deps, i), visited);
    for (size_t i = 0; i < vector_length(n->soft_deps); i++)
        collect_deps(g, vector_get(n->soft_deps, i), visited);
}

vector_t *dag_activation_order(dag_t *g, const char *target_name) {
    if (!g || !target_name) return NULL;

    if (!dag_get_node(g, target_name)) {
        log_warning("dag", "Unknown target for activation: %s", target_name);
        return NULL;
    }

    /* Collect all transitive dependencies (including the target itself) */
    hashmap_t *visited = hashmap_new();
    collect_deps(g, target_name, visited);

    /* Filter topo_order to only nodes in the visited set */
    vector_t *order = vector_new();
    for (size_t i = 0; i < vector_length(g->topo_order); i++) {
        dag_node_t *n = vector_get(g->topo_order, i);
        if (n && hashmap_has(visited, n->name))
            vector_append(order, n);
    }

    hashmap_free_shell(visited);
    return order;
}
