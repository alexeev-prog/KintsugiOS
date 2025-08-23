#include "math.h"

u32 fibonacci(int num) {
    if (num <= 0) {
        return 0;
    }

    u32 a = 0;
    u32 b = 1;

    if (num == 1) {
        return b;
    }

    for (int i = 2; i <= num; i++) {
        u32 next = a + b;
        a = b;
        b = next;
    }

    return b;
}

int binary_pow(int b, u32 e) {
    int v = 1.0;
    while (e != 0) {
        if ((e & 1) != 0) {
            v *= b;
        }
        b *= b;
        e >>= 1;
    }
    return v;
}
