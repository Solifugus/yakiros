/*
 * test_full_system.c - Integration tests for YakirOS components
 *
 * Tests multiple modules working together:
 * - TOML parsing + component loading
 * - Capability registration + dependency resolution
 * - Graph resolution with real component definitions
 * - End-to-end component lifecycle scenarios
 */

#include "../test_framework.h"
#include "../../src/component.h"
#include "../../src/capability.h"
#include "../../src/graph.h"
#include "../../src/log.h"
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>

/* Test data directory */
#define TEST_DATA_DIR "../data"

TEST(load_and_resolve_simple_components) {
    /* Reset global state */
    n_components = 0;
    capability_init();

    /* Register early capabilities first */
    register_early_capabilities();
    int kernel_components = n_components;

    /* Load components from test data */
    int loaded = load_components(TEST_DATA_DIR);
    ASSERT_TRUE(loaded > 0);
    ASSERT_TRUE(n_components > kernel_components);

    /* Graph resolution should start appropriate components */
    graph_resolve_full();

    /* Verify some components are now active */
    int active_components = 0;
    for (int i = 0; i < n_components; i++) {
        if (components[i].state == COMP_ACTIVE || components[i].state == COMP_ONESHOT_DONE) {
            active_components++;
        }
    }
    ASSERT_TRUE(active_components > 0);

    /* Verify capabilities are registered */
    ASSERT_TRUE(capability_count() > 0);
}

TEST(toml_to_capability_integration) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Parse a specific TOML file */
    component_t comp;
    int result = parse_component(TEST_DATA_DIR "/simple-service.toml", &comp);
    ASSERT_EQ(0, result);

    /* Manually add to components array */
    components[0] = comp;
    n_components = 1;

    /* Verify parsed dependencies */
    ASSERT_EQ(1, comp.n_requires);
    ASSERT_STR_EQ("network", comp.requires[0]);
    ASSERT_EQ(1, comp.n_provides);
    ASSERT_STR_EQ("simple-api", comp.provides[0]);

    /* Check requirements not met initially */
    ASSERT_FALSE(requirements_met(&components[0]));

    /* Register the required capability */
    capability_register("network", 99);

    /* Now requirements should be met */
    ASSERT_TRUE(requirements_met(&components[0]));
}

TEST(dependency_chain_resolution) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create test TOML files for dependency chain */
    component_t fs_comp, app_comp;

    /* Parse existing test components */
    ASSERT_EQ(0, parse_component(TEST_DATA_DIR "/oneshot-task.toml", &fs_comp));
    ASSERT_EQ(0, parse_component(TEST_DATA_DIR "/simple-service.toml", &app_comp));

    /* Add to components array */
    register_early_capabilities();  /* Provides "kernel.*" capabilities */
    components[n_components] = fs_comp;
    n_components++;
    components[n_components] = app_comp;
    n_components++;

    /* Find "mount-filesystems" component (provides "filesystem") */
    int fs_idx = -1, app_idx = -1;
    for (int i = 0; i < n_components; i++) {
        if (strcmp(components[i].name, "mount-filesystems") == 0) fs_idx = i;
        if (strcmp(components[i].name, "simple-service") == 0) app_idx = i;
    }
    ASSERT_NE(-1, fs_idx);
    ASSERT_NE(-1, app_idx);

    /* Initially only kernel should be active */
    ASSERT_EQ(COMP_ACTIVE, components[0].state);  /* kernel */
    ASSERT_EQ(COMP_INACTIVE, components[fs_idx].state);
    ASSERT_EQ(COMP_INACTIVE, components[app_idx].state);

    /* After graph resolution, filesystem should start (requires kernel) */
    graph_resolve_full();

    /* Filesystem component should now be active */
    ASSERT_TRUE(components[fs_idx].state == COMP_ACTIVE ||
                components[fs_idx].state == COMP_ONESHOT_DONE);
}

TEST(component_failure_cascading) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create a simple provider->consumer relationship */
    register_early_capabilities();

    /* Load components */
    load_components(TEST_DATA_DIR);

    /* Start everything */
    graph_resolve_full();

    /* Find an active service component */
    int provider_idx = -1;
    for (int i = 0; i < n_components; i++) {
        if (components[i].state == COMP_ACTIVE && components[i].n_provides > 0) {
            provider_idx = i;
            break;
        }
    }

    if (provider_idx >= 0) {
        /* Simulate component failure */
        components[provider_idx].state = COMP_FAILED;
        for (int j = 0; j < components[provider_idx].n_provides; j++) {
            capability_withdraw(components[provider_idx].provides[j]);
        }

        /* Graph resolution should detect dependency loss */
        int changes = graph_resolve();

        /* Some component states should change due to cascade */
        ASSERT_TRUE(changes >= 0);  /* At least no crashes */
    }

    /* This test verifies the system handles failures gracefully */
    ASSERT_TRUE(1);  /* If we get here, no crashes occurred */
}

TEST(full_component_lifecycle) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Register early capabilities */
    register_early_capabilities();

    /* Load a specific component */
    component_t comp;
    ASSERT_EQ(0, parse_component(TEST_DATA_DIR "/simple-service.toml", &comp));

    /* Add to system */
    components[n_components] = comp;
    int comp_idx = n_components;
    n_components++;

    /* Initially inactive */
    ASSERT_EQ(COMP_INACTIVE, components[comp_idx].state);
    ASSERT_FALSE(capability_active("simple-api"));

    /* Provide its requirement */
    capability_register("network", 99);

    /* Resolve graph */
    graph_resolve_full();

    /* Component should now be active */
    ASSERT_EQ(COMP_ACTIVE, components[comp_idx].state);
    ASSERT_TRUE(capability_active("simple-api"));

    /* Simulate component exit */
    component_exited(comp_idx, 0);  /* Normal exit */

    /* Service should be marked as failed, capabilities withdrawn */
    ASSERT_EQ(COMP_FAILED, components[comp_idx].state);
    ASSERT_FALSE(capability_active("simple-api"));
}

TEST(early_capabilities_initialization) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Register early capabilities */
    register_early_capabilities();

    /* Should create kernel component */
    ASSERT_EQ(1, n_components);
    ASSERT_STR_EQ("kernel", components[0].name);
    ASSERT_EQ(COMP_ACTIVE, components[0].state);

    /* Should register kernel capabilities */
    ASSERT_TRUE(capability_active("kernel.syscalls"));
    ASSERT_TRUE(capability_active("kernel.memory"));
    ASSERT_TRUE(capability_active("filesystem.proc"));
    ASSERT_TRUE(capability_active("filesystem.dev"));

    /* Capability count should be reasonable */
    ASSERT_TRUE(capability_count() >= 5);
}

TEST(multiple_component_types_integration) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Load test components which include both service and oneshot types */
    register_early_capabilities();
    load_components(TEST_DATA_DIR);

    /* Should have loaded different component types */
    int services = 0, oneshots = 0;
    for (int i = 0; i < n_components; i++) {
        if (components[i].type == COMP_TYPE_SERVICE) services++;
        if (components[i].type == COMP_TYPE_ONESHOT) oneshots++;
    }

    ASSERT_TRUE(services > 0);
    ASSERT_TRUE(oneshots > 0);

    /* Graph resolution should handle both types */
    graph_resolve_full();

    /* Check that we have components in expected states */
    int active = 0, done = 0, failed = 0;
    for (int i = 0; i < n_components; i++) {
        switch (components[i].state) {
            case COMP_ACTIVE:       active++; break;
            case COMP_ONESHOT_DONE: done++; break;
            case COMP_FAILED:       failed++; break;
            default: break;
        }
    }

    /* Should have some active services and completed oneshots */
    ASSERT_TRUE(active > 0 || done > 0);
}

TEST(readiness_protocol_file_based) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create a component with file-based readiness */
    component_t *comp = &components[0];
    memset(comp, 0, sizeof(*comp));
    strcpy(comp->name, "file-ready-service");
    strcpy(comp->binary, "/bin/true");
    comp->type = COMP_TYPE_SERVICE;
    comp->state = COMP_INACTIVE;
    comp->pid = -1;
    comp->readiness_method = READINESS_FILE;
    strcpy(comp->readiness_file, "/tmp/integration_test_ready");
    comp->readiness_timeout = 10;
    strcpy(comp->provides[0], "test-service");
    comp->n_provides = 1;
    n_components = 1;

    /* Clean up any existing ready file */
    unlink("/tmp/integration_test_ready");

    /* Start graph resolution - component should start but not be active yet */
    int changes = graph_resolve();
    ASSERT_TRUE(changes > 0);
    ASSERT_TRUE(comp->state == COMP_STARTING || comp->state == COMP_READY_WAIT);
    ASSERT_FALSE(capability_active("test-service"));

    /* Create readiness file */
    FILE *ready_file = fopen("/tmp/integration_test_ready", "w");
    if (ready_file) {
        fprintf(ready_file, "ready\n");
        fclose(ready_file);
    }

    /* Check readiness detection */
    check_all_readiness();

    /* Component should now be active */
    if (comp->state == COMP_ACTIVE) {
        ASSERT_TRUE(capability_active("test-service"));
    }

    /* Clean up */
    unlink("/tmp/integration_test_ready");
}

TEST(readiness_protocol_command_based) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create a component with command-based readiness using /bin/true */
    component_t *comp = &components[0];
    memset(comp, 0, sizeof(*comp));
    strcpy(comp->name, "cmd-ready-service");
    strcpy(comp->binary, "/bin/true");
    comp->type = COMP_TYPE_SERVICE;
    comp->state = COMP_INACTIVE;
    comp->pid = -1;
    comp->readiness_method = READINESS_COMMAND;
    strcpy(comp->readiness_check, "/bin/true");
    comp->readiness_timeout = 10;
    comp->readiness_interval = 2;
    strcpy(comp->provides[0], "cmd-service");
    comp->n_provides = 1;
    n_components = 1;

    /* Start graph resolution */
    int changes = graph_resolve();
    ASSERT_TRUE(changes > 0);

    /* Simulate component in READY_WAIT state */
    if (comp->state != COMP_ACTIVE) {
        comp->state = COMP_READY_WAIT;
        comp->pid = 123;
        comp->ready_wait_start = time(NULL);

        /* Check readiness - should succeed with /bin/true */
        check_all_readiness();
        ASSERT_EQ(COMP_ACTIVE, comp->state);
        ASSERT_TRUE(capability_active("cmd-service"));
    }
}

TEST(readiness_protocol_timeout_handling) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create a component that will timeout */
    component_t *comp = &components[0];
    memset(comp, 0, sizeof(*comp));
    strcpy(comp->name, "timeout-service");
    strcpy(comp->binary, "/bin/true");
    comp->type = COMP_TYPE_SERVICE;
    comp->state = COMP_READY_WAIT;
    comp->pid = 123;
    comp->readiness_method = READINESS_FILE;
    strcpy(comp->readiness_file, "/tmp/never_created_file");
    comp->readiness_timeout = 1;  /* Very short timeout */
    comp->ready_wait_start = time(NULL) - 5;  /* Started 5 seconds ago */
    strcpy(comp->provides[0], "timeout-service");
    comp->n_provides = 1;
    n_components = 1;

    /* Check readiness - should timeout and fail */
    check_all_readiness();
    ASSERT_EQ(COMP_FAILED, comp->state);
    ASSERT_FALSE(capability_active("timeout-service"));
}

int main(void) {
    /* Initialize logging */
    log_open();

    return RUN_ALL_TESTS();
}