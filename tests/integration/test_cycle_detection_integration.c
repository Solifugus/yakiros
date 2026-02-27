/*
 * test_cycle_detection_integration.c - Integration tests for cycle detection with real TOML files
 */

#include "../test_framework.h"
#include "../../src/component.h"
#include "../../src/capability.h"
#include "../../src/graph.h"
#include "../../src/log.h"
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>

TEST(integration_simple_cycle_detection) {
    /* Test loading actual TOML files with cycles */
    if (system("mkdir -p /tmp/test-cycle-graph.d") != 0) {
        printf("WARN: failed to create test directory\n");
    }

    /* Copy cycle test files */
    if (system("cp tests/data/cycles/cycle-a.toml /tmp/test-cycle-graph.d/") != 0 ||
        system("cp tests/data/cycles/cycle-b.toml /tmp/test-cycle-graph.d/") != 0) {
        printf("WARN: failed to copy test files - skipping integration test\n");
        return; /* Skip test if files not available */
    }

    /* Reset state */
    n_components = 0;
    capability_init();

    /* Load components */
    int loaded = load_components("/tmp/test-cycle-graph.d");
    ASSERT_TRUE(loaded >= 2);

    /* Test cycle detection */
    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    ASSERT_EQ(1, result);  /* Cycle detected */
    ASSERT_TRUE(cycle_info.cycle_length > 0);
    ASSERT_TRUE(strlen(cycle_info.error_message) > 0);
    ASSERT_TRUE(strstr(cycle_info.error_message, "cycle-a") != NULL ||
                strstr(cycle_info.error_message, "cycle-b") != NULL);

    free(cycle_info.cycle_components);

    /* Clean up */
    if (system("rm -rf /tmp/test-cycle-graph.d") != 0) {
        printf("WARN: failed to clean up test directory\n");
    }
}

TEST(integration_complex_cycle_detection) {
    /* Test loading complex 4-component cycle */
    if (system("mkdir -p /tmp/test-complex-cycle.d") != 0) {
        printf("WARN: failed to create test directory\n");
    }

    /* Copy complex cycle test files */
    if (system("cp tests/data/cycles/complex-cycle-*.toml /tmp/test-complex-cycle.d/") != 0) {
        printf("WARN: failed to copy test files - skipping integration test\n");
        return; /* Skip test if files not available */
    }

    /* Reset state */
    n_components = 0;
    capability_init();

    /* Load components */
    int loaded = load_components("/tmp/test-complex-cycle.d");
    ASSERT_TRUE(loaded >= 4);

    /* Test cycle detection */
    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    ASSERT_EQ(1, result);  /* Cycle detected */
    ASSERT_EQ(5, cycle_info.cycle_length);  /* 4 components + closing component */
    ASSERT_TRUE(strlen(cycle_info.error_message) > 0);

    free(cycle_info.cycle_components);

    /* Clean up */
    if (system("rm -rf /tmp/test-complex-cycle.d") != 0) {
        printf("WARN: failed to clean up test directory\n");
    }
}

TEST(integration_self_dependency_detection) {
    /* Test component that depends on itself */
    if (system("mkdir -p /tmp/test-self-dep.d") != 0) {
        printf("WARN: failed to create test directory\n");
    }

    /* Copy self-dependency test file */
    if (system("cp tests/data/cycles/self-dependency.toml /tmp/test-self-dep.d/") != 0) {
        printf("WARN: failed to copy test files - skipping integration test\n");
        return;
    }

    /* Reset state */
    n_components = 0;
    capability_init();

    /* Load components */
    int loaded = load_components("/tmp/test-self-dep.d");
    ASSERT_TRUE(loaded >= 1);

    /* Test cycle detection */
    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    ASSERT_EQ(1, result);  /* Cycle detected */
    ASSERT_TRUE(strstr(cycle_info.error_message, "self-dependency") != NULL);

    free(cycle_info.cycle_components);

    /* Clean up */
    if (system("rm -rf /tmp/test-self-dep.d") != 0) {
        printf("WARN: failed to clean up test directory\n");
    }
}

TEST(integration_validation_with_cycles) {
    /* Test component graph validation with actual files */
    if (system("mkdir -p /tmp/test-validation-cycle.d") != 0) {
        printf("WARN: failed to create test directory\n");
    }

    /* Copy cycle test files */
    if (system("cp tests/data/cycles/cycle-a.toml /tmp/test-validation-cycle.d/") != 0 ||
        system("cp tests/data/cycles/cycle-b.toml /tmp/test-validation-cycle.d/") != 0) {
        printf("WARN: failed to copy test files - skipping integration test\n");
        return;
    }

    /* Reset state */
    n_components = 0;
    capability_init();

    /* Load components */
    load_components("/tmp/test-validation-cycle.d");

    /* Test validation with warn_only = 0 (should fail) */
    int result = validate_component_graph(0);
    ASSERT_EQ(-1, result);

    /* Test validation with warn_only = 1 (should warn but continue) */
    result = validate_component_graph(1);
    ASSERT_EQ(0, result);

    /* Clean up */
    if (system("rm -rf /tmp/test-validation-cycle.d") != 0) {
        printf("WARN: failed to clean up test directory\n");
    }
}

TEST(integration_no_cycles_validation) {
    /* Test with components that have no cycles */
    if (system("mkdir -p /tmp/test-no-cycles.d") != 0) {
        printf("WARN: failed to create test directory\n");
    }

    /* Create valid linear dependency chain */
    FILE *f1 = fopen("/tmp/test-no-cycles.d/service-a.toml", "w");
    if (f1) {
        fprintf(f1, "[component]\n");
        fprintf(f1, "name = \"service-a\"\n");
        fprintf(f1, "binary = \"/bin/sleep\"\n");
        fprintf(f1, "args = [\"300\"]\n");
        fprintf(f1, "type = \"service\"\n");
        fprintf(f1, "\n[provides]\n");
        fprintf(f1, "capabilities = [\"test.service-a\"]\n");
        fclose(f1);
    }

    FILE *f2 = fopen("/tmp/test-no-cycles.d/service-b.toml", "w");
    if (f2) {
        fprintf(f2, "[component]\n");
        fprintf(f2, "name = \"service-b\"\n");
        fprintf(f2, "binary = \"/bin/sleep\"\n");
        fprintf(f2, "args = [\"300\"]\n");
        fprintf(f2, "type = \"service\"\n");
        fprintf(f2, "\n[provides]\n");
        fprintf(f2, "capabilities = [\"test.service-b\"]\n");
        fprintf(f2, "\n[requires]\n");
        fprintf(f2, "capabilities = [\"test.service-a\"]\n");
        fclose(f2);
    }

    /* Reset state */
    n_components = 0;
    capability_init();

    /* Load components */
    int loaded = load_components("/tmp/test-no-cycles.d");
    ASSERT_TRUE(loaded >= 2);

    /* Test cycle detection - should find no cycles */
    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    ASSERT_EQ(0, result);  /* No cycles */
    ASSERT_EQ(0, cycle_info.cycle_length);

    /* Test validation - should pass */
    result = validate_component_graph(0);
    ASSERT_EQ(0, result);

    /* Clean up */
    if (system("rm -rf /tmp/test-no-cycles.d") != 0) {
        printf("WARN: failed to clean up test directory\n");
    }
}

int main(void) {
    /* Initialize logging for tests */
    log_open();

    return RUN_ALL_TESTS();
}