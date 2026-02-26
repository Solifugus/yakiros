/*
 * hotswap.h - YakirOS Hot-Swappable Services Implementation
 *
 * This module implements zero-downtime service upgrades through file descriptor
 * passing between old and new service processes. This revolutionary capability
 * enables services to be upgraded without dropping any connections or losing state.
 */

#ifndef HOTSWAP_H
#define HOTSWAP_H

#include "toml.h"
#include <sys/types.h>

/* Hot-swap states for tracking swap progress */
typedef enum {
    SWAP_NONE,           /* No swap in progress */
    SWAP_PREPARING,      /* New process starting, waiting for readiness */
    SWAP_READY,          /* New process ready, awaiting handoff */
    SWAP_TRANSFERRING,   /* File descriptors being transferred */
    SWAP_COMPLETING,     /* Old process shutting down */
    SWAP_FAILED,         /* Swap failed, rollback if possible */
} swap_state_t;

/* Hot-swap tracking structure */
typedef struct {
    int old_component_idx;       /* Index of component being replaced */
    int new_component_idx;       /* Index of new component version */
    swap_state_t state;          /* Current swap state */
    time_t swap_start;           /* When swap process started */
    int timeout;                 /* Swap timeout in seconds */
    char swap_id[64];            /* Unique swap identifier */

    /* File descriptor tracking */
    int *fds_to_transfer;        /* Array of FDs to transfer */
    int n_fds;                   /* Number of FDs to transfer */

    /* Socket for fd-passing communication */
    int swap_socket_pair[2];     /* Unix socket pair for communication */
} hotswap_context_t;

/* Initialize hot-swap subsystem */
int hotswap_init(void);

/* Start a hot-swap operation for a component */
int hotswap_start(int component_idx, const char *new_binary_path);

/* Check if a component can be hot-swapped */
int hotswap_supported(int component_idx);

/* Get file descriptors that need to be transferred */
int hotswap_get_transfer_fds(int component_idx, int **fds, int *count);

/* Perform file descriptor passing between processes */
int hotswap_transfer_fds(hotswap_context_t *ctx);

/* Complete hot-swap by terminating old process */
int hotswap_complete(hotswap_context_t *ctx);

/* Abort hot-swap and clean up */
int hotswap_abort(hotswap_context_t *ctx);

/* Check hot-swap timeouts and handle failures */
void hotswap_check_timeouts(void);

/* Get current hot-swap contexts (for monitoring) */
hotswap_context_t* hotswap_get_contexts(int *count);

/* Hot-swap protocol messages */
#define HOTSWAP_MSG_READY        "READY"
#define HOTSWAP_MSG_TRANSFER     "TRANSFER"
#define HOTSWAP_MSG_COMPLETE     "COMPLETE"
#define HOTSWAP_MSG_ABORT        "ABORT"
#define HOTSWAP_MSG_ACK          "ACK"
#define HOTSWAP_MSG_ERROR        "ERROR"

/* Hot-swap timeouts */
#define HOTSWAP_DEFAULT_TIMEOUT  60  /* 60 seconds default timeout */
#define HOTSWAP_TRANSFER_TIMEOUT 10  /* 10 seconds for fd transfer */

#endif /* HOTSWAP_H */