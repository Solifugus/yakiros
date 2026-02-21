/*
 * test_capability.c - Tests for capability registry module
 */

#include "../test_framework.h"
#include "../../src/capability.h"
#include "../../src/log.h"
#include <string.h>

TEST(capability_init_clears_registry) {
    /* Register a capability first */
    capability_register("test-cap", 5);
    ASSERT_EQ(1, capability_count());

    /* Initialize should clear everything */
    capability_init();
    ASSERT_EQ(0, capability_count());
    ASSERT_FALSE(capability_active("test-cap"));
}

TEST(capability_register_and_lookup) {
    capability_init();

    /* Register a new capability */
    capability_register("network", 10);

    /* Check it's registered */
    ASSERT_EQ(1, capability_count());
    ASSERT_TRUE(capability_active("network"));

    /* Check index lookup */
    int idx = capability_index("network");
    ASSERT_NE(-1, idx);
    ASSERT_STR_EQ("network", capability_name(idx));
    ASSERT_TRUE(capability_active_by_idx(idx));
    ASSERT_EQ(10, capability_provider(idx));
}

TEST(capability_register_duplicate_updates_provider) {
    capability_init();

    /* Register capability with provider 5 */
    capability_register("filesystem", 5);
    ASSERT_EQ(1, capability_count());
    ASSERT_EQ(5, capability_provider(capability_index("filesystem")));

    /* Register same capability with different provider */
    capability_register("filesystem", 8);
    ASSERT_EQ(1, capability_count()); /* Count shouldn't increase */
    ASSERT_EQ(8, capability_provider(capability_index("filesystem")));
    ASSERT_TRUE(capability_active("filesystem"));
}

TEST(capability_withdraw) {
    capability_init();

    /* Register and withdraw capability */
    capability_register("database", 3);
    ASSERT_TRUE(capability_active("database"));

    capability_withdraw("database");
    ASSERT_FALSE(capability_active("database"));

    /* Capability should still exist in registry, just not active */
    ASSERT_NE(-1, capability_index("database"));
    ASSERT_EQ(1, capability_count());
}

TEST(capability_withdraw_nonexistent) {
    capability_init();

    /* Withdrawing non-existent capability should not crash */
    capability_withdraw("nonexistent");
    ASSERT_EQ(0, capability_count());
}

TEST(capability_lookup_nonexistent) {
    capability_init();

    ASSERT_EQ(-1, capability_index("nonexistent"));
    ASSERT_FALSE(capability_active("nonexistent"));
    ASSERT_NULL(capability_name(-1));
    ASSERT_NULL(capability_name(100));
    ASSERT_FALSE(capability_active_by_idx(-1));
    ASSERT_FALSE(capability_active_by_idx(100));
    ASSERT_EQ(-1, capability_provider(-1));
    ASSERT_EQ(-1, capability_provider(100));
}

TEST(multiple_capabilities) {
    capability_init();

    /* Register multiple capabilities */
    capability_register("network", 1);
    capability_register("filesystem", 2);
    capability_register("database", 3);
    capability_register("logging", 4);

    ASSERT_EQ(4, capability_count());

    /* Check they're all active */
    ASSERT_TRUE(capability_active("network"));
    ASSERT_TRUE(capability_active("filesystem"));
    ASSERT_TRUE(capability_active("database"));
    ASSERT_TRUE(capability_active("logging"));

    /* Check providers */
    ASSERT_EQ(1, capability_provider(capability_index("network")));
    ASSERT_EQ(2, capability_provider(capability_index("filesystem")));
    ASSERT_EQ(3, capability_provider(capability_index("database")));
    ASSERT_EQ(4, capability_provider(capability_index("logging")));

    /* Withdraw one and check the others are unaffected */
    capability_withdraw("filesystem");
    ASSERT_FALSE(capability_active("filesystem"));
    ASSERT_TRUE(capability_active("network"));
    ASSERT_TRUE(capability_active("database"));
    ASSERT_TRUE(capability_active("logging"));
}

TEST(capability_registry_iteration) {
    capability_init();

    /* Register some capabilities */
    const char *expected[] = {"alpha", "beta", "gamma"};
    const int providers[] = {10, 20, 30};

    for (int i = 0; i < 3; i++) {
        capability_register(expected[i], providers[i]);
    }

    ASSERT_EQ(3, capability_count());

    /* Iterate through registry and verify */
    for (int i = 0; i < capability_count(); i++) {
        const char *name = capability_name(i);
        ASSERT_NOT_NULL(name);

        /* Find expected name */
        int found = -1;
        for (int j = 0; j < 3; j++) {
            if (strcmp(name, expected[j]) == 0) {
                found = j;
                break;
            }
        }
        ASSERT_NE(-1, found);
        ASSERT_EQ(providers[found], capability_provider(i));
        ASSERT_TRUE(capability_active_by_idx(i));
    }
}

TEST(capability_name_truncation) {
    capability_init();

    /* Create a very long capability name (longer than MAX_NAME) */
    char long_name[256];
    memset(long_name, 'x', 255);
    long_name[255] = '\0';

    capability_register(long_name, 42);
    ASSERT_EQ(1, capability_count());

    /* The name should be truncated but still findable by truncated version */
    int idx = capability_index(long_name);
    if (idx == -1) {
        /* Try with truncated version */
        char truncated[128];
        strncpy(truncated, long_name, 127);
        truncated[127] = '\0';
        idx = capability_index(truncated);
    }

    /* Should find it somehow (exact behavior depends on MAX_NAME) */
    ASSERT_NE(-1, idx);
}

int main(void) {
    /* Initialize logging for tests */
    log_open();

    return RUN_ALL_TESTS();
}