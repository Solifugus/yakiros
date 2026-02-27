/*
 * graph.h - YakirOS graph resolution engine
 */

#ifndef GRAPH_H
#define GRAPH_H

/* Single pass graph resolution, returns number of state changes */
int graph_resolve(void);

/* Iterate graph resolution until stable */
void graph_resolve_full(void);

/* Cycle detection and graph analysis */

/* Cycle information structure */
typedef struct {
    int *cycle_components;    /* Array of component indices forming the cycle */
    int cycle_length;         /* Number of components in the cycle */
    char error_message[512];  /* Human-readable description of the cycle */
} cycle_info_t;

/* DFS colors for cycle detection */
typedef enum {
    DFS_WHITE = 0,  /* Unvisited */
    DFS_GRAY  = 1,  /* Currently being explored */
    DFS_BLACK = 2   /* Completely explored */
} dfs_color_t;

/* Detect dependency cycles in the component graph */
int graph_detect_cycles(cycle_info_t *cycle_info);

/* Perform topological sort of components, returns 0 on success, -1 if cycles detected */
int graph_topological_sort(int *sorted_components, int max_components);

/* Validate that adding a component wouldn't create cycles */
int graph_validate_component_addition(const char *component_name);

/* Find strongly connected components in the dependency graph */
int graph_find_strongly_connected_components(int **scc_components, int *scc_count);

/* Find dependency path between two capabilities */
int graph_find_dependency_path(const char *from_capability, const char *to_capability,
                               char *path_description, int max_description_len);

/* Get detailed graph analysis metrics */
typedef struct {
    int total_components;
    int total_capabilities;
    int max_dependency_depth;
    int strongly_connected_components;
    int total_edges;
    double average_dependencies_per_component;
} graph_metrics_t;

int graph_analyze_metrics(graph_metrics_t *metrics);

#endif /* GRAPH_H */