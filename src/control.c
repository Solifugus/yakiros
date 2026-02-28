/*
 * control.c - YakirOS control socket implementation (minimal version)
 */

#include "control.h"
#include "component.h"
#include "capability.h"
#include "graph.h"
#include "log.h"
#include "checkpoint.h"
#include "checkpoint-mgmt.h"
#include "kexec.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>

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
        /* Enhanced table format status display */
        rlen = snprintf(response, sizeof(response),
                        "COMPONENT            STATE      PID     UPTIME  RESTARTS\n"
                        "────────────────────────────────────────────────────────\n");

        for (int i = 0; i < n_components; i++) {
            component_t *comp = &components[i];
            const char *state_str = "UNKNOWN";
            switch (comp->state) {
                case COMP_INACTIVE:     state_str = "INACTIVE";  break;
                case COMP_STARTING:     state_str = "STARTING";  break;
                case COMP_READY_WAIT:   state_str = "READY_WAIT"; break;
                case COMP_ACTIVE:       state_str = "ACTIVE";    break;
                case COMP_DEGRADED:     state_str = "DEGRADED";  break;
                case COMP_FAILED:       state_str = "FAILED";    break;
                case COMP_ONESHOT_DONE: state_str = "DONE";      break;
            }

            /* Calculate uptime */
            char uptime_str[32];
            if (comp->last_restart > 0 && (comp->state == COMP_ACTIVE || comp->state == COMP_READY_WAIT)) {
                time_t uptime = time(NULL) - comp->last_restart;
                if (uptime >= 3600) {
                    snprintf(uptime_str, sizeof(uptime_str), "%ldh%ldm",
                             uptime / 3600, (uptime % 3600) / 60);
                } else if (uptime >= 60) {
                    snprintf(uptime_str, sizeof(uptime_str), "%ldm%lds",
                             uptime / 60, uptime % 60);
                } else {
                    snprintf(uptime_str, sizeof(uptime_str), "%lds", uptime);
                }
            } else if (comp->state == COMP_ONESHOT_DONE) {
                strncpy(uptime_str, "-", sizeof(uptime_str) - 1);
                uptime_str[sizeof(uptime_str) - 1] = '\0';
            } else {
                strncpy(uptime_str, "0s", sizeof(uptime_str) - 1);
                uptime_str[sizeof(uptime_str) - 1] = '\0';
            }

            /* Format PID field */
            char pid_str[16];
            if (comp->pid > 0) {
                snprintf(pid_str, sizeof(pid_str), "%d", comp->pid);
            } else if (comp->state == COMP_ONESHOT_DONE) {
                strncpy(pid_str, "-", sizeof(pid_str) - 1);
                pid_str[sizeof(pid_str) - 1] = '\0';
            } else {
                strncpy(pid_str, "-", sizeof(pid_str) - 1);
                pid_str[sizeof(pid_str) - 1] = '\0';
            }

            /* Add row to table with proper alignment */
            rlen += snprintf(response + rlen, sizeof(response) - rlen,
                             "%-20s %-10s %-7s %-7s %d\n",
                             comp->name, state_str, pid_str, uptime_str, comp->restart_count);
        }

        /* Add summary statistics */
        int active_count = 0, failed_count = 0, starting_count = 0, degraded_count = 0;
        for (int i = 0; i < n_components; i++) {
            component_t *comp = &components[i];
            switch (comp->state) {
                case COMP_ACTIVE:
                case COMP_ONESHOT_DONE:
                    active_count++;
                    break;
                case COMP_DEGRADED:
                    degraded_count++;
                    break;
                case COMP_FAILED:
                    failed_count++;
                    break;
                case COMP_STARTING:
                case COMP_READY_WAIT:
                    starting_count++;
                    break;
                default:
                    break;
            }
        }

        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                         "────────────────────────────────────────────────────────\n"
                         "Summary: %d active, %d degraded, %d starting, %d failed, %d total\n",
                         active_count, degraded_count, starting_count, failed_count, n_components);

    } else if (strcmp(cmd, "caps") == 0 || strcmp(cmd, "capabilities") == 0) {
        /* Show all capabilities with status and provider */
        rlen = snprintf(response, sizeof(response),
                        "CAPABILITY                     STATUS  PROVIDER\n"
                        "──────────────────────────────────────────────────────────\n");

        int total_caps = capability_count();
        int up_count = 0, down_count = 0;

        for (int i = 0; i < total_caps; i++) {
            const char *cap_name = capability_name(i);
            int active = capability_active_by_idx(i);
            int degraded = capability_degraded_by_idx(i);
            int provider_idx = capability_provider(i);

            const char *status = "DOWN";
            if (active) {
                status = degraded ? "DEGRADED" : "UP";
            }
            const char *provider = "-";

            if (active && provider_idx >= 0 && provider_idx < n_components) {
                provider = components[provider_idx].name;
                up_count++;
            } else {
                down_count++;
            }

            rlen += snprintf(response + rlen, sizeof(response) - rlen,
                             "%-30s %-7s %s\n",
                             cap_name, status, provider);
        }

        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                         "──────────────────────────────────────────────────────────\n"
                         "Total: %d capabilities (%d up, %d down)\n",
                         total_caps, up_count, down_count);

    } else if (strncmp(cmd, "tree", 4) == 0) {
        /* Show dependency tree for a component */
        const char *component_name = NULL;
        if (strlen(cmd) > 5) {
            component_name = cmd + 5; /* Skip "tree " */
        }

        if (!component_name || strlen(component_name) == 0) {
            rlen = snprintf(response, sizeof(response),
                            "Error: tree command requires component name\n"
                            "Usage: tree <component_name>\n");
        } else {
            /* Find the component */
            int comp_idx = -1;
            for (int i = 0; i < n_components; i++) {
                if (strcmp(components[i].name, component_name) == 0) {
                    comp_idx = i;
                    break;
                }
            }

            if (comp_idx == -1) {
                rlen = snprintf(response, sizeof(response),
                                "Error: component '%s' not found\n", component_name);
            } else {
                component_t *comp = &components[comp_idx];

                /* Start the tree output */
                rlen = snprintf(response, sizeof(response), "%s\n", comp->name);

                /* Show requirements */
                for (int i = 0; i < comp->n_requires; i++) {
                    const char *req_cap = comp->requires[i];
                    int cap_active = capability_active(req_cap);
                    const char *status = cap_active ? "UP" : "DOWN";

                    /* Find provider component */
                    const char *provider = "-";
                    int provider_idx = -1;
                    if (cap_active) {
                        int cap_idx = capability_index(req_cap);
                        if (cap_idx >= 0) {
                            provider_idx = capability_provider(cap_idx);
                            if (provider_idx >= 0 && provider_idx < n_components) {
                                provider = components[provider_idx].name;
                            }
                        }
                    }

                    /* Use tree characters */
                    const char *tree_prefix = (i == comp->n_requires - 1) ? "└──" : "├──";

                    rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                     "%s requires: %s (%s%s%s)\n",
                                     tree_prefix, req_cap, status,
                                     (provider && strcmp(provider, "-") != 0) ? ", from " : "",
                                     (provider && strcmp(provider, "-") != 0) ? provider : "");

                    /* Recursively show provider's dependencies if it's active */
                    if (provider_idx >= 0 && provider_idx < n_components &&
                        strcmp(provider, "-") != 0 && components[provider_idx].n_requires > 0) {

                        component_t *provider_comp = &components[provider_idx];
                        const char *sub_tree_prefix = (i == comp->n_requires - 1) ? "    " : "│   ";

                        for (int j = 0; j < provider_comp->n_requires; j++) {
                            const char *sub_req_cap = provider_comp->requires[j];
                            int sub_cap_active = capability_active(sub_req_cap);
                            const char *sub_status = sub_cap_active ? "UP" : "DOWN";

                            const char *sub_provider = "-";
                            if (sub_cap_active) {
                                int sub_cap_idx = capability_index(sub_req_cap);
                                if (sub_cap_idx >= 0) {
                                    int sub_provider_idx = capability_provider(sub_cap_idx);
                                    if (sub_provider_idx >= 0 && sub_provider_idx < n_components) {
                                        sub_provider = components[sub_provider_idx].name;
                                    }
                                }
                            }

                            const char *sub_tree_char = (j == provider_comp->n_requires - 1) ? "└──" : "├──";

                            rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                             "%s%s requires: %s (%s%s%s)\n",
                                             sub_tree_prefix, sub_tree_char, sub_req_cap, sub_status,
                                             (sub_provider && strcmp(sub_provider, "-") != 0) ? ", from " : "",
                                             (sub_provider && strcmp(sub_provider, "-") != 0) ? sub_provider : "");
                        }
                    }
                }

                /* Show provides */
                if (comp->n_provides > 0) {
                    rlen += snprintf(response + rlen, sizeof(response) - rlen, "provides:\n");
                    for (int i = 0; i < comp->n_provides; i++) {
                        const char *tree_prefix = (i == comp->n_provides - 1) ? "└──" : "├──";
                        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                         "%s %s\n", tree_prefix, comp->provides[i]);
                    }
                }
            }
        }

    } else if (strncmp(cmd, "rdeps", 5) == 0) {
        /* Show reverse dependencies for a capability */
        const char *capability_name = NULL;
        if (strlen(cmd) > 6) {
            capability_name = cmd + 6; /* Skip "rdeps " */
        }

        if (!capability_name || strlen(capability_name) == 0) {
            rlen = snprintf(response, sizeof(response),
                            "Error: rdeps command requires capability name\n"
                            "Usage: rdeps <capability_name>\n");
        } else {
            rlen = snprintf(response, sizeof(response), "%s:\n", capability_name);

            int found_deps = 0;
            /* Search all components for this capability in their requirements */
            for (int i = 0; i < n_components; i++) {
                component_t *comp = &components[i];
                for (int j = 0; j < comp->n_requires; j++) {
                    if (strcmp(comp->requires[j], capability_name) == 0) {
                        const char *state_str = "UNKNOWN";
                        switch (comp->state) {
                            case COMP_INACTIVE:     state_str = "INACTIVE";  break;
                            case COMP_STARTING:     state_str = "STARTING";  break;
                            case COMP_READY_WAIT:   state_str = "READY_WAIT"; break;
                            case COMP_ACTIVE:       state_str = "ACTIVE";    break;
                            case COMP_DEGRADED:     state_str = "DEGRADED";  break;
                            case COMP_FAILED:       state_str = "FAILED";    break;
                            case COMP_ONESHOT_DONE: state_str = "DONE";      break;
                        }

                        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                         "  → %s (%s)\n", comp->name, state_str);
                        found_deps++;
                        break; /* Each component only listed once even if it requires the cap multiple times */
                    }
                }
            }

            if (found_deps == 0) {
                rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                 "  (no components depend on this capability)\n");
            } else {
                rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                 "Total: %d component(s) depend on this capability\n", found_deps);
            }
        }

    } else if (strncmp(cmd, "simulate remove", 15) == 0) {
        /* Show impact of removing a component */
        const char *component_name = NULL;
        if (strlen(cmd) > 16) {
            component_name = cmd + 16; /* Skip "simulate remove " */
        }

        if (!component_name || strlen(component_name) == 0) {
            rlen = snprintf(response, sizeof(response),
                            "Error: simulate remove command requires component name\n"
                            "Usage: simulate remove <component_name>\n");
        } else {
            /* Find the component */
            int comp_idx = -1;
            for (int i = 0; i < n_components; i++) {
                if (strcmp(components[i].name, component_name) == 0) {
                    comp_idx = i;
                    break;
                }
            }

            if (comp_idx == -1) {
                rlen = snprintf(response, sizeof(response),
                                "Error: component '%s' not found\n", component_name);
            } else {
                component_t *comp = &components[comp_idx];

                rlen = snprintf(response, sizeof(response),
                                "Removing %s would:\n", component_name);

                /* Show capabilities that would be withdrawn */
                if (comp->n_provides > 0) {
                    rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                     "  - Withdraw capabilities:\n");
                    for (int i = 0; i < comp->n_provides; i++) {
                        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                         "    → %s\n", comp->provides[i]);
                    }

                    /* Show directly affected components */
                    int affected_count = 0;
                    rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                     "  - Directly affect components:\n");

                    for (int i = 0; i < comp->n_provides; i++) {
                        const char *cap = comp->provides[i];
                        for (int j = 0; j < n_components; j++) {
                            if (j == comp_idx) continue; /* Skip the component being removed */

                            component_t *other_comp = &components[j];
                            for (int k = 0; k < other_comp->n_requires; k++) {
                                if (strcmp(other_comp->requires[k], cap) == 0) {
                                    const char *state_str = "UNKNOWN";
                                    switch (other_comp->state) {
                                        case COMP_INACTIVE:     state_str = "INACTIVE";  break;
                                        case COMP_STARTING:     state_str = "STARTING";  break;
                                        case COMP_READY_WAIT:   state_str = "READY_WAIT"; break;
                                        case COMP_ACTIVE:       state_str = "ACTIVE";    break;
                                        case COMP_DEGRADED:     state_str = "DEGRADED";  break;
                                        case COMP_FAILED:       state_str = "FAILED";    break;
                                        case COMP_ONESHOT_DONE: state_str = "DONE";      break;
                                    }

                                    rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                                     "    → %s (requires %s, currently %s)\n",
                                                     other_comp->name, cap, state_str);
                                    affected_count++;
                                    break; /* Each component only listed once */
                                }
                            }
                        }
                    }

                    if (affected_count == 0) {
                        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                         "    (no other components would be affected)\n");
                    } else {
                        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                         "  - Total: %d component(s) would lose required capabilities\n",
                                         affected_count);
                    }
                } else {
                    rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                     "  - No capabilities would be withdrawn (component provides none)\n"
                                     "  - No other components would be affected\n");
                }
            }
        }

    } else if (strcmp(cmd, "dot") == 0) {
        /* Output dependency graph in Graphviz DOT format */
        rlen = snprintf(response, sizeof(response),
                        "digraph yakiros {\n"
                        "    rankdir=LR;\n"
                        "    node [shape=box, style=filled];\n"
                        "\n"
                        "    // Components\n");

        /* Add component nodes with colors based on state */
        for (int i = 0; i < n_components; i++) {
            component_t *comp = &components[i];
            const char *color = "lightgray";
            switch (comp->state) {
                case COMP_ACTIVE:
                case COMP_ONESHOT_DONE:
                    color = "lightgreen";
                    break;
                case COMP_FAILED:
                    color = "lightcoral";
                    break;
                case COMP_STARTING:
                case COMP_READY_WAIT:
                    color = "lightyellow";
                    break;
                default:
                    color = "lightgray";
                    break;
            }

            rlen += snprintf(response + rlen, sizeof(response) - rlen,
                             "    \"%s\" [fillcolor=%s];\n",
                             comp->name, color);
        }

        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                         "\n    // Capabilities\n");

        /* Add capability nodes */
        int total_caps = capability_count();
        for (int i = 0; i < total_caps; i++) {
            const char *cap_name = capability_name(i);
            int active = capability_active_by_idx(i);
            const char *color = active ? "lightblue" : "lightcoral";

            rlen += snprintf(response + rlen, sizeof(response) - rlen,
                             "    \"%s\" [shape=ellipse, fillcolor=%s];\n",
                             cap_name, color);
        }

        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                         "\n    // Dependencies\n");

        /* Add dependency edges (component -> required capability) */
        for (int i = 0; i < n_components; i++) {
            component_t *comp = &components[i];
            for (int j = 0; j < comp->n_requires; j++) {
                rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                 "    \"%s\" -> \"%s\" [color=red];\n",
                                 comp->name, comp->requires[j]);
            }
        }

        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                         "\n    // Provisions\n");

        /* Add provision edges (component -> provided capability) */
        for (int i = 0; i < n_components; i++) {
            component_t *comp = &components[i];
            for (int j = 0; j < comp->n_provides; j++) {
                rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                 "    \"%s\" -> \"%s\" [color=green, arrowhead=diamond];\n",
                                 comp->name, comp->provides[j]);
            }
        }

        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                         "\n    // Legend\n"
                         "    subgraph cluster_legend {\n"
                         "        label=\"Legend\";\n"
                         "        style=filled;\n"
                         "        fillcolor=lightgray;\n"
                         "        \"Component\" [shape=box, fillcolor=lightgreen];\n"
                         "        \"Capability\" [shape=ellipse, fillcolor=lightblue];\n"
                         "        \"Component\" -> \"Capability\" [label=\"requires\", color=red];\n"
                         "        \"Component\" -> \"Provided Cap\" [label=\"provides\", color=green, arrowhead=diamond];\n"
                         "    }\n"
                         "}\n");

    } else if (strncmp(cmd, "log", 3) == 0) {
        /* Show recent log entries for a component */
        const char *args = NULL;
        if (strlen(cmd) > 4) {
            args = cmd + 4; /* Skip "log " */
        }

        if (!args || strlen(args) == 0) {
            rlen = snprintf(response, sizeof(response),
                            "Error: log command requires component name\n"
                            "Usage: log <component_name> [lines]\n");
        } else {
            /* Parse component name and optional line count */
            char component_name[MAX_NAME];
            int lines = 20; /* default */

            if (sscanf(args, "%127s %d", component_name, &lines) < 1) {
                rlen = snprintf(response, sizeof(response),
                                "Error: invalid log command format\n"
                                "Usage: log <component_name> [lines]\n");
            } else {
                /* Find the component */
                int comp_idx = -1;
                for (int i = 0; i < n_components; i++) {
                    if (strcmp(components[i].name, component_name) == 0) {
                        comp_idx = i;
                        break;
                    }
                }

                if (comp_idx == -1) {
                    rlen = snprintf(response, sizeof(response),
                                    "Error: component '%s' not found\n", component_name);
                } else {
                    /* Construct log file path */
                    char log_path[MAX_PATH];
                    snprintf(log_path, sizeof(log_path), "/run/graph/%s.log", component_name);

                    /* Try to read the log file */
                    FILE *log_file = fopen(log_path, "r");
                    if (!log_file) {
                        rlen = snprintf(response, sizeof(response),
                                        "Log file for component '%s' not found at %s\n"
                                        "(Per-component logging may not be enabled)\n",
                                        component_name, log_path);
                    } else {
                        rlen = snprintf(response, sizeof(response),
                                        "Recent logs for component '%s' (last %d lines):\n"
                                        "────────────────────────────────────────────────\n",
                                        component_name, lines);

                        /* Read the file and show last N lines */
                        char line[1024];
                        char *log_lines[100]; /* Buffer for up to 100 lines */
                        int line_count = 0;

                        /* Read all lines into memory */
                        while (fgets(line, sizeof(line), log_file) && line_count < 100) {
                            size_t len = strlen(line);
                            log_lines[line_count] = malloc(len + 1);
                            if (log_lines[line_count]) {
                                strcpy(log_lines[line_count], line);
                                line_count++;
                            }
                        }

                        fclose(log_file);

                        /* Show the last N lines */
                        int start_line = (line_count > lines) ? line_count - lines : 0;
                        for (int i = start_line; i < line_count; i++) {
                            int remaining = sizeof(response) - rlen;
                            if (remaining > (int)strlen(log_lines[i]) + 1) {
                                rlen += snprintf(response + rlen, remaining, "%s", log_lines[i]);
                            }
                            free(log_lines[i]);
                        }

                        /* Free any remaining allocated lines */
                        for (int i = 0; i < start_line; i++) {
                            free(log_lines[i]);
                        }

                        if (line_count == 0) {
                            rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                             "(log file is empty)\n");
                        }
                    }
                }
            }
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

    } else if (strncmp(cmd, "upgrade", 7) == 0) {
        /* Hot-swap upgrade a component */
        const char *component_name = NULL;
        if (strlen(cmd) > 8) {
            component_name = cmd + 8; /* Skip "upgrade " */
        }

        if (!component_name || strlen(component_name) == 0) {
            rlen = snprintf(response, sizeof(response),
                            "Error: upgrade command requires component name\n"
                            "Usage: upgrade <component_name>\n");
        } else {
            int result = component_upgrade(component_name);
            if (result == 0) {
                rlen = snprintf(response, sizeof(response),
                                "Hot-swap upgrade initiated for component '%s'\n", component_name);
            } else if (result == -1) {
                rlen = snprintf(response, sizeof(response),
                                "Error: component '%s' not found\n", component_name);
            } else if (result == -2) {
                rlen = snprintf(response, sizeof(response),
                                "Error: component '%s' does not support hot-swap (handoff != \"fd-passing\")\n", component_name);
            } else if (result == -3) {
                rlen = snprintf(response, sizeof(response),
                                "Error: component '%s' is not currently active\n", component_name);
            } else {
                rlen = snprintf(response, sizeof(response),
                                "Error: upgrade failed for component '%s' (error code %d)\n", component_name, result);
            }
        }

    } else if (strncmp(cmd, "checkpoint", 10) == 0) {
        /* Create checkpoint of a component */
        const char *component_name = NULL;
        if (strlen(cmd) > 11) {
            component_name = cmd + 11; /* Skip "checkpoint " */
        }

        if (!component_name || strlen(component_name) == 0) {
            rlen = snprintf(response, sizeof(response),
                            "Error: checkpoint command requires component name\n"
                            "Usage: checkpoint <component_name>\n");
        } else {
            int result = component_checkpoint(component_name);
            if (result == 0) {
                rlen = snprintf(response, sizeof(response),
                                "Checkpoint created successfully for component '%s'\n", component_name);
            } else if (result == -1) {
                rlen = snprintf(response, sizeof(response),
                                "Error: component '%s' not found\n", component_name);
            } else if (result == -2) {
                rlen = snprintf(response, sizeof(response),
                                "Error: CRIU not supported on this system\n");
            } else if (result == -3) {
                rlen = snprintf(response, sizeof(response),
                                "Error: component '%s' is not currently active\n", component_name);
            } else {
                rlen = snprintf(response, sizeof(response),
                                "Error: checkpoint failed for component '%s' (error code %d)\n", component_name, result);
            }
        }

    } else if (strncmp(cmd, "restore", 7) == 0) {
        /* Restore component from checkpoint */
        char component_name[128] = {0};
        char checkpoint_id[64] = {0};

        /* Parse "restore <component> [checkpoint_id]" */
        int args_parsed = sscanf(cmd, "restore %127s %63s", component_name, checkpoint_id);

        if (args_parsed < 1) {
            rlen = snprintf(response, sizeof(response),
                            "Error: restore command requires component name\n"
                            "Usage: restore <component_name> [checkpoint_id]\n");
        } else {
            const char *checkpoint_ptr = (args_parsed >= 2 && strlen(checkpoint_id) > 0) ? checkpoint_id : NULL;
            int result = component_restore(component_name, checkpoint_ptr);

            if (result == 0) {
                if (checkpoint_ptr) {
                    rlen = snprintf(response, sizeof(response),
                                    "Component '%s' restored successfully from checkpoint %s\n",
                                    component_name, checkpoint_ptr);
                } else {
                    rlen = snprintf(response, sizeof(response),
                                    "Component '%s' restored successfully from latest checkpoint\n",
                                    component_name);
                }
            } else if (result == -1) {
                rlen = snprintf(response, sizeof(response),
                                "Error: component '%s' not found\n", component_name);
            } else if (result == -2) {
                rlen = snprintf(response, sizeof(response),
                                "Error: CRIU not supported on this system\n");
            } else if (result == -3) {
                rlen = snprintf(response, sizeof(response),
                                "Error: no checkpoints found for component '%s'\n", component_name);
            } else {
                rlen = snprintf(response, sizeof(response),
                                "Error: restore failed for component '%s' (error code %d)\n", component_name, result);
            }
        }

    } else if (strncmp(cmd, "checkpoint-list", 15) == 0) {
        /* List available checkpoints */
        const char *component_name = NULL;
        if (strlen(cmd) > 16) {
            component_name = cmd + 16; /* Skip "checkpoint-list " */
            /* Trim leading/trailing whitespace */
            while (*component_name == ' ') component_name++;
            if (strlen(component_name) == 0) component_name = NULL;
        }

        checkpoint_entry_t *head = NULL;
        int count = checkpoint_list_checkpoints(component_name, 1, &head); /* persistent storage */

        if (count < 0) {
            rlen = snprintf(response, sizeof(response),
                            "Error: failed to list checkpoints\n");
        } else if (count == 0) {
            if (component_name) {
                rlen = snprintf(response, sizeof(response),
                                "No checkpoints found for component '%s'\n", component_name);
            } else {
                rlen = snprintf(response, sizeof(response),
                                "No checkpoints found\n");
            }
        } else {
            int pos = snprintf(response, sizeof(response),
                              "Available checkpoints%s%s:\n",
                              component_name ? " for " : "",
                              component_name ? component_name : "");

            checkpoint_entry_t *current = head;
            while (current && pos < (int)sizeof(response) - 100) {
                char time_str[64];
                struct tm *tm_info = localtime(&current->metadata.timestamp);
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

                pos += snprintf(response + pos, sizeof(response) - pos,
                               "  %s: %s (%s, %zu bytes)\n",
                               current->id,
                               current->metadata.component_name,
                               time_str,
                               current->metadata.image_size);
                current = current->next;
            }
            rlen = pos;
        }

        checkpoint_free_list(head);

    } else if (strncmp(cmd, "checkpoint-rm", 13) == 0) {
        /* Remove specific checkpoint */
        char component_name[128] = {0};
        char checkpoint_id[64] = {0};

        /* Parse "checkpoint-rm <component> <checkpoint_id>" */
        int args_parsed = sscanf(cmd, "checkpoint-rm %127s %63s", component_name, checkpoint_id);

        if (args_parsed < 2) {
            rlen = snprintf(response, sizeof(response),
                            "Error: checkpoint-rm command requires component name and checkpoint ID\n"
                            "Usage: checkpoint-rm <component_name> <checkpoint_id>\n");
        } else {
            int result = checkpoint_remove(component_name, checkpoint_id, 1); /* persistent storage */

            if (result == 0) {
                rlen = snprintf(response, sizeof(response),
                                "Checkpoint %s removed successfully for component '%s'\n",
                                checkpoint_id, component_name);
            } else {
                rlen = snprintf(response, sizeof(response),
                                "Error: failed to remove checkpoint %s for component '%s'\n",
                                checkpoint_id, component_name);
            }
        }

    } else if (strncmp(cmd, "migrate", 7) == 0) {
        /* Checkpoint and prepare for migration */
        const char *component_name = NULL;
        if (strlen(cmd) > 8) {
            component_name = cmd + 8; /* Skip "migrate " */
        }

        if (!component_name || strlen(component_name) == 0) {
            rlen = snprintf(response, sizeof(response),
                            "Error: migrate command requires component name\n"
                            "Usage: migrate <component_name>\n");
        } else {
            /* First create a checkpoint */
            int result = component_checkpoint(component_name);
            if (result == 0) {
                /* Find the latest checkpoint ID */
                char latest_id[CHECKPOINT_ID_MAX_LEN];
                char checkpoint_path[MAX_CHECKPOINT_PATH];

                if (checkpoint_find_latest(component_name, 1, /* persistent storage */
                                          latest_id, sizeof(latest_id),
                                          checkpoint_path, sizeof(checkpoint_path)) == 0) {
                    rlen = snprintf(response, sizeof(response),
                                    "Component '%s' checkpointed successfully for migration\n"
                                    "Checkpoint ID: %s\n"
                                    "Path: %s\n"
                                    "Use 'checkpoint-archive %s %s <archive_path>' to create portable archive\n",
                                    component_name, latest_id, checkpoint_path,
                                    component_name, latest_id);
                } else {
                    rlen = snprintf(response, sizeof(response),
                                    "Component '%s' checkpointed, but unable to determine checkpoint ID\n",
                                    component_name);
                }
            } else if (result == -1) {
                rlen = snprintf(response, sizeof(response),
                                "Error: component '%s' not found\n", component_name);
            } else if (result == -2) {
                rlen = snprintf(response, sizeof(response),
                                "Error: CRIU not supported on this system\n");
            } else if (result == -3) {
                rlen = snprintf(response, sizeof(response),
                                "Error: component '%s' is not currently active\n", component_name);
            } else {
                rlen = snprintf(response, sizeof(response),
                                "Error: migration checkpoint failed for component '%s' (error code %d)\n",
                                component_name, result);
            }
        }

    } else if (strcmp(cmd, "check-cycles") == 0) {
        /* Detect and report dependency cycles */
        cycle_info_t cycle_info;
        int result = graph_detect_cycles(&cycle_info);

        if (result < 0) {
            rlen = snprintf(response, sizeof(response),
                            "Error: failed to perform cycle detection\n");
        } else if (result == 1) {
            rlen = snprintf(response, sizeof(response),
                            "CYCLE DETECTED: %s\n\n", cycle_info.error_message);

            if (cycle_info.cycle_length > 0) {
                rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                "Components involved in the cycle:\n");
                for (int i = 0; i < cycle_info.cycle_length - 1; i++) {
                    int comp_idx = cycle_info.cycle_components[i];
                    if (comp_idx < n_components) {
                        rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                        "  %d. %s\n", i + 1, components[comp_idx].name);
                    }
                }
            }
            free(cycle_info.cycle_components);
        } else {
            rlen = snprintf(response, sizeof(response),
                            "✓ No dependency cycles detected\n"
                            "The component graph is valid.\n");
        }

    } else if (strcmp(cmd, "analyze") == 0) {
        /* Show comprehensive graph analysis and metrics */
        graph_metrics_t metrics;
        int result = graph_analyze_metrics(&metrics);

        if (result < 0) {
            rlen = snprintf(response, sizeof(response),
                            "Error: failed to analyze graph metrics\n");
        } else {
            rlen = snprintf(response, sizeof(response),
                            "GRAPH ANALYSIS\n"
                            "══════════════\n\n"
                            "Components:               %d\n"
                            "Capabilities:             %d\n"
                            "Total Dependencies:       %d\n"
                            "Avg Dependencies/Comp:    %.2f\n"
                            "Max Dependency Depth:     %d\n"
                            "Strongly Connected Comp:  %d\n\n",
                            metrics.total_components,
                            metrics.total_capabilities,
                            metrics.total_edges,
                            metrics.average_dependencies_per_component,
                            metrics.max_dependency_depth,
                            metrics.strongly_connected_components);

            /* Add cycle check */
            cycle_info_t cycle_info;
            int cycle_result = graph_detect_cycles(&cycle_info);
            if (cycle_result == 1) {
                rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                "⚠️  WARNING: Dependency cycles detected!\n"
                                "   %s\n", cycle_info.error_message);
                free(cycle_info.cycle_components);
            } else if (cycle_result == 0) {
                rlen += snprintf(response + rlen, sizeof(response) - rlen,
                                "✓ Graph Status: No cycles detected\n");
            }
        }

    } else if (strcmp(cmd, "validate") == 0) {
        /* Validate current graph configuration */
        int result = validate_component_graph(1); /* warn_only = 1 for status check */

        if (result == 0) {
            rlen = snprintf(response, sizeof(response),
                            "✓ Graph validation passed\n"
                            "  No dependency cycles detected\n"
                            "  All components have valid configurations\n");
        } else {
            rlen = snprintf(response, sizeof(response),
                            "⚠️  Graph validation found issues\n"
                            "  Check logs for detailed cycle information\n");
        }

    } else if (strncmp(cmd, "path", 4) == 0) {
        /* Show dependency path between capabilities */
        const char *args = cmd + 4;
        while (*args == ' ') args++; /* skip spaces */

        /* Parse capability names */
        char cap1[128], cap2[128];
        if (sscanf(args, "%127s %127s", cap1, cap2) != 2) {
            rlen = snprintf(response, sizeof(response),
                            "Error: path command requires two capability names\n"
                            "Usage: path <capability1> <capability2>\n");
        } else {
            char path_desc[512];
            int result = graph_find_dependency_path(cap1, cap2, path_desc, sizeof(path_desc));

            if (result == 0) {
                rlen = snprintf(response, sizeof(response),
                                "Dependency path from '%s' to '%s':\n%s\n", cap1, cap2, path_desc);
            } else {
                rlen = snprintf(response, sizeof(response),
                                "Error: could not find dependency path from '%s' to '%s'\n", cap1, cap2);
            }
        }

    } else if (strcmp(cmd, "scc") == 0) {
        /* Show strongly connected components */
        int *scc_components = NULL;
        int scc_count = 0;
        int result = graph_find_strongly_connected_components(&scc_components, &scc_count);

        if (result < 0) {
            rlen = snprintf(response, sizeof(response),
                            "Error: failed to find strongly connected components\n");
        } else if (scc_count == 0) {
            rlen = snprintf(response, sizeof(response),
                            "No strongly connected components found\n"
                            "(This feature is not yet fully implemented)\n");
        } else {
            rlen = snprintf(response, sizeof(response),
                            "Found %d strongly connected components\n", scc_count);
        }

        if (scc_components) {
            free(scc_components);
        }

    } else if (strncmp(cmd, "kexec", 5) == 0) {
        /* Perform live kernel upgrade */
        if (strncmp(cmd, "kexec --dry-run", 15) == 0) {
            /* Dry run mode - validate without executing */
            const char *kernel_path = NULL;
            if (strlen(cmd) > 16) {
                kernel_path = cmd + 16; /* Skip "kexec --dry-run " */
            }

            if (!kernel_path || strlen(kernel_path) == 0) {
                rlen = snprintf(response, sizeof(response),
                                "Error: kernel path required\n"
                                "Usage: kexec --dry-run <kernel_path> [--initrd <initrd_path>] [--append <cmdline>]\n");
            } else {
                /* Parse additional arguments (simplified parsing) */
                char kernel_only[MAX_KERNEL_PATH];
                sscanf(kernel_path, "%1023s", kernel_only); /* Get just the kernel path */

                LOG_INFO("performing dry-run kexec validation for kernel: %s", kernel_only);

                int result = kexec_perform(kernel_only, NULL, NULL, KEXEC_FLAG_DRY_RUN);

                if (result == KEXEC_SUCCESS) {
                    rlen = snprintf(response, sizeof(response),
                                    "✓ Dry run successful - kexec would proceed with kernel: %s\n"
                                    "  - Kernel validation: PASSED\n"
                                    "  - System readiness: READY\n"
                                    "  - CRIU support: AVAILABLE\n"
                                    "  - Checkpoint storage: ACCESSIBLE\n\n"
                                    "Use 'kexec %s' to perform the actual kernel upgrade.\n", kernel_only, kernel_only);
                } else {
                    rlen = snprintf(response, sizeof(response),
                                    "✗ Dry run failed: %s\n"
                                    "Kernel upgrade cannot proceed with current configuration.\n",
                                    kexec_error_string(result));
                }
            }
        } else {
            /* Full kexec execution */
            const char *kernel_path = NULL;
            if (strlen(cmd) > 6) {
                kernel_path = cmd + 6; /* Skip "kexec " */
            }

            if (!kernel_path || strlen(kernel_path) == 0) {
                rlen = snprintf(response, sizeof(response),
                                "Error: kernel path required\n"
                                "Usage: kexec <kernel_path> [--initrd <initrd_path>] [--append <cmdline>]\n"
                                "       kexec --dry-run <kernel_path> [options]\n\n"
                                "Examples:\n"
                                "  kexec /boot/vmlinuz-6.1.0-new\n"
                                "  kexec /boot/vmlinuz-6.1.0-new --initrd /boot/initrd.img-6.1.0-new\n"
                                "  kexec --dry-run /boot/vmlinuz-6.1.0-new  # Test without executing\n\n"
                                "WARNING: This will replace the running kernel. All processes will be\n"
                                "checkpointed and restored, but this is a dangerous operation!\n");
            } else {
                /* Parse arguments (simplified parsing) */
                char kernel_only[MAX_KERNEL_PATH];
                char *initrd_path = NULL;
                char *cmdline = NULL;

                /* Extract just the kernel path for now (full parsing would be more complex) */
                sscanf(kernel_path, "%1023s", kernel_only);

                /* Look for --initrd option */
                char *initrd_opt = strstr(kernel_path, "--initrd");
                if (initrd_opt) {
                    /* This is simplified - production would need proper argument parsing */
                    static char initrd_buf[MAX_KERNEL_PATH];
                    if (sscanf(initrd_opt, "--initrd %1023s", initrd_buf) == 1) {
                        initrd_path = initrd_buf;
                    }
                }

                /* Look for --append option */
                char *append_opt = strstr(kernel_path, "--append");
                if (append_opt) {
                    /* This is simplified - production would need proper quoted string parsing */
                    static char cmdline_buf[MAX_CMDLINE_LEN];
                    if (sscanf(append_opt, "--append \"%2047[^\"]\"", cmdline_buf) == 1) {
                        cmdline = cmdline_buf;
                    }
                }

                LOG_INFO("initiating live kernel upgrade: kernel=%s, initrd=%s, cmdline=%s",
                         kernel_only, initrd_path ? initrd_path : "none", cmdline ? cmdline : "default");

                rlen = snprintf(response, sizeof(response),
                                "=== LIVE KERNEL UPGRADE INITIATED ===\n"
                                "Target kernel: %s\n"
                                "Initrd: %s\n"
                                "Command line: %s\n\n"
                                "Phase 1: Validation...\n", kernel_only,
                                initrd_path ? initrd_path : "none",
                                cmdline ? cmdline : "default");

                (void)!write(client_fd, response, rlen); /* Send status update */

                /* Perform the kexec - this should not return on success */
                int result = kexec_perform(kernel_only, initrd_path, cmdline, KEXEC_FLAG_NONE);

                /* If we get here, kexec failed */
                rlen = snprintf(response, sizeof(response),
                                "\n✗ KEXEC FAILED: %s\n"
                                "The kernel upgrade did not complete successfully.\n"
                                "System remains on current kernel.\n", kexec_error_string(result));
            }
        }

    } else {
        rlen = snprintf(response, sizeof(response),
                        "Unknown command: %s\n"
                        "Available commands: status, caps, tree <component>, rdeps <capability>, simulate remove <component>, dot, log <component> [lines], readiness, check-readiness [component], upgrade <component>, check-cycles, analyze, validate, path <cap1> <cap2>, scc, checkpoint <component>, restore <component> [checkpoint_id], checkpoint-list [component], checkpoint-rm <component> <checkpoint_id>, migrate <component>, kexec <kernel> [--initrd <initrd>] [--append <cmdline>], kexec --dry-run <kernel> [options]\n", cmd);
    }

    (void)!write(client_fd, response, rlen);
}