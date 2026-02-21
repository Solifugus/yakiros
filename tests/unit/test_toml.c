/*
 * test_toml.c - Tests for TOML parser module
 */

#include "../test_framework.h"
#include "../../src/toml.h"
#include "../../src/log.h"
#include <string.h>
#include <unistd.h>

/* Test data directory relative to test executable */
#define TEST_DATA_DIR "../data"

TEST(parse_simple_service) {
    component_t comp;
    int result = parse_component(TEST_DATA_DIR "/simple-service.toml", &comp);

    ASSERT_EQ(0, result);
    ASSERT_STR_EQ("simple-service", comp.name);
    ASSERT_STR_EQ("/usr/bin/simple-daemon", comp.binary);
    ASSERT_EQ(COMP_TYPE_SERVICE, comp.type);
    ASSERT_EQ(COMP_INACTIVE, comp.state);

    /* Check arguments */
    ASSERT_EQ(2, comp.argc);
    ASSERT_STR_EQ("--config", comp.args[0]);
    ASSERT_STR_EQ("/etc/simple.conf", comp.args[1]);

    /* Check dependencies */
    ASSERT_EQ(1, comp.n_requires);
    ASSERT_STR_EQ("network", comp.requires[0]);

    ASSERT_EQ(1, comp.n_provides);
    ASSERT_STR_EQ("simple-api", comp.provides[0]);
}

TEST(parse_complex_service) {
    component_t comp;
    int result = parse_component(TEST_DATA_DIR "/complex-service.toml", &comp);

    ASSERT_EQ(0, result);
    ASSERT_STR_EQ("database-server", comp.name);
    ASSERT_STR_EQ("/usr/bin/postgres", comp.binary);
    ASSERT_EQ(COMP_TYPE_SERVICE, comp.type);

    /* Check complex arguments */
    ASSERT_EQ(4, comp.argc);
    ASSERT_STR_EQ("--config-file", comp.args[0]);
    ASSERT_STR_EQ("/etc/postgresql/postgresql.conf", comp.args[1]);
    ASSERT_STR_EQ("--data", comp.args[2]);
    ASSERT_STR_EQ("/var/lib/postgres", comp.args[3]);

    /* Check multiple requires */
    ASSERT_EQ(3, comp.n_requires);
    ASSERT_STR_EQ("filesystem", comp.requires[0]);
    ASSERT_STR_EQ("network", comp.requires[1]);
    ASSERT_STR_EQ("logging", comp.requires[2]);

    /* Check multiple provides */
    ASSERT_EQ(2, comp.n_provides);
    ASSERT_STR_EQ("database", comp.provides[0]);
    ASSERT_STR_EQ("postgresql", comp.provides[1]);

    /* Check optional dependencies */
    ASSERT_EQ(1, comp.n_optional);
    ASSERT_STR_EQ("monitoring", comp.optional[0]);

    /* Check lifecycle settings */
    ASSERT_EQ(1, comp.reload_signal);  /* SIGHUP */
    ASSERT_STR_EQ("/usr/bin/pg_isready", comp.health_check);
    ASSERT_EQ(30, comp.health_interval);
}

TEST(parse_oneshot_component) {
    component_t comp;
    int result = parse_component(TEST_DATA_DIR "/oneshot-task.toml", &comp);

    ASSERT_EQ(0, result);
    ASSERT_STR_EQ("mount-filesystems", comp.name);
    ASSERT_STR_EQ("/bin/mount", comp.binary);
    ASSERT_EQ(COMP_TYPE_ONESHOT, comp.type);

    /* Check oneshot args */
    ASSERT_EQ(1, comp.argc);
    ASSERT_STR_EQ("-a", comp.args[0]);

    /* Check dependencies for oneshot */
    ASSERT_EQ(1, comp.n_requires);
    ASSERT_STR_EQ("kernel", comp.requires[0]);
    ASSERT_EQ(1, comp.n_provides);
    ASSERT_STR_EQ("filesystem", comp.provides[0]);
}

TEST(parse_nonexistent_file) {
    component_t comp;
    int result = parse_component("/nonexistent/file.toml", &comp);

    ASSERT_NE(0, result);
}

TEST(parse_empty_file) {
    component_t comp;
    int result = parse_component(TEST_DATA_DIR "/empty.toml", &comp);

    ASSERT_NE(0, result);  /* Should fail due to missing name */
}

TEST(parse_invalid_syntax) {
    component_t comp;
    int result = parse_component(TEST_DATA_DIR "/invalid-syntax.toml", &comp);

    ASSERT_NE(0, result);  /* Should fail due to parsing errors or validation */
}

TEST(parse_missing_name) {
    component_t comp;
    int result = parse_component(TEST_DATA_DIR "/missing-name.toml", &comp);

    ASSERT_NE(0, result);  /* Should fail validation */
}

TEST(component_defaults) {
    component_t comp;
    int result = parse_component(TEST_DATA_DIR "/simple-service.toml", &comp);

    ASSERT_EQ(0, result);

    /* Check default values are set correctly */
    ASSERT_EQ(COMP_INACTIVE, comp.state);
    ASSERT_EQ(HANDOFF_NONE, comp.handoff);
    ASSERT_EQ(0, comp.reload_signal);
    ASSERT_EQ(0, comp.health_interval);
    ASSERT_EQ(-1, comp.pid);
    ASSERT_EQ(0, comp.restart_count);

    /* Config path should be set */
    ASSERT_STR_EQ(TEST_DATA_DIR "/simple-service.toml", comp.config_path);
}

int main(void) {
    /* Initialize logging for tests */
    log_open();

    return RUN_ALL_TESTS();
}