/*
 * test_control.c - Tests for control socket interface
 *
 * Tests Unix socket setup, command handling, and client communication.
 * Uses temporary socket paths to avoid conflicts with running system.
 */

#include "../test_framework.h"
#include "../../src/control.h"
#include "../../src/component.h"
#include "../../src/capability.h"
#include "../../src/log.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/* Test socket path in /tmp to avoid system conflicts */
#define TEST_CONTROL_SOCKET "/tmp/yakiros_test_control.sock"

/* Override CONTROL_SOCKET for testing */
#undef CONTROL_SOCKET
#define CONTROL_SOCKET TEST_CONTROL_SOCKET

/* Helper to create a mock component for testing status output */
static void create_status_test_component(int idx, const char *name, comp_state_t state, int pid) {
    component_t *comp = &components[idx];
    memset(comp, 0, sizeof(*comp));

    strncpy(comp->name, name, MAX_NAME - 1);
    strncpy(comp->binary, "/bin/test", MAX_PATH - 1);
    comp->type = COMP_TYPE_SERVICE;
    comp->state = state;
    comp->pid = pid;
}

/* Helper to connect to the control socket */
static int connect_to_control_socket(void) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, TEST_CONTROL_SOCKET, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

/* Helper to send command and read response */
static int send_command_get_response(const char *command, char *response, size_t response_size) {
    int sock = connect_to_control_socket();
    if (sock < 0) return -1;

    /* Send command */
    ssize_t sent = write(sock, command, strlen(command));
    if (sent != (ssize_t)strlen(command)) {
        close(sock);
        return -1;
    }

    /* Read response */
    ssize_t received = read(sock, response, response_size - 1);
    close(sock);

    if (received < 0) return -1;
    response[received] = '\0';
    return (int)received;
}

TEST(setup_control_socket_success) {
    /* Clean up any existing socket */
    unlink(TEST_CONTROL_SOCKET);

    /* Set up control socket */
    int control_fd = setup_control_socket();
    ASSERT_NE(-1, control_fd);

    /* Socket file should exist */
    ASSERT_EQ(0, access(TEST_CONTROL_SOCKET, F_OK));

    /* Clean up */
    close(control_fd);
    unlink(TEST_CONTROL_SOCKET);
}

TEST(setup_control_socket_creates_listening_socket) {
    unlink(TEST_CONTROL_SOCKET);

    int control_fd = setup_control_socket();
    ASSERT_NE(-1, control_fd);

    /* Should be able to connect to it */
    int client_sock = connect_to_control_socket();
    ASSERT_NE(-1, client_sock);

    close(client_sock);
    close(control_fd);
    unlink(TEST_CONTROL_SOCKET);
}

TEST(handle_control_command_status_empty_system) {
    /* Reset component state */
    n_components = 0;
    capability_init();

    unlink(TEST_CONTROL_SOCKET);
    int control_fd = setup_control_socket();
    ASSERT_NE(-1, control_fd);

    /* Send status command and get response */
    char response[1024];
    int response_len = send_command_get_response("status", response, sizeof(response));
    ASSERT_TRUE(response_len > 0);

    /* Should contain status header */
    ASSERT_TRUE(strstr(response, "YakirOS Status:") != NULL);

    close(control_fd);
    unlink(TEST_CONTROL_SOCKET);
}

TEST(handle_control_command_status_with_components) {
    /* Reset state and create test components */
    n_components = 0;
    capability_init();

    create_status_test_component(0, "test-service", COMP_ACTIVE, 1234);
    create_status_test_component(1, "init-task", COMP_ONESHOT_DONE, 0);
    create_status_test_component(2, "failed-service", COMP_FAILED, -1);
    create_status_test_component(3, "starting-service", COMP_STARTING, 5678);
    n_components = 4;

    unlink(TEST_CONTROL_SOCKET);
    int control_fd = setup_control_socket();
    ASSERT_NE(-1, control_fd);

    /* Send status command */
    char response[2048];
    int response_len = send_command_get_response("status", response, sizeof(response));
    ASSERT_TRUE(response_len > 0);

    /* Check response contains component information */
    ASSERT_TRUE(strstr(response, "YakirOS Status:") != NULL);
    ASSERT_TRUE(strstr(response, "test-service") != NULL);
    ASSERT_TRUE(strstr(response, "ACTIVE") != NULL);
    ASSERT_TRUE(strstr(response, "1234") != NULL);
    ASSERT_TRUE(strstr(response, "init-task") != NULL);
    ASSERT_TRUE(strstr(response, "DONE") != NULL);
    ASSERT_TRUE(strstr(response, "failed-service") != NULL);
    ASSERT_TRUE(strstr(response, "FAILED") != NULL);
    ASSERT_TRUE(strstr(response, "starting-service") != NULL);
    ASSERT_TRUE(strstr(response, "STARTING") != NULL);
    ASSERT_TRUE(strstr(response, "5678") != NULL);

    close(control_fd);
    unlink(TEST_CONTROL_SOCKET);
}

TEST(handle_control_command_unknown_command) {
    n_components = 0;

    unlink(TEST_CONTROL_SOCKET);
    int control_fd = setup_control_socket();
    ASSERT_NE(-1, control_fd);

    /* Send unknown command */
    char response[1024];
    int response_len = send_command_get_response("unknown_cmd", response, sizeof(response));
    ASSERT_TRUE(response_len > 0);

    /* Should indicate unknown command */
    ASSERT_TRUE(strstr(response, "Unknown command") != NULL);
    ASSERT_TRUE(strstr(response, "unknown_cmd") != NULL);

    close(control_fd);
    unlink(TEST_CONTROL_SOCKET);
}

TEST(handle_control_command_whitespace_handling) {
    n_components = 0;

    unlink(TEST_CONTROL_SOCKET);
    int control_fd = setup_control_socket();
    ASSERT_NE(-1, control_fd);

    /* Send command with whitespace */
    char response[1024];
    int response_len = send_command_get_response("  status  \n", response, sizeof(response));
    ASSERT_TRUE(response_len > 0);

    /* Should still process as status command */
    ASSERT_TRUE(strstr(response, "YakirOS Status:") != NULL);

    close(control_fd);
    unlink(TEST_CONTROL_SOCKET);
}

TEST(control_socket_multiple_clients) {
    n_components = 0;

    unlink(TEST_CONTROL_SOCKET);
    int control_fd = setup_control_socket();
    ASSERT_NE(-1, control_fd);

    /* Multiple clients should be able to connect and get responses */
    for (int i = 0; i < 3; i++) {
        char response[1024];
        int response_len = send_command_get_response("status", response, sizeof(response));
        ASSERT_TRUE(response_len > 0);
        ASSERT_TRUE(strstr(response, "YakirOS Status:") != NULL);
    }

    close(control_fd);
    unlink(TEST_CONTROL_SOCKET);
}

TEST(control_socket_cleanup_on_close) {
    unlink(TEST_CONTROL_SOCKET);

    int control_fd = setup_control_socket();
    ASSERT_NE(-1, control_fd);
    ASSERT_EQ(0, access(TEST_CONTROL_SOCKET, F_OK));

    /* Close socket */
    close(control_fd);

    /* Socket file should still exist (Unix socket behavior) */
    ASSERT_EQ(0, access(TEST_CONTROL_SOCKET, F_OK));

    /* But new setup should work (unlink first) */
    unlink(TEST_CONTROL_SOCKET);
    int control_fd2 = setup_control_socket();
    ASSERT_NE(-1, control_fd2);

    close(control_fd2);
    unlink(TEST_CONTROL_SOCKET);
}

int main(void) {
    /* Initialize logging for tests */
    log_open();

    /* Make sure we clean up any leftover test socket */
    unlink(TEST_CONTROL_SOCKET);

    int result = RUN_ALL_TESTS();

    /* Final cleanup */
    unlink(TEST_CONTROL_SOCKET);

    return result;
}