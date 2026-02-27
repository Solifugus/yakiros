/*
 * test_cycle_detection.c - Comprehensive tests for dependency cycle detection
 *
 * Tests various cycle patterns and edge cases to ensure robust cycle detection.
 */

#include "../test_framework.h"
#include "../../src/graph.h"
#include "../../src/component.h"
#include "../../src/capability.h"
#include "../../src/log.h"
#include <string.h>
#include <stdio.h>

/* Test helper to create a mock component for cycle testing */
static void create_cycle_test_component(int idx, const char *name,
                                       const char **requires, int n_req,
                                       const char **provides, int n_prov) {
    component_t *comp = &components[idx];
    memset(comp, 0, sizeof(*comp));

    strncpy(comp->name, name, MAX_NAME - 1);
    strncpy(comp->binary, "/bin/sleep", MAX_PATH - 1);  /* Mock binary */
    comp->type = COMP_TYPE_SERVICE;
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

TEST(cycle_self_dependency) {
    /* Test component that depends on itself */
    n_components = 0;
    capability_init();

    const char *req[] = {"self-cap"};
    const char *prov[] = {"self-cap"};
    create_cycle_test_component(0, "self-dep", req, 1, prov, 1);
    n_components = 1;

    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    ASSERT_EQ(1, result);  /* Cycle detected */
    ASSERT_TRUE(cycle_info.cycle_length > 0);
    ASSERT_TRUE(strstr(cycle_info.error_message, "self-dep") != NULL);

    free(cycle_info.cycle_components);
}

TEST(cycle_diamond_dependency) {
    /* Test diamond dependency (no cycle):
     *     A
     *   /   \
     *  B     C
     *   \   /
     *     D
     */
    n_components = 0;
    capability_init();

    const char *a_req[] = {};
    const char *a_prov[] = {"cap-a"};
    create_cycle_test_component(0, "comp-a", a_req, 0, a_prov, 1);

    const char *b_req[] = {"cap-a"};
    const char *b_prov[] = {"cap-b"};
    create_cycle_test_component(1, "comp-b", b_req, 1, b_prov, 1);

    const char *c_req[] = {"cap-a"};
    const char *c_prov[] = {"cap-c"};
    create_cycle_test_component(2, "comp-c", c_req, 1, c_prov, 1);

    const char *d_req[] = {"cap-b", "cap-c"};
    const char *d_prov[] = {"cap-d"};
    create_cycle_test_component(3, "comp-d", d_req, 2, d_prov, 1);

    n_components = 4;

    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    ASSERT_EQ(0, result);  /* No cycles - diamond is valid DAG */
}

TEST(cycle_multiple_separate_cycles) {
    /* Test graph with two separate cycles:
     * Cycle 1: A -> B -> A
     * Cycle 2: C -> D -> E -> C
     */
    n_components = 0;
    capability_init();

    /* First cycle */
    const char *a_req[] = {"cap-b"};
    const char *a_prov[] = {"cap-a"};
    create_cycle_test_component(0, "comp-a", a_req, 1, a_prov, 1);

    const char *b_req[] = {"cap-a"};
    const char *b_prov[] = {"cap-b"};
    create_cycle_test_component(1, "comp-b", b_req, 1, b_prov, 1);

    /* Second cycle */
    const char *c_req[] = {"cap-e"};
    const char *c_prov[] = {"cap-c"};
    create_cycle_test_component(2, "comp-c", c_req, 1, c_prov, 1);

    const char *d_req[] = {"cap-c"};
    const char *d_prov[] = {"cap-d"};
    create_cycle_test_component(3, "comp-d", d_req, 1, d_prov, 1);

    const char *e_req[] = {"cap-d"};
    const char *e_prov[] = {"cap-e"};
    create_cycle_test_component(4, "comp-e", e_req, 1, e_prov, 1);

    n_components = 5;

    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    ASSERT_EQ(1, result);  /* At least one cycle detected */
    ASSERT_TRUE(cycle_info.cycle_length > 0);

    free(cycle_info.cycle_components);
}

TEST(cycle_large_graph_no_cycles) {
    /* Test larger graph without cycles to ensure performance */
    n_components = 0;
    capability_init();

    /* Create chain of 10 components: 0 -> 1 -> 2 -> ... -> 9 */
    for (int i = 0; i < 10; i++) {
        char name[32], cap_name[32], prev_cap[32];
        snprintf(name, sizeof(name), "comp-%d", i);
        snprintf(cap_name, sizeof(cap_name), "cap-%d", i);

        if (i == 0) {
            const char *req[] = {};
            const char *prov[] = {cap_name};
            create_cycle_test_component(i, name, req, 0, prov, 1);
        } else {
            snprintf(prev_cap, sizeof(prev_cap), "cap-%d", i-1);
            const char *req[] = {prev_cap};
            const char *prov[] = {cap_name};
            create_cycle_test_component(i, name, req, 1, prov, 1);
        }
    }
    n_components = 10;

    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    ASSERT_EQ(0, result);  /* No cycles */
    ASSERT_EQ(0, cycle_info.cycle_length);
}

TEST(cycle_complex_interconnected_no_cycle) {
    /* Test complex interconnected graph without cycles */
    n_components = 0;
    capability_init();

    /* Create complex valid graph:
     * A provides cap-a
     * B requires cap-a, provides cap-b
     * C requires cap-a, provides cap-c
     * D requires cap-b and cap-c, provides cap-d
     * E requires cap-d, provides cap-e
     */

    const char *a_req[] = {};
    const char *a_prov[] = {"cap-a"};
    create_cycle_test_component(0, "comp-a", a_req, 0, a_prov, 1);

    const char *b_req[] = {"cap-a"};
    const char *b_prov[] = {"cap-b"};
    create_cycle_test_component(1, "comp-b", b_req, 1, b_prov, 1);

    const char *c_req[] = {"cap-a"};
    const char *c_prov[] = {"cap-c"};
    create_cycle_test_component(2, "comp-c", c_req, 1, c_prov, 1);

    const char *d_req[] = {"cap-b", "cap-c"};
    const char *d_prov[] = {"cap-d"};
    create_cycle_test_component(3, "comp-d", d_req, 2, d_prov, 1);

    const char *e_req[] = {"cap-d"};
    const char *e_prov[] = {"cap-e"};
    create_cycle_test_component(4, "comp-e", e_req, 1, e_prov, 1);

    n_components = 5;

    cycle_info_t cycle_info;
    int result = graph_detect_cycles(&cycle_info);

    ASSERT_EQ(0, result);  /* No cycles */

    /* Also test topological sort works */
    int sorted[MAX_COMPONENTS];
    int topo_result = graph_topological_sort(sorted, MAX_COMPONENTS);
    ASSERT_EQ(0, topo_result);
}

TEST(cycle_detection_error_handling) {
    /* Test error handling in cycle detection */

    /* Test with NULL parameter */
    int result = graph_detect_cycles(NULL);
    ASSERT_EQ(-1, result);

    /* Test with valid parameter but empty graph */
    n_components = 0;
    capability_init();

    cycle_info_t cycle_info;
    result = graph_detect_cycles(&cycle_info);
    ASSERT_EQ(0, result);  /* Empty graph has no cycles */
}

TEST(topological_sort_error_handling) {
    /* Test error handling in topological sort */

    /* Test with NULL parameter */
    int result = graph_topological_sort(NULL, 10);
    ASSERT_EQ(-1, result);

    /* Test with insufficient buffer size */
    n_components = 5;
    int small_buffer[2];
    result = graph_topological_sort(small_buffer, 2);
    ASSERT_EQ(-1, result);
}

TEST(graph_validation_integration) {
    /* Test the component validation function */
    n_components = 0;
    capability_init();

    /* Create valid graph */
    const char *a_req[] = {};
    const char *a_prov[] = {"cap-a"};
    create_cycle_test_component(0, "comp-a", a_req, 0, a_prov, 1);

    const char *b_req[] = {"cap-a"};
    const char *b_prov[] = {"cap-b"};
    create_cycle_test_component(1, "comp-b", b_req, 1, b_prov, 1);

    n_components = 2;

    /* Test validation - should pass */
    int result = validate_component_graph(0);  /* warn_only = 0 */
    ASSERT_EQ(0, result);

    /* Add cycle and test again */
    const char *c_req[] = {"cap-b"};
    const char *c_prov[] = {"cap-a"};  /* Creates cycle with comp-a */
    create_cycle_test_component(2, "comp-c", c_req, 1, c_prov, 1);
    n_components = 3;

    /* Test validation - should fail */
    result = validate_component_graph(0);
    ASSERT_EQ(-1, result);

    /* Test validation with warn_only - should succeed but warn */
    result = validate_component_graph(1);
    ASSERT_EQ(0, result);
}

int main(void) {
    /* Initialize logging for tests */
    log_open();

    return RUN_ALL_TESTS();
}