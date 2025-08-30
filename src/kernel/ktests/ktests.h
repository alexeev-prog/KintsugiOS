#ifndef KTESTS_KTESTS_H
#define KTESTS_KTESTS_H

#include "../kklibc/ctypes.h"

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("TEST FAILED: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            return -1; \
        } \
    } while (0)

#define TEST_PASS() \
    do { \
        printf("TEST PASSED\n"); \
        return 0; \
    } while (0)

typedef struct {
    char* test_name;
    int (*handler)();
} test_case_t;

typedef struct {
    char* test_suite_name;
    test_case_t cases[];
} test_suite_t;

int execute_test_case(test_case_t test_case);

void execute_test_suite(test_suite_t test_suite);

#endif
