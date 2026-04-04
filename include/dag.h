#ifndef __DAG_H__
#define __DAG_H__

#include "claw.h"
#include "vector.h"
#include "hashmap.h"
#include "config.h"

/* Tricolor DFS state for cycle detection */
typedef enum {
    NODE_WHITE = 0,   /* Unvisited */
    NODE_GRAY,        /* Currently on the DFS stack */
    NODE_BLACK        /* Fully processed */
} node_color_t;

typedef struct dag_node {
    char          *name;           /* Unit name (e.g. "sshd.service") */
    unit_type_t    unit_type;
    vector_t      *hard_deps;      /* Names of hard dependencies (requires/after) */
    vector_t      *soft_deps;      /* Names of soft dependencies (wants) */
    vector_t      *dependents;     /* Names of units that depend on this one */
    node_color_t   color;          /* Used during cycle detection */
    int            topo_index;     /* Position in sorted order (-1 = unsorted) */
} dag_node_t;

typedef struct {
    hashmap_t *nodes;              /* name → dag_node_t* */
    vector_t  *topo_order;         /* Cached topological sort (dag_node_t*) */
    int        dirty;              /* Non-zero if topo_order needs recompute */
} dag_t;

/* Create new graph */
dag_t *dag_new(void);

/* Free graph (does NOT free the unit structs pointed to by nodes) */
void dag_free(dag_t *g);

/* Add a named node; returns 0 on success, -1 if name already exists */
int dag_add_node(dag_t *g, const char *name, unit_type_t type);

/* Add a directed dependency edge from → to.
 * Both nodes must already exist.  dep_type controls hard vs soft. */
int dag_add_edge(dag_t *g, const char *from, const char *to, dep_type_t dep_type);

/* Get a node by name (returns NULL if not found) */
dag_node_t *dag_get_node(dag_t *g, const char *name);

/*
 * Detect cycles using tricolor DFS.
 * Returns 0 if acyclic, -1 if a cycle is found.
 * On cycle, writes the offending edge names to cycle_a / cycle_b
 * (if non-NULL, caller must free).
 */
int dag_detect_cycles(dag_t *g, char **cycle_a, char **cycle_b);

/*
 * Compute and cache topological order (respects all hard + soft edges).
 * Returns 0 on success, -1 if cycles detected.
 */
int dag_topo_sort(dag_t *g);

/*
 * Return nodes whose hard dependencies are all satisfied,
 * i.e. nodes ready to activate in parallel.
 * Caller must free the returned vector (contents are dag_node_t* — do not free them).
 */
vector_t *dag_ready_nodes(dag_t *g, hashmap_t *active_set);

/*
 * Populate a DAG from a loaded config.
 * Returns 0 on success.
 */
int dag_build_from_config(dag_t *g, const config_t *cfg);

/*
 * Return all nodes that must be activated to reach `target_name`,
 * in topological (dependency-first) order.
 *
 * Walks the transitive hard + soft dependencies of target_name,
 * then returns matching nodes filtered from topo_order.
 * Caller must free the vector shell (vector_free_shell); do not free contents.
 * Returns NULL if target_name is not found.
 */
vector_t *dag_activation_order(dag_t *g, const char *target_name);

#endif /* __DAG_H__ */
