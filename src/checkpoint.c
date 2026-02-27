/*
 * checkpoint.c - YakirOS CRIU integration implementation
 *
 * Implements low-level CRIU wrapper functions for process checkpoint/restore
 * operations. Provides robust error handling, timeout management, and
 * validation for production use as PID 1.
 */

#define _POSIX_C_SOURCE 200809L

#include "checkpoint.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Path to CRIU binary */
#define CRIU_BINARY "/usr/sbin/criu"

/* Alternative paths to try if CRIU_BINARY doesn't exist */
static const char *criu_paths[] = {
    "/usr/sbin/criu",
    "/usr/bin/criu",
    "/sbin/criu",
    "/bin/criu",
    NULL
};

/* Find CRIU binary in system PATH or common locations */
static const char *find_criu_binary(void) {
    static char found_path[256] = {0};

    /* If we already found it, return cached result */
    if (found_path[0] != '\0') {
        return found_path;
    }

    /* Try each possible path */
    for (int i = 0; criu_paths[i] != NULL; i++) {
        if (access(criu_paths[i], X_OK) == 0) {
            strncpy(found_path, criu_paths[i], sizeof(found_path) - 1);
            found_path[sizeof(found_path) - 1] = '\0';
            return found_path;
        }
    }

    return NULL;
}

/* Execute CRIU command with timeout handling */
int execute_criu_command(char *const argv[], int timeout_sec,
                        char *output, size_t output_size) {
    const char *criu_binary = find_criu_binary();
    if (!criu_binary) {
        LOG_ERR("CRIU binary not found in standard locations");
        return CHECKPOINT_ERROR_CRIU_NOT_FOUND;
    }

    /* Create pipe for capturing output */
    int pipefd[2];
    if (output && pipe(pipefd) == -1) {
        LOG_ERR("Failed to create pipe for CRIU output: %s", strerror(errno));
        return CHECKPOINT_ERROR_CRIU_NOT_FOUND;
    }

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERR("Failed to fork for CRIU execution: %s", strerror(errno));
        if (output) {
            close(pipefd[0]);
            close(pipefd[1]);
        }
        return CHECKPOINT_ERROR_CRIU_NOT_FOUND;
    }

    if (pid == 0) {
        /* Child process - execute CRIU */

        /* Set up output redirection if requested */
        if (output) {
            close(pipefd[0]); /* Close read end */
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);
        } else {
            /* Redirect to /dev/null to avoid noise */
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
        }

        /* Create new argv with correct binary path */
        char *exec_argv[32];  /* Reasonable limit for arguments */
        exec_argv[0] = (char *)criu_binary;
        for (int i = 1; argv[i] != NULL && i < 31; i++) {
            exec_argv[i] = argv[i];
        }
        exec_argv[31] = NULL; /* Ensure null termination */

        /* Execute CRIU */
        execv(criu_binary, exec_argv);

        /* If we get here, exec failed */
        fprintf(stderr, "Failed to execute CRIU: %s\n", strerror(errno));
        _exit(127);
    }

    /* Parent process - wait for completion with timeout */
    if (output) {
        close(pipefd[1]); /* Close write end */

        /* Read output from pipe */
        ssize_t bytes_read = read(pipefd[0], output, output_size - 1);
        if (bytes_read > 0) {
            output[bytes_read] = '\0';
        } else {
            output[0] = '\0';
        }
        close(pipefd[0]);
    }

    /* Handle timeout if specified */
    if (timeout_sec > 0) {
        int elapsed = 0;
        int status;
        pid_t result;

        while (elapsed < timeout_sec) {
            result = waitpid(pid, &status, WNOHANG);
            if (result == pid) {
                /* Process completed */
                if (WIFEXITED(status)) {
                    int exit_code = WEXITSTATUS(status);
                    if (exit_code == 0) {
                        return CHECKPOINT_SUCCESS;
                    } else {
                        LOG_ERR("CRIU command failed with exit code %d", exit_code);
                        return CHECKPOINT_ERROR_RESTORE_FAILED;
                    }
                } else if (WIFSIGNALED(status)) {
                    LOG_ERR("CRIU command killed by signal %d", WTERMSIG(status));
                    return CHECKPOINT_ERROR_RESTORE_FAILED;
                }
            } else if (result == -1) {
                LOG_ERR("waitpid failed: %s", strerror(errno));
                return CHECKPOINT_ERROR_RESTORE_FAILED;
            }

            sleep(1);
            elapsed++;
        }

        /* Timeout occurred - kill the process */
        LOG_ERR("CRIU command timed out after %d seconds", timeout_sec);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0); /* Clean up zombie */
        return CHECKPOINT_ERROR_TIMEOUT;
    } else {
        /* No timeout - wait indefinitely */
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            LOG_ERR("waitpid failed: %s", strerror(errno));
            return CHECKPOINT_ERROR_RESTORE_FAILED;
        }

        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            if (exit_code == 0) {
                return CHECKPOINT_SUCCESS;
            } else {
                LOG_ERR("CRIU command failed with exit code %d", exit_code);
                return CHECKPOINT_ERROR_RESTORE_FAILED;
            }
        } else if (WIFSIGNALED(status)) {
            LOG_ERR("CRIU command killed by signal %d", WTERMSIG(status));
            return CHECKPOINT_ERROR_RESTORE_FAILED;
        }
    }

    return CHECKPOINT_ERROR_RESTORE_FAILED;
}

/* Check if CRIU is supported on this system */
int criu_is_supported(void) {
    /* Check if CRIU binary exists and is executable */
    const char *criu_binary = find_criu_binary();
    if (!criu_binary) {
        LOG_INFO("CRIU not supported: binary not found");
        return CHECKPOINT_ERROR_CRIU_NOT_FOUND;
    }

    /* Check kernel support by running 'criu check' */
    char *argv[] = { "criu", "check", NULL };
    char output[1024];

    int result = execute_criu_command(argv, 10, output, sizeof(output));
    if (result != CHECKPOINT_SUCCESS) {
        LOG_INFO("CRIU not supported: kernel check failed");
        if (output[0] != '\0') {
            LOG_INFO("CRIU check output: %s", output);
        }
        return CHECKPOINT_ERROR_KERNEL_UNSUPPORTED;
    }

    LOG_INFO("CRIU is supported on this system");
    return CHECKPOINT_SUCCESS;
}

/* Get CRIU version information */
int criu_get_version(criu_version_t *version) {
    if (!version) {
        return CHECKPOINT_ERROR_RESTORE_FAILED;
    }

    char *argv[] = { "criu", "--version", NULL };
    char output[256];

    int result = execute_criu_command(argv, 5, output, sizeof(output));
    if (result != CHECKPOINT_SUCCESS) {
        return result;
    }

    /* Parse version string like "Version: 3.15" or "Version: 3.16.1" */
    memset(version, 0, sizeof(*version));

    char *version_line = strstr(output, "Version:");
    if (version_line) {
        /* Skip "Version: " prefix */
        version_line += 9;

        /* Parse major.minor.patch */
        if (sscanf(version_line, "%d.%d.%d",
                  &version->major, &version->minor, &version->patch) >= 2) {
            LOG_INFO("CRIU version: %d.%d.%d",
                     version->major, version->minor, version->patch);
            return CHECKPOINT_SUCCESS;
        }
    }

    LOG_ERR("Failed to parse CRIU version from output: %s", output);
    return CHECKPOINT_ERROR_RESTORE_FAILED;
}

/* Checkpoint a running process */
int criu_checkpoint_process(pid_t pid, const char *image_dir, int leave_running) {
    if (pid <= 0 || !image_dir) {
        return CHECKPOINT_ERROR_PROCESS_NOT_FOUND;
    }

    /* Check if process exists */
    if (kill(pid, 0) != 0) {
        if (errno == ESRCH) {
            LOG_ERR("Process %d not found for checkpoint", pid);
            return CHECKPOINT_ERROR_PROCESS_NOT_FOUND;
        } else if (errno == EPERM) {
            LOG_ERR("Permission denied for checkpointing process %d", pid);
            return CHECKPOINT_ERROR_PERMISSION_DENIED;
        }
    }

    /* Ensure image directory exists */
    if (mkdir(image_dir, 0755) != 0 && errno != EEXIST) {
        LOG_ERR("Failed to create checkpoint directory %s: %s",
                image_dir, strerror(errno));
        return CHECKPOINT_ERROR_RESTORE_FAILED;
    }

    /* Build CRIU dump command */
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d", pid);

    char *argv[16];
    int argc = 0;

    argv[argc++] = "criu";
    argv[argc++] = "dump";
    argv[argc++] = "-t";
    argv[argc++] = pid_str;
    argv[argc++] = "-D";
    argv[argc++] = (char *)image_dir;
    argv[argc++] = "--shell-job";  /* Allow dumping processes with shell job control */
    argv[argc++] = "-v4";          /* Verbose logging for debugging */

    if (leave_running) {
        argv[argc++] = "--leave-running";
    }

    argv[argc] = NULL;

    LOG_INFO("Checkpointing process %d to %s (leave_running=%d)",
             pid, image_dir, leave_running);

    char output[2048];
    int result = execute_criu_command(argv, CHECKPOINT_DEFAULT_TIMEOUT,
                                     output, sizeof(output));

    if (result != CHECKPOINT_SUCCESS) {
        LOG_ERR("CRIU checkpoint failed for process %d", pid);
        if (output[0] != '\0') {
            LOG_ERR("CRIU output: %s", output);
        }
        return result;
    }

    LOG_INFO("Successfully checkpointed process %d to %s", pid, image_dir);
    return CHECKPOINT_SUCCESS;
}

/* Restore a process from checkpoint images */
pid_t criu_restore_process(const char *image_dir) {
    if (!image_dir) {
        return CHECKPOINT_ERROR_RESTORE_FAILED;
    }

    /* Validate checkpoint images first */
    int validation_result = checkpoint_validate_image(image_dir);
    if (validation_result != CHECKPOINT_SUCCESS) {
        LOG_ERR("Checkpoint validation failed: %s",
                checkpoint_error_string(validation_result));
        return validation_result;
    }

    /* Build CRIU restore command */
    char *argv[] = {
        "criu",
        "restore",
        "-D", (char *)image_dir,
        "--shell-job",
        "-v4",
        NULL
    };

    LOG_INFO("Restoring process from %s", image_dir);

    char output[2048];
    int result = execute_criu_command(argv, CHECKPOINT_DEFAULT_TIMEOUT,
                                     output, sizeof(output));

    if (result != CHECKPOINT_SUCCESS) {
        LOG_ERR("CRIU restore failed from %s", image_dir);
        if (output[0] != '\0') {
            LOG_ERR("CRIU output: %s", output);
        }
        return result;
    }

    /* Parse restored PID from output */
    /* CRIU restore output typically contains a line like "Restored process with PID 12345" */
    pid_t restored_pid = 0;
    char *pid_line = strstr(output, "PID");
    if (pid_line) {
        if (sscanf(pid_line, "PID %d", &restored_pid) == 1 && restored_pid > 0) {
            LOG_INFO("Successfully restored process with PID %d from %s",
                     restored_pid, image_dir);
            return restored_pid;
        }
    }

    /* If we can't parse PID from output, the restore likely failed */
    LOG_ERR("Could not determine restored PID from CRIU output: %s", output);
    return CHECKPOINT_ERROR_RESTORE_FAILED;
}

/* Validate checkpoint images */
int checkpoint_validate_image(const char *image_dir) {
    if (!image_dir) {
        return CHECKPOINT_ERROR_IMAGE_CORRUPT;
    }

    /* Check if directory exists */
    struct stat st;
    if (stat(image_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        LOG_ERR("Checkpoint directory does not exist: %s", image_dir);
        return CHECKPOINT_ERROR_IMAGE_CORRUPT;
    }

    /* Check for essential CRIU image files */
    const char *required_files[] = {
        "core",       /* At least one core file should exist */
        "mm",         /* Memory mappings */
        "pstree.img", /* Process tree */
        NULL
    };

    for (int i = 0; required_files[i] != NULL; i++) {
        char pattern[512];
        snprintf(pattern, sizeof(pattern), "%s/%s*", image_dir, required_files[i]);

        /* Simple check - look for files starting with the required prefix */
        /* In a real implementation, we might use glob() or readdir() */
        char test_file[512];
        snprintf(test_file, sizeof(test_file), "%s/%s-1.img",
                image_dir, required_files[i]);

        if (access(test_file, R_OK) != 0) {
            LOG_ERR("Missing or unreadable checkpoint file pattern: %s*",
                   required_files[i]);
            return CHECKPOINT_ERROR_IMAGE_CORRUPT;
        }
    }

    LOG_INFO("Checkpoint images in %s appear valid", image_dir);
    return CHECKPOINT_SUCCESS;
}

/* Get human-readable error message */
const char *checkpoint_error_string(int error_code) {
    switch (error_code) {
        case CHECKPOINT_SUCCESS:
            return "Success";
        case CHECKPOINT_ERROR_CRIU_NOT_FOUND:
            return "CRIU binary not found";
        case CHECKPOINT_ERROR_KERNEL_UNSUPPORTED:
            return "Kernel does not support checkpoint/restore";
        case CHECKPOINT_ERROR_PROCESS_NOT_FOUND:
            return "Process not found";
        case CHECKPOINT_ERROR_PERMISSION_DENIED:
            return "Permission denied";
        case CHECKPOINT_ERROR_TIMEOUT:
            return "Operation timed out";
        case CHECKPOINT_ERROR_IMAGE_CORRUPT:
            return "Checkpoint image corrupt or missing";
        case CHECKPOINT_ERROR_RESTORE_FAILED:
            return "Restore operation failed";
        default:
            return "Unknown error";
    }
}