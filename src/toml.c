/*
 * toml.c - Minimal TOML parser implementation for YakirOS
 *
 * Parses component declaration files in TOML format.
 * Handles the minimal subset needed for system components.
 */

#include "toml.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#define MAX_LINE 1024

/* TOML section types */
typedef enum {
    SECTION_NONE,
    SECTION_COMPONENT,
    SECTION_PROVIDES,
    SECTION_REQUIRES,
    SECTION_OPTIONAL,
    SECTION_LIFECYCLE,
    SECTION_RESOURCES,
    SECTION_ISOLATION,
    SECTION_CHECKPOINT,
} toml_section_t;

/* Helper function to trim whitespace */
static char *trim(char *str) {
    while (isspace(*str)) str++;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) *end-- = '\0';
    return str;
}

/* Parse section headers like [component], [requires], etc. */
static toml_section_t parse_section(const char *line) {
    if (strstr(line, "[component]"))  return SECTION_COMPONENT;
    if (strstr(line, "[provides]"))   return SECTION_PROVIDES;
    if (strstr(line, "[requires]"))   return SECTION_REQUIRES;
    if (strstr(line, "[optional]"))   return SECTION_OPTIONAL;
    if (strstr(line, "[lifecycle]"))  return SECTION_LIFECYCLE;
    if (strstr(line, "[resources]"))  return SECTION_RESOURCES;
    if (strstr(line, "[isolation]"))  return SECTION_ISOLATION;
    if (strstr(line, "[checkpoint]")) return SECTION_CHECKPOINT;
    return SECTION_NONE; /* unknown section, skip */
}

/* Parse a TOML array like ["value1", "value2"] into dest, return count */
static int parse_array(const char *value, char dest[][MAX_NAME], int max) {
    int count = 0;
    const char *p = strchr(value, '[');
    if (!p) return 0;
    p++; /* skip [ */

    while (*p && count < max) {
        /* skip whitespace and commas */
        while (*p && (isspace(*p) || *p == ',')) p++;
        if (*p == ']') break;

        if (*p == '"') {
            p++; /* skip opening quote */
            const char *start = p;
            while (*p && *p != '"') p++;
            if (*p == '"') {
                int len = p - start;
                if (len < MAX_NAME) {
                    memcpy(dest[count], start, len);
                    dest[count][len] = '\0';
                    count++;
                }
                p++; /* skip closing quote */
            }
        } else {
            /* unquoted value (until comma or ]) */
            const char *start = p;
            while (*p && *p != ',' && *p != ']') p++;
            int len = p - start;
            if (len < MAX_NAME) {
                memcpy(dest[count], start, len);
                dest[count][len] = '\0';
                count++;
            }
        }
    }
    return count;
}

/* Parse signal names to signal numbers */
static int parse_signal(const char *name) {
    if (strcmp(name, "SIGHUP") == 0)  return SIGHUP;
    if (strcmp(name, "SIGUSR1") == 0) return SIGUSR1;
    if (strcmp(name, "SIGUSR2") == 0) return SIGUSR2;
    if (strcmp(name, "SIGTERM") == 0) return SIGTERM;
    return 0; /* no signal */
}

/* Parse handoff method names */
static handoff_t parse_handoff(const char *name) {
    if (strcmp(name, "fd-passing") == 0)  return HANDOFF_FD_PASSING;
    if (strcmp(name, "state-file") == 0)  return HANDOFF_STATE_FILE;
    if (strcmp(name, "checkpoint") == 0)  return HANDOFF_CHECKPOINT;
    return HANDOFF_NONE;
}

/* Parse a component TOML file, populate a component_t */
int parse_component(const char *path, component_t *comp) {
    FILE *f = fopen(path, "r");
    if (!f) {
        LOG_ERR("cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    /* Initialize component with defaults */
    memset(comp, 0, sizeof(*comp));
    comp->type = COMP_TYPE_SERVICE;
    comp->state = COMP_INACTIVE;
    comp->handoff = HANDOFF_NONE;
    comp->reload_signal = 0;
    comp->health_interval = 0;
    comp->health_timeout = 10;                /* default 10 second timeout */
    comp->health_fail_threshold = 3;          /* default 3 failures before DEGRADED */
    comp->health_restart_threshold = 5;       /* default 5 failures before restart */
    comp->health_consecutive_failures = 0;
    comp->last_health_check = 0;
    comp->last_health_result = 0;
    comp->pid = -1;
    snprintf(comp->config_path, MAX_PATH, "%s", path);

    /* Initialize readiness protocol defaults */
    comp->readiness_method = READINESS_NONE;  /* backward compatibility */
    comp->readiness_timeout = 30;             /* default 30 seconds */
    comp->readiness_interval = 5;             /* default 5 second health check interval */
    comp->readiness_signal = 0;               /* no signal */
    comp->ready_wait_start = 0;

    /* Initialize cgroup resource limits defaults */
    memset(comp->cgroup_path, 0, MAX_PATH);
    memset(comp->memory_max, 0, 32);
    memset(comp->memory_high, 0, 32);
    memset(comp->cpu_max, 0, 32);
    comp->cpu_weight = 0;                     /* 0 = no weight specified, use default */
    comp->io_weight = 0;                      /* 0 = no weight specified, use default */
    comp->pids_max = 0;                       /* 0 = no limit */

    /* Initialize namespace isolation defaults */
    memset(comp->isolation_namespaces, 0, 256);
    strncpy(comp->isolation_root, "/", MAX_PATH - 1);  /* default to root filesystem */
    memset(comp->isolation_hostname, 0, MAX_NAME);

    /* Initialize checkpoint configuration defaults */
    comp->checkpoint_enabled = 0;                      /* disabled by default */
    memset(comp->checkpoint_preserve_fds, 0, 256);
    comp->checkpoint_leave_running = 1;                /* default: leave running during checkpoint */
    comp->checkpoint_memory_estimate = 0;              /* no estimate by default */
    comp->checkpoint_max_age = 24;                     /* default: cleanup after 24 hours */

    char line[MAX_LINE];
    toml_section_t section = SECTION_NONE;

    while (fgets(line, sizeof(line), f)) {
        char *trimmed = trim(line);

        /* Skip empty lines and comments */
        if (!*trimmed || *trimmed == '#') continue;

        /* Section header */
        if (*trimmed == '[') {
            section = parse_section(trimmed);
            continue;
        }

        /* Key-value pair */
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(trimmed);
        char *val = trim(eq + 1);

        /* Remove quotes from string values */
        if (*val == '"' && val[strlen(val) - 1] == '"') {
            val[strlen(val) - 1] = '\0';
            val++;
        }

        switch (section) {
        case SECTION_COMPONENT:
            if (strcmp(key, "name") == 0)
                strncpy(comp->name, val, MAX_NAME - 1);
            else if (strcmp(key, "binary") == 0)
                strncpy(comp->binary, val, MAX_PATH - 1);
            else if (strcmp(key, "type") == 0) {
                comp->type = (strcmp(val, "oneshot") == 0)
                    ? COMP_TYPE_ONESHOT : COMP_TYPE_SERVICE;
            } else if (strcmp(key, "args") == 0) {
                comp->argc = parse_array(val, comp->args, MAX_ARGS);
            }
            break;

        case SECTION_PROVIDES:
            if (strcmp(key, "capabilities") == 0)
                comp->n_provides = parse_array(val, comp->provides, MAX_DEPS);
            break;

        case SECTION_REQUIRES:
            if (strcmp(key, "capabilities") == 0)
                comp->n_requires = parse_array(val, comp->requires, MAX_DEPS);
            break;

        case SECTION_OPTIONAL:
            if (strcmp(key, "capabilities") == 0)
                comp->n_optional = parse_array(val, comp->optional, MAX_DEPS);
            break;

        case SECTION_LIFECYCLE:
            if (strcmp(key, "reload_signal") == 0)
                comp->reload_signal = parse_signal(val);
            else if (strcmp(key, "handoff") == 0)
                comp->handoff = parse_handoff(val);
            else if (strcmp(key, "health_check") == 0)
                strncpy(comp->health_check, val, MAX_PATH - 1);
            else if (strcmp(key, "health_interval") == 0)
                comp->health_interval = atoi(val);
            else if (strcmp(key, "health_timeout") == 0) {
                comp->health_timeout = atoi(val);
                if (comp->health_timeout <= 0) {
                    comp->health_timeout = 10; /* default */
                }
            }
            else if (strcmp(key, "health_fail_threshold") == 0) {
                comp->health_fail_threshold = atoi(val);
                if (comp->health_fail_threshold <= 0) {
                    comp->health_fail_threshold = 3; /* default */
                }
            }
            else if (strcmp(key, "health_restart_threshold") == 0) {
                comp->health_restart_threshold = atoi(val);
                if (comp->health_restart_threshold <= 0) {
                    comp->health_restart_threshold = 5; /* default */
                }
            }
            /* Readiness protocol configuration */
            else if (strcmp(key, "readiness_file") == 0) {
                strncpy(comp->readiness_file, val, MAX_PATH - 1);
                comp->readiness_method = READINESS_FILE;
            }
            else if (strcmp(key, "readiness_check") == 0) {
                strncpy(comp->readiness_check, val, MAX_PATH - 1);
                comp->readiness_method = READINESS_COMMAND;
            }
            else if (strcmp(key, "readiness_signal") == 0) {
                comp->readiness_signal = parse_signal(val);
                if (comp->readiness_signal > 0) {
                    comp->readiness_method = READINESS_SIGNAL;
                }
            }
            else if (strcmp(key, "readiness_timeout") == 0) {
                comp->readiness_timeout = atoi(val);
                if (comp->readiness_timeout <= 0) {
                    comp->readiness_timeout = 30; /* default */
                }
            }
            else if (strcmp(key, "readiness_interval") == 0) {
                comp->readiness_interval = atoi(val);
                if (comp->readiness_interval <= 0) {
                    comp->readiness_interval = 5; /* default */
                }
            }
            break;

        case SECTION_RESOURCES:
            if (strcmp(key, "cgroup") == 0) {
                strncpy(comp->cgroup_path, val, MAX_PATH - 1);
            }
            else if (strcmp(key, "memory_max") == 0) {
                strncpy(comp->memory_max, val, 31);
            }
            else if (strcmp(key, "memory_high") == 0) {
                strncpy(comp->memory_high, val, 31);
            }
            else if (strcmp(key, "cpu_weight") == 0) {
                comp->cpu_weight = atoi(val);
                if (comp->cpu_weight < 1) comp->cpu_weight = 1;
                if (comp->cpu_weight > 10000) comp->cpu_weight = 10000;
            }
            else if (strcmp(key, "cpu_max") == 0) {
                strncpy(comp->cpu_max, val, 31);
            }
            else if (strcmp(key, "io_weight") == 0) {
                comp->io_weight = atoi(val);
                if (comp->io_weight < 1) comp->io_weight = 1;
                if (comp->io_weight > 10000) comp->io_weight = 10000;
            }
            else if (strcmp(key, "pids_max") == 0) {
                comp->pids_max = atoi(val);
                if (comp->pids_max < 0) comp->pids_max = 0;
            }
            break;

        case SECTION_ISOLATION:
            if (strcmp(key, "namespaces") == 0) {
                strncpy(comp->isolation_namespaces, val, 255);
            }
            else if (strcmp(key, "root") == 0) {
                strncpy(comp->isolation_root, val, MAX_PATH - 1);
            }
            else if (strcmp(key, "hostname") == 0) {
                strncpy(comp->isolation_hostname, val, MAX_NAME - 1);
            }
            break;

        case SECTION_CHECKPOINT:
            if (strcmp(key, "enabled") == 0) {
                comp->checkpoint_enabled = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
            }
            else if (strcmp(key, "preserve_fds") == 0) {
                strncpy(comp->checkpoint_preserve_fds, val, 255);
            }
            else if (strcmp(key, "leave_running") == 0) {
                comp->checkpoint_leave_running = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
            }
            else if (strcmp(key, "memory_estimate") == 0) {
                comp->checkpoint_memory_estimate = atoi(val);
                if (comp->checkpoint_memory_estimate < 0) comp->checkpoint_memory_estimate = 0;
            }
            else if (strcmp(key, "max_age") == 0) {
                comp->checkpoint_max_age = atoi(val);
                if (comp->checkpoint_max_age < 1) comp->checkpoint_max_age = 24; /* default 24 hours */
            }
            break;

        default:
            /* Ignore unknown sections for now */
            break;
        }
    }

    fclose(f);

    /* Validation */
    if (!comp->name[0]) {
        LOG_ERR("component in %s has no name", path);
        return -1;
    }
    if (!comp->binary[0]) {
        LOG_ERR("component '%s' has no binary", comp->name);
        return -1;
    }

    return 0;
}