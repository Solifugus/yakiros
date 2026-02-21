/*
 * control.c - YakirOS control socket implementation (minimal version)
 */

#include "control.h"
#include "component.h"
#include "capability.h"
#include "log.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

static char *trim(char *str) {
    while (isspace(*str)) str++;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) *end-- = '\0';
    return str;
}

int setup_control_socket(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERR("control socket failed: %s", strerror(errno));
        return -1;
    }

    unlink(CONTROL_SOCKET);

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, CONTROL_SOCKET, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERR("bind %s failed: %s", CONTROL_SOCKET, strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, 5) < 0) {
        close(fd);
        return -1;
    }

    LOG_INFO("control socket ready: %s", CONTROL_SOCKET);
    return fd;
}

void handle_control_command(int client_fd) {
    char buf[1024];
    int n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = '\0';

    char *cmd = trim(buf);
    char response[4096];
    int rlen = 0;

    if (strcmp(cmd, "status") == 0) {
        rlen = snprintf(response, sizeof(response), "YakirOS Status:\n");

        /* Add enhanced status info with readiness details */
        for (int i = 0; i < n_components; i++) {
            component_t *comp = &components[i];
            const char *state_str = "UNKNOWN";
            switch (comp->state) {
                case COMP_INACTIVE:     state_str = "INACTIVE";  break;
                case COMP_STARTING:     state_str = "STARTING";  break;
                case COMP_READY_WAIT:   state_str = "READY_WAIT"; break;
                case COMP_ACTIVE:       state_str = "ACTIVE";    break;
                case COMP_FAILED:       state_str = "FAILED";    break;
                case COMP_ONESHOT_DONE: state_str = "DONE";      break;
            }

            rlen += snprintf(response + rlen, sizeof(response) - rlen,
                             "  %s: %s (pid %d)",
                             comp->name, state_str, comp->pid);

            /* Add readiness information for components with readiness protocol */
            if (comp->readiness_method != READINESS_NONE) {
                const char *method_str = "UNKNOWN";
                switch (comp->readiness_method) {
                    case READINESS_NONE:    method_str = "none"; break;
                    case READINESS_FILE:    method_str = "file"; break;
                    case READINESS_COMMAND: method_str = "command"; break;
                    case READINESS_SIGNAL:  method_str = "signal"; break;
                }

                rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                 " [readiness:%s,timeout:%ds]",
                                 method_str, comp->readiness_timeout);

                /* Show time spent in READY_WAIT */
                if (comp->state == COMP_READY_WAIT && comp->ready_wait_start > 0) {
                    time_t elapsed = time(NULL) - comp->ready_wait_start;
                    rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                     " [waiting:%lds]", (long)elapsed);
                }

                /* Show specific readiness details */
                switch (comp->readiness_method) {
                    case READINESS_FILE:
                        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                         " [file:%s]", comp->readiness_file);
                        break;
                    case READINESS_COMMAND:
                        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                         " [cmd:%s,interval:%ds]",
                                         comp->readiness_check, comp->readiness_interval);
                        break;
                    case READINESS_SIGNAL:
                        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                         " [signal:%d]", comp->readiness_signal);
                        break;
                    case READINESS_NONE:
                        break;
                }
            }

            rlen += snprintf(response + rlen, sizeof(response) - rlen, "\n");
        }
    } else if (strcmp(cmd, "readiness") == 0) {
        /* Show detailed readiness information */
        rlen = snprintf(response, sizeof(response), "Readiness Status:\n");

        int waiting_count = 0, ready_count = 0, timeout_count = 0;

        for (int i = 0; i < n_components; i++) {
            component_t *comp = &components[i];

            if (comp->readiness_method == READINESS_NONE) continue;

            const char *method_str = "unknown";
            switch (comp->readiness_method) {
                case READINESS_FILE:    method_str = "file"; break;
                case READINESS_COMMAND: method_str = "command"; break;
                case READINESS_SIGNAL:  method_str = "signal"; break;
                case READINESS_NONE:    method_str = "none"; break;
            }

            rlen += snprintf(response + rlen, sizeof(response) - rlen,
                             "  %s: method=%s, timeout=%ds",
                             comp->name, method_str, comp->readiness_timeout);

            switch (comp->state) {
                case COMP_READY_WAIT:
                    waiting_count++;
                    if (comp->ready_wait_start > 0) {
                        time_t elapsed = time(NULL) - comp->ready_wait_start;
                        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                         " [WAITING %lds]", (long)elapsed);
                    } else {
                        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                         " [WAITING]");
                    }
                    break;
                case COMP_ACTIVE:
                    ready_count++;
                    rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                     " [READY]");
                    break;
                case COMP_FAILED:
                    timeout_count++;
                    rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                     " [FAILED/TIMEOUT]");
                    break;
                default:
                    rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                     " [%s]",
                                     comp->state == COMP_INACTIVE ? "INACTIVE" :
                                     comp->state == COMP_STARTING ? "STARTING" : "OTHER");
                    break;
            }

            rlen += snprintf(response + rlen, sizeof(response) - rlen, "\n");
        }

        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                         "\nSummary: %d ready, %d waiting, %d failed/timeout\n",
                         ready_count, waiting_count, timeout_count);

    } else if (strncmp(cmd, "check-readiness", 15) == 0) {
        /* Trigger readiness check for all or specific component */
        const char *component_name = NULL;
        if (strlen(cmd) > 16) {
            component_name = cmd + 16; /* Skip "check-readiness " */
        }

        int checks_performed = 0;

        for (int i = 0; i < n_components; i++) {
            component_t *comp = &components[i];

            if (component_name && strcmp(comp->name, component_name) != 0) {
                continue;
            }

            if (comp->state == COMP_READY_WAIT) {
                checks_performed++;
            }
        }

        /* Trigger the actual readiness checks */
        if (checks_performed > 0) {
            check_all_readiness();
        }

        if (component_name) {
            rlen = snprintf(response, sizeof(response),
                            "Readiness check triggered for component '%s'\n", component_name);
        } else {
            rlen = snprintf(response, sizeof(response),
                            "Readiness checks triggered for %d components\n", checks_performed);
        }

    } else {
        rlen = snprintf(response, sizeof(response),
                        "Unknown command: %s\n"
                        "Available commands: status, readiness, check-readiness [component]\n", cmd);
    }

    (void)!write(client_fd, response, rlen);
}