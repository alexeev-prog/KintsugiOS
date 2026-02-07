/*------------------------------------------------------------------------------
 *  Kintsugi OS KKLIBC source code
 *  File: kklibc/stdio.c
 *  Title: Набор средств ввода/вывода
 *	Description: null
 * ----------------------------------------------------------------------------*/

#include "stdio.h"

#include "../drivers/screen.h"
#include "stdlib.h"

#define PRINTF_BUF_SIZE 1024
static char printf_buf[PRINTF_BUF_SIZE];

unsigned int format_string_core(char* buf, unsigned int size, char* fmt, va_list args) {
    unsigned int i = 0;
    char num_buf[32];
    const char* s;

    while (*fmt && (size == 0 || i < size - 1)) {
        if (*fmt != '%') {
            buf[i++] = *fmt++;
            continue;
        }

        fmt++;

        int left_align = 0;
        int width = 0;
        int zero_pad = 0;
        int hex_prefix = 0;

        if (*fmt == '-') {
            left_align = 1;
            fmt++;
        }

        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        if (*fmt == '0') {
            zero_pad = 1;
            fmt++;
        }

        if (width > 0 && fmt[0] == '#' && fmt[1] == 'x') {
            hex_prefix = 1;
            fmt += 2;
        }

        switch (*fmt) {
            case 'd': {
                int num = va_arg(args, int);
                int negative = 0;
                unsigned int unum;
                int digits = 0;

                if (num < 0) {
                    negative = 1;
                    unum = (unsigned int)(-num);
                } else {
                    unum = (unsigned int)num;
                }

                do {
                    num_buf[digits++] = '0' + (unum % 10);
                    unum /= 10;
                } while (unum > 0);

                if (negative) {
                    num_buf[digits++] = '-';
                }

                int total_digits = digits;
                int actual_digits = total_digits;

                if (!left_align && width > total_digits) {
                    int padding = width - total_digits;
                    while (padding-- > 0 && (size == 0 || i < size - 1)) {
                        buf[i++] = zero_pad ? '0' : ' ';
                    }
                }

                while (actual_digits-- > 0 && (size == 0 || i < size - 1)) {
                    buf[i++] = num_buf[actual_digits];
                }

                if (left_align && width > total_digits) {
                    int padding = width - total_digits;
                    while (padding-- > 0 && (size == 0 || i < size - 1)) {
                        buf[i++] = ' ';
                    }
                }
                break;
            }

            case 'x': {
                unsigned int num = va_arg(args, unsigned int);
                const char* hex_digits = "0123456789abcdef";
                int digits = 0;
                int total_len = 0;

                unsigned int temp = num;
                do {
                    digits++;
                    temp >>= 4;
                } while (temp > 0);

                if (hex_prefix && num != 0) {
                    total_len = digits + 2;
                } else {
                    total_len = digits;
                }

                if (!left_align && width > total_len) {
                    int padding = width - total_len;
                    while (padding-- > 0 && (size == 0 || i < size - 1)) {
                        buf[i++] = zero_pad ? '0' : ' ';
                    }
                }

                if (hex_prefix && num != 0) {
                    if (size == 0 || i < size - 1) {
                        buf[i++] = '0';
                    }
                    if (size == 0 || i < size - 1) {
                        buf[i++] = 'x';
                    }
                }

                int idx = digits;
                while (idx-- > 0) {
                    int shift = idx * 4;
                    char hex_char = hex_digits[(num >> shift) & 0xF];
                    if (size == 0 || i < size - 1) {
                        buf[i++] = hex_char;
                    }
                }

                if (left_align && width > total_len) {
                    int padding = width - total_len;
                    while (padding-- > 0 && (size == 0 || i < size - 1)) {
                        buf[i++] = ' ';
                    }
                }
                break;
            }

            case 's': {
                s = va_arg(args, char*);
                if (!s) {
                    s = "(null)";
                }

                const char* p = s;
                int len = 0;
                while (*p++) {
                    len++;
                }

                p = s;

                if (!left_align && width > len) {
                    int padding = width - len;
                    while (padding-- > 0 && (size == 0 || i < size - 1)) {
                        buf[i++] = ' ';
                    }
                }

                while (*p && (size == 0 || i < size - 1)) {
                    buf[i++] = *p++;
                }

                if (left_align && width > len) {
                    int padding = width - len;
                    while (padding-- > 0 && (size == 0 || i < size - 1)) {
                        buf[i++] = ' ';
                    }
                }
                break;
            }

            case 'c': {
                char c = (char)va_arg(args, int);

                if (!left_align && width > 1) {
                    int padding = width - 1;
                    while (padding-- > 0 && (size == 0 || i < size - 1)) {
                        buf[i++] = ' ';
                    }
                }

                if (size == 0 || i < size - 1) {
                    buf[i++] = c;
                }

                if (left_align && width > 1) {
                    int padding = width - 1;
                    while (padding-- > 0 && (size == 0 || i < size - 1)) {
                        buf[i++] = ' ';
                    }
                }
                break;
            }

            case 'u': {
                unsigned int num = va_arg(args, unsigned int);
                int digits = 0;

                do {
                    num_buf[digits++] = '0' + (num % 10);
                    num /= 10;
                } while (num > 0);

                int total_digits = digits;

                if (!left_align && width > total_digits) {
                    int padding = width - total_digits;
                    while (padding-- > 0 && (size == 0 || i < size - 1)) {
                        buf[i++] = zero_pad ? '0' : ' ';
                    }
                }

                while (total_digits-- > 0 && (size == 0 || i < size - 1)) {
                    buf[i++] = num_buf[total_digits];
                }

                if (left_align && width > digits) {
                    int padding = width - digits;
                    while (padding-- > 0 && (size == 0 || i < size - 1)) {
                        buf[i++] = ' ';
                    }
                }
                break;
            }

            default: {
                int len = 1;
                if (*fmt != '\0') {
                    len = 2;
                }

                if (!left_align && width > len) {
                    int padding = width - len;
                    while (padding-- > 0 && (size == 0 || i < size - 1)) {
                        buf[i++] = ' ';
                    }
                }

                if (size == 0 || i < size - 1) {
                    buf[i++] = '%';
                }
                if (*fmt != '\0' && (size == 0 || i < size - 1)) {
                    buf[i++] = *fmt;
                }

                if (left_align && width > len) {
                    int padding = width - len;
                    while (padding-- > 0 && (size == 0 || i < size - 1)) {
                        buf[i++] = ' ';
                    }
                }
                break;
            }
        }
        fmt++;
    }

    if (size == 0 || i < size) {
        buf[i] = '\0';
    } else if (size > 0) {
        buf[size - 1] = '\0';
    }

    return i;
}

static void format_string(char* buf, char* fmt, va_list args) {
    format_string_core(buf, 0, fmt, args);
}

void printf(char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    format_string(printf_buf, fmt, args);
    va_end(args);
    kprint(printf_buf);
}

void printf_colored(char* fmt, int color, ...) {
    va_list args;
    va_start(args, color);
    format_string(printf_buf, fmt, args);
    va_end(args);
    kprint_colored(printf_buf, color);
}

void printf_at(char* fmt, int col, int row, int color, ...) {
    va_list args;
    va_start(args, color);
    format_string(printf_buf, fmt, args);
    va_end(args);
    kprint_at(printf_buf, col, row, color);
}
