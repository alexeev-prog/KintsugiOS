#ifndef LIBC_STDIO_H
#define LIBC_STDIO_H

typedef char* va_list;
#define va_start(ap, last) (ap = (va_list)&last + sizeof(last))
#define va_arg(ap, type) (*(type*)((ap += sizeof(type)) - sizeof(type)))
#define va_end(ap) (ap = (va_list)0)

void kprintf(char *fmt, ...);
void kprintf_colored(char *fmt, int color, ...);
void kprintf_at(char *fmt, int col, int row, int color, ...);

#endif // LIBC_STDIO_H
