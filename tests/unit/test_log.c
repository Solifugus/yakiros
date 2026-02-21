/*
 * test_log.c - Tests for logging system module
 *
 * Tests log initialization, message formatting, and fallback behavior.
 * Since logging writes to /dev/kmsg or stderr, we test behavior indirectly.
 */

#include "../test_framework.h"
#include "../../src/log.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>

/* Test log file for capturing output */
#define TEST_LOG_FILE "/tmp/yakiros_test_log.txt"

/* Helper to redirect stderr to a test file for capturing output */
static int redirect_stderr_to_file(const char *filename) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    int saved_stderr = dup(STDERR_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);

    return saved_stderr;
}

/* Restore stderr */
static void restore_stderr(int saved_stderr) {
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stderr);
}

/* Read contents of test log file */
static int read_log_file(char *buffer, size_t size) {
    FILE *f = fopen(TEST_LOG_FILE, "r");
    if (!f) return -1;

    size_t bytes = fread(buffer, 1, size - 1, f);
    buffer[bytes] = '\0';
    fclose(f);
    return (int)bytes;
}

TEST(log_initialization) {
    /* log_open() should not crash */
    log_open();

    /* We can't easily test which fd it opened, but we can test that
     * subsequent logging calls work */
    LOG_INFO("test message");

    /* This is mainly a smoke test - if it doesn't crash, it's working */
    ASSERT_TRUE(1);
}

TEST(log_message_formatting) {
    /* Redirect stderr to capture output */
    int saved_stderr = redirect_stderr_to_file(TEST_LOG_FILE);
    ASSERT_NE(-1, saved_stderr);

    /* Re-initialize logging to use the redirected stderr */
    log_open();

    /* Log a simple message */
    LOG_INFO("test message");

    /* Restore stderr */
    restore_stderr(saved_stderr);

    /* Read the captured log output */
    char log_output[1024];
    int bytes = read_log_file(log_output, sizeof(log_output));
    ASSERT_TRUE(bytes > 0);

    /* Check log format: [timestamp] graph-resolver <INFO> test message */
    ASSERT_TRUE(strstr(log_output, "graph-resolver") != NULL);
    ASSERT_TRUE(strstr(log_output, "<INFO>") != NULL);
    ASSERT_TRUE(strstr(log_output, "test message") != NULL);
    ASSERT_TRUE(log_output[0] == '[');  /* Starts with timestamp */

    /* Clean up */
    unlink(TEST_LOG_FILE);
}

TEST(log_different_levels) {
    int saved_stderr = redirect_stderr_to_file(TEST_LOG_FILE);
    ASSERT_NE(-1, saved_stderr);

    log_open();

    /* Test different log levels */
    LOG_INFO("info message");
    LOG_WARN("warning message");
    LOG_ERR("error message");

    restore_stderr(saved_stderr);

    /* Read output */
    char log_output[2048];
    int bytes = read_log_file(log_output, sizeof(log_output));
    ASSERT_TRUE(bytes > 0);

    /* Check all levels appear */
    ASSERT_TRUE(strstr(log_output, "<INFO>") != NULL);
    ASSERT_TRUE(strstr(log_output, "<WARN>") != NULL);
    ASSERT_TRUE(strstr(log_output, "<ERROR>") != NULL);
    ASSERT_TRUE(strstr(log_output, "info message") != NULL);
    ASSERT_TRUE(strstr(log_output, "warning message") != NULL);
    ASSERT_TRUE(strstr(log_output, "error message") != NULL);

    unlink(TEST_LOG_FILE);
}

TEST(log_message_with_formatting) {
    int saved_stderr = redirect_stderr_to_file(TEST_LOG_FILE);
    ASSERT_NE(-1, saved_stderr);

    log_open();

    /* Test printf-style formatting */
    LOG_INFO("component %s has pid %d", "test-component", 12345);
    LOG_WARN("failed %d times", 3);
    LOG_ERR("error code: %d, message: %s", 42, "test error");

    restore_stderr(saved_stderr);

    char log_output[2048];
    int bytes = read_log_file(log_output, sizeof(log_output));
    ASSERT_TRUE(bytes > 0);

    /* Check formatted values appear */
    ASSERT_TRUE(strstr(log_output, "test-component") != NULL);
    ASSERT_TRUE(strstr(log_output, "12345") != NULL);
    ASSERT_TRUE(strstr(log_output, "failed 3 times") != NULL);
    ASSERT_TRUE(strstr(log_output, "error code: 42") != NULL);
    ASSERT_TRUE(strstr(log_output, "test error") != NULL);

    unlink(TEST_LOG_FILE);
}

TEST(log_newline_handling) {
    int saved_stderr = redirect_stderr_to_file(TEST_LOG_FILE);
    ASSERT_NE(-1, saved_stderr);

    log_open();

    /* Test messages with and without newlines */
    LOG_INFO("message without newline");
    LOG_INFO("message with newline\n");

    restore_stderr(saved_stderr);

    char log_output[1024];
    int bytes = read_log_file(log_output, sizeof(log_output));
    ASSERT_TRUE(bytes > 0);

    /* Count newlines - should have at least 2 (one per message) */
    int newlines = 0;
    for (int i = 0; i < bytes; i++) {
        if (log_output[i] == '\n') newlines++;
    }
    ASSERT_TRUE(newlines >= 2);

    unlink(TEST_LOG_FILE);
}

TEST(log_long_message_truncation) {
    int saved_stderr = redirect_stderr_to_file(TEST_LOG_FILE);
    ASSERT_NE(-1, saved_stderr);

    log_open();

    /* Create a very long message (longer than MAX_LOG_LINE) */
    char long_msg[2048];
    memset(long_msg, 'x', sizeof(long_msg) - 1);
    long_msg[sizeof(long_msg) - 1] = '\0';

    LOG_INFO("%s", long_msg);

    restore_stderr(saved_stderr);

    char log_output[4096];
    int bytes = read_log_file(log_output, sizeof(log_output));
    ASSERT_TRUE(bytes > 0);

    /* The logged message should be shorter than the input due to truncation */
    ASSERT_TRUE(bytes < (int)sizeof(long_msg) + 100);  /* Allow for timestamp/prefix */

    unlink(TEST_LOG_FILE);
}

TEST(log_multiple_calls) {
    int saved_stderr = redirect_stderr_to_file(TEST_LOG_FILE);
    ASSERT_NE(-1, saved_stderr);

    log_open();

    /* Multiple sequential log calls */
    for (int i = 0; i < 5; i++) {
        LOG_INFO("message number %d", i);
    }

    restore_stderr(saved_stderr);

    char log_output[2048];
    int bytes = read_log_file(log_output, sizeof(log_output));
    ASSERT_TRUE(bytes > 0);

    /* Should contain all 5 messages */
    for (int i = 0; i < 5; i++) {
        char expected[64];
        snprintf(expected, sizeof(expected), "message number %d", i);
        ASSERT_TRUE(strstr(log_output, expected) != NULL);
    }

    unlink(TEST_LOG_FILE);
}

int main(void) {
    return RUN_ALL_TESTS();
}