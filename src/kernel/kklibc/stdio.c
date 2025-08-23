#include "stdlib.h"
#include "stdio.h"
#include "../drivers/screen.h"

#define PRINTF_BUF_SIZE 1024
static char printf_buf[PRINTF_BUF_SIZE];

static void format_string(char *buf, char *fmt, va_list args) {
    char *ptr = buf;
    char *s;
    int num;
    char num_buf[32];
    char c;

    while (*fmt) {
        if (*fmt != '%') {
            *ptr++ = *fmt++;
            continue;
        }

        fmt++;
        switch (*fmt) {
            case 'd':
                num = va_arg(args, int);
                int_to_ascii(num, num_buf);
                s = num_buf;
                while (*s) *ptr++ = *s++;
                break;
            case 'x':
                num = va_arg(args, int);
                hex_to_ascii(num, num_buf);
                s = num_buf;
                while (*s) *ptr++ = *s++;
                break;
            case 's':
                s = va_arg(args, char*);
                while (*s) *ptr++ = *s++;
                break;
            case 'c':
                c = (char)va_arg(args, int);
                *ptr++ = c;
                break;
            default:
                *ptr++ = '%';
                *ptr++ = *fmt;
                break;
        }
        fmt++;
    }
    *ptr = '\0';
}

void kprintf(char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    format_string(printf_buf, fmt, args);
    va_end(args);
    kprint(printf_buf);
}

void kprintf_colored(char *fmt, int color, ...) {
    va_list args;
    va_start(args, color);
    format_string(printf_buf, fmt, args);
    va_end(args);
    kprint_colored(printf_buf, color);
}

void kprintf_at(char *fmt, int col, int row, int color, ...) {
    va_list args;
    va_start(args, color);
    format_string(printf_buf, fmt, args);
    va_end(args);
    kprint_at(printf_buf, col, row, color);
}
