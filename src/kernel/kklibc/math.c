/*------------------------------------------------------------------------------
 *  Kintsugi OS KKLIBC source code
 *  File: kklibc/math.c
 *  Title: Набор математических функций и алгоритмов
 *	Description: null
 * ----------------------------------------------------------------------------*/
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

float calculate_discriminant(float a, float b, float c) {
    float discriminant = b * b - 4 * a * c;

    return discriminant;
}

u32 factorial(long n) {
    if (n == 0) {
        return 1;
    }

    return (unsigned)n * factorial(n - 1);
}

u32 cfactorial_sum(char num_chars[]) {
    u32 fact_num;
    u32 sum = 0;

    for (int i = 0; num_chars[i]; i++) {
        int ith_num = num_chars[i] - '0';
        fact_num = factorial(ith_num);
        sum = sum + fact_num;
    }
    return sum;
}

u32 ifactorial_sum(long nums[], int size) {
    u32 fact_num;
    u32 sum = 0;
    for (int i = 0; i < size; i++) {
        fact_num = factorial(nums[i]);
        sum += fact_num;
    }
    return sum;
}
