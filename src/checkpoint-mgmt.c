/*
 * checkpoint-mgmt.c - YakirOS checkpoint storage lifecycle implementation
 *
 * Implements checkpoint storage management, metadata handling, cleanup
 * policies, and quota enforcement for the YakirOS checkpoint system.
 */

#include "checkpoint-mgmt.h"
#include "log.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* JSON parsing helpers for metadata */
static int write_json_string(FILE *fp, const char *key, const char *value, int last) {
    return fprintf(fp, "  \"%s\": \"%s\"%s\n", key, value, last ? "" : ",");
}

static int write_json_int(FILE *fp, const char *key, int value, int last) {
    return fprintf(fp, "  \"%s\": %d%s\n", key, value, last ? "" : ",");
}

static int write_json_time(FILE *fp, const char *key, time_t value, int last) {
    return fprintf(fp, "  \"%s\": %ld%s\n", key, value, last ? "" : ",");
}

/* Calculate directory size recursively */
static size_t calculate_directory_size(const char *path) {
    struct stat st;
    size_t total_size = 0;

    if (stat(path, &st) != 0) {
        return 0;
    }

    if (S_ISREG(st.st_mode)) {
        return st.st_size;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            return 0;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            total_size += calculate_directory_size(full_path);
        }

        closedir(dir);
    }

    return total_size;
}

/* Remove directory recursively */
static int remove_directory_recursive(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        return -1;
    }

    struct dirent *entry;
    int result = 0;

    while ((entry = readdir(dir)) != NULL && result == 0) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                result = remove_directory_recursive(full_path);
            } else {
                result = unlink(full_path);
            }
        }
    }

    closedir(dir);

    if (result == 0) {
        result = rmdir(path);
    }

    return result;
}

/* Create directory path recursively */
static int create_directory_path(const char *path) {
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

/* Initialize checkpoint storage directories */
int checkpoint_init_storage(void) {
    /* Create runtime checkpoint directory */
    if (create_directory_path(CHECKPOINT_RUN_DIR) != 0) {
        LOG_ERR("Failed to create runtime checkpoint directory %s: %s",
                CHECKPOINT_RUN_DIR, strerror(errno));
        return -1;
    }

    /* Create persistent checkpoint directory */
    if (create_directory_path(CHECKPOINT_VAR_DIR) != 0) {
        LOG_ERR("Failed to create persistent checkpoint directory %s: %s",
                CHECKPOINT_VAR_DIR, strerror(errno));
        return -1;
    }

    LOG_INFO("Initialized checkpoint storage directories");
    return 0;
}

/* Save checkpoint metadata to JSON file */
int checkpoint_save_metadata(const char *image_dir, const checkpoint_metadata_t *metadata) {
    if (!image_dir || !metadata) {
        return -1;
    }

    char metadata_path[512];
    snprintf(metadata_path, sizeof(metadata_path), "%s/metadata.json", image_dir);

    FILE *fp = fopen(metadata_path, "w");
    if (!fp) {
        LOG_ERR("Failed to create metadata file %s: %s", metadata_path, strerror(errno));
        return -1;
    }

    fprintf(fp, "{\n");
    write_json_string(fp, "component_name", metadata->component_name, 0);
    write_json_int(fp, "original_pid", metadata->original_pid, 0);
    write_json_time(fp, "timestamp", metadata->timestamp, 0);
    write_json_int(fp, "image_size", (int)metadata->image_size, 0);
    write_json_string(fp, "capabilities", metadata->capabilities, 0);

    fprintf(fp, "  \"criu_version\": {\n");
    write_json_int(fp, "major", metadata->criu_version.major, 0);
    write_json_int(fp, "minor", metadata->criu_version.minor, 0);
    write_json_int(fp, "patch", metadata->criu_version.patch, 1);
    fprintf(fp, "  },\n");

    write_json_int(fp, "leave_running", metadata->leave_running, 0);
    write_json_string(fp, "preserve_fds", metadata->preserve_fds, 1);
    fprintf(fp, "}\n");

    fclose(fp);

    LOG_INFO("Saved checkpoint metadata to %s", metadata_path);
    return 0;
}

/* Load checkpoint metadata from JSON file */
int checkpoint_load_metadata(const char *image_dir, checkpoint_metadata_t *metadata) {
    if (!image_dir || !metadata) {
        return -1;
    }

    char metadata_path[512];
    snprintf(metadata_path, sizeof(metadata_path), "%s/metadata.json", image_dir);

    FILE *fp = fopen(metadata_path, "r");
    if (!fp) {
        LOG_ERR("Failed to open metadata file %s: %s", metadata_path, strerror(errno));
        return -1;
    }

    /* Simple JSON parsing - in a production system you'd use a proper JSON parser */
    memset(metadata, 0, sizeof(*metadata));

    char line[512];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char key[128], value[384];

        /* Parse "key": "value" format */
        if (sscanf(line, " \"%127[^\"]\": \"%383[^\"]\"", key, value) == 2) {
            if (strcmp(key, "component_name") == 0) {
                strncpy(metadata->component_name, value, sizeof(metadata->component_name) - 1);
                metadata->component_name[sizeof(metadata->component_name) - 1] = '\0';
            } else if (strcmp(key, "capabilities") == 0) {
                strncpy(metadata->capabilities, value, sizeof(metadata->capabilities) - 1);
                metadata->capabilities[sizeof(metadata->capabilities) - 1] = '\0';
            } else if (strcmp(key, "preserve_fds") == 0) {
                strncpy(metadata->preserve_fds, value, sizeof(metadata->preserve_fds) - 1);
                metadata->preserve_fds[sizeof(metadata->preserve_fds) - 1] = '\0';
            }
        }
        /* Parse "key": number format */
        else if (sscanf(line, " \"%127[^\"]\": %d", key, (int*)value) == 2) {
            int int_val = *(int*)value;
            if (strcmp(key, "original_pid") == 0) {
                metadata->original_pid = int_val;
            } else if (strcmp(key, "image_size") == 0) {
                metadata->image_size = int_val;
            } else if (strcmp(key, "leave_running") == 0) {
                metadata->leave_running = int_val;
            } else if (strcmp(key, "major") == 0) {
                metadata->criu_version.major = int_val;
            } else if (strcmp(key, "minor") == 0) {
                metadata->criu_version.minor = int_val;
            } else if (strcmp(key, "patch") == 0) {
                metadata->criu_version.patch = int_val;
            }
        }
        /* Parse timestamp */
        else if (sscanf(line, " \"%127[^\"]\": %ld", key, &metadata->timestamp) == 2) {
            /* timestamp already assigned */
        }
    }

    fclose(fp);

    LOG_INFO("Loaded checkpoint metadata from %s", metadata_path);
    return 0;
}

/* Create a new checkpoint directory with unique ID */
int checkpoint_create_directory(const char *component_name, int persistent,
                               char *checkpoint_id, size_t id_size,
                               char *path, size_t path_size) {
    if (!component_name || !checkpoint_id || !path) {
        return -1;
    }

    /* Generate timestamp-based checkpoint ID */
    time_t now = time(NULL);
    snprintf(checkpoint_id, id_size, CHECKPOINT_ID_FORMAT, now);

    /* Build full path */
    const char *base_dir = persistent ? CHECKPOINT_VAR_DIR : CHECKPOINT_RUN_DIR;
    snprintf(path, path_size, "%s/%s/%s", base_dir, component_name, checkpoint_id);

    /* Create the directory structure */
    if (create_directory_path(path) != 0) {
        LOG_ERR("Failed to create checkpoint directory %s: %s", path, strerror(errno));
        return -1;
    }

    LOG_INFO("Created checkpoint directory %s with ID %s", path, checkpoint_id);
    return 0;
}

/* List available checkpoints for a component */
int checkpoint_list_checkpoints(const char *component_name, int persistent,
                                checkpoint_entry_t **head) {
    if (!head) {
        return -1;
    }

    *head = NULL;
    int count = 0;

    const char *base_dir = persistent ? CHECKPOINT_VAR_DIR : CHECKPOINT_RUN_DIR;
    char search_dir[512];

    if (component_name) {
        snprintf(search_dir, sizeof(search_dir), "%s/%s", base_dir, component_name);
    } else {
        strncpy(search_dir, base_dir, sizeof(search_dir) - 1);
    }

    DIR *dir = opendir(search_dir);
    if (!dir) {
        if (errno == ENOENT) {
            LOG_INFO("Checkpoint directory %s does not exist", search_dir);
            return 0; /* No checkpoints found - not an error */
        }
        LOG_ERR("Failed to open checkpoint directory %s: %s", search_dir, strerror(errno));
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char entry_path[512];
        snprintf(entry_path, sizeof(entry_path), "%s/%s", search_dir, entry->d_name);

        struct stat st;
        if (stat(entry_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }

        /* If searching all components, handle nested structure */
        if (!component_name) {
            /* This is a component directory, recurse into it */
            checkpoint_entry_t *component_head = NULL;
            int component_count = checkpoint_list_checkpoints(entry->d_name, persistent, &component_head);

            if (component_count > 0) {
                /* Append component checkpoints to main list */
                checkpoint_entry_t *tail = component_head;
                while (tail->next) {
                    tail = tail->next;
                }
                tail->next = *head;
                *head = component_head;
                count += component_count;
            }
            continue;
        }

        /* This is a checkpoint directory */
        checkpoint_entry_t *new_entry = malloc(sizeof(checkpoint_entry_t));
        if (!new_entry) {
            LOG_ERR("Failed to allocate memory for checkpoint entry");
            continue;
        }

        memset(new_entry, 0, sizeof(*new_entry));
        strncpy(new_entry->id, entry->d_name, sizeof(new_entry->id) - 1);
        new_entry->id[sizeof(new_entry->id) - 1] = '\0';
        strncpy(new_entry->path, entry_path, sizeof(new_entry->path) - 1);
        new_entry->path[sizeof(new_entry->path) - 1] = '\0';

        /* Load metadata if available */
        if (checkpoint_load_metadata(entry_path, &new_entry->metadata) != 0) {
            /* If metadata loading fails, populate basic info */
            strncpy(new_entry->metadata.component_name, component_name,
                   sizeof(new_entry->metadata.component_name) - 1);
            new_entry->metadata.timestamp = st.st_mtime;
        }

        /* Insert in sorted order (newest first) */
        if (!*head || new_entry->metadata.timestamp > (*head)->metadata.timestamp) {
            new_entry->next = *head;
            *head = new_entry;
        } else {
            checkpoint_entry_t *current = *head;
            while (current->next &&
                   current->next->metadata.timestamp > new_entry->metadata.timestamp) {
                current = current->next;
            }
            new_entry->next = current->next;
            current->next = new_entry;
        }

        count++;
    }

    closedir(dir);

    LOG_INFO("Found %d checkpoints for component %s", count,
             component_name ? component_name : "(all)");
    return count;
}

/* Free checkpoint list */
void checkpoint_free_list(checkpoint_entry_t *head) {
    while (head) {
        checkpoint_entry_t *next = head->next;
        free(head);
        head = next;
    }
}

/* Clean up old checkpoints */
int checkpoint_cleanup(const char *component_name, int keep_count,
                      int max_age_hours, int persistent) {
    checkpoint_entry_t *head = NULL;
    int total_count = checkpoint_list_checkpoints(component_name, persistent, &head);

    if (total_count <= 0) {
        return 0; /* Nothing to clean up */
    }

    if (keep_count == 0) {
        keep_count = MAX_CHECKPOINTS_PER_COMPONENT;
    }

    time_t now = time(NULL);
    time_t max_age_seconds = max_age_hours * 3600;
    int removed_count = 0;

    checkpoint_entry_t *current = head;
    int position = 0;

    while (current) {
        int should_remove = 0;

        /* Remove if too old */
        if (max_age_hours > 0 && (now - current->metadata.timestamp) > max_age_seconds) {
            should_remove = 1;
            LOG_INFO("Checkpoint %s too old (%ld hours), removing",
                     current->id, (now - current->metadata.timestamp) / 3600);
        }
        /* Remove if beyond keep count */
        else if (position >= keep_count) {
            should_remove = 1;
            LOG_INFO("Checkpoint %s beyond keep count (%d), removing", current->id, keep_count);
        }

        if (should_remove) {
            if (remove_directory_recursive(current->path) == 0) {
                removed_count++;
                LOG_INFO("Removed old checkpoint %s", current->path);
            } else {
                LOG_ERR("Failed to remove checkpoint %s: %s", current->path, strerror(errno));
            }
        }

        current = current->next;
        position++;
    }

    checkpoint_free_list(head);

    LOG_INFO("Cleanup complete: removed %d checkpoints", removed_count);
    return removed_count;
}

/* Remove a specific checkpoint */
int checkpoint_remove(const char *component_name, const char *checkpoint_id,
                     int persistent) {
    if (!component_name || !checkpoint_id) {
        return -1;
    }

    const char *base_dir = persistent ? CHECKPOINT_VAR_DIR : CHECKPOINT_RUN_DIR;
    char checkpoint_path[512];
    snprintf(checkpoint_path, sizeof(checkpoint_path), "%s/%s/%s",
            base_dir, component_name, checkpoint_id);

    if (remove_directory_recursive(checkpoint_path) != 0) {
        LOG_ERR("Failed to remove checkpoint %s: %s", checkpoint_path, strerror(errno));
        return -1;
    }

    LOG_INFO("Removed checkpoint %s for component %s", checkpoint_id, component_name);
    return 0;
}

/* Calculate storage usage */
int checkpoint_storage_usage(const char *component_name, int persistent,
                           checkpoint_quota_t *quota) {
    if (!quota) {
        return -1;
    }

    memset(quota, 0, sizeof(*quota));
    quota->quota_bytes = DEFAULT_STORAGE_QUOTA_MB * 1024 * 1024;
    quota->max_checkpoints = MAX_CHECKPOINTS_PER_COMPONENT;

    checkpoint_entry_t *head = NULL;
    int count = checkpoint_list_checkpoints(component_name, persistent, &head);

    if (count < 0) {
        return count;
    }

    quota->current_count = count;

    checkpoint_entry_t *current = head;
    while (current) {
        size_t checkpoint_size = calculate_directory_size(current->path);
        quota->used_bytes += checkpoint_size;
        current = current->next;
    }

    checkpoint_free_list(head);

    LOG_INFO("Storage usage for %s: %zu bytes used / %zu bytes quota (%d checkpoints)",
             component_name ? component_name : "(all)",
             quota->used_bytes, quota->quota_bytes, quota->current_count);

    return 0;
}

/* Find latest checkpoint */
int checkpoint_find_latest(const char *component_name, int persistent,
                          char *latest_id, size_t id_size,
                          char *path, size_t path_size) {
    if (!component_name || !latest_id || !path) {
        return -1;
    }

    checkpoint_entry_t *head = NULL;
    int count = checkpoint_list_checkpoints(component_name, persistent, &head);

    if (count <= 0) {
        checkpoint_free_list(head);
        return -1; /* No checkpoints found */
    }

    /* List is sorted newest first, so head is the latest */
    strncpy(latest_id, head->id, id_size - 1);
    latest_id[id_size - 1] = '\0';

    strncpy(path, head->path, path_size - 1);
    path[path_size - 1] = '\0';

    checkpoint_free_list(head);

    LOG_INFO("Found latest checkpoint %s at %s for component %s",
             latest_id, path, component_name);
    return 0;
}

/* Migrate checkpoint to persistent storage */
int checkpoint_migrate_to_persistent(const char *component_name,
                                    const char *checkpoint_id) {
    if (!component_name || !checkpoint_id) {
        return -1;
    }

    char src_path[512], dst_path[512];
    snprintf(src_path, sizeof(src_path), "%s/%s/%s",
            CHECKPOINT_RUN_DIR, component_name, checkpoint_id);
    snprintf(dst_path, sizeof(dst_path), "%s/%s/%s",
            CHECKPOINT_VAR_DIR, component_name, checkpoint_id);

    /* Create destination directory */
    char dst_parent[512];
    snprintf(dst_parent, sizeof(dst_parent), "%s/%s", CHECKPOINT_VAR_DIR, component_name);
    if (create_directory_path(dst_parent) != 0) {
        LOG_ERR("Failed to create destination directory %s: %s", dst_parent, strerror(errno));
        return -1;
    }

    /* Use system cp command for recursive copy */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cp -r '%s' '%s'", src_path, dst_path);

    int result = system(cmd);
    if (result != 0) {
        LOG_ERR("Failed to migrate checkpoint %s to persistent storage", checkpoint_id);
        return -1;
    }

    LOG_INFO("Migrated checkpoint %s for component %s to persistent storage",
             checkpoint_id, component_name);
    return 0;
}