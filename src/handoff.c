/*
 * handoff.c - YakirOS hot-swap file descriptor passing implementation
 *
 * Implements Unix domain socket file descriptor passing using SCM_RIGHTS.
 * This enables zero-downtime service upgrades by passing open file
 * descriptors from old process to new process.
 */

#define _GNU_SOURCE

#include "handoff.h"
#include "log.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <stdlib.h>

int send_fds(int sock, int *fds, int n_fds) {
    if (sock < 0 || !fds || n_fds <= 0 || n_fds > MAX_FDS_PER_MSG) {
        errno = EINVAL;
        return -1;
    }

    struct msghdr msg;
    struct iovec iov;
    char dummy_data = 1;

    /* Allocate control message buffer for SCM_RIGHTS */
    size_t cmsg_len = CMSG_SPACE(n_fds * sizeof(int));
    char *cmsg_buf = malloc(cmsg_len);
    if (!cmsg_buf) {
        LOG_ERR("malloc failed for control message buffer");
        return -1;
    }

    /* Set up message structure */
    memset(&msg, 0, sizeof(msg));

    /* We need to send at least 1 byte of data along with the fds */
    iov.iov_base = &dummy_data;
    iov.iov_len = 1;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    /* Set up control message for SCM_RIGHTS */
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = cmsg_len;

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(n_fds * sizeof(int));

    /* Copy file descriptors into control message data */
    memcpy(CMSG_DATA(cmsg), fds, n_fds * sizeof(int));

    /* Send the message */
    ssize_t result = sendmsg(sock, &msg, 0);

    free(cmsg_buf);

    if (result < 0) {
        LOG_ERR("sendmsg failed for fd passing: %s", strerror(errno));
        return -1;
    }

    LOG_INFO("sent %d file descriptors over handoff socket", n_fds);
    return 0;
}

int recv_fds(int sock, int *fds, int max_fds) {
    if (sock < 0 || !fds || max_fds <= 0) {
        errno = EINVAL;
        return -1;
    }

    struct msghdr msg;
    struct iovec iov;
    char dummy_data;

    /* Allocate control message buffer for SCM_RIGHTS */
    size_t cmsg_len = CMSG_SPACE(max_fds * sizeof(int));
    char *cmsg_buf = malloc(cmsg_len);
    if (!cmsg_buf) {
        LOG_ERR("malloc failed for control message buffer");
        return -1;
    }

    /* Set up message structure */
    memset(&msg, 0, sizeof(msg));

    iov.iov_base = &dummy_data;
    iov.iov_len = 1;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    msg.msg_control = cmsg_buf;
    msg.msg_controllen = cmsg_len;

    /* Receive the message */
    ssize_t result = recvmsg(sock, &msg, 0);

    if (result < 0) {
        LOG_ERR("recvmsg failed for fd passing: %s", strerror(errno));
        free(cmsg_buf);
        return -1;
    }

    /* Extract file descriptors from control message */
    int n_fds = 0;
    struct cmsghdr *cmsg;

    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            size_t data_len = cmsg->cmsg_len - CMSG_LEN(0);
            n_fds = data_len / sizeof(int);

            if (n_fds > max_fds) {
                LOG_WARN("received %d fds but can only handle %d, truncating", n_fds, max_fds);
                n_fds = max_fds;
            }

            memcpy(fds, CMSG_DATA(cmsg), n_fds * sizeof(int));
            break;
        }
    }

    free(cmsg_buf);

    if (n_fds == 0) {
        LOG_WARN("received message but no file descriptors found");
        return 0;
    }

    LOG_INFO("received %d file descriptors over handoff socket", n_fds);
    return n_fds;
}

int send_handoff_complete(int sock) {
    if (sock < 0) {
        errno = EINVAL;
        return -1;
    }

    ssize_t written = write(sock, HANDOFF_COMPLETE_MSG, HANDOFF_COMPLETE_LEN);
    if (written != HANDOFF_COMPLETE_LEN) {
        LOG_ERR("failed to send handoff complete message: %s", strerror(errno));
        return -1;
    }

    LOG_INFO("sent handoff complete message");
    return 0;
}

int wait_handoff_complete(int sock, int timeout_ms) {
    if (sock < 0) {
        errno = EINVAL;
        return -1;
    }

    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int poll_result = poll(&pfd, 1, timeout_ms);
    if (poll_result < 0) {
        LOG_ERR("poll failed waiting for handoff complete: %s", strerror(errno));
        return -1;
    }

    if (poll_result == 0) {
        LOG_WARN("timeout waiting for handoff complete message");
        errno = ETIMEDOUT;
        return -1;
    }

    char buffer[HANDOFF_COMPLETE_LEN + 1];
    ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
        LOG_ERR("failed to read handoff complete message: %s", strerror(errno));
        return -1;
    }

    buffer[bytes_read] = '\0';

    if (strncmp(buffer, HANDOFF_COMPLETE_MSG, HANDOFF_COMPLETE_LEN) == 0) {
        LOG_INFO("received handoff complete message");
        return 0;
    } else {
        LOG_ERR("received invalid handoff message: '%s'", buffer);
        errno = EPROTO;
        return -1;
    }
}

int create_handoff_socketpair(int socks[2]) {
    if (!socks) {
        errno = EINVAL;
        return -1;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, socks) < 0) {
        LOG_ERR("socketpair failed for handoff: %s", strerror(errno));
        return -1;
    }

    LOG_INFO("created handoff socketpair: %d <-> %d", socks[0], socks[1]);
    return 0;
}