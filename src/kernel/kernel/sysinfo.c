#include "sysinfo.h"

#include "../mm/heap.h"
#include "../kklibc/stdio.h"
#include "../kklibc/stdlib.h"
#include "../mm/memory.h"

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
    memory_stats_t physical = memory_stats();
    sys_info.total_memory = physical.usable_kib > 0x3fffff ? 0xfffffc00u : physical.usable_kib * 1024;
    sys_info.used_memory = physical.allocated_pages * PAGE_SIZE;
    sys_info.free_memory = physical.free_pages * PAGE_SIZE;

    extern char __bss_end[];
    sys_info.kernel_memory = (u32)__bss_end - 0x7e00;
    sys_info.heap_size = info.heap_size;
    sys_info.heap_used = info.total_used;
    sys_info.heap_free = info.total_free;

}

system_info_t* get_system_info() {
    detect_memory();
    return &sys_info;
}
