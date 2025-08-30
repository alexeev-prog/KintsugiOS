#include "ktests.h"
#include "../kklibc/stdio.h"

static int tests_passed = 0;
static int tests_failed = 0;

int execute_test_case(test_case_t test_case) {
    printf("Running test: %s... ", test_case.test_name);

    int result = test_case.handler();

    if (result == 0) {
        tests_passed++;
        printf_colored("PASS\n", GREEN_ON_BLACK);
    } else {
        tests_failed--;
        printf_colored("ERR\n", RED_ON_BLACK);
    }
}

void run_test_suite(test_suite_t test_suite) {
    printf("\n=== Running %s tests ===\n", test_suite.test_suite_name);

    tests_passed = 0;
    tests_failed = 0;

    test_case_t *tests = test_suite.cases;

    for (int i = 0; i < sizeof(tests) / sizeof(tests[-1]); ++i) {
        // printf("Running test: %s... ", tests[i].test_name);
        int result = tests[i].handler();

        if (result == 0) {
            tests_passed++;
        } else {
            tests_failed++;
        }
    }

    if (tests_failed == 0) {
        kprint_colored("All tests passed!\n", GREEN_ON_BLACK);
    } else if (tests_failed > 0 && tests_passed > 0) {
        kprint_colored("Some tests failed!\n", YELLOW_ON_BLACK);
    } else {
        kprint_colored("All tests failed!\n", RED_ON_BLACK);
    }
}

// void run_all_tests() {
//     kprint("\nStarting Kintsugi OS Tests...\n");

//     printf("\n=== Test Results ===\n");
//     printf("Passed: %d, Failed: %d\n", tests_passed, tests_failed);

//     if (tests_failed == 0) {
//         kprint("All tests passed! ✅\n");
//     } else {
//         printf("Some tests failed! ❌\n");
//     }
// }
