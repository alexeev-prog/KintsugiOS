#include "sysinfo.h"

#include "../kklibc/mem.h"
#include "../kklibc/paging.h"
#include "../kklibc/stdio.h"
#include "../kklibc/stdlib.h"

static system_info_t sys_info;

/**
 * @brief Обнаружение информации о процессоре
 */
void detect_cpu(void) {
    u32 eax, ebx, ecx, edx;
    char vendor[13];

    // Получаем информацию о вендоре
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));

    *((u32*)vendor) = ebx;
    *((u32*)(vendor + 4)) = edx;
    *((u32*)(vendor + 8)) = ecx;
    vendor[12] = '\0';

    strcpy(sys_info.cpu_vendor, vendor);

    // Получаем дополнительную информацию о процессоре
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));

    // Упрощение - предполагаем 1 ядро
    sys_info.cpu_cores = 1;
}

/**
 * @brief Обнаружение информации о памяти
 */
void detect_memory() {
    // Используем битовую карту фреймов из пейджинга для определения памяти
    sys_info.total_memory = nframes * PAGE_SIZE;

    // Получаем информацию о куче
    meminfo_t info = get_meminfo();
    sys_info.free_memory = info.total_free;
    sys_info.used_memory = info.total_used;
    sys_info.kernel_memory = free_mem_addr - HEAP_START;
    sys_info.heap_size = heap_current_end - HEAP_START;

    // Более точный подсчет использования памяти
    u32 used_frames = 0;
    for (u32 i = 0; i < nframes; i++) {
        if (test_frame(i * PAGE_SIZE)) {
            used_frames++;
        }
    }
    sys_info.used_memory = used_frames * PAGE_SIZE;
    sys_info.free_memory = sys_info.total_memory - sys_info.used_memory;
}

/**
 * @brief Получение системной информации
 *
 * @return system_info_t*
 */
system_info_t* get_system_info() {
    // Обновляем информацию при каждом запросе
    detect_memory();
    return &sys_info;
}
