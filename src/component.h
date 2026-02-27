/*
 * component.h - YakirOS component lifecycle management
 *
 * Manages component states, dependencies, and process supervision.
 */

#ifndef COMPONENT_H
#define COMPONENT_H

#include "toml.h"
#include "capability.h"
#include "cgroup.h"

#define MAX_COMPONENTS 256
#define GRAPH_DIR "/etc/graph.d"

/* Check if a component's requirements are met */
int requirements_met(component_t *comp);

/* Start a component (fork/exec) */
int component_start(int idx);

/* Handle component process exit */
void component_exited(int idx, int status);

/* Check for readiness timeout and handle failure */
void check_readiness_timeout(int idx);

/* Mark component as ready and register capabilities */
void component_ready(int idx);

/* Check readiness for all components in READY_WAIT state */
void check_all_readiness(void);

/* Check health for all components with health checks enabled */
void check_all_health(void);

/* Check OOM events for all components with cgroups */
void check_all_oom_events(void);

/* Hot-swap upgrade a component to new version with three-level fallback */
int component_upgrade(const char *component_name);

/* Create checkpoint of a running component for backup/migration */
int component_checkpoint(const char *component_name);

/* Restore component from checkpoint (latest if checkpoint_id is NULL) */
int component_restore(const char *component_name, const char *checkpoint_id);

/* Load all component declarations from directory */
int load_components(const char *dir);

/* Validate component graph for cycles and other issues */
int validate_component_graph(int warn_only);

/* Register early kernel capabilities */
void register_early_capabilities(void);

/* Access to global component array */
extern component_t components[MAX_COMPONENTS];
extern int n_components;

#endif /* COMPONENT_H */