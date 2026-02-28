/*
 * kexec.c - Live kernel upgrade implementation for YakirOS
 */

#define _GNU_SOURCE
#include "kexec.h"
#include "log.h"
#include "component.h"
#include "checkpoint.h"
#include "checkpoint-mgmt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>
#include <linux/kexec.h>
#include <linux/reboot.h>
#include <time.h>
#include <dirent.h>

/* Default checkpoint directory that survives kexec */
#define DEFAULT_CHECKPOINT_DIR "/checkpoint"
#define MANIFEST_FILENAME "manifest.json"
#define KEXEC_LOGS_FILENAME "kexec.log"

/* Kernel magic bytes for validation */
#define KERNEL_MAGIC_GZIP 0x1f8b        /* gzip compressed kernel */
#define KERNEL_MAGIC_BZIP2 0x425a       /* bzip2 compressed kernel */
#define KERNEL_MAGIC_LZMA 0x5d00        /* LZMA compressed kernel */
#define KERNEL_MAGIC_XZ 0xfd37          /* XZ compressed kernel */
#define KERNEL_MAGIC_LZ4 0x184c         /* LZ4 compressed kernel */

/* ELF magic for uncompressed kernels */
#define ELF_MAGIC 0x464c457f            /* ELF header magic */

/* Safety limits */
#define MIN_KERNEL_SIZE (512 * 1024)    /* 512KB minimum */
#define MAX_KERNEL_SIZE (200 * 1024 * 1024) /* 200MB maximum */
#define MIN_FREE_SPACE (2ULL * 1024 * 1024 * 1024) /* 2GB free space required */

/* Static variables for kexec state */
static int kexec_initialized = 0;
static char current_kernel_version[256];
static char checkpoint_base_dir[MAX_KERNEL_PATH];

/* Initialize kexec subsystem */
int kexec_init(void) {
    if (kexec_initialized) {
        return KEXEC_SUCCESS;
    }

    LOG_INFO("initializing kexec subsystem");

    /* Get current kernel version */
    if (kexec_get_current_kernel_version(current_kernel_version,
                                       sizeof(current_kernel_version)) != KEXEC_SUCCESS) {
        LOG_WARN("could not determine current kernel version");
        strcpy(current_kernel_version, "unknown");
    }

    /* Set default checkpoint directory */
    strcpy(checkpoint_base_dir, DEFAULT_CHECKPOINT_DIR);

    /* Parse kernel command line for custom checkpoint location */
    FILE *cmdline_file = fopen("/proc/cmdline", "r");
    if (cmdline_file) {
        char cmdline[MAX_CMDLINE_LEN];
        if (fgets(cmdline, sizeof(cmdline), cmdline_file)) {
            char parsed_dir[MAX_KERNEL_PATH];
            if (kexec_parse_cmdline(cmdline, parsed_dir, sizeof(parsed_dir)) == KEXEC_SUCCESS) {
                strcpy(checkpoint_base_dir, parsed_dir);
                LOG_INFO("using checkpoint directory from cmdline: %s", checkpoint_base_dir);
            }
        }
        fclose(cmdline_file);
    }

    /* Create checkpoint directory if it doesn't exist */
    struct stat st;
    if (stat(checkpoint_base_dir, &st) != 0) {
        if (mkdir(checkpoint_base_dir, 0755) != 0) {
            LOG_ERR("failed to create checkpoint directory %s: %s",
                     checkpoint_base_dir, strerror(errno));
            return KEXEC_ERROR_CHECKPOINT_STORAGE;
        }
    }

    kexec_initialized = 1;
    LOG_INFO("kexec subsystem initialized (checkpoint dir: %s, current kernel: %s)",
             checkpoint_base_dir, current_kernel_version);

    return KEXEC_SUCCESS;
}

/* Cleanup kexec subsystem */
void kexec_cleanup(void) {
    if (!kexec_initialized) {
        return;
    }

    LOG_INFO("cleaning up kexec subsystem");
    kexec_initialized = 0;
}

/* Validate a kernel image */
int kexec_validate_kernel(const char *kernel_path, kernel_validation_t *validation) {
    if (!kernel_path || !validation) {
        return KEXEC_ERROR_INVALID_KERNEL;
    }

    memset(validation, 0, sizeof(*validation));

    /* Check if file exists and is readable */
    struct stat st;
    if (stat(kernel_path, &st) != 0) {
        LOG_ERR("kernel file not found: %s", kernel_path);
        return KEXEC_ERROR_INVALID_KERNEL;
    }

    if (!S_ISREG(st.st_mode)) {
        LOG_ERR("kernel path is not a regular file: %s", kernel_path);
        return KEXEC_ERROR_INVALID_KERNEL;
    }

    validation->file_size = st.st_size;

    /* Enhanced size validation */
    if (validation->file_size < MIN_KERNEL_SIZE) {
        LOG_ERR("kernel file too small (%lu bytes, minimum %d): %s",
                 validation->file_size, MIN_KERNEL_SIZE, kernel_path);
        return KEXEC_ERROR_INVALID_KERNEL;
    }

    if (validation->file_size > MAX_KERNEL_SIZE) {
        LOG_ERR("kernel file too large (%lu bytes, maximum %d): %s",
                 validation->file_size, MAX_KERNEL_SIZE, kernel_path);
        return KEXEC_ERROR_INVALID_KERNEL;
    }

    /* Enhanced magic byte validation */
    int fd = open(kernel_path, O_RDONLY);
    if (fd < 0) {
        LOG_ERR("cannot open kernel file for validation: %s", strerror(errno));
        return KEXEC_ERROR_INVALID_KERNEL;
    }

    /* Read first 8 bytes for comprehensive magic detection */
    uint8_t magic_bytes[8];
    ssize_t bytes_read = read(fd, magic_bytes, sizeof(magic_bytes));
    close(fd);

    if (bytes_read < 4) {
        LOG_ERR("cannot read magic bytes from kernel file (read %zd bytes)", bytes_read);
        return KEXEC_ERROR_INVALID_KERNEL;
    }

    uint16_t magic16 = (magic_bytes[1] << 8) | magic_bytes[0];
    uint32_t magic32 = (magic_bytes[3] << 24) | (magic_bytes[2] << 16) |
                       (magic_bytes[1] << 8) | magic_bytes[0];

    /* Check for known compression and format magic bytes */
    validation->has_valid_magic = 0;

    if (magic16 == KERNEL_MAGIC_GZIP) {
        validation->has_valid_magic = 1;
        strcpy(validation->version, "gzip-compressed");
        LOG_INFO("detected gzip compressed kernel");
    } else if (magic16 == KERNEL_MAGIC_BZIP2) {
        validation->has_valid_magic = 1;
        strcpy(validation->version, "bzip2-compressed");
        LOG_INFO("detected bzip2 compressed kernel");
    } else if (magic16 == KERNEL_MAGIC_LZMA) {
        validation->has_valid_magic = 1;
        strcpy(validation->version, "lzma-compressed");
        LOG_INFO("detected LZMA compressed kernel");
    } else if (magic16 == KERNEL_MAGIC_XZ) {
        validation->has_valid_magic = 1;
        strcpy(validation->version, "xz-compressed");
        LOG_INFO("detected XZ compressed kernel");
    } else if (magic16 == KERNEL_MAGIC_LZ4) {
        validation->has_valid_magic = 1;
        strcpy(validation->version, "lz4-compressed");
        LOG_INFO("detected LZ4 compressed kernel");
    } else if (magic32 == ELF_MAGIC) {
        validation->has_valid_magic = 1;
        strcpy(validation->version, "elf-uncompressed");
        LOG_INFO("detected uncompressed ELF kernel");
    } else {
        LOG_WARN("kernel magic bytes not recognized (0x%02x%02x%02x%02x)",
                magic_bytes[0], magic_bytes[1], magic_bytes[2], magic_bytes[3]);
        LOG_WARN("proceeding with validation anyway - kernel may still be valid");
        validation->has_valid_magic = 0;
        strcpy(validation->version, "unknown-format");
    }

    /* Determine architecture - simplified detection */
    strcpy(validation->architecture, "x86_64"); /* Default assumption */

    /* Try to extract version information (this is best-effort) */
    snprintf(validation->version, sizeof(validation->version), "unknown");

    validation->is_valid = 1;
    LOG_INFO("kernel validation successful: %s (%lu bytes, magic=%s)",
             kernel_path, validation->file_size,
             validation->has_valid_magic ? "valid" : "unknown");

    return KEXEC_SUCCESS;
}

/* Validate initrd image */
int kexec_validate_initrd(const char *initrd_path) {
    if (!initrd_path) {
        return KEXEC_SUCCESS; /* NULL initrd is okay */
    }

    struct stat st;
    if (stat(initrd_path, &st) != 0) {
        LOG_ERR("initrd file not found: %s", initrd_path);
        return KEXEC_ERROR_INVALID_INITRD;
    }

    if (!S_ISREG(st.st_mode)) {
        LOG_ERR("initrd path is not a regular file: %s", initrd_path);
        return KEXEC_ERROR_INVALID_INITRD;
    }

    if (st.st_size == 0) {
        LOG_ERR("initrd file is empty: %s", initrd_path);
        return KEXEC_ERROR_INVALID_INITRD;
    }

    if (st.st_size > 500 * 1024 * 1024) { /* 500MB limit for initrd */
        LOG_ERR("initrd file too large (%lu bytes): %s", st.st_size, initrd_path);
        return KEXEC_ERROR_INVALID_INITRD;
    }

    LOG_INFO("initrd validation successful: %s (%lu bytes)", initrd_path, st.st_size);
    return KEXEC_SUCCESS;
}

/* Enhanced system readiness check with comprehensive safety validation */
int kexec_check_ready(void) {
    if (!kexec_initialized) {
        LOG_ERR("kexec subsystem not initialized");
        return KEXEC_ERROR_SYSTEM_BUSY;
    }

    LOG_INFO("performing comprehensive system readiness check for kexec");

    /* 1. Privilege check */
    if (getuid() != 0 || geteuid() != 0) {
        LOG_ERR("kexec requires root privileges (uid=%d, euid=%d)", getuid(), geteuid());
        return KEXEC_ERROR_PERMISSION_DENIED;
    }

    /* 2. PID 1 check - kexec should only be run by init system */
    if (getpid() != 1) {
        LOG_WARN("kexec not running as PID 1 - this may cause issues");
        LOG_WARN("consider using the graph-resolver control interface instead");
    }

    /* 3. Check CRIU availability and version */
    criu_version_t criu_version;
    int criu_result = criu_get_version(&criu_version);
    if (criu_result != CHECKPOINT_SUCCESS) {
        LOG_ERR("CRIU not available or supported: %s", checkpoint_error_string(criu_result));
        LOG_ERR("cannot checkpoint processes for kexec without CRIU");
        return KEXEC_ERROR_CHECKPOINT_FAILED;
    }

    if (criu_version.major < 3) {
        LOG_ERR("CRIU version too old (v%d.%d.%d, need >= 3.0.0)",
                 criu_version.major, criu_version.minor, criu_version.patch);
        return KEXEC_ERROR_CHECKPOINT_FAILED;
    }

    LOG_INFO("CRIU v%d.%d.%d available and supported",
             criu_version.major, criu_version.minor, criu_version.patch);

    /* 4. Check checkpoint storage directory */
    struct stat st;
    if (stat(checkpoint_base_dir, &st) != 0) {
        LOG_ERR("checkpoint directory does not exist: %s", checkpoint_base_dir);
        return KEXEC_ERROR_CHECKPOINT_STORAGE;
    }

    if (!S_ISDIR(st.st_mode)) {
        LOG_ERR("checkpoint path is not a directory: %s", checkpoint_base_dir);
        return KEXEC_ERROR_CHECKPOINT_STORAGE;
    }

    if (access(checkpoint_base_dir, R_OK | W_OK | X_OK) != 0) {
        LOG_ERR("checkpoint directory not accessible (need rwx): %s", checkpoint_base_dir);
        return KEXEC_ERROR_CHECKPOINT_STORAGE;
    }

    /* 5. Check available disk space using statvfs() */
    struct statvfs vfs;
    if (statvfs(checkpoint_base_dir, &vfs) == 0) {
        uint64_t available_bytes = (uint64_t)vfs.f_bavail * vfs.f_frsize;
        if (available_bytes < MIN_FREE_SPACE) {
            LOG_ERR("insufficient disk space for checkpoints: %lu MB available, %lu MB required",
                     available_bytes / (1024 * 1024), MIN_FREE_SPACE / (1024 * 1024));
            return KEXEC_ERROR_CHECKPOINT_STORAGE;
        }
        LOG_INFO("checkpoint storage has %lu MB available space", available_bytes / (1024 * 1024));
    } else {
        LOG_WARN("cannot check available space in %s: %s", checkpoint_base_dir, strerror(errno));
    }

    /* 6. Verify kexec syscall availability */
    if (syscall(__NR_kexec_load, 0, 0, NULL, 0) < 0) {
        if (errno == ENOSYS) {
            LOG_ERR("kexec_load syscall not supported by kernel");
            return KEXEC_ERROR_SYSTEM_BUSY;
        } else if (errno != EINVAL && errno != EPERM) {
            LOG_ERR("kexec_load syscall failed: %s", strerror(errno));
            return KEXEC_ERROR_SYSTEM_BUSY;
        }
    }

    /* 7. Check if kexec utility is available */
    if (system("which kexec >/dev/null 2>&1") != 0) {
        LOG_ERR("kexec utility not found in PATH - install kexec-tools package");
        return KEXEC_ERROR_SYSTEM_BUSY;
    }

    /* 8. Check if we're already in a kexec'd kernel */
    FILE *kexec_crash = fopen("/sys/kernel/kexec_crash_loaded", "r");
    if (kexec_crash) {
        char value;
        if (fread(&value, 1, 1, kexec_crash) == 1 && value == '1') {
            LOG_WARN("kernel has crash kernel loaded - this may interfere with kexec");
        }
        fclose(kexec_crash);
    }

    /* 9. Memory pressure check */
    FILE *meminfo = fopen("/proc/meminfo", "r");
    if (meminfo) {
        char line[256];
        uint64_t mem_available = 0;
        while (fgets(line, sizeof(line), meminfo)) {
            if (sscanf(line, "MemAvailable: %lu kB", &mem_available) == 1) {
                break;
            }
        }
        fclose(meminfo);

        if (mem_available > 0) {
            mem_available *= 1024; /* Convert to bytes */
            if (mem_available < (512 * 1024 * 1024)) { /* 512MB minimum */
                LOG_ERR("insufficient available memory: %lu MB (need at least 512 MB)",
                         mem_available / (1024 * 1024));
                return KEXEC_ERROR_SYSTEM_BUSY;
            }
            LOG_INFO("system has %lu MB available memory", mem_available / (1024 * 1024));
        }
    }

    /* 10. Check for active swapping (may interfere with checkpoint) */
    FILE *swaps = fopen("/proc/swaps", "r");
    if (swaps) {
        char line[256];
        int swap_count = 0;
        while (fgets(line, sizeof(line), swaps)) {
            if (strstr(line, "/dev/") || strstr(line, "file")) {
                swap_count++;
            }
        }
        fclose(swaps);

        if (swap_count > 0) {
            LOG_WARN("active swap detected - this may slow down checkpoint/restore");
        }
    }

    LOG_INFO("=== SYSTEM READINESS CHECK COMPLETE ===");
    LOG_INFO("✓ Privileges: root");
    LOG_INFO("✓ CRIU: v%d.%d.%d available", criu_version.major, criu_version.minor, criu_version.patch);
    LOG_INFO("✓ Storage: %s accessible", checkpoint_base_dir);
    LOG_INFO("✓ Syscalls: kexec_load available");
    LOG_INFO("✓ Utilities: kexec command found");
    LOG_INFO("✓ Memory: sufficient available");
    LOG_INFO("System ready for live kernel upgrade");
    LOG_INFO("=======================================");

    return KEXEC_SUCCESS;
}

/* Save old kernel information before kexec for post-upgrade reporting */
static int save_pre_kexec_info(const char *checkpoint_dir) {
    char info_path[2048];
    snprintf(info_path, sizeof(info_path), "%s/pre-kexec-info.txt", checkpoint_dir);

    FILE *fp = fopen(info_path, "w");
    if (!fp) {
        LOG_ERR("cannot save pre-kexec info: %s", strerror(errno));
        return KEXEC_ERROR_CHECKPOINT_STORAGE;
    }

    /* Save current kernel version */
    char kernel_version[256];
    if (kexec_get_current_kernel_version(kernel_version, sizeof(kernel_version)) == KEXEC_SUCCESS) {
        fprintf(fp, "old_kernel_version=%s\n", kernel_version);
    }

    /* Save timestamp */
    time_t now = time(NULL);
    fprintf(fp, "kexec_timestamp=%lu\n", now);
    fprintf(fp, "kexec_date=%s", ctime(&now)); /* ctime includes newline */

    /* Save system info */
    struct utsname uts;
    if (uname(&uts) == 0) {
        fprintf(fp, "hostname=%s\n", uts.nodename);
        fprintf(fp, "architecture=%s\n", uts.machine);
    }

    /* Save component count for validation */
    int component_count = n_components;
    fprintf(fp, "component_count=%d\n", component_count);

    fclose(fp);
    LOG_INFO("saved pre-kexec system information to %s", info_path);
    return KEXEC_SUCCESS;
}

/* Validate all checkpoints before proceeding with kexec */
static int validate_all_checkpoints(const checkpoint_manifest_t *manifest) {
    if (!manifest || manifest->entry_count == 0) {
        LOG_INFO("no checkpoints to validate");
        return KEXEC_SUCCESS;
    }

    LOG_INFO("validating %u checkpoints before kexec", manifest->entry_count);

    int validation_failures = 0;
    int critical_failures = 0;

    for (uint32_t i = 0; i < manifest->entry_count; i++) {
        const checkpoint_manifest_entry_t *entry = &manifest->entries[i];

        LOG_INFO("validating checkpoint %u: %s (%s)", i + 1, entry->component_name, entry->checkpoint_id);

        /* Check if checkpoint directory exists */
        if (access(entry->checkpoint_path, F_OK) != 0) {
            LOG_ERR("checkpoint path missing: %s", entry->checkpoint_path);
            critical_failures++;
            continue;
        }

        /* Validate checkpoint integrity */
        int result = checkpoint_validate_image(entry->checkpoint_path);
        if (result != CHECKPOINT_SUCCESS) {
            LOG_ERR("checkpoint validation failed for %s: %s",
                     entry->component_name, checkpoint_error_string(result));

            /* Check if this is a critical component */
            component_t *comp = NULL;
            for (int j = 0; j < n_components; j++) {
                if (strcmp(components[j].name, entry->component_name) == 0) {
                    comp = &components[j];
                    break;
                }
            }
            if (comp && comp->type == COMP_TYPE_SERVICE) {
                /* Service components are more critical than oneshot */
                critical_failures++;
            } else {
                validation_failures++;
            }
        } else {
            LOG_INFO("checkpoint validation passed for %s", entry->component_name);
        }
    }

    if (critical_failures > 0) {
        LOG_ERR("checkpoint validation failed: %d critical failures", critical_failures);
        LOG_ERR("kexec cannot proceed safely - critical service checkpoints invalid");
        return KEXEC_ERROR_CHECKPOINT_FAILED;
    }

    if (validation_failures > 0) {
        LOG_WARN("checkpoint validation completed with %d non-critical failures", validation_failures);
        LOG_WARN("some oneshot components may not restore properly after kexec");
        LOG_WARN("proceeding with kexec anyway");
    } else {
        LOG_INFO("all checkpoint validations passed successfully");
    }

    return KEXEC_SUCCESS;
}

/* Create checkpoints of all managed processes */
int kexec_checkpoint_all(const char *checkpoint_dir, checkpoint_manifest_t **manifest) {
    if (!checkpoint_dir || !manifest) {
        return KEXEC_ERROR_CHECKPOINT_FAILED;
    }

    LOG_INFO("creating checkpoints of all managed processes");

    /* Get list of all active components */
    int component_count = n_components;
    if (component_count <= 0) {
        LOG_WARN("no active components to checkpoint");
        /* Create empty manifest */
        *manifest = calloc(1, sizeof(checkpoint_manifest_t));
        if (!*manifest) {
            return KEXEC_ERROR_CHECKPOINT_FAILED;
        }
        (*manifest)->version = 1;
        (*manifest)->entry_count = 0;
        (*manifest)->creation_time = time(NULL);
        return KEXEC_SUCCESS;
    }

    /* Allocate manifest with space for all components */
    size_t manifest_size = sizeof(checkpoint_manifest_t) +
                          component_count * sizeof(checkpoint_manifest_entry_t);
    *manifest = calloc(1, manifest_size);
    if (!*manifest) {
        LOG_ERR("failed to allocate checkpoint manifest");
        return KEXEC_ERROR_CHECKPOINT_FAILED;
    }

    /* Initialize manifest header */
    (*manifest)->version = 1;
    (*manifest)->entry_count = 0;
    (*manifest)->creation_time = time(NULL);
    strcpy((*manifest)->old_kernel_version, current_kernel_version);

    /* Checkpoint each active component */
    for (int i = 0; i < component_count; i++) {
        component_t *comp = &components[i];
        if (!comp || comp->state != COMP_ACTIVE) {
            continue; /* Skip inactive components */
        }

        LOG_INFO("checkpointing component: %s (pid %d)", comp->name, comp->pid);

        /* Create checkpoint */
        char checkpoint_id[CHECKPOINT_ID_MAX_LEN];
        char checkpoint_path[MAX_CHECKPOINT_PATH];

        int result = checkpoint_create_directory(comp->name, 0, /* temporary */
                                               checkpoint_id, sizeof(checkpoint_id),
                                               checkpoint_path, sizeof(checkpoint_path));
        if (result != 0) {
            LOG_ERR("failed to create checkpoint directory for %s", comp->name);
            free(*manifest);
            return KEXEC_ERROR_CHECKPOINT_FAILED;
        }

        /* Perform CRIU checkpoint */
        result = criu_checkpoint_process(comp->pid, checkpoint_path, 1); /* leave running */
        if (result != CHECKPOINT_SUCCESS) {
            LOG_ERR("CRIU checkpoint failed for %s: %s",
                     comp->name, checkpoint_error_string(result));
            free(*manifest);
            return KEXEC_ERROR_CHECKPOINT_FAILED;
        }

        /* Add to manifest */
        checkpoint_manifest_entry_t *entry = &(*manifest)->entries[(*manifest)->entry_count];
        strcpy(entry->component_name, comp->name);
        strcpy(entry->checkpoint_id, checkpoint_id);
        strcpy(entry->checkpoint_path, checkpoint_path);
        entry->original_pid = comp->pid;
        entry->timestamp = time(NULL);
        entry->restore_priority = i; /* Restore in original order */

        (*manifest)->entry_count++;
    }

    LOG_INFO("successfully checkpointed %d components", (*manifest)->entry_count);
    return KEXEC_SUCCESS;
}

/* Load new kernel using kexec_load syscall */
int kexec_load_kernel(const char *kernel_path, const char *initrd_path, const char *cmdline) {
    LOG_INFO("loading kernel for kexec: %s", kernel_path);

    /* This is a simplified implementation - in production we'd need to:
     * 1. Read and parse the kernel image
     * 2. Set up proper segment structures for kexec_load()
     * 3. Handle initrd loading
     * 4. Set up command line properly
     *
     * For now, we'll use the kexec utility program which handles these details
     */

    /* Build kexec command */
    char kexec_cmd[4096];
    int ret = snprintf(kexec_cmd, sizeof(kexec_cmd),
                      "kexec -l '%s'", kernel_path);

    if (initrd_path) {
        ret += snprintf(kexec_cmd + ret, sizeof(kexec_cmd) - ret,
                       " --initrd='%s'", initrd_path);
    }

    if (cmdline) {
        ret += snprintf(kexec_cmd + ret, sizeof(kexec_cmd) - ret,
                       " --append='%s'", cmdline);
    }

    if ((size_t)ret >= sizeof(kexec_cmd)) {
        LOG_ERR("kexec command line too long");
        return KEXEC_ERROR_CMDLINE_TOO_LONG;
    }

    LOG_INFO("executing: %s", kexec_cmd);
    int result = system(kexec_cmd);

    if (result != 0) {
        LOG_ERR("kexec load failed with exit code %d", result);
        return KEXEC_ERROR_LOAD_FAILED;
    }

    LOG_INFO("kernel loaded successfully for kexec");
    return KEXEC_SUCCESS;
}

/* Execute kexec (no return on success) */
int kexec_execute(void) {
    LOG_INFO("executing kexec - transferring to new kernel (no return expected)");

    /* Sync filesystems before kexec */
    sync();

    /* Execute kexec */
    int result = system("kexec -e");

    /* If we get here, kexec failed */
    LOG_ERR("kexec execution failed with exit code %d", result);
    return KEXEC_ERROR_EXEC_FAILED;
}

/* Save checkpoint manifest */
int kexec_save_manifest(const char *checkpoint_dir, const checkpoint_manifest_t *manifest) {
    if (!checkpoint_dir || !manifest) {
        return KEXEC_ERROR_CHECKPOINT_FAILED;
    }

    char manifest_path[2048];
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s", checkpoint_dir, MANIFEST_FILENAME);

    LOG_INFO("saving checkpoint manifest to %s", manifest_path);

    FILE *fp = fopen(manifest_path, "w");
    if (!fp) {
        LOG_ERR("cannot create manifest file: %s", strerror(errno));
        return KEXEC_ERROR_CHECKPOINT_STORAGE;
    }

    /* Write JSON manifest */
    fprintf(fp, "{\n");
    fprintf(fp, "  \"version\": %u,\n", manifest->version);
    fprintf(fp, "  \"entry_count\": %u,\n", manifest->entry_count);
    fprintf(fp, "  \"creation_time\": %lu,\n", manifest->creation_time);
    fprintf(fp, "  \"old_kernel_version\": \"%s\",\n", manifest->old_kernel_version);
    fprintf(fp, "  \"new_kernel_path\": \"%s\",\n", manifest->new_kernel_path);
    fprintf(fp, "  \"initrd_path\": \"%s\",\n", manifest->initrd_path);
    fprintf(fp, "  \"cmdline\": \"%s\",\n", manifest->cmdline);
    fprintf(fp, "  \"entries\": [\n");

    for (uint32_t i = 0; i < manifest->entry_count; i++) {
        const checkpoint_manifest_entry_t *entry = &manifest->entries[i];
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"component_name\": \"%s\",\n", entry->component_name);
        fprintf(fp, "      \"checkpoint_id\": \"%s\",\n", entry->checkpoint_id);
        fprintf(fp, "      \"checkpoint_path\": \"%s\",\n", entry->checkpoint_path);
        fprintf(fp, "      \"original_pid\": %d,\n", entry->original_pid);
        fprintf(fp, "      \"timestamp\": %lu,\n", entry->timestamp);
        fprintf(fp, "      \"restore_priority\": %d\n", entry->restore_priority);
        fprintf(fp, "    }%s\n", (i < manifest->entry_count - 1) ? "," : "");
    }

    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    fclose(fp);

    /* Ensure data is written to disk */
    sync();

    LOG_INFO("checkpoint manifest saved successfully");
    return KEXEC_SUCCESS;
}

/* Check if post-kexec restoration is needed */
int kexec_needs_restore(const char *checkpoint_dir) {
    if (!checkpoint_dir) {
        checkpoint_dir = checkpoint_base_dir;
    }

    char manifest_path[2048];
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s", checkpoint_dir, MANIFEST_FILENAME);

    return (access(manifest_path, F_OK) == 0) ? 1 : 0;
}

/* Convert error code to string */
const char *kexec_error_string(kexec_error_t error_code) {
    switch (error_code) {
    case KEXEC_SUCCESS:
        return "Success";
    case KEXEC_ERROR_INVALID_KERNEL:
        return "Invalid kernel image";
    case KEXEC_ERROR_CHECKPOINT_FAILED:
        return "Checkpoint operation failed";
    case KEXEC_ERROR_LOAD_FAILED:
        return "Kernel load failed";
    case KEXEC_ERROR_EXEC_FAILED:
        return "Kexec execution failed";
    case KEXEC_ERROR_PERMISSION_DENIED:
        return "Permission denied";
    case KEXEC_ERROR_SYSTEM_BUSY:
        return "System not ready for kexec";
    case KEXEC_ERROR_INVALID_INITRD:
        return "Invalid initrd image";
    case KEXEC_ERROR_CMDLINE_TOO_LONG:
        return "Command line too long";
    case KEXEC_ERROR_CHECKPOINT_STORAGE:
        return "Checkpoint storage unavailable";
    default:
        return "Unknown error";
    }
}

/* Get current kernel version */
int kexec_get_current_kernel_version(char *buffer, size_t buffer_size) {
    struct utsname uts;
    if (uname(&uts) != 0) {
        return KEXEC_ERROR_SYSTEM_BUSY;
    }

    if (strlen(uts.release) >= buffer_size) {
        return KEXEC_ERROR_CMDLINE_TOO_LONG;
    }

    strcpy(buffer, uts.release);
    return KEXEC_SUCCESS;
}

/* Parse kernel command line for YakirOS options */
int kexec_parse_cmdline(const char *cmdline, char *checkpoint_dir, size_t checkpoint_dir_size) {
    if (!cmdline || !checkpoint_dir) {
        return KEXEC_ERROR_INVALID_KERNEL;
    }

    /* Look for spliceos.checkpoint= or yakiros.checkpoint= parameter */
    const char *checkpoint_param = strstr(cmdline, "yakiros.checkpoint=");
    if (!checkpoint_param) {
        checkpoint_param = strstr(cmdline, "spliceos.checkpoint="); /* Legacy name */
    }

    if (checkpoint_param) {
        const char *start = strchr(checkpoint_param, '=') + 1;
        const char *end = strchr(start, ' ');
        if (!end) {
            end = start + strlen(start);
        }

        size_t len = end - start;
        if (len >= checkpoint_dir_size) {
            return KEXEC_ERROR_CMDLINE_TOO_LONG;
        }

        strncpy(checkpoint_dir, start, len);
        checkpoint_dir[len] = '\0';
        return KEXEC_SUCCESS;
    }

    /* No custom checkpoint directory found */
    return KEXEC_ERROR_INVALID_KERNEL;
}

/* Main kexec perform function - this orchestrates everything */
int kexec_perform(const char *kernel_path, const char *initrd_path,
                  const char *cmdline, kexec_flags_t flags) {

    if (!kernel_path) {
        return KEXEC_ERROR_INVALID_KERNEL;
    }

    LOG_INFO("starting kexec sequence: kernel=%s, initrd=%s, dry_run=%s",
             kernel_path, initrd_path ? initrd_path : "none",
             (flags & KEXEC_FLAG_DRY_RUN) ? "yes" : "no");

    /* Phase 1: Validation */
    LOG_INFO("phase 1: validation");

    kernel_validation_t validation;
    int result = kexec_validate_kernel(kernel_path, &validation);
    if (result != KEXEC_SUCCESS) {
        LOG_ERR("kernel validation failed: %s", kexec_error_string(result));
        return result;
    }

    result = kexec_validate_initrd(initrd_path);
    if (result != KEXEC_SUCCESS) {
        LOG_ERR("initrd validation failed: %s", kexec_error_string(result));
        return result;
    }

    result = kexec_check_ready();
    if (result != KEXEC_SUCCESS) {
        LOG_ERR("system readiness check failed: %s", kexec_error_string(result));
        return result;
    }

    if (flags & KEXEC_FLAG_DRY_RUN) {
        LOG_INFO("dry run successful - kexec would proceed");
        return KEXEC_SUCCESS;
    }

    /* Phase 2: Save pre-kexec information */
    LOG_INFO("phase 2: saving pre-kexec system information");

    result = save_pre_kexec_info(checkpoint_base_dir);
    if (result != KEXEC_SUCCESS) {
        LOG_ERR("failed to save pre-kexec info: %s", kexec_error_string(result));
        /* Continue anyway - not critical */
    }

    /* Phase 3: Checkpoint all processes */
    LOG_INFO("phase 3: checkpointing all managed processes");

    checkpoint_manifest_t *manifest = NULL;
    result = kexec_checkpoint_all(checkpoint_base_dir, &manifest);
    if (result != KEXEC_SUCCESS) {
        LOG_ERR("checkpoint phase failed: %s", kexec_error_string(result));
        return result;
    }

    /* Update manifest with kexec details */
    strcpy(manifest->new_kernel_path, kernel_path);
    if (initrd_path) {
        strcpy(manifest->initrd_path, initrd_path);
    }
    if (cmdline) {
        strcpy(manifest->cmdline, cmdline);
    }

    /* Phase 4: Validate all checkpoints before proceeding */
    LOG_INFO("phase 4: validating checkpoint integrity");

    result = validate_all_checkpoints(manifest);
    if (result != KEXEC_SUCCESS) {
        LOG_ERR("checkpoint validation failed: %s", kexec_error_string(result));
        LOG_ERR("ABORTING KEXEC - system safety compromised");
        free(manifest);
        return result;
    }

    /* Phase 5: Save manifest to persistent storage */
    LOG_INFO("phase 5: saving checkpoint manifest");

    result = kexec_save_manifest(checkpoint_base_dir, manifest);
    if (result != KEXEC_SUCCESS) {
        LOG_ERR("manifest save failed: %s", kexec_error_string(result));
        free(manifest);
        return result;
    }

    /* Phase 6: Load new kernel */
    LOG_INFO("phase 6: loading new kernel into memory");

    result = kexec_load_kernel(kernel_path, initrd_path, cmdline);
    if (result != KEXEC_SUCCESS) {
        LOG_ERR("kernel load failed: %s", kexec_error_string(result));
        free(manifest);
        return result;
    }

    free(manifest); /* No longer needed */

    /* Phase 7: Execute kexec (should not return) */
    LOG_INFO("phase 7: executing kexec - goodbye current kernel!");
    LOG_INFO("=== POINT OF NO RETURN ===");
    LOG_INFO("All safety checks passed, checkpoints validated");
    LOG_INFO("Transferring control to new kernel...");
    LOG_INFO("kexec sequence initiated successfully - new kernel should take over");

    result = kexec_execute();

    /* If we get here, kexec failed */
    LOG_ERR("kexec execution failed: %s", kexec_error_string(result));
    return result;
}

/* Load checkpoint manifest from persistent storage */
int kexec_load_manifest(const char *checkpoint_dir, checkpoint_manifest_t **manifest) {
    if (!checkpoint_dir || !manifest) {
        return KEXEC_ERROR_CHECKPOINT_FAILED;
    }

    char manifest_path[2048];
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s", checkpoint_dir, MANIFEST_FILENAME);

    LOG_INFO("loading checkpoint manifest from %s", manifest_path);

    FILE *fp = fopen(manifest_path, "r");
    if (!fp) {
        LOG_ERR("cannot open manifest file: %s", strerror(errno));
        return KEXEC_ERROR_CHECKPOINT_STORAGE;
    }

    /* Read file size */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || file_size > MAX_CHECKPOINT_MANIFEST_LEN) {
        LOG_ERR("manifest file has invalid size: %ld bytes", file_size);
        fclose(fp);
        return KEXEC_ERROR_CHECKPOINT_STORAGE;
    }

    /* Read manifest file content */
    char *json_content = malloc(file_size + 1);
    if (!json_content) {
        LOG_ERR("failed to allocate memory for manifest");
        fclose(fp);
        return KEXEC_ERROR_CHECKPOINT_FAILED;
    }

    size_t bytes_read = fread(json_content, 1, file_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        LOG_ERR("failed to read complete manifest file");
        free(json_content);
        return KEXEC_ERROR_CHECKPOINT_STORAGE;
    }
    json_content[file_size] = '\0';

    /* Parse JSON manually (simple parser for our known format) */
    /* This is a simplified parser - in production we'd use a proper JSON library */

    uint32_t version = 0, entry_count = 0;
    uint64_t creation_time = 0;
    char old_kernel_version[256] = {0};
    char new_kernel_path[MAX_KERNEL_PATH] = {0};
    char initrd_path[MAX_KERNEL_PATH] = {0};
    char cmdline[MAX_CMDLINE_LEN] = {0};

    /* Extract key values using simple string parsing */
    char *ptr;
    if ((ptr = strstr(json_content, "\"version\":")) != NULL) {
        sscanf(ptr, "\"version\": %u", &version);
    }
    if ((ptr = strstr(json_content, "\"entry_count\":")) != NULL) {
        sscanf(ptr, "\"entry_count\": %u", &entry_count);
    }
    if ((ptr = strstr(json_content, "\"creation_time\":")) != NULL) {
        sscanf(ptr, "\"creation_time\": %lu", &creation_time);
    }
    if ((ptr = strstr(json_content, "\"old_kernel_version\":")) != NULL) {
        sscanf(ptr, "\"old_kernel_version\": \"%255[^\"]\"", old_kernel_version);
    }
    if ((ptr = strstr(json_content, "\"new_kernel_path\":")) != NULL) {
        sscanf(ptr, "\"new_kernel_path\": \"%1023[^\"]\"", new_kernel_path);
    }
    if ((ptr = strstr(json_content, "\"initrd_path\":")) != NULL) {
        sscanf(ptr, "\"initrd_path\": \"%1023[^\"]\"", initrd_path);
    }
    if ((ptr = strstr(json_content, "\"cmdline\":")) != NULL) {
        sscanf(ptr, "\"cmdline\": \"%2047[^\"]\"", cmdline);
    }

    /* Allocate manifest structure */
    size_t manifest_size = sizeof(checkpoint_manifest_t) +
                          entry_count * sizeof(checkpoint_manifest_entry_t);
    *manifest = calloc(1, manifest_size);
    if (!*manifest) {
        LOG_ERR("failed to allocate manifest structure");
        free(json_content);
        return KEXEC_ERROR_CHECKPOINT_FAILED;
    }

    /* Fill manifest header */
    (*manifest)->version = version;
    (*manifest)->entry_count = entry_count;
    (*manifest)->creation_time = creation_time;
    strcpy((*manifest)->old_kernel_version, old_kernel_version);
    strcpy((*manifest)->new_kernel_path, new_kernel_path);
    strcpy((*manifest)->initrd_path, initrd_path);
    strcpy((*manifest)->cmdline, cmdline);

    /* Parse entries (simplified - would need better JSON parsing in production) */
    ptr = strstr(json_content, "\"entries\":");
    if (ptr) {
        ptr = strchr(ptr, '[');
        if (ptr) {
            ptr++; /* Skip opening bracket */

            for (uint32_t i = 0; i < entry_count && ptr; i++) {
                checkpoint_manifest_entry_t *entry = &(*manifest)->entries[i];

                /* Find next entry object */
                ptr = strchr(ptr, '{');
                if (!ptr) break;
                ptr++;

                /* Parse entry fields */
                char *entry_end = strchr(ptr, '}');
                if (!entry_end) break;

                char *field_ptr;
                if ((field_ptr = strstr(ptr, "\"component_name\":")) != NULL && field_ptr < entry_end) {
                    sscanf(field_ptr, "\"component_name\": \"%255[^\"]\"", entry->component_name);
                }
                if ((field_ptr = strstr(ptr, "\"checkpoint_id\":")) != NULL && field_ptr < entry_end) {
                    sscanf(field_ptr, "\"checkpoint_id\": \"%63[^\"]\"", entry->checkpoint_id);
                }
                if ((field_ptr = strstr(ptr, "\"checkpoint_path\":")) != NULL && field_ptr < entry_end) {
                    sscanf(field_ptr, "\"checkpoint_path\": \"%511[^\"]\"", entry->checkpoint_path);
                }
                if ((field_ptr = strstr(ptr, "\"original_pid\":")) != NULL && field_ptr < entry_end) {
                    sscanf(field_ptr, "\"original_pid\": %d", &entry->original_pid);
                }
                if ((field_ptr = strstr(ptr, "\"timestamp\":")) != NULL && field_ptr < entry_end) {
                    sscanf(field_ptr, "\"timestamp\": %lu", &entry->timestamp);
                }
                if ((field_ptr = strstr(ptr, "\"restore_priority\":")) != NULL && field_ptr < entry_end) {
                    sscanf(field_ptr, "\"restore_priority\": %d", &entry->restore_priority);
                }

                ptr = entry_end + 1;
            }
        }
    }

    free(json_content);

    LOG_INFO("loaded checkpoint manifest: %u entries from %s kernel",
             (*manifest)->entry_count, (*manifest)->old_kernel_version);

    return KEXEC_SUCCESS;
}

/* Restore all processes from checkpoint manifest */
int kexec_restore_all(const char *checkpoint_dir, const checkpoint_manifest_t *manifest) {
    if (!checkpoint_dir || !manifest) {
        return KEXEC_ERROR_CHECKPOINT_FAILED;
    }

    LOG_INFO("restoring %u checkpointed processes", manifest->entry_count);

    if (manifest->entry_count == 0) {
        LOG_INFO("no processes to restore");
        return KEXEC_SUCCESS;
    }

    /* Sort entries by restore priority (lower number = restore first) */
    /* For simplicity, we'll restore in the order they appear (which should be priority order) */

    int restored_count = 0;
    int failed_count = 0;

    for (uint32_t i = 0; i < manifest->entry_count; i++) {
        const checkpoint_manifest_entry_t *entry = &manifest->entries[i];

        LOG_INFO("restoring component %s from checkpoint %s (original pid %d)",
                 entry->component_name, entry->checkpoint_id, entry->original_pid);

        /* Validate checkpoint still exists */
        if (access(entry->checkpoint_path, F_OK) != 0) {
            LOG_ERR("checkpoint path no longer exists: %s", entry->checkpoint_path);
            failed_count++;
            continue;
        }

        /* Validate checkpoint integrity */
        int validation_result = checkpoint_validate_image(entry->checkpoint_path);
        if (validation_result != CHECKPOINT_SUCCESS) {
            LOG_ERR("checkpoint validation failed for %s: %s",
                     entry->component_name, checkpoint_error_string(validation_result));
            failed_count++;
            continue;
        }

        /* Restore the process using CRIU */
        pid_t restored_pid = criu_restore_process(entry->checkpoint_path);
        if (restored_pid < 0) {
            LOG_ERR("CRIU restore failed for %s: %s",
                     entry->component_name, checkpoint_error_string(restored_pid));
            failed_count++;
            continue;
        }

        LOG_INFO("successfully restored %s: old pid %d -> new pid %d",
                 entry->component_name, entry->original_pid, restored_pid);

        /* Update component record with new PID */
        /* Note: This assumes component structures are available -
         * in practice we'd need to reload component configurations first */

        restored_count++;
    }

    LOG_INFO("restoration complete: %d successful, %d failed", restored_count, failed_count);

    if (failed_count > 0) {
        LOG_WARN("some processes failed to restore - system may be partially functional");
        return KEXEC_ERROR_CHECKPOINT_FAILED;
    }

    return KEXEC_SUCCESS;
}

/* Clean up checkpoint data after successful restoration */
int kexec_cleanup_checkpoints(const char *checkpoint_dir) {
    if (!checkpoint_dir) {
        checkpoint_dir = checkpoint_base_dir;
    }

    LOG_INFO("cleaning up checkpoint data in %s", checkpoint_dir);

    /* Remove manifest file */
    char manifest_path[2048];
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s", checkpoint_dir, MANIFEST_FILENAME);

    if (unlink(manifest_path) != 0) {
        LOG_WARN("failed to remove manifest file %s: %s",
                 manifest_path, strerror(errno));
    }

    /* Remove checkpoint directories */
    DIR *dir = opendir(checkpoint_dir);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') {
                continue; /* Skip . and .. */
            }

            char entry_path[2048];
            snprintf(entry_path, sizeof(entry_path), "%s/%s", checkpoint_dir, entry->d_name);

            struct stat st;
            if (stat(entry_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                /* Remove directory and contents recursively */
                char rm_cmd[2048 + 50];
                snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", entry_path);
                int result = system(rm_cmd);
                if (result != 0) {
                    LOG_WARN("failed to remove checkpoint directory %s", entry_path);
                }
            }
        }
        closedir(dir);
    }

    LOG_INFO("checkpoint cleanup completed");
    return KEXEC_SUCCESS;
}