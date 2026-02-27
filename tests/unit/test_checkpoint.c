/*
 * test_checkpoint.c - Unit tests for CRIU checkpoint/restore functionality
 */

#define _GNU_SOURCE
#include "../test_framework.h"
#include "../../src/checkpoint.h"
#include "../../src/checkpoint-mgmt.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>

/* Test CRIU support detection */
TEST(criu_is_supported) {
    int result = criu_is_supported();

    /* Result should be either success or one of the expected error codes */
    ASSERT_TRUE(result == CHECKPOINT_SUCCESS ||
                result == CHECKPOINT_ERROR_CRIU_NOT_FOUND ||
                result == CHECKPOINT_ERROR_KERNEL_UNSUPPORTED);
}

/* Test CRIU version retrieval */
TEST(criu_get_version) {
    criu_version_t version;
    int result = criu_get_version(&version);

    if (result == CHECKPOINT_SUCCESS) {
        /* If CRIU is available, version should be reasonable */
        ASSERT_TRUE(version.major >= 3);  /* Modern CRIU versions */
        ASSERT_TRUE(version.minor >= 0);
        ASSERT_TRUE(version.patch >= 0);
    } else {
        /* If CRIU is not available, should get expected error */
        ASSERT_TRUE(result == CHECKPOINT_ERROR_CRIU_NOT_FOUND ||
                    result == CHECKPOINT_ERROR_KERNEL_UNSUPPORTED);
    }
}

/* Test CRIU version with invalid argument */
TEST(criu_get_version_invalid_args) {
    int result = criu_get_version(NULL);
    ASSERT_NE(result, CHECKPOINT_SUCCESS);
}

/* Test error string function */
TEST(checkpoint_error_string) {
    ASSERT_STR_EQ(checkpoint_error_string(CHECKPOINT_SUCCESS), "Success");
    ASSERT_STR_EQ(checkpoint_error_string(CHECKPOINT_ERROR_CRIU_NOT_FOUND), "CRIU binary not found");
    ASSERT_STR_EQ(checkpoint_error_string(CHECKPOINT_ERROR_KERNEL_UNSUPPORTED), "Kernel does not support checkpoint/restore");
    ASSERT_STR_EQ(checkpoint_error_string(CHECKPOINT_ERROR_PROCESS_NOT_FOUND), "Process not found");
    ASSERT_STR_EQ(checkpoint_error_string(CHECKPOINT_ERROR_PERMISSION_DENIED), "Permission denied");
    ASSERT_STR_EQ(checkpoint_error_string(CHECKPOINT_ERROR_TIMEOUT), "Operation timed out");
    ASSERT_STR_EQ(checkpoint_error_string(CHECKPOINT_ERROR_IMAGE_CORRUPT), "Checkpoint image corrupt or missing");
    ASSERT_STR_EQ(checkpoint_error_string(CHECKPOINT_ERROR_RESTORE_FAILED), "Restore operation failed");
    ASSERT_STR_EQ(checkpoint_error_string(-999), "Unknown error");
}

/* Test checkpoint validation with invalid directory */
TEST(checkpoint_validate_image_invalid_dir) {
    int result = checkpoint_validate_image(NULL);
    ASSERT_EQ(result, CHECKPOINT_ERROR_IMAGE_CORRUPT);

    result = checkpoint_validate_image("/nonexistent/directory");
    ASSERT_EQ(result, CHECKPOINT_ERROR_IMAGE_CORRUPT);
}

/* Test checkpoint process with invalid arguments */
TEST(criu_checkpoint_process_invalid_args) {
    int result = criu_checkpoint_process(0, NULL, 0);
    ASSERT_EQ(result, CHECKPOINT_ERROR_PROCESS_NOT_FOUND);

    result = criu_checkpoint_process(-1, "/tmp/test", 0);
    ASSERT_EQ(result, CHECKPOINT_ERROR_PROCESS_NOT_FOUND);

    result = criu_checkpoint_process(1, NULL, 0);  /* pid 1 with NULL path */
    ASSERT_EQ(result, CHECKPOINT_ERROR_PROCESS_NOT_FOUND);
}

/* Test checkpoint process with nonexistent PID */
TEST(criu_checkpoint_process_nonexistent_pid) {
    char tmpdir[] = "/tmp/checkpoint_test_XXXXXX";
    ASSERT_TRUE(mkdtemp(tmpdir) != NULL);

    /* Use a PID that's very unlikely to exist */
    pid_t fake_pid = 99999;
    int result = criu_checkpoint_process(fake_pid, tmpdir, 0);
    ASSERT_EQ(result, CHECKPOINT_ERROR_PROCESS_NOT_FOUND);

    /* Clean up */
    rmdir(tmpdir);
}

/* Test restore with invalid arguments */
TEST(criu_restore_process_invalid_args) {
    pid_t result = criu_restore_process(NULL);
    ASSERT_TRUE(result < 0);

    result = criu_restore_process("/nonexistent/directory");
    ASSERT_TRUE(result < 0);
}

/* Test checkpoint storage initialization */
TEST(checkpoint_init_storage) {
    int result = checkpoint_init_storage();
    ASSERT_EQ(result, 0);

    /* Verify directories were created */
    struct stat st;
    ASSERT_EQ(stat("/run/graph/checkpoints", &st), 0);
    ASSERT_TRUE(S_ISDIR(st.st_mode));

    ASSERT_EQ(stat("/var/lib/graph/checkpoints", &st), 0);
    ASSERT_TRUE(S_ISDIR(st.st_mode));
}

/* Test checkpoint directory creation */
TEST(checkpoint_create_directory) {
    char checkpoint_id[CHECKPOINT_ID_MAX_LEN];
    char path[MAX_CHECKPOINT_PATH];

    int result = checkpoint_create_directory("test-component", 0, /* temporary */
                                           checkpoint_id, sizeof(checkpoint_id),
                                           path, sizeof(path));
    ASSERT_EQ(result, 0);
    ASSERT_TRUE(strlen(checkpoint_id) > 0);
    ASSERT_TRUE(strlen(path) > 0);
    ASSERT_TRUE(strstr(path, "test-component") != NULL);
    ASSERT_TRUE(strstr(path, checkpoint_id) != NULL);

    /* Verify directory was created */
    struct stat st;
    ASSERT_EQ(stat(path, &st), 0);
    ASSERT_TRUE(S_ISDIR(st.st_mode));

    /* Clean up */
    rmdir(path);
}

/* Test checkpoint directory creation with invalid arguments */
TEST(checkpoint_create_directory_invalid_args) {
    char checkpoint_id[CHECKPOINT_ID_MAX_LEN];
    char path[MAX_CHECKPOINT_PATH];

    int result = checkpoint_create_directory(NULL, 0, checkpoint_id, sizeof(checkpoint_id), path, sizeof(path));
    ASSERT_NE(result, 0);

    result = checkpoint_create_directory("test", 0, NULL, sizeof(checkpoint_id), path, sizeof(path));
    ASSERT_NE(result, 0);

    result = checkpoint_create_directory("test", 0, checkpoint_id, sizeof(checkpoint_id), NULL, sizeof(path));
    ASSERT_NE(result, 0);
}

/* Test checkpoint metadata save and load */
TEST(checkpoint_metadata_save_load) {
    char tmpdir[] = "/tmp/checkpoint_meta_test_XXXXXX";
    ASSERT_TRUE(mkdtemp(tmpdir) != NULL);

    /* Create test metadata */
    checkpoint_metadata_t original_metadata;
    memset(&original_metadata, 0, sizeof(original_metadata));
    strcpy(original_metadata.component_name, "test-service");
    original_metadata.original_pid = 12345;
    original_metadata.timestamp = time(NULL);
    original_metadata.image_size = 1024 * 1024;  /* 1MB */
    strcpy(original_metadata.capabilities, "network,filesystem");
    original_metadata.criu_version.major = 3;
    original_metadata.criu_version.minor = 15;
    original_metadata.criu_version.patch = 0;
    original_metadata.leave_running = 1;
    strcpy(original_metadata.preserve_fds, "network,filesystem");

    /* Save metadata */
    int result = checkpoint_save_metadata(tmpdir, &original_metadata);
    ASSERT_EQ(result, 0);

    /* Load metadata */
    checkpoint_metadata_t loaded_metadata;
    result = checkpoint_load_metadata(tmpdir, &loaded_metadata);
    ASSERT_EQ(result, 0);

    /* Verify metadata matches */
    ASSERT_STR_EQ(loaded_metadata.component_name, "test-service");
    ASSERT_EQ(loaded_metadata.original_pid, 12345);
    ASSERT_EQ(loaded_metadata.timestamp, original_metadata.timestamp);
    ASSERT_EQ(loaded_metadata.image_size, 1024 * 1024);
    ASSERT_STR_EQ(loaded_metadata.capabilities, "network,filesystem");
    ASSERT_EQ(loaded_metadata.criu_version.major, 3);
    ASSERT_EQ(loaded_metadata.criu_version.minor, 15);
    ASSERT_EQ(loaded_metadata.criu_version.patch, 0);
    ASSERT_EQ(loaded_metadata.leave_running, 1);
    ASSERT_STR_EQ(loaded_metadata.preserve_fds, "network,filesystem");

    /* Clean up */
    char metadata_file[512];
    snprintf(metadata_file, sizeof(metadata_file), "%s/metadata.json", tmpdir);
    unlink(metadata_file);
    rmdir(tmpdir);
}

/* Test checkpoint metadata with invalid arguments */
TEST(checkpoint_metadata_invalid_args) {
    checkpoint_metadata_t metadata;
    memset(&metadata, 0, sizeof(metadata));

    int result = checkpoint_save_metadata(NULL, &metadata);
    ASSERT_NE(result, 0);

    result = checkpoint_save_metadata("/tmp", NULL);
    ASSERT_NE(result, 0);

    result = checkpoint_load_metadata(NULL, &metadata);
    ASSERT_NE(result, 0);

    result = checkpoint_load_metadata("/tmp", NULL);
    ASSERT_NE(result, 0);
}

/* Test checkpoint listing with no checkpoints */
TEST(checkpoint_list_checkpoints_empty) {
    checkpoint_entry_t *head = NULL;

    int count = checkpoint_list_checkpoints("nonexistent-component", 0, &head);
    ASSERT_EQ(count, 0);
    ASSERT_EQ(head, NULL);
}

/* Test checkpoint listing with invalid arguments */
TEST(checkpoint_list_checkpoints_invalid_args) {
    int count = checkpoint_list_checkpoints("test", 0, NULL);
    ASSERT_TRUE(count < 0);
}

/* Test checkpoint removal with nonexistent component */
TEST(checkpoint_remove_nonexistent) {
    int result = checkpoint_remove("nonexistent", "123456789", 0);
    ASSERT_NE(result, 0);
}

/* Test checkpoint removal with invalid arguments */
TEST(checkpoint_remove_invalid_args) {
    int result = checkpoint_remove(NULL, "123", 0);
    ASSERT_NE(result, 0);

    result = checkpoint_remove("test", NULL, 0);
    ASSERT_NE(result, 0);
}

/* Test storage usage calculation */
TEST(checkpoint_storage_usage) {
    checkpoint_quota_t quota;

    int result = checkpoint_storage_usage("nonexistent-component", 0, &quota);
    ASSERT_EQ(result, 0);  /* Should succeed even with no checkpoints */
    ASSERT_EQ(quota.current_count, 0);
    ASSERT_EQ(quota.used_bytes, 0);
    ASSERT_TRUE(quota.quota_bytes > 0);  /* Should have default quota */
    ASSERT_TRUE(quota.max_checkpoints > 0);
}

/* Test storage usage with invalid arguments */
TEST(checkpoint_storage_usage_invalid_args) {
    int result = checkpoint_storage_usage("test", 0, NULL);
    ASSERT_NE(result, 0);
}

/* Test find latest checkpoint with no checkpoints */
TEST(checkpoint_find_latest_none) {
    char latest_id[CHECKPOINT_ID_MAX_LEN];
    char path[MAX_CHECKPOINT_PATH];

    int result = checkpoint_find_latest("nonexistent-component", 0,
                                       latest_id, sizeof(latest_id),
                                       path, sizeof(path));
    ASSERT_NE(result, 0);
}

/* Test find latest checkpoint with invalid arguments */
TEST(checkpoint_find_latest_invalid_args) {
    char latest_id[CHECKPOINT_ID_MAX_LEN];
    char path[MAX_CHECKPOINT_PATH];

    int result = checkpoint_find_latest(NULL, 0, latest_id, sizeof(latest_id), path, sizeof(path));
    ASSERT_NE(result, 0);

    result = checkpoint_find_latest("test", 0, NULL, sizeof(latest_id), path, sizeof(path));
    ASSERT_NE(result, 0);

    result = checkpoint_find_latest("test", 0, latest_id, sizeof(latest_id), NULL, sizeof(path));
    ASSERT_NE(result, 0);
}

/* Test checkpoint cleanup with no checkpoints */
TEST(checkpoint_cleanup_empty) {
    int result = checkpoint_cleanup("nonexistent-component", 5, 24, 0);
    ASSERT_EQ(result, 0);  /* Should succeed with 0 removed */
}

/* Integration test: full checkpoint workflow simulation */
TEST(checkpoint_full_workflow_simulation) {
    /* Skip this test if CRIU is not available */
    if (criu_is_supported() != CHECKPOINT_SUCCESS) {
        printf("SKIP: CRIU not available for full workflow test\n");
        return;
    }

    /* Create test component directory structure */
    char checkpoint_id[CHECKPOINT_ID_MAX_LEN];
    char checkpoint_path[MAX_CHECKPOINT_PATH];

    int result = checkpoint_create_directory("test-workflow", 0, /* temporary */
                                           checkpoint_id, sizeof(checkpoint_id),
                                           checkpoint_path, sizeof(checkpoint_path));
    ASSERT_EQ(result, 0);

    /* Create some mock checkpoint files (CRIU would create these) */
    char test_file[512];
    snprintf(test_file, sizeof(test_file), "%s/core-1.img", checkpoint_path);
    FILE *fp = fopen(test_file, "w");
    ASSERT_TRUE(fp != NULL);
    fprintf(fp, "mock checkpoint data");
    fclose(fp);

    snprintf(test_file, sizeof(test_file), "%s/mm-1.img", checkpoint_path);
    fp = fopen(test_file, "w");
    ASSERT_TRUE(fp != NULL);
    fprintf(fp, "mock memory mapping data");
    fclose(fp);

    snprintf(test_file, sizeof(test_file), "%s/pstree.img", checkpoint_path);
    fp = fopen(test_file, "w");
    ASSERT_TRUE(fp != NULL);
    fprintf(fp, "mock process tree data");
    fclose(fp);

    /* Test checkpoint validation */
    result = checkpoint_validate_image(checkpoint_path);
    ASSERT_EQ(result, CHECKPOINT_SUCCESS);

    /* Create and save metadata */
    checkpoint_metadata_t metadata;
    memset(&metadata, 0, sizeof(metadata));
    strcpy(metadata.component_name, "test-workflow");
    metadata.original_pid = 999;
    metadata.timestamp = time(NULL);
    metadata.image_size = 1024;
    strcpy(metadata.capabilities, "test");

    result = checkpoint_save_metadata(checkpoint_path, &metadata);
    ASSERT_EQ(result, 0);

    /* Test listing checkpoints */
    checkpoint_entry_t *head = NULL;
    int count = checkpoint_list_checkpoints("test-workflow", 0, &head);
    ASSERT_EQ(count, 1);
    ASSERT_TRUE(head != NULL);
    ASSERT_STR_EQ(head->metadata.component_name, "test-workflow");

    checkpoint_free_list(head);

    /* Test storage usage */
    checkpoint_quota_t quota;
    result = checkpoint_storage_usage("test-workflow", 0, &quota);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(quota.current_count, 1);
    ASSERT_TRUE(quota.used_bytes > 0);

    /* Test cleanup */
    result = checkpoint_cleanup("test-workflow", 0, 0, 0); /* Remove all */
    ASSERT_EQ(result, 1); /* Should remove 1 checkpoint */

    /* Verify cleanup worked */
    count = checkpoint_list_checkpoints("test-workflow", 0, &head);
    ASSERT_EQ(count, 0);
    ASSERT_EQ(head, NULL);
}

int main(void) {
    return RUN_ALL_TESTS();
}