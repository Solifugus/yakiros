/*
 * graphctl - CLI for the YakirOS graph resolver
 *
 * Communicates with graph-resolver via unix socket.
 *
 * Build: musl-gcc -static -O2 -o graphctl graphctl.c
 *        (or: gcc -static -O2 -o graphctl graphctl.c)
 *
 * Usage:
 *   graphctl status                    Show all components and capabilities
 *   graphctl readiness                 Show detailed readiness information
 *   graphctl check-readiness [name]    Trigger readiness check for component(s)
 *   graphctl pending                   Show components waiting on dependencies
 *   graphctl resolve                   Trigger graph re-resolution
 *   graphctl tree <name>               Show dependency tree for a component
 *   graphctl reload                    Reload all component declarations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define CONTROL_SOCKET "/tmp/graph-resolver.sock"
#define BUF_SIZE 8192

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: graphctl <command> [args...]\n");
        fprintf(stderr, "Commands:\n");
        fprintf(stderr, "  status                    Show all components and capabilities\n");
        fprintf(stderr, "  readiness                 Show detailed readiness information\n");
        fprintf(stderr, "  check-readiness [name]    Trigger readiness check for component(s)\n");
        fprintf(stderr, "  pending                   Show components waiting on dependencies\n");
        fprintf(stderr, "  resolve                   Trigger graph re-resolution\n");
        fprintf(stderr, "  tree <name>               Show dependency tree for a component\n");
        fprintf(stderr, "  reload                    Reload all component declarations\n");
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
    int n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        fputs(buf, stdout);
    }

    close(fd);
    return 0;
}
