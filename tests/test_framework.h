/*
 * test_framework.h - Minimal C test framework for YakirOS
 *
 * Simple header-only test framework with assertions and test registration.
 * Usage:
 *   #include "test_framework.h"
 *   TEST(test_name) { ASSERT_EQ(1, 1); }
 *   int main() { return RUN_ALL_TESTS(); }
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test statistics */
static int test_count = 0;
static int test_passed = 0;
static int test_failed = 0;
static int assertions = 0;
static const char* current_test = NULL;

/* Colors for output */
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RESET "\033[0m"

/* Test registration and running */
typedef void (*test_func_t)(void);

struct test_case {
    const char* name;
    test_func_t func;
};

static struct test_case test_cases[256];
static int num_test_cases = 0;

#define TEST(test_name) \
    void test_##test_name(void); \
    __attribute__((constructor)) \
    void register_test_##test_name(void) { \
        test_cases[num_test_cases].name = #test_name; \
        test_cases[num_test_cases].func = test_##test_name; \
        num_test_cases++; \
    } \
    void test_##test_name(void)

/* Assertion macros */
#define ASSERT_EQ(expected, actual) \
    do { \
        assertions++; \
        if ((expected) != (actual)) { \
            printf("  " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: Expected %ld, got %ld\n", \
                   __FILE__, __LINE__, (long)(expected), (long)(actual)); \
            return; \
        } \
    } while(0)

#define ASSERT_NE(expected, actual) \
    do { \
        assertions++; \
        if ((expected) == (actual)) { \
            printf("  " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: Expected != %ld, got %ld\n", \
                   __FILE__, __LINE__, (long)(expected), (long)(actual)); \
            return; \
        } \
    } while(0)

#define ASSERT_TRUE(condition) \
    do { \
        assertions++; \
        if (!(condition)) { \
            printf("  " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: Expected true, got false\n", \
                   __FILE__, __LINE__); \
            return; \
        } \
    } while(0)

#define ASSERT_FALSE(condition) \
    do { \
        assertions++; \
        if (condition) { \
            printf("  " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: Expected false, got true\n", \
                   __FILE__, __LINE__); \
            return; \
        } \
    } while(0)

#define ASSERT_NULL(ptr) \
    do { \
        assertions++; \
        if ((ptr) != NULL) { \
            printf("  " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: Expected NULL, got %p\n", \
                   __FILE__, __LINE__, (void*)(ptr)); \
            return; \
        } \
    } while(0)

#define ASSERT_NOT_NULL(ptr) \
    do { \
        assertions++; \
        if ((ptr) == NULL) { \
            printf("  " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: Expected non-NULL pointer\n", \
                   __FILE__, __LINE__); \
            return; \
        } \
    } while(0)

#define ASSERT_STR_EQ(expected, actual) \
    do { \
        assertions++; \
        if (strcmp((expected), (actual)) != 0) { \
            printf("  " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: Expected \"%s\", got \"%s\"\n", \
                   __FILE__, __LINE__, (expected), (actual)); \
            return; \
        } \
    } while(0)

#define ASSERT_STR_NE(expected, actual) \
    do { \
        assertions++; \
        if (strcmp((expected), (actual)) == 0) { \
            printf("  " COLOR_RED "FAIL" COLOR_RESET ": %s:%d: Expected != \"%s\", got \"%s\"\n", \
                   __FILE__, __LINE__, (expected), (actual)); \
            return; \
        } \
    } while(0)

/* Test runner */
static int RUN_ALL_TESTS(void) {
    printf("Running %d tests...\n\n", num_test_cases);

    for (int i = 0; i < num_test_cases; i++) {
        current_test = test_cases[i].name;
        printf("[ RUN      ] %s\n", current_test);

        int assertions_before = assertions;
        test_count++;

        test_cases[i].func();

        int test_assertions = assertions - assertions_before;
        if (test_assertions > 0) {
            printf("  " COLOR_GREEN "PASS" COLOR_RESET ": %d assertions\n", test_assertions);
            test_passed++;
        } else {
            printf("  " COLOR_YELLOW "SKIP" COLOR_RESET ": No assertions\n");
            test_failed++;
        }
    }

    printf("\n=== Test Summary ===\n");
    printf("Tests run:    %d\n", test_count);
    printf("Passed:       " COLOR_GREEN "%d" COLOR_RESET "\n", test_passed);
    printf("Failed:       " COLOR_RED "%d" COLOR_RESET "\n", test_failed);
    printf("Assertions:   %d\n", assertions);

    if (test_failed > 0) {
        printf("\n" COLOR_RED "SOME TESTS FAILED" COLOR_RESET "\n");
        return 1;
    } else {
        printf("\n" COLOR_GREEN "ALL TESTS PASSED" COLOR_RESET "\n");
        return 0;
    }
}

/* Helper macros for test setup/teardown */
#define TEST_SETUP() do { } while(0)
#define TEST_TEARDOWN() do { } while(0)

#endif /* TEST_FRAMEWORK_H */