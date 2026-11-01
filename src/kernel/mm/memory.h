#ifndef MM_MEMORY_H
#define MM_MEMORY_H
#include "../kklibc/ctypes.h"
#define PAGE_SIZE 4096u
#define PMM_LIMIT 0x10000000u
#define VMM_WRITE 2u

typedef struct __attribute__((packed)) {
    unsigned long long base, length;
    u32 type, attributes;
} memory_region_t;

typedef struct {
    u32 managed_pages, free_pages, allocated_pages, reserved_pages;
    u32 usable_kib, mapped_pages, map_entries;
} memory_stats_t;

void memory_init(void);
u32 pmm_alloc_page(void);
int pmm_free_page(u32 address);
int vmm_map_page(u32 virtual_address, u32 physical_address, u32 flags);
int vmm_unmap_page(u32 virtual_address);
u32 vmm_translate(u32 address);
memory_stats_t memory_stats(void);
void memory_print_info(void);
void memory_print_map(void);
#endif
