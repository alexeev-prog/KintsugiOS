/*------------------------------------------------------------------------------
 *  Kintsugi OS KKLIBC source code
 *  File: kklibc/math.c
 *  Title: Набор математических функций и алгоритмов
 *	Description: null
 * ----------------------------------------------------------------------------*/
#include "math.h"

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
