/*
 * graphctl - CLI for the YakirOS graph resolver
 *
 * Communicates with graph-resolver via unix socket.
 *
 * Build: musl-gcc -static -O2 -o graphctl graphctl.c
 *        (or: gcc -static -O2 -o graphctl graphctl.c)
 *
 * Usage:
 *   Basic Commands:
 *   graphctl status                    Show all components and capabilities
 *   graphctl caps                      Show all capabilities with status and provider
 *   graphctl readiness                 Show detailed readiness information
 *   graphctl check-readiness [name]    Trigger readiness check for component(s)
 *   graphctl pending                   Show components waiting on dependencies
 *   graphctl resolve                   Trigger graph re-resolution
 *   graphctl tree <name>               Show dependency tree for a component
 *   graphctl reload                    Reload all component declarations
 *   graphctl upgrade <name>            Hot-swap upgrade component to new version
 *
 *   Graph Analysis Commands:
 *   graphctl check-cycles              Detect and report dependency cycles
 *   graphctl analyze                   Show comprehensive graph analysis and metrics
 *   graphctl validate                  Validate current graph configuration
 *   graphctl path <cap1> <cap2>        Show dependency path between capabilities
 *   graphctl scc                       Show strongly connected components
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

/* ANSI color codes */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"

/* Color state */
static int use_colors = 0;

#define CONTROL_SOCKET "/tmp/graph-resolver.sock"
#define BUF_SIZE 8192

/* Apply colors to output based on keywords */
static void colorize_output(const char *line) {
    if (!use_colors) {
        fputs(line, stdout);
        return;
    }

    /* Simple keyword-based coloring */
    if (strstr(line, "ACTIVE") || strstr(line, "DONE") || strstr(line, " UP ")) {
        printf("%s%s%s", COLOR_GREEN, line, COLOR_RESET);
    } else if (strstr(line, "FAILED") || strstr(line, "DOWN") || strstr(line, "Error:")) {
        printf("%s%s%s", COLOR_RED, line, COLOR_RESET);
    } else if (strstr(line, "STARTING") || strstr(line, "READY_WAIT") || strstr(line, "DEGRADED")) {
        printf("%s%s%s", COLOR_YELLOW, line, COLOR_RESET);
    } else if (strstr(line, "CAPABILITY") || strstr(line, "COMPONENT") || strstr(line, "Summary:") || strstr(line, "Total:")) {
        printf("%s%s%s", COLOR_CYAN, line, COLOR_RESET);
    } else {
        fputs(line, stdout);
    }
}

int main(int argc, char *argv[]) {
    /* Detect if stdout is a terminal for color output */
    use_colors = isatty(STDOUT_FILENO);

    if (argc < 2) {
        fprintf(stderr, "Usage: graphctl <command> [args...]\n");
        fprintf(stderr, "Commands:\n");
        fprintf(stderr, "  status                    Show all components and capabilities\n");
        fprintf(stderr, "  caps                      Show all capabilities with status and provider\n");
        fprintf(stderr, "  readiness                 Show detailed readiness information\n");
        fprintf(stderr, "  check-readiness [name]    Trigger readiness check for component(s)\n");
        fprintf(stderr, "  pending                   Show components waiting on dependencies\n");
        fprintf(stderr, "  resolve                   Trigger graph re-resolution\n");
        fprintf(stderr, "  tree <name>               Show dependency tree for a component\n");
        fprintf(stderr, "  reload                    Reload all component declarations\n");
        fprintf(stderr, "  upgrade <name>            Hot-swap upgrade component to new version\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Graph Analysis Commands:\n");
        fprintf(stderr, "  check-cycles              Detect and report dependency cycles\n");
        fprintf(stderr, "  analyze                   Show comprehensive graph analysis and metrics\n");
        fprintf(stderr, "  validate                  Validate current graph configuration\n");
        fprintf(stderr, "  path <cap1> <cap2>        Show dependency path between capabilities\n");
        fprintf(stderr, "  scc                       Show strongly connected components\n");
        return 1;
    }

    /* build the command string */
    char cmd[1024];
    int offset = 0;
    for (int i = 1; i < argc; i++) {
        if (i > 1) cmd[offset++] = ' ';
        int n = snprintf(cmd + offset, sizeof(cmd) - offset, "%s", argv[i]);
        offset += n;
    }

    /* connect to resolver */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, CONTROL_SOCKET, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect (is graph-resolver running?)");
        close(fd);
        return 1;
    }

    /* send command */
    (void)!write(fd, cmd, strlen(cmd));

    /* shutdown write side to signal end of command */
    shutdown(fd, SHUT_WR);

    /* read response */
    char buf[BUF_SIZE];
    char line_buf[BUF_SIZE];
    int line_pos = 0;
    int n;

    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';

        if (use_colors) {
            /* Process character by character to handle line breaks for coloring */
            for (int i = 0; i < n; i++) {
                if (buf[i] == '\n') {
                    line_buf[line_pos] = '\n';
                    line_buf[line_pos + 1] = '\0';
                    colorize_output(line_buf);
                    line_pos = 0;
                } else if (line_pos < BUF_SIZE - 2) {
                    line_buf[line_pos++] = buf[i];
                }
            }
        } else {
            /* No colors - just output directly */
            fputs(buf, stdout);
        }
    }

    /* Output any remaining line buffer */
    if (use_colors && line_pos > 0) {
        line_buf[line_pos] = '\0';
        colorize_output(line_buf);
    }

    close(fd);
    return 0;
}
