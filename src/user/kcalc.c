#include "runtime.h"

static int number(const char* s, u32* out) {
    u32 value = 0, base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    if (!*s) {
        return 0;
    }
    while (*s) {
        u32 digit = *s >= '0' && *s <= '9' ? *s - '0'
            : *s >= 'a' && *s <= 'f'       ? *s - 'a' + 10
                                           : *s - 'A' + 10;
        if (digit >= base) {
            return 0;
        }
        value = value * base + digit;
        ++s;
    }
    *out = value;
    return 1;
}

static void print_number(u32 value) {
    char text[11];
    u32 i = 10;
    text[i] = 0;
    do {
        text[--i] = '0' + value % 10;
        value /= 10;
    } while (value);
    write_all(1, text + i, 10 - i);
    write_all(1, "\n", 1);
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) {
        write_all(1, "Usage: kcalc A [+|-|*|/] B\n", 29);
        return 2;
    }
    u32 a, b, result;
    if (!number(argv[1], &a) || (argc == 4 && (!number(argv[3], &b)))) {
        return 2;
    }
    if (argc == 2) {
        result = a;
    } else if (argv[2][0] == '+') {
        result = a + b;
    } else if (argv[2][0] == '-') {
        result = a - b;
    } else if (argv[2][0] == '*') {
        result = a * b;
    } else if (argv[2][0] == '/' && b) {
        result = a / b;
    } else {
        return 2;
    }
    print_number(result);
    return 0;
}
