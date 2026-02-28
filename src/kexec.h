/*
 * kexec.h - Live kernel upgrade support for YakirOS
 *
 * This module implements kexec-based live kernel upgrades integrated with
 * CRIU checkpoint/restore. The goal is to enable kernel upgrades without
 * rebooting, preserving all running processes and system state.
 *
 * The kexec sequence:
 * 1. Checkpoint all managed processes using CRIU
 * 2. Save checkpoints to persistent storage (/checkpoint)
 * 3. Load new kernel into memory with kexec_load()
 * 4. Execute new kernel with kexec_exec() (no return)
 * 5. New kernel boots, graph-resolver starts as PID 1
 * 6. Detect checkpoint data and restore all processes
 * 7. System continues with new kernel, all processes preserved
 */

#ifndef KEXEC_H
#define KEXEC_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/* Maximum path lengths for kernel-related files */
#define MAX_KERNEL_PATH 1024
#define MAX_CMDLINE_LEN 2048
#define MAX_CHECKPOINT_MANIFEST_LEN 4096

/* Kexec error codes */
typedef enum {
    KEXEC_SUCCESS = 0,
    KEXEC_ERROR_INVALID_KERNEL = -1,     /* Kernel file invalid or missing */
    KEXEC_ERROR_CHECKPOINT_FAILED = -2,   /* Failed to checkpoint processes */
    KEXEC_ERROR_LOAD_FAILED = -3,        /* kexec_load() failed */
    KEXEC_ERROR_EXEC_FAILED = -4,        /* kexec() syscall failed */
    KEXEC_ERROR_PERMISSION_DENIED = -5,   /* Insufficient privileges */
    KEXEC_ERROR_SYSTEM_BUSY = -6,        /* System not ready for kexec */
    KEXEC_ERROR_INVALID_INITRD = -7,     /* Initrd file invalid or missing */
    KEXEC_ERROR_CMDLINE_TOO_LONG = -8,   /* Command line exceeds limits */
    KEXEC_ERROR_CHECKPOINT_STORAGE = -9   /* Checkpoint storage unavailable */
} kexec_error_t;

/* Kexec operation flags */
typedef enum {
    KEXEC_FLAG_NONE = 0,
    KEXEC_FLAG_DRY_RUN = 1,              /* Validate only, don't execute */
    KEXEC_FLAG_FORCE = 2,                /* Skip some safety checks */
    KEXEC_FLAG_PRESERVE_LOGS = 4         /* Keep detailed logs across kexec */
} kexec_flags_t;

/* Checkpoint manifest entry */
typedef struct {
    char component_name[256];
    char checkpoint_id[64];
    char checkpoint_path[512];
    pid_t original_pid;
    uint64_t timestamp;
    int restore_priority;                 /* Lower number = restore first */
} checkpoint_manifest_entry_t;

/* Checkpoint manifest structure */
typedef struct {
    uint32_t version;                     /* Manifest format version */
    uint32_t entry_count;                 /* Number of checkpoint entries */
    uint64_t creation_time;               /* When manifest was created */
    char old_kernel_version[256];         /* Kernel version before kexec */
    char new_kernel_path[MAX_KERNEL_PATH]; /* Path to new kernel */
    char initrd_path[MAX_KERNEL_PATH];    /* Path to initrd (optional) */
    char cmdline[MAX_CMDLINE_LEN];        /* Kernel command line */
    checkpoint_manifest_entry_t entries[]; /* Variable length array */
} checkpoint_manifest_t;

/* Kernel image validation result */
typedef struct {
    int is_valid;                         /* 1 if kernel appears valid */
    uint64_t file_size;                   /* Size of kernel file */
    char version[256];                    /* Kernel version if detectable */
    char architecture[64];                /* Target architecture */
    int has_valid_magic;                  /* 1 if magic bytes are correct */
} kernel_validation_t;

/* Core kexec functions */

/**
 * Initialize kexec subsystem
 * Must be called before other kexec functions
 */
int kexec_init(void);

/**
 * Cleanup kexec subsystem
 */
void kexec_cleanup(void);

/**
 * Validate a kernel image for kexec compatibility
 * @param kernel_path Path to kernel image file
 * @param validation Output structure with validation results
 * @return KEXEC_SUCCESS on success, error code on failure
 */
int kexec_validate_kernel(const char *kernel_path, kernel_validation_t *validation);

/**
 * Validate initrd image (optional)
 * @param initrd_path Path to initrd file (can be NULL)
 * @return KEXEC_SUCCESS if valid or NULL, error code on failure
 */
int kexec_validate_initrd(const char *initrd_path);

/**
 * Check if system is ready for kexec operation
 * Verifies prerequisites like CRIU availability, checkpoint storage, etc.
 * @return KEXEC_SUCCESS if ready, error code explaining what's wrong
 */
int kexec_check_ready(void);

/**
 * Create checkpoints of all managed processes
 * @param checkpoint_dir Directory to store checkpoints
 * @param manifest Output manifest of created checkpoints
 * @return KEXEC_SUCCESS on success, error code on failure
 */
int kexec_checkpoint_all(const char *checkpoint_dir, checkpoint_manifest_t **manifest);

/**
 * Restore all processes from checkpoint manifest
 * Called during system startup after kexec
 * @param checkpoint_dir Directory containing checkpoints
 * @param manifest Checkpoint manifest to restore from
 * @return KEXEC_SUCCESS on success, error code on failure
 */
int kexec_restore_all(const char *checkpoint_dir, const checkpoint_manifest_t *manifest);

/**
 * Load new kernel into memory using kexec_load()
 * @param kernel_path Path to kernel image
 * @param initrd_path Path to initrd (can be NULL)
 * @param cmdline Kernel command line arguments
 * @return KEXEC_SUCCESS on success, error code on failure
 */
int kexec_load_kernel(const char *kernel_path, const char *initrd_path, const char *cmdline);

/**
 * Execute the loaded kernel (no return on success)
 * This function will not return if successful - the new kernel takes over
 * @return Only returns on error - error code indicating failure
 */
int kexec_execute(void);

/**
 * Perform complete kexec sequence
 * This is the main entry point that orchestrates the entire process:
 * 1. Validate kernel and system readiness
 * 2. Checkpoint all processes
 * 3. Save checkpoint manifest
 * 4. Load new kernel
 * 5. Execute kexec (no return on success)
 *
 * @param kernel_path Path to new kernel image
 * @param initrd_path Path to initrd (optional, can be NULL)
 * @param cmdline Kernel command line (optional, can be NULL for default)
 * @param flags Operation flags (e.g., KEXEC_FLAG_DRY_RUN)
 * @return Only returns on error or dry-run - error code or KEXEC_SUCCESS
 */
int kexec_perform(const char *kernel_path, const char *initrd_path,
                  const char *cmdline, kexec_flags_t flags);

/* Checkpoint persistence functions */

/**
 * Save checkpoint manifest to persistent storage
 * @param checkpoint_dir Directory containing checkpoints
 * @param manifest Manifest to save
 * @return KEXEC_SUCCESS on success, error code on failure
 */
int kexec_save_manifest(const char *checkpoint_dir, const checkpoint_manifest_t *manifest);

/**
 * Load checkpoint manifest from persistent storage
 * @param checkpoint_dir Directory containing checkpoints
 * @param manifest Output pointer to loaded manifest (caller must free)
 * @return KEXEC_SUCCESS on success, error code on failure
 */
int kexec_load_manifest(const char *checkpoint_dir, checkpoint_manifest_t **manifest);

/**
 * Check if post-kexec restoration is needed
 * Called during system startup to detect if we just came from kexec
 * @param checkpoint_dir Directory to check for checkpoint data
 * @return 1 if restoration needed, 0 if not, -1 on error
 */
int kexec_needs_restore(const char *checkpoint_dir);

/**
 * Clean up checkpoint data after successful restoration
 * @param checkpoint_dir Directory containing checkpoint data
 * @return KEXEC_SUCCESS on success, error code on failure
 */
int kexec_cleanup_checkpoints(const char *checkpoint_dir);

/* Utility functions */

/**
 * Convert kexec error code to human-readable string
 * @param error_code Error code from kexec functions
 * @return Static string describing the error
 */
const char *kexec_error_string(kexec_error_t error_code);

/**
 * Get current kernel version string
 * @param buffer Output buffer for version string
 * @param buffer_size Size of output buffer
 * @return KEXEC_SUCCESS on success, error code on failure
 */
int kexec_get_current_kernel_version(char *buffer, size_t buffer_size);

/**
 * Parse kernel command line for YakirOS-specific options
 * @param cmdline Kernel command line string
 * @param checkpoint_dir Output buffer for checkpoint directory path
 * @param checkpoint_dir_size Size of checkpoint_dir buffer
 * @return KEXEC_SUCCESS on success, error code on failure
 */
int kexec_parse_cmdline(const char *cmdline, char *checkpoint_dir, size_t checkpoint_dir_size);

#endif /* KEXEC_H */