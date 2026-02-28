/*
 * cgroup.c - YakirOS cgroup v2 resource management implementation
 *
 * Manages cgroup creation, resource limits, and cleanup for component isolation.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sched.h>
#include <signal.h>

#include "cgroup.h"
#include "toml.h"
#include "log.h"

/* Check if cgroup v2 is mounted */
static int cgroup_is_mounted(void) {
    struct stat st;
    return (stat(CGROUP_MOUNT_POINT "/cgroup.controllers", &st) == 0);
}

/* Mount cgroup v2 filesystem */
static int cgroup_mount(void) {
    if (cgroup_is_mounted()) {
        return 0; /* already mounted */
    }

    /* Create mount point if needed */
    mkdir(CGROUP_MOUNT_POINT, 0755);

    /* Mount cgroup v2 */
    if (mount("cgroup2", CGROUP_MOUNT_POINT, "cgroup2", 0, NULL) < 0) {
        LOG_ERR("failed to mount cgroup v2: %s", strerror(errno));
        return -1;
    }

    LOG_INFO("mounted cgroup v2 at %s", CGROUP_MOUNT_POINT);
    return 0;
}

/* Initialize cgroup subsystem */
int cgroup_init(void) {
    /* Mount cgroup v2 if not already mounted */
    if (cgroup_mount() < 0) {
        return -1;
    }

    /* Create our root cgroup directory */
    struct stat st;
    if (stat(CGROUP_ROOT, &st) < 0) {
        if (mkdir(CGROUP_ROOT, 0755) < 0) {
            LOG_ERR("failed to create cgroup root %s: %s", CGROUP_ROOT, strerror(errno));
            return -1;
        }
        LOG_INFO("created cgroup root: %s", CGROUP_ROOT);
    }

    /* Enable memory, cpu, io, and pids controllers for our subtree */
    char subtree_control_path[512];
    snprintf(subtree_control_path, sizeof(subtree_control_path),
             "%s/cgroup.subtree_control", CGROUP_MOUNT_POINT);

    int fd = open(subtree_control_path, O_WRONLY);
    if (fd >= 0) {
        if (write(fd, "+memory +cpu +io +pids", 21) < 0) {
            LOG_WARN("failed to enable cgroup controllers at root: %s", strerror(errno));
        }
        close(fd);
    }

    /* Enable controllers for our graph subtree */
    snprintf(subtree_control_path, sizeof(subtree_control_path),
             "%s/cgroup.subtree_control", CGROUP_ROOT);

    fd = open(subtree_control_path, O_WRONLY);
    if (fd >= 0) {
        if (write(fd, "+memory +cpu +io +pids", 21) < 0) {
            LOG_WARN("failed to enable cgroup controllers for %s: %s", CGROUP_ROOT, strerror(errno));
        } else {
            LOG_INFO("enabled cgroup controllers for %s", CGROUP_ROOT);
        }
        close(fd);
    }

    return 0;
}

/* Build full cgroup path */
char *cgroup_build_path(const char *cgroup_path) {
    static char full_path[1024];

    if (cgroup_path[0] == '/') {
        /* Absolute path starting from cgroup root */
        snprintf(full_path, sizeof(full_path), "%s%s", CGROUP_ROOT, cgroup_path);
    } else {
        /* Relative path */
        snprintf(full_path, sizeof(full_path), "%s/%s", CGROUP_ROOT, cgroup_path);
    }

    return full_path;
}

/* Check if cgroup exists */
int cgroup_exists(const char *cgroup_path) {
    char *full_path = cgroup_build_path(cgroup_path);
    struct stat st;
    return (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode));
}

/* Create cgroup directory recursively */
static int mkdir_recursive(const char *path) {
    char tmp[1024];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }

    return mkdir(tmp, 0755);
}

/* Create cgroup for a component */
int cgroup_create(const char *component_name, const char *cgroup_path) {
    char *full_path;

    /* Use component name as path if no explicit path given */
    if (!cgroup_path || strlen(cgroup_path) == 0) {
        full_path = cgroup_build_path(component_name);
    } else {
        full_path = cgroup_build_path(cgroup_path);
    }

    /* Create directory if it doesn't exist */
    if (!cgroup_exists(cgroup_path ? cgroup_path : component_name)) {
        if (mkdir_recursive(full_path) < 0 && errno != EEXIST) {
            LOG_ERR("failed to create cgroup %s: %s", full_path, strerror(errno));
            return -1;
        }
        LOG_INFO("created cgroup: %s", full_path);
    }

    return 0;
}

/* Add process to cgroup */
int cgroup_add_process(const char *cgroup_path, pid_t pid) {
    char *full_path = cgroup_build_path(cgroup_path);
    char procs_path[1024];
    char pid_str[16];

    snprintf(procs_path, sizeof(procs_path), "%s/cgroup.procs", full_path);
    snprintf(pid_str, sizeof(pid_str), "%d", pid);

    int fd = open(procs_path, O_WRONLY);
    if (fd < 0) {
        LOG_ERR("failed to open %s: %s", procs_path, strerror(errno));
        return -1;
    }

    ssize_t written = write(fd, pid_str, strlen(pid_str));
    close(fd);

    if (written < 0) {
        LOG_ERR("failed to add pid %d to cgroup %s: %s", pid, cgroup_path, strerror(errno));
        return -1;
    }

    LOG_INFO("added pid %d to cgroup %s", pid, cgroup_path);
    return 0;
}

/* Write value to cgroup file */
static int cgroup_write_file(const char *cgroup_path, const char *filename, const char *value) {
    char *full_path = cgroup_build_path(cgroup_path);
    char file_path[1024];

    snprintf(file_path, sizeof(file_path), "%s/%s", full_path, filename);

    int fd = open(file_path, O_WRONLY);
    if (fd < 0) {
        LOG_ERR("failed to open %s: %s", file_path, strerror(errno));
        return -1;
    }

    ssize_t written = write(fd, value, strlen(value));
    close(fd);

    if (written < 0) {
        LOG_ERR("failed to write '%s' to %s: %s", value, file_path, strerror(errno));
        return -1;
    }

    LOG_INFO("set %s = %s", file_path, value);
    return 0;
}

/* Parse memory limit string (e.g., "64M" -> bytes) */
static long long parse_memory_limit(const char *limit_str) {
    if (!limit_str || strlen(limit_str) == 0) {
        return -1;
    }

    char *endptr;
    long long value = strtoll(limit_str, &endptr, 10);

    if (endptr == limit_str) {
        return -1; /* no digits */
    }

    /* Handle suffixes */
    if (*endptr) {
        switch (*endptr) {
            case 'K': case 'k': value *= 1024; break;
            case 'M': case 'm': value *= 1024 * 1024; break;
            case 'G': case 'g': value *= 1024 * 1024 * 1024; break;
            default:
                return -1; /* unknown suffix */
        }
    }

    return value;
}

/* Apply resource limits */
int cgroup_set_memory_max(const char *cgroup_path, const char *limit) {
    if (!limit || strlen(limit) == 0) {
        return 0; /* no limit specified */
    }

    long long bytes = parse_memory_limit(limit);
    if (bytes < 0) {
        LOG_ERR("invalid memory limit: %s", limit);
        return -1;
    }

    char bytes_str[32];
    snprintf(bytes_str, sizeof(bytes_str), "%lld", bytes);

    return cgroup_write_file(cgroup_path, "memory.max", bytes_str);
}

int cgroup_set_memory_high(const char *cgroup_path, const char *limit) {
    if (!limit || strlen(limit) == 0) {
        return 0; /* no limit specified */
    }

    long long bytes = parse_memory_limit(limit);
    if (bytes < 0) {
        LOG_ERR("invalid memory high limit: %s", limit);
        return -1;
    }

    char bytes_str[32];
    snprintf(bytes_str, sizeof(bytes_str), "%lld", bytes);

    return cgroup_write_file(cgroup_path, "memory.high", bytes_str);
}

int cgroup_set_cpu_weight(const char *cgroup_path, int weight) {
    if (weight <= 0) {
        return 0; /* no weight specified */
    }

    /* Clamp weight to valid range (1-10000) */
    if (weight < 1) weight = 1;
    if (weight > 10000) weight = 10000;

    char weight_str[16];
    snprintf(weight_str, sizeof(weight_str), "%d", weight);

    return cgroup_write_file(cgroup_path, "cpu.weight", weight_str);
}

int cgroup_set_cpu_max(const char *cgroup_path, const char *limit) {
    if (!limit || strlen(limit) == 0) {
        return 0; /* no limit specified */
    }

    return cgroup_write_file(cgroup_path, "cpu.max", limit);
}

int cgroup_set_io_weight(const char *cgroup_path, int weight) {
    if (weight <= 0) {
        return 0; /* no weight specified */
    }

    /* Clamp weight to valid range (1-10000) */
    if (weight < 1) weight = 1;
    if (weight > 10000) weight = 10000;

    char weight_str[16];
    snprintf(weight_str, sizeof(weight_str), "%d", weight);

    return cgroup_write_file(cgroup_path, "io.weight", weight_str);
}

int cgroup_set_pids_max(const char *cgroup_path, int limit) {
    if (limit <= 0) {
        return 0; /* no limit specified */
    }

    char limit_str[16];
    snprintf(limit_str, sizeof(limit_str), "%d", limit);

    return cgroup_write_file(cgroup_path, "pids.max", limit_str);
}

/* Apply all resource limits from component configuration */
int cgroup_apply_limits(const char *cgroup_path, const component_t *comp) {
    int ret = 0;

    if (cgroup_set_memory_max(cgroup_path, comp->memory_max) < 0) ret = -1;
    if (cgroup_set_memory_high(cgroup_path, comp->memory_high) < 0) ret = -1;
    if (cgroup_set_cpu_weight(cgroup_path, comp->cpu_weight) < 0) ret = -1;
    if (cgroup_set_cpu_max(cgroup_path, comp->cpu_max) < 0) ret = -1;
    if (cgroup_set_io_weight(cgroup_path, comp->io_weight) < 0) ret = -1;
    if (cgroup_set_pids_max(cgroup_path, comp->pids_max) < 0) ret = -1;

    return ret;
}

/* Setup OOM event monitoring */
int cgroup_setup_oom_monitor(const char *cgroup_path) {
    char *full_path = cgroup_build_path(cgroup_path);
    char events_path[1024];

    snprintf(events_path, sizeof(events_path), "%s/memory.events", full_path);

    /* Check if memory.events file exists */
    if (access(events_path, R_OK) < 0) {
        if (errno == ENOENT) {
            /* No memory controller available, not an error */
            return 0;
        }
        LOG_ERR("cannot access %s: %s", events_path, strerror(errno));
        return -1;
    }

    LOG_INFO("setup OOM monitoring for %s", cgroup_path);
    return 0;
}

/* Check for OOM events */
int cgroup_check_oom_events(const char *cgroup_path) {
    char *full_path = cgroup_build_path(cgroup_path);
    char events_path[1024];
    char line[256];
    FILE *f;
    long oom_kill_count = 0;

    snprintf(events_path, sizeof(events_path), "%s/memory.events", full_path);

    f = fopen(events_path, "r");
    if (!f) {
        if (errno != ENOENT) {
            LOG_WARN("failed to open %s: %s", events_path, strerror(errno));
        }
        return 0; /* No events or file doesn't exist */
    }

    /* Parse memory.events file */
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "oom_kill %ld", &oom_kill_count) == 1) {
            break;
        }
    }

    fclose(f);

    if (oom_kill_count > 0) {
        LOG_ERR("OOM kill detected in cgroup %s (count: %ld)", cgroup_path, oom_kill_count);
        return (int)oom_kill_count;
    }

    return 0; /* No OOM events */
}

/* Clean up cgroup when component exits */
int cgroup_cleanup(const char *cgroup_path) {
    char *full_path = cgroup_build_path(cgroup_path);

    /* Remove the cgroup directory (will only work if empty) */
    if (rmdir(full_path) < 0) {
        if (errno != ENOENT) {
            LOG_WARN("failed to remove cgroup %s: %s", full_path, strerror(errno));
            return -1;
        }
    } else {
        LOG_INFO("cleaned up cgroup: %s", full_path);
    }

    return 0;
}

/* Namespace isolation functions */

/* Parse namespaces string and return clone flags */
int isolation_parse_namespaces(const char *namespaces_str) {
    if (!namespaces_str || strlen(namespaces_str) == 0) {
        return 0; /* no namespaces requested */
    }

    int flags = 0;
    char *str_copy = strdup(namespaces_str);
    char *token = strtok(str_copy, ",");

    while (token != NULL) {
        /* Remove quotes if present */
        if (token[0] == '"') token++;
        if (token[strlen(token) - 1] == '"') token[strlen(token) - 1] = '\0';

        /* Trim whitespace */
        while (*token && (*token == ' ' || *token == '\t')) token++;
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) *end-- = '\0';

        if (strcmp(token, "mount") == 0 || strcmp(token, "mnt") == 0) {
            flags |= CLONE_NEWNS;
        } else if (strcmp(token, "pid") == 0) {
            flags |= CLONE_NEWPID;
        } else if (strcmp(token, "net") == 0) {
            flags |= CLONE_NEWNET;
        } else if (strcmp(token, "uts") == 0) {
            flags |= CLONE_NEWUTS;
        } else if (strcmp(token, "ipc") == 0) {
            flags |= CLONE_NEWIPC;
        } else if (strcmp(token, "user") == 0) {
            flags |= CLONE_NEWUSER;
        } else {
            LOG_WARN("unknown namespace type: %s", token);
        }

        token = strtok(NULL, ",");
    }

    free(str_copy);
    return flags;
}

/* Setup private mount namespace with /tmp */
int isolation_setup_mount_namespace(void) {
    /* Create a private /tmp for this component */
    if (mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777") < 0) {
        LOG_WARN("failed to mount private /tmp: %s", strerror(errno));
        return -1;
    }

    LOG_INFO("mounted private /tmp for component");
    return 0;
}

/* Set hostname in UTS namespace */
int isolation_setup_hostname(const char *hostname) {
    if (!hostname || strlen(hostname) == 0) {
        return 0; /* no hostname specified */
    }

    if (sethostname(hostname, strlen(hostname)) < 0) {
        LOG_WARN("failed to set hostname to %s: %s", hostname, strerror(errno));
        return -1;
    }

    LOG_INFO("set hostname to %s", hostname);
    return 0;
}

/* Setup all requested namespaces for a component */
int isolation_setup_namespaces(const component_t *comp) {
    int flags = isolation_parse_namespaces(comp->isolation_namespaces);

    if (flags == 0) {
        return 0; /* no namespaces requested */
    }

    /* Call unshare to create new namespaces */
    if (unshare(flags) < 0) {
        LOG_ERR("failed to create namespaces (flags=%d): %s", flags, strerror(errno));
        return -1;
    }

    LOG_INFO("created namespaces for %s (flags=%d)", comp->name, flags);

    /* Setup mount namespace if requested */
    if (flags & CLONE_NEWNS) {
        isolation_setup_mount_namespace();
    }

    /* Setup UTS namespace hostname if requested */
    if (flags & CLONE_NEWUTS) {
        isolation_setup_hostname(comp->isolation_hostname);
    }

    return 0;
}