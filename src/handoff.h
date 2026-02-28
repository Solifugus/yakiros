/*
 * handoff.h - YakirOS hot-swap file descriptor passing
 *
 * Implements Unix domain socket file descriptor passing using SCM_RIGHTS
 * for zero-downtime service upgrades. This enables the core hot-swap
 * capability that makes YakirOS truly rebootless.
 */

#ifndef HANDOFF_H
#define HANDOFF_H

#include <sys/types.h>

/* Maximum number of file descriptors that can be passed in one message */
#define MAX_FDS_PER_MSG 32

/* Standard file descriptor number used for handoff socket */
#define HANDOFF_FD 4

/* Environment variable name for handoff socket */
#define HANDOFF_FD_ENV "HANDOFF_FD"

/* Protocol messages */
#define HANDOFF_COMPLETE_MSG "HANDOFF_COMPLETE\n"
#define HANDOFF_COMPLETE_LEN 16

/* Send file descriptors over a Unix domain socket using SCM_RIGHTS
 *
 * sock: Unix domain socket
 * fds: Array of file descriptors to send
 * n_fds: Number of file descriptors (max MAX_FDS_PER_MSG)
 *
 * Returns: 0 on success, -1 on error
 */
int send_fds(int sock, int *fds, int n_fds);

/* Receive file descriptors from a Unix domain socket using SCM_RIGHTS
 *
 * sock: Unix domain socket
 * fds: Buffer to store received file descriptors
 * max_fds: Maximum number of file descriptors to receive
 *
 * Returns: Number of file descriptors received, -1 on error
 */
int recv_fds(int sock, int *fds, int max_fds);

/* Send a handoff completion message over the socket
 *
 * sock: Unix domain socket
 *
 * Returns: 0 on success, -1 on error
 */
int send_handoff_complete(int sock);

/* Wait for handoff completion message from the socket
 *
 * sock: Unix domain socket
 * timeout_ms: Timeout in milliseconds (0 for blocking)
 *
 * Returns: 0 on success, -1 on error/timeout
 */
int wait_handoff_complete(int sock, int timeout_ms);

/* Create a Unix domain socketpair for handoff communication
 *
 * socks: Array of 2 ints to store the socket pair
 *
 * Returns: 0 on success, -1 on error
 */
int create_handoff_socketpair(int socks[2]);

#endif /* HANDOFF_H */