#ifndef KERNEL_LOG_H
#define KERNEL_LOG_H

#include "../cpu/isr.h"

void log_init(void);
void log_write(const char* message);
int log_read(u32 offset, char* buffer, u32 capacity);
__attribute__((noreturn)) void panic(const char* message, const registers_t* registers);

#endif
