/*
 * checkpoint-mgmt.h - YakirOS checkpoint storage lifecycle management
 *
 * Manages checkpoint metadata, storage organization, cleanup policies,
 * and quota enforcement for the YakirOS checkpoint system.
 *
 * Storage layout:
 *   /run/graph/checkpoints/<component>/
 *   ├── metadata.json          # Component metadata, capabilities, timestamp
 *   ├── <timestamp>/           # Individual checkpoint directories
 *   │   ├── dump-*             # CRIU image files
 *   │   ├── pagemap-*          # Memory mappings
 *   │   └── ...                # Other CRIU files
 *   └── ...
 *
 *   /var/lib/graph/checkpoints/<component>/  # Long-term storage
 *   └── (same structure as /run)
 */

#ifndef CHECKPOINT_MGMT_H
#define CHECKPOINT_MGMT_H

#include "checkpoint.h"
#include <sys/types.h>
#include <time.h>

/* Storage locations for checkpoints */
#define CHECKPOINT_RUN_DIR "/run/graph/checkpoints"
#define CHECKPOINT_VAR_DIR "/var/lib/graph/checkpoints"

/* Maximum number of checkpoints to keep per component */
#define MAX_CHECKPOINTS_PER_COMPONENT 10

/* Default storage quota per component (in MB) */
#define DEFAULT_STORAGE_QUOTA_MB 100

/* Checkpoint ID format: timestamp-based for sorting */
#define CHECKPOINT_ID_FORMAT "%ld"
#define CHECKPOINT_ID_MAX_LEN 256

/* Storage quota information */
typedef struct {
    size_t quota_bytes;          /* Maximum storage allowed */
    size_t used_bytes;           /* Currently used storage */
    int max_checkpoints;         /* Maximum number of checkpoints to keep */
    int current_count;           /* Current number of checkpoints */
} checkpoint_quota_t;

/* Checkpoint list entry for enumeration */
typedef struct checkpoint_entry {
    char id[CHECKPOINT_ID_MAX_LEN];     /* Checkpoint ID (timestamp) */
    char path[MAX_CHECKPOINT_PATH];      /* Full path to checkpoint directory */
    checkpoint_metadata_t metadata;      /* Checkpoint metadata */
    struct checkpoint_entry *next;       /* Linked list pointer */
} checkpoint_entry_t;

/* Save checkpoint metadata to JSON file
 *
 * image_dir: Directory containing checkpoint images
 * metadata: Metadata structure to save
 *
 * Creates a metadata.json file in the checkpoint directory with:
 * - Component name and capabilities
 * - Timestamp and original PID
 * - CRIU version and configuration
 * - Size information
 *
 * Returns: 0 on success, negative error code on failure
 */
int checkpoint_save_metadata(const char *image_dir, const checkpoint_metadata_t *metadata);

/* Load checkpoint metadata from JSON file
 *
 * image_dir: Directory containing checkpoint images
 * metadata: Structure to populate with loaded metadata
 *
 * Reads metadata.json from the checkpoint directory and populates
 * the metadata structure. Validates required fields.
 *
 * Returns: 0 on success, negative error code on failure
 */
int checkpoint_load_metadata(const char *image_dir, checkpoint_metadata_t *metadata);

/* Create a new checkpoint directory with unique ID
 *
 * component_name: Name of component being checkpointed
 * persistent: If true, use /var/lib storage; if false, use /run storage
 * checkpoint_id: Buffer to store generated checkpoint ID
 * id_size: Size of checkpoint_id buffer
 * path: Buffer to store full path to checkpoint directory
 * path_size: Size of path buffer
 *
 * Creates directory structure:
 *   <storage>/<component>/<timestamp>/
 *
 * The timestamp-based ID ensures chronological ordering and uniqueness.
 *
 * Returns: 0 on success, negative error code on failure
 */
int checkpoint_create_directory(const char *component_name, int persistent,
                               char *checkpoint_id, size_t id_size,
                               char *path, size_t path_size);

/* List available checkpoints for a component
 *
 * component_name: Component to list checkpoints for (or NULL for all)
 * persistent: Search persistent storage if true, temporary if false
 * head: Pointer to store head of linked list
 *
 * Returns a linked list of checkpoint entries sorted by timestamp
 * (newest first). Caller must free the list using checkpoint_free_list().
 *
 * Returns: Number of checkpoints found, negative error code on failure
 */
int checkpoint_list_checkpoints(const char *component_name, int persistent,
                                checkpoint_entry_t **head);

/* Free checkpoint list returned by checkpoint_list_checkpoints
 *
 * head: Head of linked list to free
 */
void checkpoint_free_list(checkpoint_entry_t *head);

/* Clean up old checkpoints based on policy
 *
 * component_name: Component to clean up (or NULL for all components)
 * keep_count: Maximum number of checkpoints to keep (0 = use default)
 * max_age_hours: Maximum age in hours (0 = no age limit)
 * persistent: Clean persistent storage if true, temporary if false
 *
 * Cleanup policy:
 * 1. Remove checkpoints older than max_age_hours
 * 2. Keep only the newest keep_count checkpoints
 * 3. Ensure total storage stays under quota
 *
 * Returns: Number of checkpoints removed, negative error code on failure
 */
int checkpoint_cleanup(const char *component_name, int keep_count,
                      int max_age_hours, int persistent);

/* Remove a specific checkpoint
 *
 * component_name: Component name
 * checkpoint_id: ID of checkpoint to remove
 * persistent: Remove from persistent storage if true, temporary if false
 *
 * Completely removes the checkpoint directory and all its contents.
 *
 * Returns: 0 on success, negative error code on failure
 */
int checkpoint_remove(const char *component_name, const char *checkpoint_id,
                     int persistent);

/* Calculate storage usage for a component
 *
 * component_name: Component to analyze (or NULL for all components)
 * persistent: Analyze persistent storage if true, temporary if false
 * quota: Structure to populate with usage information
 *
 * Calculates:
 * - Total storage used by checkpoints
 * - Number of checkpoints
 * - Storage quota (from configuration or default)
 *
 * Returns: 0 on success, negative error code on failure
 */
int checkpoint_storage_usage(const char *component_name, int persistent,
                           checkpoint_quota_t *quota);

/* Find the latest checkpoint for a component
 *
 * component_name: Component to find checkpoint for
 * persistent: Search persistent storage if true, temporary if false
 * latest_id: Buffer to store checkpoint ID
 * id_size: Size of latest_id buffer
 * path: Buffer to store full path to checkpoint directory
 * path_size: Size of path buffer
 *
 * Finds the most recent checkpoint based on timestamp.
 *
 * Returns: 0 on success, negative error code on failure
 */
int checkpoint_find_latest(const char *component_name, int persistent,
                          char *latest_id, size_t id_size,
                          char *path, size_t path_size);

/* Migrate checkpoint from temporary to persistent storage
 *
 * component_name: Component name
 * checkpoint_id: ID of checkpoint to migrate
 *
 * Moves checkpoint from /run/graph/checkpoints to /var/lib/graph/checkpoints
 * for long-term storage. Useful for creating permanent backups before
 * system upgrades.
 *
 * Returns: 0 on success, negative error code on failure
 */
int checkpoint_migrate_to_persistent(const char *component_name,
                                    const char *checkpoint_id);

/* Archive checkpoint to external location
 *
 * component_name: Component name
 * checkpoint_id: ID of checkpoint to archive
 * archive_path: Path to create archive file (e.g., "/backup/checkpoint.tar.gz")
 * persistent: Archive from persistent storage if true, temporary if false
 *
 * Creates a compressed tar archive of the checkpoint for external storage
 * or migration to another system.
 *
 * Returns: 0 on success, negative error code on failure
 */
int checkpoint_archive(const char *component_name, const char *checkpoint_id,
                      const char *archive_path, int persistent);

/* Extract checkpoint from archive
 *
 * archive_path: Path to archive file
 * component_name: Component name (for destination directory)
 * persistent: Extract to persistent storage if true, temporary if false
 * checkpoint_id: Buffer to store extracted checkpoint ID
 * id_size: Size of checkpoint_id buffer
 *
 * Extracts a checkpoint archive and sets up the directory structure.
 *
 * Returns: 0 on success, negative error code on failure
 */
int checkpoint_extract_archive(const char *archive_path,
                              const char *component_name, int persistent,
                              char *checkpoint_id, size_t id_size);

/* Initialize checkpoint storage directories
 *
 * Creates the base directory structure if it doesn't exist:
 * - /run/graph/checkpoints/
 * - /var/lib/graph/checkpoints/
 *
 * Called during system initialization.
 *
 * Returns: 0 on success, negative error code on failure
 */
int checkpoint_init_storage(void);

#endif /* CHECKPOINT_MGMT_H */