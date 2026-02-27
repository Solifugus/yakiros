/*
 * graph-resolver.c - YakirOS PID 1 Main
 *
 * A dependency graph resolver that replaces traditional init.
 * This file contains only the main() function and core init system setup.
 * All functionality is implemented in separate modules.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/reboot.h>

/* YakirOS modules */
#include "log.h"
#include "toml.h"
#include "capability.h"
#include "component.h"
#include "graph.h"
#include "control.h"
#include "cgroup.h"

/* Global state */
static int sigchld_pipe[2] = {-1, -1};
static volatile int running = 1;
static volatile int reload_config = 0;
static volatile int dump_state = 0;

/* SIGCHLD handler - write to self-pipe for epoll */
static void sigchld_handler(int sig) {
    (void)sig;
    char c = 1;
    (void)!write(sigchld_pipe[1], &c, 1);
}

/* Reap zombie children */
static void reap_children(void) {
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int found = 0;

        /* Find which component this was */
        for (int i = 0; i < n_components; i++) {
            if (components[i].pid == pid) {
                component_exited(i, status);
                found = 1;
                break;
            }
        }

        if (!found) {
            LOG_INFO("reaped orphan pid %d", pid);
        }
    }
}

/* Mount virtual filesystems needed for early boot */
static void mount_early_fs(void) {
    struct {
        const char *fstype;
        const char *target;
        const char *opts;
    } mounts[] = {
        { "proc",     "/proc",     "nosuid,noexec,nodev" },
        { "sysfs",    "/sys",      "nosuid,noexec,nodev" },
        { "devtmpfs", "/dev",      "mode=0755,nosuid" },
        { "tmpfs",    "/run",      "mode=0755,nosuid,nodev" },
        { "devpts",   "/dev/pts",  "mode=0620,gid=5,nosuid,noexec" },
        { NULL, NULL, NULL }
    };

    for (int i = 0; mounts[i].fstype; i++) {
        /* Create mount point if needed */
        mkdir(mounts[i].target, 0755);

        if (mount(mounts[i].fstype, mounts[i].target, mounts[i].fstype,
                  MS_NOATIME, mounts[i].opts) < 0) {
            LOG_WARN("mount %s on %s failed: %s",
                     mounts[i].fstype, mounts[i].target, strerror(errno));
        } else {
            LOG_INFO("mounted %s on %s", mounts[i].fstype, mounts[i].target);
        }
    }
}

/* Handle inotify events for /etc/graph.d changes */
static void handle_inotify(int inotify_fd) {
    char buf[4096];
    int n = read(inotify_fd, buf, sizeof(buf));
    if (n <= 0) return;

    LOG_INFO("graph.d changed, reloading");

    /* Save current component states for restoration */
    pid_t pids[MAX_COMPONENTS];
    comp_state_t states[MAX_COMPONENTS];
    char names[MAX_COMPONENTS][MAX_NAME];
    int old_count = n_components;

    for (int i = 0; i < n_components; i++) {
        pids[i] = components[i].pid;
        states[i] = components[i].state;
        memcpy(names[i], components[i].name, MAX_NAME);
        names[i][MAX_NAME - 1] = '\0';
    }

    /* Reload components */
    n_components = 0;
    capability_init();
    register_early_capabilities();
    load_components(GRAPH_DIR);

    /* Validate reloaded graph (warn only, don't crash running system) */
    validate_component_graph(1);

    /* Restore states for components that still exist */
    for (int i = 0; i < n_components; i++) {
        for (int j = 0; j < old_count; j++) {
            if (strcmp(components[i].name, names[j]) == 0) {
                components[i].pid = pids[j];
                components[i].state = states[j];
                /* Re-register capabilities for active components */
                if (states[j] == COMP_ACTIVE || states[j] == COMP_ONESHOT_DONE) {
                    for (int k = 0; k < components[i].n_provides; k++) {
                        capability_register(components[i].provides[k], i);
                    }
                }
                break;
            }
        }
    }

    /* Trigger graph re-resolution */
    graph_resolve_full();
}

/* Emergency shell fallback for PID 1 - never exit */
static void emergency_shell(void) {
    if (getpid() == 1) {
        LOG_ERR("CRITICAL: PID 1 failure - dropping to emergency shell");
        execl("/bin/sh", "sh", NULL);
        /* If even /bin/sh fails, try other shells */
        execl("/bin/bash", "bash", NULL);
        execl("/sbin/sulogin", "sulogin", NULL);
        /* If all else fails, infinite loop to prevent kernel panic */
        LOG_ERR("CRITICAL: All emergency shells failed - entering infinite loop");
        while (1) sleep(60);
    }
}

/* Signal handlers for graceful shutdown */
static void shutdown_handler(int sig) {
    (void)sig;
    running = 0;
}

/* Signal handler for config reload (SIGUSR1) */
static void reload_handler(int sig) {
    (void)sig;
    reload_config = 1;
}

/* Signal handler for state dump (SIGUSR2) */
static void dump_handler(int sig) {
    (void)sig;
    dump_state = 1;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* Check if we're PID 1 */
    if (getpid() != 1) {
        fprintf(stderr,
                "graph-resolver: WARNING: not running as PID 1 (pid=%d)\n"
                "  Running in test mode.\n", getpid());
    }

    /* Initialize logging */
    log_open();
    LOG_INFO("=== YakirOS graph-resolver starting ===");

    /* Initialize subsystems */
    capability_init();

    /* Initialize cgroup v2 subsystem (only if PID 1) */
    if (getpid() == 1) {
        if (cgroup_init() < 0) {
            LOG_ERR("failed to initialize cgroup subsystem");
            emergency_shell();
            return 1;
        }
    }

    /* Early boot: mount virtual filesystems (only if PID 1) */
    if (getpid() == 1) {
        mount_early_fs();
    }

    /* Set up self-pipe for SIGCHLD */
    if (pipe2(sigchld_pipe, O_CLOEXEC) < 0) {
        LOG_ERR("pipe2 failed: %s", strerror(errno));
        emergency_shell();
        return 1;
    }

    /* Signal handlers */
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    /* Graceful shutdown handlers */
    sa.sa_handler = shutdown_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    /* Config reload handler (SIGUSR1) */
    sa.sa_handler = reload_handler;
    sigaction(SIGUSR1, &sa, NULL);

    /* State dump handler (SIGUSR2) */
    sa.sa_handler = dump_handler;
    sigaction(SIGUSR2, &sa, NULL);

    /* Register kernel capabilities and load components */
    register_early_capabilities();
    int loaded = load_components(GRAPH_DIR);
    LOG_INFO("loaded %d components", loaded);

    /* Validate component graph for cycles */
    if (validate_component_graph(0) < 0) {
        LOG_ERR("component graph validation failed - cannot continue");
        emergency_shell();
        return 1;
    }

    /* Set up epoll for event handling */
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        LOG_ERR("epoll_create failed: %s", strerror(errno));
        emergency_shell();
        return 1;
    }

    /* Add SIGCHLD pipe to epoll */
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = sigchld_pipe[0];
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sigchld_pipe[0], &ev);

    /* Set up control socket */
    int control_fd = setup_control_socket();
    if (control_fd >= 0) {
        ev.events = EPOLLIN;
        ev.data.fd = control_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, control_fd, &ev);
    }

    /* Set up inotify on /etc/graph.d/ */
    int inotify_fd = inotify_init1(IN_CLOEXEC);
    if (inotify_fd >= 0) {
        inotify_add_watch(inotify_fd, GRAPH_DIR, IN_CREATE | IN_DELETE | IN_MODIFY);
        ev.events = EPOLLIN;
        ev.data.fd = inotify_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, inotify_fd, &ev);
    }

    /* Initial graph resolution */
    LOG_INFO("performing initial graph resolution");
    graph_resolve_full();

    /* Main event loop */
    LOG_INFO("entering main event loop");
    #define MAX_EPOLL_EVENTS 32
    struct epoll_event events[MAX_EPOLL_EVENTS];

    while (running) {
        /* Handle signal requests */
        if (reload_config) {
            reload_config = 0;
            LOG_INFO("SIGUSR1 received - reloading configuration");
            handle_inotify(inotify_fd); /* Reuse the inotify reload logic */
        }

        if (dump_state) {
            dump_state = 0;
            LOG_INFO("SIGUSR2 received - dumping system state");
            LOG_INFO("=== SYSTEM STATE DUMP ===");
            LOG_INFO("Components: %d active", n_components);
            for (int i = 0; i < n_components; i++) {
                const char *state_str = "UNKNOWN";
                switch (components[i].state) {
                    case COMP_INACTIVE:     state_str = "INACTIVE";  break;
                    case COMP_STARTING:     state_str = "STARTING";  break;
                    case COMP_READY_WAIT:   state_str = "READY_WAIT"; break;
                    case COMP_ACTIVE:       state_str = "ACTIVE";    break;
                    case COMP_DEGRADED:     state_str = "DEGRADED";  break;
                    case COMP_FAILED:       state_str = "FAILED";    break;
                    case COMP_ONESHOT_DONE: state_str = "DONE";      break;
                }
                LOG_INFO("  %s: %s (pid %d, restarts %d)",
                         components[i].name, state_str, components[i].pid, components[i].restart_count);
            }
            LOG_INFO("Capabilities: %d registered", capability_count());
            for (int i = 0; i < capability_count(); i++) {
                LOG_INFO("  %s: %s", capability_name(i),
                         capability_active_by_idx(i) ? "UP" : "DOWN");
            }
            LOG_INFO("=== END STATE DUMP ===");
        }

        int nfds = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, 1000);

        if (nfds < 0) {
            if (errno == EINTR) continue;
            LOG_ERR("epoll_wait failed: %s", strerror(errno));
            emergency_shell();
            break;
        }

        /* Check readiness for components waiting for readiness signals */
        check_all_readiness();

        /* Check health for components with health checks enabled */
        check_all_health();

        /* Check for OOM events in component cgroups */
        check_all_oom_events();

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == sigchld_pipe[0]) {
                /* Drain the pipe and reap children */
                char c;
                while (read(sigchld_pipe[0], &c, 1) > 0) {}
                reap_children();
                /* Re-resolve graph: component death may affect dependencies */
                graph_resolve_full();

            } else if (fd == control_fd) {
                /* Accept control connection */
                int client_fd = accept(control_fd, NULL, NULL);
                if (client_fd >= 0) {
                    handle_control_command(client_fd);
                    close(client_fd);
                }

            } else if (fd == inotify_fd) {
                /* Handle component directory changes */
                handle_inotify(inotify_fd);
            }
        }
    }

    /* Shutdown sequence */
    LOG_INFO("graph-resolver shutting down");

    /* Send SIGTERM to all managed processes */
    for (int i = 0; i < n_components; i++) {
        if (components[i].pid > 0) {
            kill(components[i].pid, SIGTERM);
        }
    }

    /* Wait a moment, then SIGKILL any stragglers */
    sleep(5);
    for (int i = 0; i < n_components; i++) {
        if (components[i].pid > 0) {
            kill(components[i].pid, SIGKILL);
        }
    }

    /* Final safety net - PID 1 should never exit normally */
    emergency_shell();
    return 0;
}