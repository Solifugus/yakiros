/*
 * test_framework_test.c - Test the test framework itself
 */

#include "test_framework.h"

TEST(basic_assertions) {
    ASSERT_EQ(1, 1);
    ASSERT_NE(1, 2);
    ASSERT_TRUE(1 == 1);
    ASSERT_FALSE(1 == 2);
}

TEST(string_assertions) {
    ASSERT_STR_EQ("hello", "hello");
    ASSERT_STR_NE("hello", "world");
}

TEST(pointer_assertions) {
    int value = 42;
    int* ptr = &value;

    ASSERT_NOT_NULL(ptr);
    ASSERT_NULL(NULL);
}

int main(void) {
    return RUN_ALL_TESTS();
}