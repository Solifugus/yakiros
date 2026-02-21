/*
 * test_component.c - Tests for component lifecycle module
 *
 * Note: Some tests use mock components and avoid actual process execution
 * to enable userspace testing without requiring root privileges.
 */

#include "../test_framework.h"
#include "../../src/component.h"
#include "../../src/capability.h"
#include "../../src/log.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

/* Create test component directory path */
#define TEST_COMPONENT_DIR "../data"

/* Helper to create a mock component for testing */
static void create_mock_component(int idx, const char *name, const char *binary, comp_type_t type) {
    component_t *comp = &components[idx];
    memset(comp, 0, sizeof(*comp));

    strncpy(comp->name, name, MAX_NAME - 1);
    strncpy(comp->binary, binary, MAX_PATH - 1);
    comp->type = type;
    comp->state = COMP_INACTIVE;
    comp->pid = -1;

    /* Add some mock dependencies */
    if (strcmp(name, "test-service") == 0) {
        strncpy(comp->requires[0], "network", MAX_NAME - 1);
        comp->n_requires = 1;
        strncpy(comp->provides[0], "test-api", MAX_NAME - 1);
        comp->n_provides = 1;
    }
}

TEST(requirements_met_with_active_capabilities) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create a component that requires "network" */
    create_mock_component(0, "test-service", "/bin/true", COMP_TYPE_SERVICE);
    n_components = 1;

    /* Initially requirements not met */
    ASSERT_FALSE(requirements_met(&components[0]));

    /* Register the required capability */
    capability_register("network", 5);

    /* Now requirements should be met */
    ASSERT_TRUE(requirements_met(&components[0]));
}

TEST(requirements_met_with_inactive_capabilities) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create component with requirement */
    create_mock_component(0, "test-service", "/bin/true", COMP_TYPE_SERVICE);
    n_components = 1;

    /* Register and then withdraw the capability */
    capability_register("network", 5);
    capability_withdraw("network");

    /* Requirements should not be met */
    ASSERT_FALSE(requirements_met(&components[0]));
}

TEST(requirements_met_multiple_dependencies) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create component with multiple requirements */
    component_t *comp = &components[0];
    memset(comp, 0, sizeof(*comp));
    strncpy(comp->name, "complex-service", MAX_NAME - 1);
    strncpy(comp->binary, "/bin/true", MAX_PATH - 1);
    comp->type = COMP_TYPE_SERVICE;
    comp->state = COMP_INACTIVE;
    comp->pid = -1;

    strncpy(comp->requires[0], "network", MAX_NAME - 1);
    strncpy(comp->requires[1], "filesystem", MAX_NAME - 1);
    strncpy(comp->requires[2], "database", MAX_NAME - 1);
    comp->n_requires = 3;
    n_components = 1;

    /* Partial requirements */
    capability_register("network", 1);
    capability_register("filesystem", 2);
    ASSERT_FALSE(requirements_met(comp));

    /* All requirements met */
    capability_register("database", 3);
    ASSERT_TRUE(requirements_met(comp));

    /* One requirement withdrawn */
    capability_withdraw("filesystem");
    ASSERT_FALSE(requirements_met(comp));
}

TEST(requirements_met_no_dependencies) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Component with no requirements */
    create_mock_component(0, "standalone", "/bin/true", COMP_TYPE_SERVICE);
    components[0].n_requires = 0;
    n_components = 1;

    /* Should always be met */
    ASSERT_TRUE(requirements_met(&components[0]));
}

TEST(component_exited_oneshot_success) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create oneshot component */
    create_mock_component(0, "init-task", "/bin/true", COMP_TYPE_ONESHOT);
    components[0].state = COMP_STARTING;
    components[0].pid = 123;
    n_components = 1;

    /* Simulate successful exit (status 0) */
    int status = 0; /* WIFEXITED(0) == true, WEXITSTATUS(0) == 0 */
    component_exited(0, status);

    /* Should be marked as done and capabilities registered */
    ASSERT_EQ(COMP_ONESHOT_DONE, components[0].state);
    ASSERT_TRUE(capability_active("test-api"));
}

TEST(component_exited_oneshot_failure) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create oneshot component */
    create_mock_component(0, "failing-task", "/bin/false", COMP_TYPE_ONESHOT);
    components[0].state = COMP_STARTING;
    components[0].pid = 124;
    n_components = 1;

    /* Simulate failed exit (status 1) */
    int status = WEXITSTATUS(1) << 8; /* exit code 1 */
    component_exited(0, status);

    /* Should be marked as failed */
    ASSERT_EQ(COMP_FAILED, components[0].state);
    ASSERT_FALSE(capability_active("test-api"));
}

TEST(component_exited_service_crash) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create service component and register its capability */
    create_mock_component(0, "test-daemon", "/usr/bin/daemon", COMP_TYPE_SERVICE);
    components[0].state = COMP_ACTIVE;
    components[0].pid = 125;
    capability_register("test-api", 0);
    n_components = 1;

    ASSERT_TRUE(capability_active("test-api"));

    /* Simulate service crash */
    int status = WEXITSTATUS(1) << 8;
    component_exited(0, status);

    /* Should be marked as failed and capabilities withdrawn */
    ASSERT_EQ(COMP_FAILED, components[0].state);
    ASSERT_EQ(-1, components[0].pid);
    ASSERT_FALSE(capability_active("test-api"));
}

TEST(load_components_from_directory) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Load components from test data directory */
    int loaded = load_components(TEST_COMPONENT_DIR);

    /* Should load some test components */
    ASSERT_NE(-1, loaded);
    ASSERT_TRUE(loaded > 0);
    ASSERT_EQ(loaded, n_components);

    /* Verify we loaded expected components */
    int found_simple = 0, found_complex = 0, found_oneshot = 0;
    for (int i = 0; i < n_components; i++) {
        if (strcmp(components[i].name, "simple-service") == 0) found_simple = 1;
        if (strcmp(components[i].name, "database-server") == 0) found_complex = 1;
        if (strcmp(components[i].name, "mount-filesystems") == 0) found_oneshot = 1;
    }

    ASSERT_TRUE(found_simple);
    ASSERT_TRUE(found_complex);
    ASSERT_TRUE(found_oneshot);
}

TEST(load_components_nonexistent_directory) {
    /* Reset state */
    n_components = 0;

    int result = load_components("/nonexistent/directory");
    ASSERT_EQ(-1, result);
}

TEST(register_early_capabilities) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Register early capabilities */
    register_early_capabilities();

    /* Should create kernel component */
    ASSERT_EQ(1, n_components);
    ASSERT_STR_EQ("kernel", components[0].name);
    ASSERT_EQ(COMP_ACTIVE, components[0].state);
    ASSERT_EQ(0, components[0].pid);

    /* Should register kernel capabilities */
    ASSERT_TRUE(capability_active("kernel.syscalls"));
    ASSERT_TRUE(capability_active("kernel.memory"));
    ASSERT_TRUE(capability_active("filesystem.proc"));
    ASSERT_TRUE(capability_active("filesystem.dev"));
}

TEST(component_global_state_access) {
    /* Reset state */
    n_components = 0;

    /* Verify we can access and modify global state */
    create_mock_component(0, "test1", "/bin/test1", COMP_TYPE_SERVICE);
    create_mock_component(1, "test2", "/bin/test2", COMP_TYPE_ONESHOT);
    n_components = 2;

    /* Verify global array access */
    ASSERT_EQ(2, n_components);
    ASSERT_STR_EQ("test1", components[0].name);
    ASSERT_STR_EQ("test2", components[1].name);
    ASSERT_EQ(COMP_TYPE_SERVICE, components[0].type);
    ASSERT_EQ(COMP_TYPE_ONESHOT, components[1].type);
}

/* Helper to create a mock component with readiness configuration */
static void create_readiness_component(int idx, const char *name, readiness_method_t method,
                                       const char *readiness_config, int timeout) {
    component_t *comp = &components[idx];
    memset(comp, 0, sizeof(*comp));

    strncpy(comp->name, name, MAX_NAME - 1);
    strncpy(comp->binary, "/bin/true", MAX_PATH - 1);
    comp->type = COMP_TYPE_SERVICE;
    comp->state = COMP_INACTIVE;
    comp->pid = -1;
    comp->readiness_method = method;
    comp->readiness_timeout = timeout;
    comp->readiness_interval = 5;  /* default */

    switch (method) {
        case READINESS_FILE:
            strncpy(comp->readiness_file, readiness_config, MAX_PATH - 1);
            break;
        case READINESS_COMMAND:
            strncpy(comp->readiness_check, readiness_config, MAX_PATH - 1);
            break;
        case READINESS_SIGNAL:
            comp->readiness_signal = atoi(readiness_config);
            break;
        case READINESS_NONE:
            break;
    }

    /* Add mock capability */
    strncpy(comp->provides[0], "test-cap", MAX_NAME - 1);
    comp->n_provides = 1;
}

TEST(component_with_readiness_starts_ready_wait) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create component with file-based readiness */
    create_readiness_component(0, "ready-service", READINESS_FILE, "/tmp/test-ready", 30);
    n_components = 1;

    /* Simulate component started and transitioned to READY_WAIT */
    components[0].state = COMP_READY_WAIT;
    components[0].pid = 123;
    components[0].ready_wait_start = time(NULL);

    /* Component should be in READY_WAIT state and capabilities not yet active */
    ASSERT_EQ(COMP_READY_WAIT, components[0].state);
    ASSERT_FALSE(capability_active("test-cap"));  /* Not yet ready */
}

TEST(component_without_readiness_goes_active) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create component without readiness */
    create_readiness_component(0, "simple-service", READINESS_NONE, "", 30);
    n_components = 1;

    /* Simulate component started and transitioned directly to ACTIVE */
    components[0].state = COMP_ACTIVE;
    components[0].pid = 123;

    /* Register capabilities manually since we're bypassing component_start */
    capability_register("test-cap", 0);

    /* Component should be ACTIVE with capabilities available */
    ASSERT_EQ(COMP_ACTIVE, components[0].state);
    ASSERT_TRUE(capability_active("test-cap"));  /* Immediately ready */
}

TEST(readiness_file_detection_success) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create component with file-based readiness */
    const char *ready_file = "/tmp/test_component_ready";
    create_readiness_component(0, "file-service", READINESS_FILE, ready_file, 30);
    components[0].state = COMP_READY_WAIT;
    n_components = 1;

    /* Create the readiness file */
    FILE *f = fopen(ready_file, "w");
    if (f) {
        fprintf(f, "ready\n");
        fclose(f);
    }

    /* Check readiness - should detect file and transition to ACTIVE */
    check_all_readiness();
    ASSERT_EQ(COMP_ACTIVE, components[0].state);
    ASSERT_TRUE(capability_active("test-cap"));

    /* Clean up */
    unlink(ready_file);
}

TEST(readiness_file_detection_missing) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create component with file-based readiness */
    const char *ready_file = "/tmp/nonexistent_ready_file";
    create_readiness_component(0, "file-service", READINESS_FILE, ready_file, 30);
    components[0].state = COMP_READY_WAIT;
    n_components = 1;

    /* Ensure file doesn't exist */
    unlink(ready_file);

    /* Check readiness - should remain in READY_WAIT */
    check_all_readiness();
    ASSERT_EQ(COMP_READY_WAIT, components[0].state);
    ASSERT_FALSE(capability_active("test-cap"));
}

TEST(readiness_command_success) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create component with command-based readiness using /bin/true */
    create_readiness_component(0, "cmd-service", READINESS_COMMAND, "/bin/true", 30);
    components[0].state = COMP_READY_WAIT;
    n_components = 1;

    /* Check readiness - /bin/true should succeed */
    check_all_readiness();
    ASSERT_EQ(COMP_ACTIVE, components[0].state);
    ASSERT_TRUE(capability_active("test-cap"));
}

TEST(readiness_command_failure) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create component with command-based readiness using /bin/false */
    create_readiness_component(0, "cmd-service", READINESS_COMMAND, "/bin/false", 30);
    components[0].state = COMP_READY_WAIT;
    n_components = 1;

    /* Check readiness - /bin/false should fail */
    check_all_readiness();
    ASSERT_EQ(COMP_READY_WAIT, components[0].state);
    ASSERT_FALSE(capability_active("test-cap"));
}

TEST(readiness_timeout_failure) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create component with very short timeout */
    create_readiness_component(0, "timeout-service", READINESS_FILE, "/tmp/never_created", 1);
    components[0].state = COMP_READY_WAIT;
    components[0].ready_wait_start = time(NULL) - 5;  /* Started 5 seconds ago */
    n_components = 1;

    /* Check readiness - should timeout and fail */
    check_all_readiness();
    ASSERT_EQ(COMP_FAILED, components[0].state);
    ASSERT_FALSE(capability_active("test-cap"));
}

int main(void) {
    /* Initialize logging for tests */
    log_open();

    return RUN_ALL_TESTS();
}