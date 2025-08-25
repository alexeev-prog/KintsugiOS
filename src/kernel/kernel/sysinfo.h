#ifndef SYSINFO_H
#define SYSINFO_H

#include "../kklibc/ctypes.h"

typedef struct {
    u32 total_memory;
    u32 free_memory;
    u32 used_memory;
    u32 kernel_memory;
    u32 heap_size;
    u32 cpu_speed;    // в MHz
    char cpu_vendor[13];
    u8 cpu_cores;
} system_info_t;

// Функции для получения информации
void detect_cpu(void);
void detect_memory(void);
system_info_t* get_system_info();

#endif
