/*
 * component.c - YakirOS component lifecycle management implementation
 *
 * Handles component loading, dependency checking, and process supervision.
 */

#define _GNU_SOURCE

#include "component.h"
#include "capability.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Global component storage */
component_t components[MAX_COMPONENTS];
int n_components = 0;

int requirements_met(component_t *comp) {
    for (int i = 0; i < comp->n_requires; i++) {
        if (!capability_active(comp->requires[i])) {
            return 0;
        }
    }
    return 1;
}

/* Check if a component has exceeded its readiness timeout */
void check_readiness_timeout(int idx) {
    component_t *comp = &components[idx];

    if (comp->state != COMP_READY_WAIT) {
        return;
    }

    time_t now = time(NULL);
    int timeout = comp->readiness_timeout > 0 ? comp->readiness_timeout : 30; /* default 30s */

    if (now - comp->ready_wait_start >= timeout) {
        LOG_ERR("component '%s' readiness timeout after %d seconds",
                comp->name, timeout);

        comp->state = COMP_FAILED;

        /* Kill the process if it's still running */
        if (comp->pid > 0) {
            kill(comp->pid, SIGTERM);
        }
    }
}

/* Mark a component as ready and register its capabilities */
void component_ready(int idx) {
    component_t *comp = &components[idx];

    if (comp->state != COMP_READY_WAIT) {
        LOG_WARN("component '%s' signaled ready but not in READY_WAIT state (state=%d)",
                 comp->name, comp->state);
        return;
    }

    time_t now = time(NULL);
    int wait_time = (int)(now - comp->ready_wait_start);

    LOG_INFO("component '%s' is ready (waited %d seconds)", comp->name, wait_time);

    comp->state = COMP_ACTIVE;

    /* Register capabilities for service-type components */
    if (comp->type == COMP_TYPE_SERVICE) {
        for (int i = 0; i < comp->n_provides; i++) {
            capability_register(comp->provides[i], idx);
            LOG_INFO("capability UP: %s (provided by %s)", comp->provides[i], comp->name);
        }
    }
}

/* Check if a readiness file exists for file-based readiness */
static int check_readiness_file(const char *filepath) {
    struct stat st;
    return (stat(filepath, &st) == 0);
}

/* Execute a health check command and return success/failure */
static int execute_readiness_check(const char *command) {
    if (!command || !*command) return 0;

    int status = system(command);
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* Check readiness for all components in READY_WAIT state */
void check_all_readiness(void) {
    for (int i = 0; i < n_components; i++) {
        component_t *comp = &components[i];

        if (comp->state != COMP_READY_WAIT) {
            continue;
        }

        /* First check for timeout */
        check_readiness_timeout(i);
        if (comp->state != COMP_READY_WAIT) {
            continue; /* Component timed out */
        }

        /* Check readiness based on configured method */
        int ready = 0;

        switch (comp->readiness_method) {
            case READINESS_FILE:
                if (comp->readiness_file[0]) {
                    ready = check_readiness_file(comp->readiness_file);
                    if (ready) {
                        LOG_INFO("component '%s' readiness file detected: %s",
                                 comp->name, comp->readiness_file);
                    }
                }
                break;

            case READINESS_COMMAND:
                if (comp->readiness_check[0]) {
                    ready = execute_readiness_check(comp->readiness_check);
                    if (ready) {
                        LOG_INFO("component '%s' readiness check passed: %s",
                                 comp->name, comp->readiness_check);
                    }
                }
                break;

            case READINESS_SIGNAL:
                /* Signal-based readiness is handled via signal handler, not polled */
                break;

            case READINESS_NONE:
            default:
                /* Should not happen for components in READY_WAIT state */
                LOG_WARN("component '%s' in READY_WAIT with READINESS_NONE", comp->name);
                ready = 1; /* Mark ready to recover */
                break;
        }

        if (ready) {
            component_ready(i);
        }
    }
}

int component_start(int idx) {
    component_t *comp = &components[idx];

    /* Rate limiting: don't restart too fast */
    time_t now = time(NULL);
    if (now - comp->last_restart < 30 && comp->restart_count >= 5) {
        LOG_WARN("component '%s' restarting too fast, backing off",
                 comp->name);
        return -1;
    }

    LOG_INFO("starting component '%s': %s", comp->name, comp->binary);

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERR("fork failed for '%s': %s", comp->name, strerror(errno));
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        char *argv[MAX_ARGS + 2];
        argv[0] = comp->binary;
        for (int i = 0; i < comp->argc; i++) {
            argv[i + 1] = comp->args[i];
        }
        argv[comp->argc + 1] = NULL;

        /* Reset signal handlers */
        signal(SIGCHLD, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGHUP, SIG_DFL);

        /* Create new session */
        setsid();

        /* Execute the component */
        execv(comp->binary, argv);

        /* If we get here, exec failed */
        fprintf(stderr, "graph-resolver: exec '%s' failed: %s\n",
                comp->binary, strerror(errno));
        _exit(127);
    }

    /* Parent process */
    comp->pid = pid;
    comp->state = COMP_STARTING;
    comp->restart_count++;
    comp->last_restart = now;

    /* Transition to readiness waiting or immediately active based on configuration */
    if (comp->readiness_method == READINESS_NONE) {
        /* No readiness check configured - immediately active (backward compatibility) */
        comp->state = COMP_ACTIVE;

        /* Register capabilities for service-type components */
        if (comp->type == COMP_TYPE_SERVICE) {
            for (int i = 0; i < comp->n_provides; i++) {
                capability_register(comp->provides[i], idx);
            }
        }
    } else {
        /* Readiness check configured - wait for readiness signal */
        comp->state = COMP_READY_WAIT;
        comp->ready_wait_start = now;

        LOG_INFO("component '%s' waiting for readiness signal (method=%d, timeout=%d)",
                 comp->name, comp->readiness_method, comp->readiness_timeout);

        /* Capabilities will be registered when component signals readiness */
    }

    return 0;
}

void component_exited(int idx, int status) {
    component_t *comp = &components[idx];

    if (comp->type == COMP_TYPE_ONESHOT) {
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            /* Oneshot succeeded */
            comp->state = COMP_ONESHOT_DONE;
            LOG_INFO("oneshot '%s' completed successfully", comp->name);

            /* Register capabilities */
            for (int i = 0; i < comp->n_provides; i++) {
                capability_register(comp->provides[i], idx);
            }
        } else {
            /* Oneshot failed */
            comp->state = COMP_FAILED;
            LOG_ERR("oneshot '%s' failed (status %d)", comp->name, status);
        }
    } else {
        /* Service exited */
        if (comp->state == COMP_READY_WAIT) {
            LOG_ERR("service '%s' (pid %d) exited before becoming ready (status %d)",
                    comp->name, comp->pid, status);
        } else {
            LOG_WARN("service '%s' (pid %d) exited (status %d)",
                     comp->name, comp->pid, status);
        }

        comp->state = COMP_FAILED;
        comp->pid = -1;

        /* Withdraw capabilities if they were registered */
        for (int i = 0; i < comp->n_provides; i++) {
            capability_withdraw(comp->provides[i]);
        }
    }
}

int load_components(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) {
        LOG_ERR("cannot open %s: %s", dir, strerror(errno));
        return -1;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        /* Only .toml files */
        char *dot = strrchr(ent->d_name, '.');
        if (!dot || strcmp(dot, ".toml") != 0) continue;

        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        if (n_components >= MAX_COMPONENTS) {
            LOG_ERR("component limit reached, skipping %s", path);
            break;
        }

        if (parse_component(path, &components[n_components]) == 0) {
            LOG_INFO("loaded component '%s' from %s",
                     components[n_components].name, ent->d_name);
            n_components++;
        }
    }

    closedir(d);
    return n_components;
}

void register_early_capabilities(void) {
    /* Create kernel pseudo-component */
    if (n_components < MAX_COMPONENTS) {
        int kidx = n_components++;
        component_t *kern = &components[kidx];
        memset(kern, 0, sizeof(*kern));
        strncpy(kern->name, "kernel", MAX_NAME);
        strncpy(kern->binary, "[kernel]", MAX_PATH);
        kern->type = COMP_TYPE_SERVICE;
        kern->state = COMP_ACTIVE;
        kern->pid = 0;

        /* Register kernel capabilities */
        static const char *early_caps[] = {
            "kernel.syscalls", "kernel.memory", "kernel.scheduling",
            "filesystem.proc", "filesystem.sys", "filesystem.dev",
            "filesystem.run", "filesystem.devpts",
            NULL
        };
        for (int i = 0; early_caps[i]; i++) {
            capability_register(early_caps[i], kidx);
        }
    }
}