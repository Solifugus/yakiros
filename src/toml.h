/*
 * toml.h - Minimal TOML parser for YakirOS component declarations
 *
 * Supports the minimal TOML subset needed for component configuration:
 *   [section]
 *   key = "value"
 *   key = ["value1", "value2"]
 *   key = number
 */

#ifndef TOML_H
#define TOML_H

#include <signal.h>
#include <sys/types.h>
#include <time.h>

/* Maximum sizes for arrays and strings */
#define MAX_NAME 128
#define MAX_PATH 512
#define MAX_ARGS 32
#define MAX_DEPS 32

/* Component types */
typedef enum {
    COMP_TYPE_SERVICE,  /* long-running daemon */
    COMP_TYPE_ONESHOT,  /* run once, exit 0 = success */
} comp_type_t;

/* Component states */
typedef enum {
    COMP_INACTIVE,      /* requirements not met */
    COMP_STARTING,      /* process launching */
    COMP_READY_WAIT,    /* process launched, waiting for readiness signal */
    COMP_ACTIVE,        /* running and providing capabilities */
    COMP_DEGRADED,      /* running but health checks failing */
    COMP_FAILED,        /* crashed, readiness timeout, or other failure */
    COMP_ONESHOT_DONE,  /* oneshot completed successfully */
} comp_state_t;

/* Handoff types for hot-swap */
typedef enum {
    HANDOFF_NONE,
    HANDOFF_FD_PASSING,
    HANDOFF_STATE_FILE,
    HANDOFF_CHECKPOINT,
} handoff_t;

/* Readiness signaling methods */
typedef enum {
    READINESS_NONE,      /* no readiness check (immediate active) */
    READINESS_FILE,      /* monitor file creation */
    READINESS_SIGNAL,    /* wait for signal from component */
    READINESS_COMMAND,   /* run health check command */
} readiness_method_t;

/* Component structure - populated by TOML parser */
typedef struct {
    char name[MAX_NAME];
    char binary[MAX_PATH];
    char args[MAX_ARGS][MAX_NAME];
    int  argc;
    char config_path[MAX_PATH];  /* path to the .toml file */

    comp_type_t  type;
    comp_state_t state;
    handoff_t    handoff;

    /* Dependencies */
    char requires[MAX_DEPS][MAX_NAME];
    int  n_requires;

    char provides[MAX_DEPS][MAX_NAME];
    int  n_provides;

    char optional[MAX_DEPS][MAX_NAME];
    int  n_optional;

    /* Process management */
    pid_t pid;
    int   restart_count;
    time_t last_restart;

    /* Lifecycle management */
    int      reload_signal;
    char     health_check[MAX_PATH];        /* path to health check script */
    int      health_interval;               /* health check interval in seconds */
    int      health_timeout;                /* health check timeout in seconds (default 10) */
    int      health_fail_threshold;         /* failures before entering DEGRADED (default 3) */
    int      health_restart_threshold;      /* failures before restarting (default 5) */

    /* Health check status */
    int      health_consecutive_failures;   /* current consecutive failure count */
    time_t   last_health_check;            /* timestamp of last health check */
    int      last_health_result;           /* 0=success, 1=failure, 2=timeout */

    /* Readiness protocol */
    readiness_method_t readiness_method;       /* which readiness method to use */
    char     readiness_file[MAX_PATH];         /* file to monitor for readiness */
    char     readiness_check[MAX_PATH];        /* command to check readiness */
    int      readiness_signal;                 /* signal number for readiness */
    int      readiness_timeout;                /* timeout in seconds (default 30) */
    int      readiness_interval;               /* check interval for health checks */
    time_t   ready_wait_start;                 /* when COMP_READY_WAIT state started */

    /* cgroup resource limits */
    char     cgroup_path[MAX_PATH];            /* cgroup path under /sys/fs/cgroup/graph/ */
    char     memory_max[32];                   /* memory.max limit (e.g., "64M") */
    char     memory_high[32];                  /* memory.high limit (e.g., "48M") */
    int      cpu_weight;                       /* cpu.weight (1-10000, default 100) */
    char     cpu_max[32];                      /* cpu.max limit (e.g., "50000 100000") */
    int      io_weight;                        /* io.weight (1-10000, default 100) */
    int      pids_max;                         /* pids.max limit (default 0 = no limit) */

    /* namespace isolation */
    char     isolation_namespaces[256];        /* comma-separated list: "mount,pid,net,uts,ipc" */
    char     isolation_root[MAX_PATH];         /* chroot/pivot_root target (default "/") */
    char     isolation_hostname[MAX_NAME];     /* hostname for UTS namespace */

    /* checkpoint configuration */
    int      checkpoint_enabled;               /* enable checkpoint support (default 0) */
    char     checkpoint_preserve_fds[256];     /* comma-separated list of FD types to preserve */
    int      checkpoint_leave_running;         /* leave process running during checkpoint (default 1) */
    int      checkpoint_memory_estimate;       /* expected RAM usage in MB for planning */
    int      checkpoint_max_age;               /* cleanup checkpoints older than this (hours) */
} component_t;

/* Parse a component TOML file, populate a component_t structure */
int parse_component(const char *path, component_t *comp);

#endif /* TOML_H */