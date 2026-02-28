/*
 * cgroup.h - YakirOS cgroup v2 resource management
 *
 * Provides cgroup creation, resource limit enforcement, and cleanup
 * for component process isolation and resource control.
 */

#ifndef CGROUP_H
#define CGROUP_H

#include <sys/types.h>
#include "toml.h"  /* for component_t definition */

#define CGROUP_ROOT "/sys/fs/cgroup/graph"
#define CGROUP_MOUNT_POINT "/sys/fs/cgroup"

/* Initialize cgroup subsystem */
int cgroup_init(void);

/* Create cgroup for a component */
int cgroup_create(const char *component_name, const char *cgroup_path);

/* Add process to cgroup */
int cgroup_add_process(const char *cgroup_path, pid_t pid);

/* Apply resource limits to cgroup */
int cgroup_set_memory_max(const char *cgroup_path, const char *limit);
int cgroup_set_memory_high(const char *cgroup_path, const char *limit);
int cgroup_set_cpu_weight(const char *cgroup_path, int weight);
int cgroup_set_cpu_max(const char *cgroup_path, const char *limit);
int cgroup_set_io_weight(const char *cgroup_path, int weight);
int cgroup_set_pids_max(const char *cgroup_path, int limit);

/* Apply all resource limits from component configuration */
int cgroup_apply_limits(const char *cgroup_path, const component_t *comp);

/* Monitor cgroup for OOM events */
int cgroup_setup_oom_monitor(const char *cgroup_path);
int cgroup_check_oom_events(const char *cgroup_path);

/* Clean up cgroup when component exits */
int cgroup_cleanup(const char *cgroup_path);

/* Namespace isolation functions */
int isolation_setup_namespaces(const component_t *comp);
int isolation_setup_mount_namespace(void);
int isolation_setup_hostname(const char *hostname);
int isolation_parse_namespaces(const char *namespaces_str);

/* Utility functions */
int cgroup_exists(const char *cgroup_path);
char *cgroup_build_path(const char *cgroup_path);

#endif /* CGROUP_H */