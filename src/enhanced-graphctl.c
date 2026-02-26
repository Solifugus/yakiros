/*
 * enhanced-graphctl.c - Enhanced CLI for YakirOS with Hot-Swap Support
 *
 * Extended version of graphctl that includes hot-swappable services management.
 * This demonstrates the revolutionary zero-downtime upgrade capabilities.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define CONTROL_SOCKET "/run/graph-resolver.sock"
#define BUF_SIZE 8192

static void show_usage(void) {
    fprintf(stderr, "Enhanced GraphCtl - YakirOS Hot-Swap Management\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage: graphctl <command> [args...]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Standard Commands:\n");
    fprintf(stderr, "  status                    Show all components and capabilities\n");
    fprintf(stderr, "  readiness                 Show detailed readiness information\n");
    fprintf(stderr, "  check-readiness [name]    Trigger readiness check for component(s)\n");
    fprintf(stderr, "  pending                   Show components waiting on dependencies\n");
    fprintf(stderr, "  resolve                   Trigger graph re-resolution\n");
    fprintf(stderr, "  tree <name>               Show dependency tree for a component\n");
    fprintf(stderr, "  reload                    Reload all component declarations\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "🔥 Hot-Swap Commands (REVOLUTIONARY!):\n");
    fprintf(stderr, "  swap <component> <binary> Start hot-swap of component to new binary\n");
    fprintf(stderr, "  swap-status               Show all active hot-swap operations\n");
    fprintf(stderr, "  swap-abort <swap-id>      Abort a hot-swap operation\n");
    fprintf(stderr, "  swap-supported <comp>     Check if component supports hot-swapping\n");
    fprintf(stderr, "  swap-fds <component>      Show transferrable file descriptors\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  graphctl swap sshd /usr/sbin/sshd-new    # Upgrade SSH without dropping connections\n");
    fprintf(stderr, "  graphctl swap nginx /usr/bin/nginx-v2    # Zero-downtime web server upgrade\n");
    fprintf(stderr, "  graphctl swap-status                     # Monitor hot-swap progress\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "🌟 Hot-swappable services enable ZERO-DOWNTIME upgrades!\n");
}

static int send_command(const char *socket_path, const char *command) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    if (write(sock, command, strlen(command)) < 0) {
        perror("write");
        close(sock);
        return -1;
    }

    char buf[BUF_SIZE];
    ssize_t n = read(sock, buf, BUF_SIZE - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    close(sock);
    return 0;
}

static void demonstrate_hotswap_capability(void) {
    printf("============================================\n");
    printf("  🔥 YakirOS Hot-Swappable Services Demo\n");
    printf("============================================\n");
    printf("\n");
    printf("YakirOS enables ZERO-DOWNTIME service upgrades through\n");
    printf("revolutionary file descriptor passing technology!\n");
    printf("\n");
    printf("🎯 How Hot-Swap Works:\n");
    printf("\n");
    printf("  1. 🔴 Old Service Running    → Serving connections on socket FDs\n");
    printf("  2. 🟡 New Service Starting   → Starts with 'YAKIROS_HOTSWAP=1' env\n");
    printf("  3. 🔵 Readiness Check        → New service signals when ready\n");
    printf("  4. 🟠 FD Transfer           → Socket FDs passed to new process\n");
    printf("  5. 🟢 Seamless Handoff      → New service takes over, old terminates\n");
    printf("  6. ✅ Zero Downtime         → No connections dropped!\n");
    printf("\n");
    printf("🚀 Example Hot-Swap Operations:\n");
    printf("\n");
    printf("  # Upgrade SSH server without dropping connections\n");
    printf("  graphctl swap sshd /usr/sbin/sshd-new\n");
    printf("\n");
    printf("  # Zero-downtime web server upgrade\n");
    printf("  graphctl swap nginx /opt/nginx-v2/sbin/nginx\n");
    printf("\n");
    printf("  # Hot-swap database with connection preservation\n");
    printf("  graphctl swap postgres /usr/bin/postgres-14.1\n");
    printf("\n");
    printf("⚡ Revolutionary Benefits:\n");
    printf("  • Web servers: No HTTP request drops\n");
    printf("  • SSH servers: No terminal disconnections\n");
    printf("  • Databases: No connection pool disruption\n");
    printf("  • APIs: No service interruption\n");
    printf("  • Any service: Seamless upgrades!\n");
    printf("\n");
    printf("🌟 This makes YakirOS the FIRST init system with\n");
    printf("   true zero-downtime service upgrade capability!\n");
    printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        show_usage();
        return 1;
    }

    const char *command = argv[1];
    char *socket_path = getenv("GRAPHCTL_SOCKET");
    if (!socket_path) {
        socket_path = CONTROL_SOCKET;
    }

    /* Handle hot-swap specific commands */
    if (strcmp(command, "swap") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Usage: graphctl swap <component> <new-binary-path>\n");
            fprintf(stderr, "Example: graphctl swap sshd /usr/sbin/sshd-new\n");
            return 1;
        }

        printf("🔥 Starting hot-swap for '%s' -> '%s'\n", argv[2], argv[3]);

        char swap_cmd[1024];
        snprintf(swap_cmd, sizeof(swap_cmd), "hotswap-start %s %s", argv[2], argv[3]);

        return send_command(socket_path, swap_cmd);
    }

    else if (strcmp(command, "swap-status") == 0) {
        printf("🔍 Hot-Swap Operations Status:\n\n");
        return send_command(socket_path, "hotswap-status");
    }

    else if (strcmp(command, "swap-abort") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: graphctl swap-abort <swap-id>\n");
            return 1;
        }

        printf("❌ Aborting hot-swap: %s\n", argv[2]);

        char abort_cmd[1024];
        snprintf(abort_cmd, sizeof(abort_cmd), "hotswap-abort %s", argv[2]);

        return send_command(socket_path, abort_cmd);
    }

    else if (strcmp(command, "swap-supported") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: graphctl swap-supported <component>\n");
            return 1;
        }

        char check_cmd[1024];
        snprintf(check_cmd, sizeof(check_cmd), "hotswap-supported %s", argv[2]);

        return send_command(socket_path, check_cmd);
    }

    else if (strcmp(command, "swap-fds") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: graphctl swap-fds <component>\n");
            return 1;
        }

        printf("📁 File Descriptors for '%s':\n\n", argv[2]);

        char fds_cmd[1024];
        snprintf(fds_cmd, sizeof(fds_cmd), "hotswap-fds %s", argv[2]);

        return send_command(socket_path, fds_cmd);
    }

    else if (strcmp(command, "demo-hotswap") == 0) {
        demonstrate_hotswap_capability();
        return 0;
    }

    else {
        /* Handle standard graphctl commands */
        char cmd[1024];
        int offset = 0;

        for (int i = 1; i < argc; i++) {
            if (i > 1) cmd[offset++] = ' ';
            int n = snprintf(cmd + offset, sizeof(cmd) - offset, "%s", argv[i]);
            offset += n;
        }

        return send_command(socket_path, cmd);
    }

    return 0;
}