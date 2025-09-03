#ifndef KKLIBC_ASSERT_H
#define KKLIBC_ASSERT_H

#include "stdio.h"

void __assert(char* expr, char* waited_expr) {
    if (expr == waited_expr) {
        printf("%s == %s\n", expr, waited_expr);
    } else {
        printf("%s != %s\n", expr, waited_expr);
    }
}

#endif
