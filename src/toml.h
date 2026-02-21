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
    char     health_check[MAX_PATH];
    int      health_interval;

    /* Readiness protocol */
    readiness_method_t readiness_method;       /* which readiness method to use */
    char     readiness_file[MAX_PATH];         /* file to monitor for readiness */
    char     readiness_check[MAX_PATH];        /* command to check readiness */
    int      readiness_signal;                 /* signal number for readiness */
    int      readiness_timeout;                /* timeout in seconds (default 30) */
    int      readiness_interval;               /* check interval for health checks */
    time_t   ready_wait_start;                 /* when COMP_READY_WAIT state started */
} component_t;

/* Parse a component TOML file, populate a component_t structure */
int parse_component(const char *path, component_t *comp);

#endif /* TOML_H */