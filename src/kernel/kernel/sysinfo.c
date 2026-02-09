#include "sysinfo.h"

#include "../kklibc/mem.h"
#include "../kklibc/stdio.h"
#include "../kklibc/stdlib.h"

static system_info_t sys_info;

void detect_cpu(void) {
    u32 eax, ebx, ecx, edx;
    char vendor[13];

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    *((u32*)vendor) = ebx;
    *((u32*)(vendor + 4)) = edx;
    *((u32*)(vendor + 8)) = ecx;
    vendor[12] = '\0';
    strcpy(sys_info.cpu_vendor, vendor);

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    sys_info.cpu_cores = 1;
    printf("CPU detected: %s\n", vendor);
}

void detect_memory() {
    meminfo_t info = get_meminfo();
    sys_info.total_memory = HEAP_START + HEAP_SIZE;
    sys_info.used_memory = info.total_used + (HEAP_START - 0x1000);
    sys_info.free_memory = info.total_free;

    sys_info.kernel_memory = HEAP_START - 0x1000;
    sys_info.heap_size = info.heap_size;
    sys_info.heap_used = info.total_used;
    sys_info.heap_free = info.total_free;

    printf(
        "Memory detected: total=%d; used=%d; free=%d\n",
        HEAP_START + HEAP_SIZE,
        info.total_used + (HEAP_START - 0x1000),
        info.total_free);
}

system_info_t* get_system_info() {
    detect_memory();
    return &sys_info;
}
