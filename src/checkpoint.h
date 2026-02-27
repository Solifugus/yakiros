/*
 * checkpoint.h - YakirOS CRIU integration for process checkpoint/restore
 *
 * Provides low-level interface to CRIU (Checkpoint/Restore In Userspace)
 * for complete process state preservation during hot-swap operations.
 *
 * This extends YakirOS beyond simple file descriptor passing to full
 * memory state preservation, enabling true zero-downtime upgrades with
 * complete state continuity.
 */

#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include <sys/types.h>
#include <time.h>

/* Maximum path lengths for checkpoint directories and files */
#define MAX_CHECKPOINT_PATH 512
#define MAX_CHECKPOINT_ID 64

/* Default checkpoint timeout in seconds */
#define CHECKPOINT_DEFAULT_TIMEOUT 30

/* CRIU checkpoint result codes */
#define CHECKPOINT_SUCCESS 0
#define CHECKPOINT_ERROR_CRIU_NOT_FOUND -1
#define CHECKPOINT_ERROR_KERNEL_UNSUPPORTED -2
#define CHECKPOINT_ERROR_PROCESS_NOT_FOUND -3
#define CHECKPOINT_ERROR_PERMISSION_DENIED -4
#define CHECKPOINT_ERROR_TIMEOUT -5
#define CHECKPOINT_ERROR_IMAGE_CORRUPT -6
#define CHECKPOINT_ERROR_RESTORE_FAILED -7

/* CRIU version information */
typedef struct {
    int major;
    int minor;
    int patch;
    int gitid;
} criu_version_t;

/* Checkpoint metadata for storage and validation */
typedef struct {
    char component_name[128];           /* Component this checkpoint belongs to */
    pid_t original_pid;                 /* PID of checkpointed process */
    time_t timestamp;                   /* When checkpoint was created */
    size_t image_size;                  /* Total size of checkpoint images */
    char capabilities[512];             /* Capabilities provided by component */
    criu_version_t criu_version;        /* CRIU version used for checkpoint */
    int leave_running;                  /* Whether original process was left running */
    char preserve_fds[256];             /* File descriptors to preserve */
} checkpoint_metadata_t;

/* Check if CRIU is available and the kernel supports checkpoint/restore
 *
 * This function verifies:
 * - CRIU binary is installed and executable
 * - Kernel has necessary CONFIG_CHECKPOINT_RESTORE support
 * - Current user has sufficient privileges
 *
 * Returns: CHECKPOINT_SUCCESS if supported, error code otherwise
 */
int criu_is_supported(void);

/* Get CRIU version information
 *
 * version: Pointer to structure to fill with version info
 *
 * Returns: CHECKPOINT_SUCCESS on success, error code on failure
 */
int criu_get_version(criu_version_t *version);

/* Checkpoint a running process using CRIU
 *
 * pid: Process ID to checkpoint
 * image_dir: Directory to store checkpoint images (must exist)
 * leave_running: If 1, process continues running; if 0, process exits after checkpoint
 *
 * The image_dir will contain CRIU-generated files:
 * - core-<PID>.img: Process core image
 * - mm-<PID>.img: Memory mappings
 * - pagemap-<PID>.img: Memory pages
 * - fs-<PID>.img: File system state
 * - etc.
 *
 * Returns: CHECKPOINT_SUCCESS on success, error code on failure
 */
int criu_checkpoint_process(pid_t pid, const char *image_dir, int leave_running);

/* Restore a process from CRIU checkpoint images
 *
 * image_dir: Directory containing checkpoint images
 *
 * This function:
 * 1. Validates checkpoint images exist and are not corrupted
 * 2. Forks and calls CRIU restore in child process
 * 3. Returns PID of restored process on success
 *
 * Returns: PID of restored process on success, negative error code on failure
 */
pid_t criu_restore_process(const char *image_dir);

/* Validate checkpoint images before attempting restore
 *
 * image_dir: Directory containing checkpoint images
 *
 * This function checks:
 * - Required CRIU image files exist
 * - Images are not corrupted (basic validation)
 * - Image compatibility with current CRIU version
 *
 * Returns: CHECKPOINT_SUCCESS if valid, error code otherwise
 */
int checkpoint_validate_image(const char *image_dir);

/* Get human-readable error message for checkpoint error code
 *
 * error_code: Error code returned by checkpoint functions
 *
 * Returns: Static string describing the error
 */
const char *checkpoint_error_string(int error_code);

/* Execute CRIU command with timeout handling
 *
 * argv: NULL-terminated array of command arguments
 * timeout_sec: Timeout in seconds (0 for no timeout)
 * output: Buffer to store command output (may be NULL)
 * output_size: Size of output buffer
 *
 * This is an internal helper function used by other checkpoint functions.
 * It handles:
 * - Fork/exec of CRIU binary
 * - Timeout enforcement with SIGKILL
 * - Output capture for debugging
 * - Exit status interpretation
 *
 * Returns: 0 on success, negative error code on failure
 */
int execute_criu_command(char *const argv[], int timeout_sec,
                        char *output, size_t output_size);

#endif /* CHECKPOINT_H */