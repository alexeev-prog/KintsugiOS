#include "sysinfo.h"
#include "../kklibc/mem.h"
#include "../kklibc/paging.h"
#include "../kklibc/stdlib.h"

static system_info_t sys_info;

void detect_cpu(void) {
    u32 eax, ebx, ecx, edx;
    char vendor[13];

    // Получаем vendor string
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(0));

    *((u32*)vendor) = ebx;
    *((u32*)(vendor + 4)) = edx;
    *((u32*)(vendor + 8)) = ecx;
    vendor[12] = '\0';

    strcpy(sys_info.cpu_vendor, vendor);

    // Получаем информацию о функциях процессора
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(1));

    // упрощение
    sys_info.cpu_cores = 1;
}

void detect_memory() {
    // Используем битовую карту фреймов из пейджинга для определения памяти
    sys_info.total_memory = nframes * PAGE_SIZE;
    sys_info.free_memory = 0;

    // Подсчитываем свободные фреймы
    for (u32 i = 0; i < nframes; i++) {
        if (!test_frame(i * PAGE_SIZE)) {
            sys_info.free_memory += PAGE_SIZE;
        }
    }

    sys_info.used_memory = sys_info.total_memory - sys_info.free_memory;
    sys_info.kernel_memory = free_mem_addr - HEAP_START;
    sys_info.heap_size = HEAP_SIZE;
}

system_info_t* get_system_info() {
    detect_cpu();
    detect_memory();
    return &sys_info;
}
