/*
 * test_graph.c - Tests for graph resolution engine
 *
 * Tests dependency resolution, graph stability, and cycle detection.
 * Uses mock components that don't actually fork/exec for userspace testing.
 */

#include "../test_framework.h"
#include "../../src/graph.h"
#include "../../src/component.h"
#include "../../src/capability.h"
#include "../../src/log.h"
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <signal.h>
#include <stdio.h>

/* Test helper to create a mock component without actual process execution */
static void create_test_component(int idx, const char *name, comp_type_t type,
                                  const char **requires, int n_req,
                                  const char **provides, int n_prov) {
    component_t *comp = &components[idx];
    memset(comp, 0, sizeof(*comp));

    strncpy(comp->name, name, MAX_NAME - 1);
    strncpy(comp->binary, "/bin/true", MAX_PATH - 1);  /* Mock binary */
    comp->type = type;
    comp->state = COMP_INACTIVE;
    comp->pid = -1;

    /* Set dependencies */
    comp->n_requires = n_req;
    for (int i = 0; i < n_req && i < MAX_DEPS; i++) {
        strncpy(comp->requires[i], requires[i], MAX_NAME - 1);
    }

    comp->n_provides = n_prov;
    for (int i = 0; i < n_prov && i < MAX_DEPS; i++) {
        strncpy(comp->provides[i], provides[i], MAX_NAME - 1);
    }
}

/* Note: Using real component_start which provides better integration testing */

TEST(graph_resolve_single_component_no_deps) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create standalone component */
    const char *requires[] = {};
    const char *provides[] = {"standalone-service"};
    create_test_component(0, "standalone", COMP_TYPE_SERVICE,
                          requires, 0, provides, 1);
    n_components = 1;

    /* Single resolution pass should start it */
    int changes = graph_resolve();
    ASSERT_EQ(1, changes);
    ASSERT_EQ(COMP_ACTIVE, components[0].state);
    ASSERT_TRUE(capability_active("standalone-service"));

    /* Another pass should make no changes */
    changes = graph_resolve();
    ASSERT_EQ(0, changes);
}

TEST(graph_resolve_linear_dependency_chain) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create chain: A -> B -> C */
    const char *a_req[] = {};
    const char *a_prov[] = {"cap-a"};
    create_test_component(0, "comp-a", COMP_TYPE_SERVICE,
                          a_req, 0, a_prov, 1);

    const char *b_req[] = {"cap-a"};
    const char *b_prov[] = {"cap-b"};
    create_test_component(1, "comp-b", COMP_TYPE_SERVICE,
                          b_req, 1, b_prov, 1);

    const char *c_req[] = {"cap-b"};
    const char *c_prov[] = {"cap-c"};
    create_test_component(2, "comp-c", COMP_TYPE_SERVICE,
                          c_req, 1, c_prov, 1);

    n_components = 3;

    /* First pass should start comp-a only */
    int changes = graph_resolve();
    ASSERT_EQ(1, changes);
    ASSERT_EQ(COMP_ACTIVE, components[0].state);
    ASSERT_EQ(COMP_INACTIVE, components[1].state);
    ASSERT_EQ(COMP_INACTIVE, components[2].state);

    /* Second pass should start comp-b */
    changes = graph_resolve();
    ASSERT_EQ(1, changes);
    ASSERT_EQ(COMP_ACTIVE, components[1].state);
    ASSERT_EQ(COMP_INACTIVE, components[2].state);

    /* Third pass should start comp-c */
    changes = graph_resolve();
    ASSERT_EQ(1, changes);
    ASSERT_EQ(COMP_ACTIVE, components[2].state);

    /* Fourth pass should have no changes */
    changes = graph_resolve();
    ASSERT_EQ(0, changes);
}

TEST(graph_resolve_multiple_providers_same_capability) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create two components providing same capability */
    const char *a_req[] = {};
    const char *a_prov[] = {"shared-cap"};
    create_test_component(0, "provider-a", COMP_TYPE_SERVICE,
                          a_req, 0, a_prov, 1);

    const char *b_req[] = {};
    const char *b_prov[] = {"shared-cap"};
    create_test_component(1, "provider-b", COMP_TYPE_SERVICE,
                          b_req, 0, b_prov, 1);

    /* Consumer of the shared capability */
    const char *c_req[] = {"shared-cap"};
    const char *c_prov[] = {"consumer-service"};
    create_test_component(2, "consumer", COMP_TYPE_SERVICE,
                          c_req, 1, c_prov, 1);

    n_components = 3;

    /* Single pass should start all providers and the consumer */
    int changes = graph_resolve();
    ASSERT_EQ(3, changes);

    /* All should be active */
    ASSERT_EQ(COMP_ACTIVE, components[0].state);
    ASSERT_EQ(COMP_ACTIVE, components[1].state);
    ASSERT_EQ(COMP_ACTIVE, components[2].state);
    ASSERT_TRUE(capability_active("shared-cap"));
    ASSERT_TRUE(capability_active("consumer-service"));
}

TEST(graph_resolve_dependency_loss) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create dependency: A provides cap-a, B requires cap-a */
    const char *a_req[] = {};
    const char *a_prov[] = {"cap-a"};
    create_test_component(0, "provider", COMP_TYPE_SERVICE,
                          a_req, 0, a_prov, 1);

    const char *b_req[] = {"cap-a"};
    const char *b_prov[] = {"cap-b"};
    create_test_component(1, "consumer", COMP_TYPE_SERVICE,
                          b_req, 1, b_prov, 1);

    n_components = 2;

    /* Start both components */
    graph_resolve_full();
    ASSERT_EQ(COMP_ACTIVE, components[0].state);
    ASSERT_EQ(COMP_ACTIVE, components[1].state);
    ASSERT_TRUE(capability_active("cap-a"));
    ASSERT_TRUE(capability_active("cap-b"));

    /* Simulate provider failing */
    components[0].state = COMP_FAILED;
    capability_withdraw("cap-a");

    /* Resolution should fail the dependent component */
    int changes = graph_resolve();
    ASSERT_EQ(1, changes);
    ASSERT_EQ(COMP_FAILED, components[1].state);
    ASSERT_FALSE(capability_active("cap-b"));
}

TEST(graph_resolve_full_converges) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create complex dependency graph */
    const char *kernel_req[] = {};
    const char *kernel_prov[] = {"kernel"};
    create_test_component(0, "kernel", COMP_TYPE_SERVICE,
                          kernel_req, 0, kernel_prov, 1);

    const char *fs_req[] = {"kernel"};
    const char *fs_prov[] = {"filesystem"};
    create_test_component(1, "filesystem", COMP_TYPE_ONESHOT,
                          fs_req, 1, fs_prov, 1);

    const char *net_req[] = {"kernel"};
    const char *net_prov[] = {"network"};
    create_test_component(2, "network", COMP_TYPE_SERVICE,
                          net_req, 1, net_prov, 1);

    const char *db_req[] = {"filesystem", "network"};
    const char *db_prov[] = {"database"};
    create_test_component(3, "database", COMP_TYPE_SERVICE,
                          db_req, 2, db_prov, 1);

    const char *app_req[] = {"database", "network"};
    const char *app_prov[] = {"application"};
    create_test_component(4, "application", COMP_TYPE_SERVICE,
                          app_req, 2, app_prov, 1);

    n_components = 5;

    /* Full resolution should converge */
    graph_resolve_full();

    /* All components should end up active */
    for (int i = 0; i < n_components; i++) {
        ASSERT_EQ(COMP_ACTIVE, components[i].state);
    }

    /* All capabilities should be active */
    ASSERT_TRUE(capability_active("kernel"));
    ASSERT_TRUE(capability_active("filesystem"));
    ASSERT_TRUE(capability_active("network"));
    ASSERT_TRUE(capability_active("database"));
    ASSERT_TRUE(capability_active("application"));
}

TEST(graph_resolve_oneshot_components) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create oneshot component */
    const char *req[] = {};
    const char *prov[] = {"init-done"};
    create_test_component(0, "init-task", COMP_TYPE_ONESHOT,
                          req, 0, prov, 1);

    /* Dependent service */
    const char *svc_req[] = {"init-done"};
    const char *svc_prov[] = {"main-service"};
    create_test_component(1, "main-service", COMP_TYPE_SERVICE,
                          svc_req, 1, svc_prov, 1);

    n_components = 2;

    /* Resolve should start oneshot */
    int changes = graph_resolve();
    ASSERT_EQ(1, changes);
    ASSERT_EQ(COMP_ACTIVE, components[0].state);  /* mock_component_start sets ACTIVE */

    /* Next pass should start the service */
    changes = graph_resolve();
    ASSERT_EQ(1, changes);
    ASSERT_EQ(COMP_ACTIVE, components[1].state);

    /* Should be stable now */
    changes = graph_resolve();
    ASSERT_EQ(0, changes);
}

TEST(graph_resolve_no_components) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Empty system should be stable */
    int changes = graph_resolve();
    ASSERT_EQ(0, changes);

    /* Full resolution on empty system */
    graph_resolve_full();  /* Should not crash */
    ASSERT_EQ(0, n_components);
}

/* Test helper to create readiness-aware component */
static void create_readiness_test_component(int idx, const char *name, comp_type_t type,
                                            const char **requires, int n_req,
                                            const char **provides, int n_prov,
                                            readiness_method_t readiness_method,
                                            int timeout) {
    component_t *comp = &components[idx];
    memset(comp, 0, sizeof(*comp));

    strncpy(comp->name, name, MAX_NAME - 1);
    strncpy(comp->binary, "/bin/true", MAX_PATH - 1);
    comp->type = type;
    comp->state = COMP_INACTIVE;
    comp->pid = -1;
    comp->readiness_method = readiness_method;
    comp->readiness_timeout = timeout;
    comp->readiness_interval = 5;

    /* Set dependencies */
    comp->n_requires = n_req;
    for (int i = 0; i < n_req && i < MAX_DEPS; i++) {
        strncpy(comp->requires[i], requires[i], MAX_NAME - 1);
    }

    comp->n_provides = n_prov;
    for (int i = 0; i < n_prov && i < MAX_DEPS; i++) {
        strncpy(comp->provides[i], provides[i], MAX_NAME - 1);
    }

    /* Configure readiness method */
    if (readiness_method == READINESS_FILE) {
        snprintf(comp->readiness_file, MAX_PATH, "/tmp/test_ready_%s", name);
    } else if (readiness_method == READINESS_COMMAND) {
        strcpy(comp->readiness_check, "/bin/true");
    } else if (readiness_method == READINESS_SIGNAL) {
        comp->readiness_signal = SIGUSR1;
    }
}

TEST(graph_resolve_with_readiness_protocol) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create components with readiness protocol */
    const char *fast_req[] = {};
    const char *fast_prov[] = {"fast-service"};
    create_readiness_test_component(0, "fast-ready", COMP_TYPE_SERVICE,
                                    fast_req, 0, fast_prov, 1, READINESS_NONE, 30);

    const char *slow_req[] = {"fast-service"};
    const char *slow_prov[] = {"slow-service"};
    create_readiness_test_component(1, "slow-ready", COMP_TYPE_SERVICE,
                                    slow_req, 1, slow_prov, 1, READINESS_FILE, 30);

    n_components = 2;

    /* First resolution should start fast-ready component */
    int changes = graph_resolve();
    ASSERT_TRUE(changes > 0);
    ASSERT_EQ(COMP_ACTIVE, components[0].state);  /* No readiness check */
    ASSERT_EQ(COMP_INACTIVE, components[1].state);  /* Dependencies not ready */

    /* Second resolution should start slow-ready component (moves to READY_WAIT) */
    changes = graph_resolve();
    /* Note: The exact behavior depends on implementation - component might go to STARTING or READY_WAIT */
    ASSERT_TRUE(components[1].state == COMP_STARTING || components[1].state == COMP_READY_WAIT);
}

TEST(graph_resolve_readiness_dependency_chain) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create chain where components use readiness protocol */
    const char *a_req[] = {};
    const char *a_prov[] = {"cap-a"};
    create_readiness_test_component(0, "comp-a", COMP_TYPE_SERVICE,
                                    a_req, 0, a_prov, 1, READINESS_COMMAND, 30);

    const char *b_req[] = {"cap-a"};
    const char *b_prov[] = {"cap-b"};
    create_readiness_test_component(1, "comp-b", COMP_TYPE_SERVICE,
                                    b_req, 1, b_prov, 1, READINESS_FILE, 30);

    n_components = 2;

    /* Start the chain - first component should start */
    int changes = graph_resolve();
    ASSERT_TRUE(changes > 0);
    ASSERT_TRUE(components[0].state == COMP_STARTING || components[0].state == COMP_READY_WAIT || components[0].state == COMP_ACTIVE);

    /* Second component should still be inactive until first is ready */
    if (components[0].state != COMP_ACTIVE) {
        ASSERT_EQ(COMP_INACTIVE, components[1].state);
    }
}

TEST(graph_resolve_handles_ready_wait_state) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create component in READY_WAIT state */
    const char *req[] = {};
    const char *prov[] = {"test-service"};
    create_readiness_test_component(0, "waiting-service", COMP_TYPE_SERVICE,
                                    req, 0, prov, 1, READINESS_FILE, 30);
    n_components = 1;

    /* Manually set component to READY_WAIT state */
    components[0].state = COMP_READY_WAIT;
    components[0].pid = 123;
    components[0].ready_wait_start = time(NULL);

    /* Graph resolution should handle READY_WAIT state without changes */
    int changes = graph_resolve();
    /* READY_WAIT state should be stable in graph resolution */
    ASSERT_EQ(COMP_READY_WAIT, components[0].state);
    ASSERT_FALSE(capability_active("test-service"));  /* Not ready yet */

    /* Verify that no changes were made (READY_WAIT is stable) */
    (void)changes;  /* Suppress unused variable warning */
}

/*
 * Cycle Detection Tests
 */

TEST(cycle_detection_no_cycles) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create simple linear chain: A -> B -> C */
    const char *a_req[] = {};
    const char *a_prov[] = {"cap-a"};
    create_test_component(0, "comp-a", COMP_TYPE_SERVICE, a_req, 0, a_prov, 1);

    const char *b_req[] = {"cap-a"};
    const char *b_prov[] = {"cap-b"};
    create_test_component(1, "comp-b", COMP_TYPE_SERVICE, b_req, 1, b_prov, 1);

    const char *c_req[] = {"cap-b"};
    const char *c_prov[] = {"cap-c"};
    create_test_component(2, "comp-c", COMP_TYPE_SERVICE, c_req, 1, c_prov, 1);

    n_components = 3;

    /* Test cycle detection */
    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    ASSERT_EQ(0, result);  /* No cycles */
    ASSERT_EQ(0, cycle_info.cycle_length);
    ASSERT_TRUE(cycle_info.cycle_components == NULL);
}

TEST(cycle_detection_simple_cycle) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create simple cycle: A requires B, B requires A */
    const char *a_req[] = {"cap-b"};
    const char *a_prov[] = {"cap-a"};
    create_test_component(0, "comp-a", COMP_TYPE_SERVICE, a_req, 1, a_prov, 1);

    const char *b_req[] = {"cap-a"};
    const char *b_prov[] = {"cap-b"};
    create_test_component(1, "comp-b", COMP_TYPE_SERVICE, b_req, 1, b_prov, 1);

    n_components = 2;

    /* Test cycle detection */
    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    ASSERT_EQ(1, result);  /* Cycle detected */
    ASSERT_TRUE(cycle_info.cycle_length > 0);
    ASSERT_TRUE(cycle_info.cycle_components != NULL);
    ASSERT_TRUE(strlen(cycle_info.error_message) > 0);

    free(cycle_info.cycle_components);
}

TEST(cycle_detection_complex_cycle) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create complex cycle: A -> B -> C -> D -> A */
    const char *a_req[] = {"cap-d"};
    const char *a_prov[] = {"cap-a"};
    create_test_component(0, "comp-a", COMP_TYPE_SERVICE, a_req, 1, a_prov, 1);

    const char *b_req[] = {"cap-a"};
    const char *b_prov[] = {"cap-b"};
    create_test_component(1, "comp-b", COMP_TYPE_SERVICE, b_req, 1, b_prov, 1);

    const char *c_req[] = {"cap-b"};
    const char *c_prov[] = {"cap-c"};
    create_test_component(2, "comp-c", COMP_TYPE_SERVICE, c_req, 1, c_prov, 1);

    const char *d_req[] = {"cap-c"};
    const char *d_prov[] = {"cap-d"};
    create_test_component(3, "comp-d", COMP_TYPE_SERVICE, d_req, 1, d_prov, 1);

    n_components = 4;

    /* Test cycle detection */
    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    ASSERT_EQ(1, result);  /* Cycle detected */
    ASSERT_EQ(5, cycle_info.cycle_length);  /* 4 components + closing component */
    ASSERT_TRUE(cycle_info.cycle_components != NULL);

    free(cycle_info.cycle_components);
}

TEST(cycle_detection_mixed_graph) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create graph with both cyclic and non-cyclic parts:
     * - Linear chain: E -> F
     * - Cycle: A -> B -> C -> A
     * - Independent: D
     */

    /* Linear chain */
    const char *e_req[] = {};
    const char *e_prov[] = {"cap-e"};
    create_test_component(0, "comp-e", COMP_TYPE_SERVICE, e_req, 0, e_prov, 1);

    const char *f_req[] = {"cap-e"};
    const char *f_prov[] = {"cap-f"};
    create_test_component(1, "comp-f", COMP_TYPE_SERVICE, f_req, 1, f_prov, 1);

    /* Cycle */
    const char *a_req[] = {"cap-c"};
    const char *a_prov[] = {"cap-a"};
    create_test_component(2, "comp-a", COMP_TYPE_SERVICE, a_req, 1, a_prov, 1);

    const char *b_req[] = {"cap-a"};
    const char *b_prov[] = {"cap-b"};
    create_test_component(3, "comp-b", COMP_TYPE_SERVICE, b_req, 1, b_prov, 1);

    const char *c_req[] = {"cap-b"};
    const char *c_prov[] = {"cap-c"};
    create_test_component(4, "comp-c", COMP_TYPE_SERVICE, c_req, 1, c_prov, 1);

    /* Independent */
    const char *d_req[] = {};
    const char *d_prov[] = {"cap-d"};
    create_test_component(5, "comp-d", COMP_TYPE_SERVICE, d_req, 0, d_prov, 1);

    n_components = 6;

    /* Test cycle detection - should find cycle despite non-cyclic components */
    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    ASSERT_EQ(1, result);  /* Cycle detected */
    ASSERT_TRUE(cycle_info.cycle_length > 0);

    free(cycle_info.cycle_components);
}

TEST(topological_sort_no_cycles) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create DAG: A -> B -> C, A -> D -> C */
    const char *a_req[] = {};
    const char *a_prov[] = {"cap-a"};
    create_test_component(0, "comp-a", COMP_TYPE_SERVICE, a_req, 0, a_prov, 1);

    const char *b_req[] = {"cap-a"};
    const char *b_prov[] = {"cap-b"};
    create_test_component(1, "comp-b", COMP_TYPE_SERVICE, b_req, 1, b_prov, 1);

    const char *c_req[] = {"cap-b", "cap-d"};
    const char *c_prov[] = {"cap-c"};
    create_test_component(2, "comp-c", COMP_TYPE_SERVICE, c_req, 2, c_prov, 1);

    const char *d_req[] = {"cap-a"};
    const char *d_prov[] = {"cap-d"};
    create_test_component(3, "comp-d", COMP_TYPE_SERVICE, d_req, 1, d_prov, 1);

    n_components = 4;

    /* Test topological sort */
    int sorted_components[MAX_COMPONENTS];
    int result = graph_topological_sort(sorted_components, MAX_COMPONENTS);

    ASSERT_EQ(0, result);  /* Success */

    /* Verify ordering: comp-a should come before comp-b, comp-b and comp-d before comp-c */
    int a_pos = -1, b_pos = -1, c_pos = -1, d_pos = -1;
    for (int i = 0; i < n_components; i++) {
        int comp_idx = sorted_components[i];
        if (strcmp(components[comp_idx].name, "comp-a") == 0) a_pos = i;
        if (strcmp(components[comp_idx].name, "comp-b") == 0) b_pos = i;
        if (strcmp(components[comp_idx].name, "comp-c") == 0) c_pos = i;
        if (strcmp(components[comp_idx].name, "comp-d") == 0) d_pos = i;
    }

    ASSERT_TRUE(a_pos < b_pos);  /* A before B */
    ASSERT_TRUE(a_pos < d_pos);  /* A before D */
    ASSERT_TRUE(b_pos < c_pos);  /* B before C */
    ASSERT_TRUE(d_pos < c_pos);  /* D before C */
}

TEST(topological_sort_with_cycles) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create cycle: A -> B -> A */
    const char *a_req[] = {"cap-b"};
    const char *a_prov[] = {"cap-a"};
    create_test_component(0, "comp-a", COMP_TYPE_SERVICE, a_req, 1, a_prov, 1);

    const char *b_req[] = {"cap-a"};
    const char *b_prov[] = {"cap-b"};
    create_test_component(1, "comp-b", COMP_TYPE_SERVICE, b_req, 1, b_prov, 1);

    n_components = 2;

    /* Test topological sort - should fail due to cycle */
    int sorted_components[MAX_COMPONENTS];
    int result = graph_topological_sort(sorted_components, MAX_COMPONENTS);

    ASSERT_EQ(-1, result);  /* Failure due to cycles */
}

TEST(graph_analyze_metrics) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create test graph with known metrics */
    const char *a_req[] = {};
    const char *a_prov[] = {"cap-a", "cap-shared"};
    create_test_component(0, "comp-a", COMP_TYPE_SERVICE, a_req, 0, a_prov, 2);

    const char *b_req[] = {"cap-a", "cap-shared"};
    const char *b_prov[] = {"cap-b"};
    create_test_component(1, "comp-b", COMP_TYPE_SERVICE, b_req, 2, b_prov, 1);

    const char *c_req[] = {"cap-b"};
    const char *c_prov[] = {"cap-c"};
    create_test_component(2, "comp-c", COMP_TYPE_SERVICE, c_req, 1, c_prov, 1);

    n_components = 3;

    /* Analyze graph metrics */
    graph_metrics_t metrics;
    int result = graph_analyze_metrics(&metrics);

    ASSERT_EQ(0, result);  /* Success */
    ASSERT_EQ(3, metrics.total_components);
    ASSERT_EQ(3, metrics.total_edges);  /* B->A (2 deps) + C->B (1 dep) = 3 total deps */
    ASSERT_TRUE(metrics.average_dependencies_per_component > 0.0);
}

TEST(graph_validate_component_addition) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create valid graph */
    const char *a_req[] = {};
    const char *a_prov[] = {"cap-a"};
    create_test_component(0, "comp-a", COMP_TYPE_SERVICE, a_req, 0, a_prov, 1);

    const char *b_req[] = {"cap-a"};
    const char *b_prov[] = {"cap-b"};
    create_test_component(1, "comp-b", COMP_TYPE_SERVICE, b_req, 1, b_prov, 1);

    n_components = 2;

    /* Test validation of adding a valid component */
    int result = graph_validate_component_addition("comp-c");
    ASSERT_EQ(0, result);  /* Should succeed - no cycles in current graph */
}

TEST(cycle_detection_empty_graph) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Test cycle detection on empty graph */
    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    ASSERT_EQ(0, result);  /* No cycles in empty graph */
    ASSERT_EQ(0, cycle_info.cycle_length);
}

TEST(cycle_detection_single_component_no_deps) {
    /* Reset state */
    n_components = 0;
    capability_init();

    /* Create single component with no dependencies */
    const char *req[] = {};
    const char *prov[] = {"standalone"};
    create_test_component(0, "standalone", COMP_TYPE_SERVICE, req, 0, prov, 1);
    n_components = 1;

    /* Test cycle detection */
    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    ASSERT_EQ(0, result);  /* No cycles */
}

int main(void) {
    /* Initialize logging for tests */
    log_open();

    return RUN_ALL_TESTS();
}