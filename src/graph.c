/*
 * graph.c - YakirOS graph resolution engine implementation
 */

#define _GNU_SOURCE
#include "graph.h"
#include "component.h"
#include "capability.h"
#include "log.h"
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int graph_resolve(void) {
    int changes = 0;

    for (int i = 0; i < n_components; i++) {
        component_t *comp = &components[i];

        switch (comp->state) {
        case COMP_INACTIVE:
            if (requirements_met(comp)) {
                if (component_start(i) == 0) {
                    changes++;
                }
            }
            break;

        case COMP_READY_WAIT:
            /* Check if dependencies were lost while waiting for readiness */
            if (!requirements_met(comp)) {
                LOG_WARN("component '%s' dependencies lost while waiting for readiness", comp->name);
                comp->state = COMP_FAILED;
                if (comp->pid > 0) {
                    kill(comp->pid, SIGTERM);
                }
                changes++;
            }
            break;

        case COMP_ACTIVE:
            /* Check if dependencies were lost */
            if (!requirements_met(comp)) {
                comp->state = COMP_FAILED;
                for (int j = 0; j < comp->n_provides; j++) {
                    capability_withdraw(comp->provides[j]);
                }
                changes++;
            }
            break;

        case COMP_FAILED:
            /* Try to restart failed components if their dependencies are now met */
            if (requirements_met(comp)) {
                /* Only restart if enough time has passed since last restart */
                time_t now = time(NULL);
                if (now - comp->last_restart >= 5) { /* 5 second minimum between restarts */
                    LOG_INFO("attempting to restart failed component '%s'", comp->name);
                    comp->state = COMP_INACTIVE; /* Will be started on next iteration */
                    changes++;
                }
            }
            break;

        default:
            break;
        }
    }

    return changes;
}

void graph_resolve_full(void) {
    int iterations = 0;
    int changes;

    do {
        changes = graph_resolve();
        iterations++;
        if (iterations > n_components * 2) {
            LOG_ERR("graph resolution exceeded max iterations — possible cycle");
            break;
        }
    } while (changes > 0);

    LOG_INFO("graph stable after %d iterations (%d components, %d capabilities)",
             iterations, n_components, capability_count());
}

/*
 * Cycle Detection and Graph Analysis Implementation
 */

/* Build adjacency list representing component dependencies */
static int build_dependency_graph(int **adjacency_matrix) {
    /* Allocate adjacency matrix */
    *adjacency_matrix = calloc(n_components * n_components, sizeof(int));
    if (!*adjacency_matrix) {
        LOG_ERR("failed to allocate adjacency matrix");
        return -1;
    }

    /* For each component, find which other components it depends on */
    for (int i = 0; i < n_components; i++) {
        component_t *comp = &components[i];

        /* Check each required capability */
        for (int req_idx = 0; req_idx < comp->n_requires; req_idx++) {
            const char *required_cap = comp->requires[req_idx];

            /* Find which component provides this capability by iterating through all components */
            for (int j = 0; j < n_components; j++) {
                component_t *provider = &components[j];

                /* Check if component j provides the required capability */
                for (int prov_idx = 0; prov_idx < provider->n_provides; prov_idx++) {
                    if (strcmp(provider->provides[prov_idx], required_cap) == 0) {
                        /* Component i depends on component j */
                        (*adjacency_matrix)[i * n_components + j] = 1;
                        /* LOG_INFO("dependency detected: %s -> %s (via capability %s)",
                                comp->name, provider->name, required_cap); */
                        break;
                    }
                }
            }
        }
    }

    /* Debug: log the dependency matrix */
    /* LOG_INFO("dependency matrix for %d components:", n_components);
    for (int i = 0; i < n_components; i++) {
        for (int j = 0; j < n_components; j++) {
            if ((*adjacency_matrix)[i * n_components + j]) {
                LOG_INFO("  %s depends on %s", components[i].name, components[j].name);
            }
        }
    } */

    return 0;
}

/* DFS helper for cycle detection */
static int dfs_cycle_detect(int component_idx, dfs_color_t *colors, int *parent,
                           int *adjacency_matrix, cycle_info_t *cycle_info,
                           int *path, int path_len) {
    colors[component_idx] = DFS_GRAY;
    path[path_len] = component_idx;
    path_len++;

    /* Explore all adjacent components (dependencies) */
    for (int j = 0; j < n_components; j++) {
        if (adjacency_matrix[component_idx * n_components + j]) {
            if (colors[j] == DFS_GRAY) {
                /* Back edge found - cycle detected! */
                LOG_WARN("cycle detected involving component %s -> %s",
                        components[component_idx].name, components[j].name);

                /* Build cycle information */
                cycle_info->cycle_length = 0;
                cycle_info->cycle_components = malloc(n_components * sizeof(int));

                /* Extract the cycle from the path */
                int cycle_start = -1;
                for (int k = 0; k < path_len; k++) {
                    if (path[k] == j) {
                        cycle_start = k;
                        break;
                    }
                }

                if (cycle_start >= 0) {
                    for (int k = cycle_start; k < path_len; k++) {
                        cycle_info->cycle_components[cycle_info->cycle_length++] = path[k];
                    }

                    /* Add the closing component to complete the cycle */
                    cycle_info->cycle_components[cycle_info->cycle_length++] = j;

                    /* Build human-readable error message */
                    snprintf(cycle_info->error_message, sizeof(cycle_info->error_message),
                            "Dependency cycle detected: ");
                    for (int k = 0; k < cycle_info->cycle_length - 1; k++) {
                        int comp_idx = cycle_info->cycle_components[k];
                        strncat(cycle_info->error_message, components[comp_idx].name,
                               sizeof(cycle_info->error_message) - strlen(cycle_info->error_message) - 1);
                        strncat(cycle_info->error_message, " -> ",
                               sizeof(cycle_info->error_message) - strlen(cycle_info->error_message) - 1);
                    }
                    int last_idx = cycle_info->cycle_components[cycle_info->cycle_length - 1];
                    strncat(cycle_info->error_message, components[last_idx].name,
                           sizeof(cycle_info->error_message) - strlen(cycle_info->error_message) - 1);
                }

                return 1; /* Cycle found */
            } else if (colors[j] == DFS_WHITE) {
                parent[j] = component_idx;
                if (dfs_cycle_detect(j, colors, parent, adjacency_matrix,
                                   cycle_info, path, path_len) == 1) {
                    return 1; /* Cycle found in recursive call */
                }
            }
        }
    }

    colors[component_idx] = DFS_BLACK;
    return 0; /* No cycle found */
}

int graph_detect_cycles(cycle_info_t *cycle_info) {
    if (!cycle_info) {
        LOG_ERR("cycle_info parameter is NULL");
        return -1;
    }

    /* Initialize cycle info */
    cycle_info->cycle_components = NULL;
    cycle_info->cycle_length = 0;
    memset(cycle_info->error_message, 0, sizeof(cycle_info->error_message));

    if (n_components == 0) {
        return 0; /* No components, no cycles */
    }

    /* Build dependency graph */
    int *adjacency_matrix = NULL;
    if (build_dependency_graph(&adjacency_matrix) < 0) {
        return -1;
    }

    /* Initialize DFS state */
    dfs_color_t *colors = calloc(n_components, sizeof(dfs_color_t));
    int *parent = calloc(n_components, sizeof(int));
    int *path = malloc(n_components * sizeof(int));

    if (!colors || !parent || !path) {
        LOG_ERR("failed to allocate memory for cycle detection");
        free(adjacency_matrix);
        free(colors);
        free(parent);
        free(path);
        return -1;
    }

    /* Initialize all components as unvisited */
    for (int i = 0; i < n_components; i++) {
        colors[i] = DFS_WHITE;
        parent[i] = -1;
    }

    /* Run DFS from each unvisited component */
    for (int i = 0; i < n_components; i++) {
        if (colors[i] == DFS_WHITE) {
            if (dfs_cycle_detect(i, colors, parent, adjacency_matrix,
                               cycle_info, path, 0) == 1) {
                /* Cycle found */
                free(adjacency_matrix);
                free(colors);
                free(parent);
                free(path);
                return 1;
            }
        }
    }

    /* Cleanup */
    free(adjacency_matrix);
    free(colors);
    free(parent);
    free(path);

    return 0; /* No cycles found */
}

int graph_topological_sort(int *sorted_components, int max_components) {
    if (!sorted_components) {
        LOG_ERR("sorted_components parameter is NULL");
        return -1;
    }

    if (max_components < n_components) {
        LOG_ERR("max_components (%d) is less than n_components (%d)",
                max_components, n_components);
        return -1;
    }

    /* First check for cycles */
    cycle_info_t cycle_info;
    if (graph_detect_cycles(&cycle_info) == 1) {
        LOG_ERR("cannot perform topological sort: graph contains cycles");
        free(cycle_info.cycle_components);
        return -1;
    }

    /* Build dependency graph */
    int *adjacency_matrix = NULL;
    if (build_dependency_graph(&adjacency_matrix) < 0) {
        return -1;
    }

    /* Calculate in-degrees */
    int *in_degree = calloc(n_components, sizeof(int));
    if (!in_degree) {
        LOG_ERR("failed to allocate memory for in-degree calculation");
        free(adjacency_matrix);
        return -1;
    }

    for (int i = 0; i < n_components; i++) {
        for (int j = 0; j < n_components; j++) {
            if (adjacency_matrix[i * n_components + j]) {
                in_degree[j]++;
            }
        }
    }

    /* Kahn's algorithm for topological sorting */
    int *queue = malloc(n_components * sizeof(int));
    int queue_front = 0, queue_rear = 0;
    int sorted_count = 0;

    if (!queue) {
        LOG_ERR("failed to allocate memory for topological sort");
        free(adjacency_matrix);
        free(in_degree);
        return -1;
    }

    /* Add all components with in-degree 0 to queue */
    for (int i = 0; i < n_components; i++) {
        if (in_degree[i] == 0) {
            queue[queue_rear++] = i;
        }
    }

    /* Process queue */
    while (queue_front < queue_rear) {
        int current = queue[queue_front++];
        sorted_components[sorted_count++] = current;

        /* Update in-degrees of adjacent components */
        for (int j = 0; j < n_components; j++) {
            if (adjacency_matrix[current * n_components + j]) {
                in_degree[j]--;
                if (in_degree[j] == 0) {
                    queue[queue_rear++] = j;
                }
            }
        }
    }

    free(adjacency_matrix);
    free(in_degree);
    free(queue);

    if (sorted_count != n_components) {
        LOG_ERR("topological sort failed: only sorted %d out of %d components",
                sorted_count, n_components);
        return -1;
    }

    return 0; /* Success */
}

int graph_validate_component_addition(const char *component_name) {
    /* For now, validate by checking if the current graph has cycles.
     * TODO: In a complete implementation, we would temporarily add the
     * component to the graph and test for cycles, but this requires
     * parsing the component file and simulating the addition.
     */
    if (!component_name) {
        LOG_ERR("component_name parameter is NULL");
        return -1;
    }

    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    if (result == 1) {
        LOG_WARN("current graph has cycles that would be problematic when adding component '%s'",
                component_name);
        LOG_WARN("cycle details: %s", cycle_info.error_message);
        free(cycle_info.cycle_components);
        return -1;
    }

    if (result < 0) {
        LOG_ERR("failed to perform cycle detection for component addition validation");
        return -1;
    }

    LOG_INFO("component '%s' can be safely added (no existing cycles detected)",
            component_name);
    return 0; /* Component addition would be valid */
}

int graph_find_strongly_connected_components(int **scc_components, int *scc_count) {
    /* Placeholder for Tarjan's or Kosaraju's SCC algorithm */
    if (!scc_components || !scc_count) {
        return -1;
    }

    *scc_count = 0;
    *scc_components = NULL;

    LOG_INFO("strongly connected component detection not yet implemented");
    return 0;
}

int graph_find_dependency_path(const char *from_capability, const char *to_capability,
                               char *path_description, int max_description_len) {
    /* Placeholder for dependency path finding using BFS */
    if (!from_capability || !to_capability || !path_description) {
        return -1;
    }

    snprintf(path_description, max_description_len,
            "Dependency path finding from '%s' to '%s' not yet implemented",
            from_capability, to_capability);

    return 0;
}

int graph_analyze_metrics(graph_metrics_t *metrics) {
    if (!metrics) {
        return -1;
    }

    memset(metrics, 0, sizeof(graph_metrics_t));

    /* Basic metrics */
    metrics->total_components = n_components;
    metrics->total_capabilities = capability_count();

    /* Calculate dependencies and edges */
    int total_dependencies = 0;
    for (int i = 0; i < n_components; i++) {
        total_dependencies += components[i].n_requires;
    }

    metrics->total_edges = total_dependencies;
    metrics->average_dependencies_per_component =
        n_components > 0 ? (double)total_dependencies / n_components : 0.0;

    /* TODO: Implement max_dependency_depth and strongly_connected_components */
    metrics->max_dependency_depth = 0;
    metrics->strongly_connected_components = 0;

    return 0;
}