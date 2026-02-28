/*
 * echo-server.c - Test TCP echo server for YakirOS hot-swap testing
 *
 * This server demonstrates hot-swap capability by:
 * 1. Listening on a TCP port and echoing data back to clients
 * 2. Handling SIGUSR1 by passing its listen socket over HANDOFF_FD
 * 3. Accepting an inherited listen socket via HANDOFF_FD on startup
 *
 * Usage:
 *   echo-server <port>                # Normal startup
 *   HANDOFF_FD=4 echo-server <port>   # Hot-swap startup (inherits socket)
 *
 * Build: cc -o echo-server echo-server.c ../src/handoff.c ../src/log.c
 */

#define _GNU_SOURCE

#include "../src/handoff.h"
#include "../src/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/types.h>
#include <fcntl.h>

/* Global state */
static int listen_fd = -1;
static int handoff_fd = -1;
static volatile int should_handoff = 0;
static int server_port = 0;

/* Signal handler for SIGUSR1 - initiate handoff */
void handoff_signal_handler(int sig) {
    (void)sig;
    should_handoff = 1;
}

/* Create and bind a TCP listen socket */
int create_listen_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERR("socket failed: %s", strerror(errno));
        return -1;
    }

    /* Set SO_REUSEADDR to avoid "Address already in use" errors */
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_WARN("setsockopt SO_REUSEADDR failed: %s", strerror(errno));
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERR("bind to port %d failed: %s", port, strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, 5) < 0) {
        LOG_ERR("listen failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    LOG_INFO("listening on port %d (fd %d)", port, fd);
    return fd;
}

/* Perform handoff by sending listen socket over handoff_fd */
int perform_handoff(void) {
    if (handoff_fd < 0) {
        LOG_ERR("handoff requested but no HANDOFF_FD available");
        return -1;
    }

    LOG_INFO("performing handoff: sending listen socket fd %d", listen_fd);

    /* Send the listen socket over the handoff channel */
    if (send_fds(handoff_fd, &listen_fd, 1) != 0) {
        LOG_ERR("failed to send listen socket during handoff");
        return -1;
    }

    /* Send handoff completion message */
    if (send_handoff_complete(handoff_fd) != 0) {
        LOG_ERR("failed to send handoff complete message");
        return -1;
    }

    LOG_INFO("handoff complete - old process exiting");
    close(handoff_fd);
    close(listen_fd);
    return 0;
}

/* Handle client connection - simple echo loop */
void handle_client(int client_fd) {
    char buffer[1024];
    ssize_t bytes;

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    getpeername(client_fd, (struct sockaddr *)&client_addr, &client_len);

    LOG_INFO("client connected from %s:%d (fd %d)",
             inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_fd);

    while ((bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes] = '\0';

        /* Echo the data back */
        if (send(client_fd, buffer, bytes, 0) != bytes) {
            LOG_ERR("send failed: %s", strerror(errno));
            break;
        }

        /* Check for handoff signal during client handling */
        if (should_handoff) {
            LOG_INFO("handoff requested during client session");
            break;
        }
    }

    if (bytes < 0 && errno != ECONNRESET) {
        LOG_ERR("recv failed: %s", strerror(errno));
    }

    LOG_INFO("client disconnected (fd %d)", client_fd);
    close(client_fd);
}

/* Main server loop */
int run_server(void) {
    LOG_INFO("echo server running on port %d (pid %d)", server_port, getpid());

    struct pollfd pfd;
    pfd.fd = listen_fd;
    pfd.events = POLLIN;

    while (!should_handoff) {
        pfd.revents = 0;

        /* Poll with 1 second timeout to check for handoff signal */
        int poll_result = poll(&pfd, 1, 1000);

        if (poll_result < 0) {
            if (errno == EINTR) continue; /* Interrupted by signal */
            LOG_ERR("poll failed: %s", strerror(errno));
            return -1;
        }

        if (poll_result == 0) continue; /* Timeout - check handoff flag and continue */

        /* New connection available */
        if (pfd.revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
            if (client_fd < 0) {
                LOG_ERR("accept failed: %s", strerror(errno));
                continue;
            }

            /* Handle client in child process for simplicity */
            pid_t child_pid = fork();
            if (child_pid == 0) {
                /* Child process - handle client */
                close(listen_fd);
                handle_client(client_fd);
                exit(0);
            } else if (child_pid > 0) {
                /* Parent process - close client fd and continue */
                close(client_fd);
            } else {
                LOG_ERR("fork failed: %s", strerror(errno));
                close(client_fd);
            }
        }
    }

    LOG_INFO("server loop exiting for handoff");
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    server_port = atoi(argv[1]);
    if (server_port <= 0 || server_port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return 1;
    }

    /* Set up signal handler for handoff */
    signal(SIGUSR1, handoff_signal_handler);

    /* Check if we're starting with inherited socket (hot-swap mode) */
    char *handoff_fd_str = getenv(HANDOFF_FD_ENV);
    if (handoff_fd_str) {
        handoff_fd = atoi(handoff_fd_str);
        LOG_INFO("hot-swap startup detected, HANDOFF_FD=%d", handoff_fd);

        /* Receive the listen socket from old process */
        int received_fd;
        int n_received = recv_fds(handoff_fd, &received_fd, 1);
        if (n_received != 1) {
            LOG_ERR("failed to receive listen socket during hot-swap startup");
            return 1;
        }

        listen_fd = received_fd;
        LOG_INFO("inherited listen socket fd %d from previous instance", listen_fd);

    } else {
        /* Normal startup - create listen socket */
        listen_fd = create_listen_socket(server_port);
        if (listen_fd < 0) {
            return 1;
        }
    }

    /* Run the main server loop */
    int result = run_server();

    /* If handoff was requested, perform it */
    if (should_handoff) {
        if (perform_handoff() == 0) {
            return 0; /* Clean handoff exit */
        } else {
            return 1; /* Handoff failed */
        }
    }

    /* Clean shutdown */
    close(listen_fd);
    return result;
}