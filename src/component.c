/*
 * component.c - YakirOS component lifecycle management implementation
 *
 * Handles component loading, dependency checking, and process supervision.
 */

#define _GNU_SOURCE

#include "component.h"
#include "capability.h"
#include "handoff.h"
#include "graph.h"
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
#include <stdlib.h>
#include <sys/types.h>

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

    /* Prepare cgroup for this component */
    const char *cgroup_path = (strlen(comp->cgroup_path) > 0) ? comp->cgroup_path : comp->name;
    if (cgroup_create(comp->name, cgroup_path) < 0) {
        LOG_WARN("failed to create cgroup for %s", comp->name);
    }

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

        /* Set up namespace isolation if configured */
        if (isolation_setup_namespaces(comp) < 0) {
            LOG_ERR("failed to setup namespaces for %s", comp->name);
            _exit(126);
        }

        /* Set up per-component logging */
        char log_dir[] = "/run/graph";
        char log_path[MAX_PATH];
        snprintf(log_path, sizeof(log_path), "%s/%s.log", log_dir, comp->name);

        /* Create log directory if it doesn't exist */
        mkdir(log_dir, 0755); /* Ignore errors if it already exists */

        /* Open log file for append */
        int log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (log_fd >= 0) {
            /* Redirect stdout and stderr to log file */
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);

            /* Log startup message */
            time_t start_time = time(NULL);
            printf("[%ld] Starting component '%s' (pid %d)\n",
                   start_time, comp->name, getpid());
            fflush(stdout);
        }

        /* Execute the component */
        execv(comp->binary, argv);

        /* If we get here, exec failed */
        fprintf(stderr, "graph-resolver: exec '%s' failed: %s\n",
                comp->binary, strerror(errno));
        _exit(127);
    }

    /* Parent process */
    comp->pid = pid;

    /* Add child process to cgroup and apply resource limits */
    if (cgroup_add_process(cgroup_path, pid) < 0) {
        LOG_WARN("failed to add process %d to cgroup for %s", pid, comp->name);
    }

    if (cgroup_apply_limits(cgroup_path, comp) < 0) {
        LOG_WARN("failed to apply resource limits to cgroup for %s", comp->name);
    }

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

    /* Clean up cgroup when component exits */
    const char *cgroup_path = (strlen(comp->cgroup_path) > 0) ? comp->cgroup_path : comp->name;
    if (cgroup_cleanup(cgroup_path) < 0) {
        LOG_WARN("failed to cleanup cgroup for %s", comp->name);
    }
}

/* Check OOM events for all components with cgroups */
void check_all_oom_events(void) {
    for (int i = 0; i < n_components; i++) {
        component_t *comp = &components[i];

        /* Skip inactive or failed components */
        if (comp->state == COMP_INACTIVE || comp->state == COMP_FAILED) {
            continue;
        }

        /* Check for OOM events in component's cgroup */
        const char *cgroup_path = (strlen(comp->cgroup_path) > 0) ? comp->cgroup_path : comp->name;
        int oom_count = cgroup_check_oom_events(cgroup_path);

        if (oom_count > 0) {
            LOG_ERR("component '%s' hit OOM limit, marking as failed", comp->name);

            /* Mark component as failed due to OOM */
            comp->state = COMP_FAILED;
            comp->pid = -1;

            /* Withdraw capabilities */
            for (int j = 0; j < comp->n_provides; j++) {
                capability_withdraw(comp->provides[j]);
            }

            /* Component will be restarted by graph resolver if needed */
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

int validate_component_graph(int warn_only) {
    /* Validate the component graph for cycles and other issues */
    cycle_info_t cycle_info;
    int cycle_result = graph_detect_cycles(&cycle_info);

    if (cycle_result < 0) {
        LOG_ERR("failed to perform cycle detection");
        return -1;
    }

    if (cycle_result == 1) {
        /* Cycles detected */
        if (warn_only) {
            LOG_WARN("dependency cycles detected: %s", cycle_info.error_message);
            LOG_WARN("continuing with potentially unstable graph - manual intervention may be required");
        } else {
            LOG_ERR("dependency cycles detected: %s", cycle_info.error_message);
            LOG_ERR("refusing to start with cyclic dependencies");
        }

        /* Log detailed cycle information */
        if (cycle_info.cycle_length > 0) {
            LOG_WARN("cycle involves %d components:", cycle_info.cycle_length);
            for (int i = 0; i < cycle_info.cycle_length - 1; i++) {
                int comp_idx = cycle_info.cycle_components[i];
                if (comp_idx < n_components) {
                    LOG_WARN("  - %s", components[comp_idx].name);
                }
            }
        }

        free(cycle_info.cycle_components);
        return warn_only ? 0 : -1;
    }

    LOG_INFO("graph validation passed: no dependency cycles detected");
    return 0;
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

/* Hot-swap upgrade a component to new version
 * Returns:
 *   0 = success (upgrade initiated)
 *  -1 = component not found
 *  -2 = component doesn't support hot-swap (handoff != "fd-passing")
 *  -3 = component not currently active
 *  -4 = other error
 */
int component_upgrade(const char *component_name) {
    /* Find component by name */
    int idx = -1;
    for (int i = 0; i < n_components; i++) {
        if (strcmp(components[i].name, component_name) == 0) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        LOG_ERR("upgrade: component '%s' not found", component_name);
        return -1;
    }

    component_t *comp = &components[idx];

    /* Check if component supports hot-swap */
    if (comp->handoff != HANDOFF_FD_PASSING) {
        LOG_ERR("upgrade: component '%s' does not support hot-swap (handoff=%d)",
                component_name, comp->handoff);
        return -2;
    }

    /* Check if component is currently active */
    if (comp->state != COMP_ACTIVE) {
        LOG_ERR("upgrade: component '%s' is not active (state=%d)",
                component_name, comp->state);
        return -3;
    }

    LOG_INFO("upgrade: initiating hot-swap for component '%s' (pid %d)",
             component_name, comp->pid);

    /* Step 1: Create socketpair for handoff communication */
    int handoff_socks[2];
    if (create_handoff_socketpair(handoff_socks) != 0) {
        LOG_ERR("upgrade: failed to create handoff socketpair for '%s'", component_name);
        return -4;
    }

    LOG_INFO("upgrade: created handoff socketpair for '%s': %d <-> %d",
             component_name, handoff_socks[0], handoff_socks[1]);

    /* Step 2: Fork new process with handoff socket */
    pid_t new_pid = fork();
    if (new_pid < 0) {
        LOG_ERR("upgrade: fork failed for '%s': %s", component_name, strerror(errno));
        close(handoff_socks[0]);
        close(handoff_socks[1]);
        return -4;
    }

    if (new_pid == 0) {
        /* Child process - new component instance */
        close(handoff_socks[0]); /* Close resolver's end */

        /* Set up HANDOFF_FD environment variable */
        char handoff_fd_str[32];
        snprintf(handoff_fd_str, sizeof(handoff_fd_str), "%d", handoff_socks[1]);
        setenv(HANDOFF_FD_ENV, handoff_fd_str, 1);

        /* Duplicate the handoff socket to HANDOFF_FD (4) */
        if (dup2(handoff_socks[1], HANDOFF_FD) < 0) {
            fprintf(stderr, "upgrade: dup2 failed for handoff fd: %s\n", strerror(errno));
            _exit(127);
        }
        close(handoff_socks[1]);

        /* Prepare argv for new process */
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
        signal(SIGUSR1, SIG_DFL);

        /* Create new session */
        setsid();

        LOG_INFO("upgrade: executing new instance of '%s'", component_name);

        /* Execute the new component */
        execv(comp->binary, argv);

        /* If we get here, exec failed */
        fprintf(stderr, "upgrade: exec '%s' failed: %s\n", comp->binary, strerror(errno));
        _exit(127);
    }

    /* Parent process - graph resolver */
    close(handoff_socks[1]); /* Close new process's end */

    LOG_INFO("upgrade: new instance of '%s' started (pid %d)", component_name, new_pid);

    /* Step 3: Signal old process to initiate handoff */
    if (kill(comp->pid, SIGUSR1) != 0) {
        LOG_ERR("upgrade: failed to signal old process %d: %s", comp->pid, strerror(errno));
        close(handoff_socks[0]);
        kill(new_pid, SIGTERM); /* Clean up new process */
        return -4;
    }

    LOG_INFO("upgrade: sent SIGUSR1 to old process %d for handoff", comp->pid);

    /* Step 4: Wait for handoff completion from old process */
    int handoff_result = wait_handoff_complete(handoff_socks[0], 10000); /* 10 second timeout */

    if (handoff_result != 0) {
        LOG_ERR("upgrade: handoff completion failed for '%s'", component_name);
        close(handoff_socks[0]);
        kill(new_pid, SIGTERM); /* Clean up new process */
        return -4;
    }

    LOG_INFO("upgrade: received handoff complete from old process for '%s'", component_name);
    close(handoff_socks[0]);

    /* Step 5: Update component record with new PID and wait for readiness */
    pid_t old_pid = comp->pid;
    comp->pid = new_pid;
    comp->state = (comp->readiness_method == READINESS_NONE) ? COMP_ACTIVE : COMP_READY_WAIT;
    comp->ready_wait_start = time(NULL);

    LOG_INFO("upgrade: transitioned component '%s' from pid %d to pid %d",
             component_name, old_pid, new_pid);

    /* Old process should exit cleanly after sending handoff complete */
    /* Give it a moment, then verify it's gone */
    sleep(1);
    if (kill(old_pid, 0) == 0) {
        LOG_WARN("upgrade: old process %d still alive after handoff, sending SIGTERM", old_pid);
        kill(old_pid, SIGTERM);
    }

    /* If using readiness protocol, capabilities will be re-registered when new process signals ready */
    if (comp->readiness_method == READINESS_NONE) {
        LOG_INFO("upgrade: hot-swap complete for '%s' - service immediately active", component_name);
    } else {
        LOG_INFO("upgrade: hot-swap complete for '%s' - waiting for readiness signal", component_name);
    }

    return 0; /* Success */
}

/* Execute health check for a component
 * Returns: 0=success, 1=failure, 2=timeout */
static int execute_health_check(component_t *comp) {
    if (!comp->health_check[0]) {
        return 0; /* No health check configured - assume healthy */
    }

    LOG_INFO("running health check for '%s': %s", comp->name, comp->health_check);

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERR("fork failed for health check '%s': %s", comp->name, strerror(errno));
        return 1; /* Treat fork failure as health failure */
    }

    if (pid == 0) {
        /* Child process - execute health check */
        char *argv[] = { "/bin/sh", "-c", comp->health_check, NULL };

        /* Reset signal handlers */
        signal(SIGCHLD, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGHUP, SIG_DFL);

        /* Create new session */
        setsid();

        /* Redirect stdout/stderr to /dev/null to avoid noise */
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execv("/bin/sh", argv);
        _exit(127); /* Health check script not found */
    }

    /* Parent process - wait for health check to complete with timeout */
    time_t start_time = time(NULL);
    int timeout = comp->health_timeout > 0 ? comp->health_timeout : 10;
    int status;
    pid_t result;

    while ((result = waitpid(pid, &status, WNOHANG)) == 0) {
        if (time(NULL) - start_time >= timeout) {
            /* Timeout - kill the health check process */
            LOG_WARN("health check for '%s' timed out after %d seconds", comp->name, timeout);
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0); /* Clean up zombie */
            return 2; /* Timeout */
        }
        usleep(100000); /* Sleep for 100ms */
    }

    if (result < 0) {
        LOG_ERR("waitpid failed for health check '%s': %s", comp->name, strerror(errno));
        return 1; /* Treat as failure */
    }

    /* Check exit status */
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 0) {
            LOG_INFO("health check for '%s' passed", comp->name);
            return 0; /* Success */
        } else {
            LOG_WARN("health check for '%s' failed with exit code %d", comp->name, exit_code);
            return 1; /* Failure */
        }
    } else {
        LOG_WARN("health check for '%s' terminated abnormally", comp->name);
        return 1; /* Failure */
    }
}

/* Handle health check result and update component state */
static void handle_health_result(int idx, int result) {
    component_t *comp = &components[idx];
    comp->last_health_check = time(NULL);
    comp->last_health_result = result;

    if (result == 0) {
        /* Health check passed */
        if (comp->state == COMP_DEGRADED) {
            /* Recovery from degraded state */
            comp->state = COMP_ACTIVE;
            comp->health_consecutive_failures = 0;
            LOG_INFO("component '%s' recovered from DEGRADED state", comp->name);

            /* Mark capabilities as no longer degraded */
            for (int i = 0; i < comp->n_provides; i++) {
                capability_mark_degraded(comp->provides[i], 0);
            }
        } else {
            /* Continue being healthy */
            comp->health_consecutive_failures = 0;
        }
    } else {
        /* Health check failed */
        comp->health_consecutive_failures++;
        LOG_WARN("health check failed for '%s' (consecutive failures: %d)",
                 comp->name, comp->health_consecutive_failures);

        /* State transitions based on failure count */
        if (comp->state == COMP_ACTIVE) {
            if (comp->health_consecutive_failures >= comp->health_fail_threshold) {
                /* Transition to DEGRADED */
                comp->state = COMP_DEGRADED;
                LOG_WARN("component '%s' entered DEGRADED state after %d failures",
                         comp->name, comp->health_consecutive_failures);

                /* Mark capabilities as degraded */
                for (int i = 0; i < comp->n_provides; i++) {
                    capability_mark_degraded(comp->provides[i], 1);
                }
            }
        } else if (comp->state == COMP_DEGRADED) {
            if (comp->health_consecutive_failures >= comp->health_restart_threshold) {
                /* Transition to FAILED - restart the component */
                comp->state = COMP_FAILED;
                LOG_ERR("component '%s' failed after %d consecutive health failures - restarting",
                        comp->name, comp->health_consecutive_failures);

                /* Withdraw capabilities */
                for (int i = 0; i < comp->n_provides; i++) {
                    capability_withdraw(comp->provides[i]);
                }

                /* Kill the process */
                if (comp->pid > 0) {
                    kill(comp->pid, SIGTERM);
                }
                comp->pid = -1;
                comp->health_consecutive_failures = 0;
            }
        }
    }
}

/* Check health for all components with health checks enabled */
void check_all_health(void) {
    time_t now = time(NULL);

    for (int i = 0; i < n_components; i++) {
        component_t *comp = &components[i];

        /* Only check health for active or degraded components with health checks configured */
        if ((comp->state != COMP_ACTIVE && comp->state != COMP_DEGRADED) ||
            !comp->health_check[0]) {
            continue;
        }

        /* Check if it's time for a health check */
        int interval = comp->health_interval > 0 ? comp->health_interval : 60; /* default 60 seconds */
        if (comp->last_health_check > 0 && (now - comp->last_health_check) < interval) {
            continue; /* Not time for health check yet */
        }

        /* Execute health check */
        int result = execute_health_check(comp);
        handle_health_result(i, result);
    }
}