#include "memory.h"

#include "../kernel/log.h"
#include "../kklibc/kklibc.h"

#define FRAMES (PMM_LIMIT / PAGE_SIZE)
static u32 used[FRAMES / 32], owned[FRAMES / 32];
static u32 directory[1024] __attribute__((aligned(4096)));
/* Direct physical window: supervisor-only, never returned as heap memory. */
static u32 direct[64][1024] __attribute__((aligned(4096)));
static memory_region_t regions[64];
static memory_stats_t stats;
static u32 cursor = 256;

static int bit(u32* map, u32 n) {
    return (map[n / 32] >> (n % 32)) & 1;
}

static void set(u32* map, u32 n) {
    map[n / 32] |= 1u << (n % 32);
}

static void clear(u32* map, u32 n) {
    map[n / 32] &= ~(1u << (n % 32));
}

u32 pmm_alloc_page(void) {
    if (!stats.free_pages) {
        return 0;
    }
    for (u32 i = 0; i < FRAMES; ++i) {
        u32 n = (cursor + i) % FRAMES;
        if (!bit(used, n)) {
            set(used, n);
            set(owned, n);
            cursor = (n + 1) % FRAMES;
            --stats.free_pages;
            ++stats.allocated_pages;
            memset((void*)(n * PAGE_SIZE), 0, PAGE_SIZE);
            return n * PAGE_SIZE;
        }
    }
    return 0;
}

int pmm_free_page(u32 address) {
    if (!address || address >= PMM_LIMIT || address % PAGE_SIZE) {
        return 0;
    }
    u32 n = address / PAGE_SIZE;
    if (!bit(owned, n)) {
        return 0;
    }
    clear(owned, n);
    clear(used, n);
    ++stats.free_pages;
    --stats.allocated_pages;
    return 1;
}

int vmm_map_page(u32 va, u32 pa, u32 flags) {
    if (va < PMM_LIMIT || va % PAGE_SIZE || pa % PAGE_SIZE || pa >= PMM_LIMIT || (flags & ~VMM_WRITE)) {
        return 0;
    }
    u32 d = va >> 22, t = (va >> 12) & 1023;
    if (!(directory[d] & 1)) {
        u32 page = pmm_alloc_page();
        if (!page) {
            return 0;
        }
        directory[d] = page | 3;
    }
    u32* table = (u32*)(directory[d] & ~4095u);
    if (table[t] & 1) {
        return 0;
    }
    table[t] = pa | flags | 1;
    __asm__ volatile("invlpg (%0)" ::"r"(va) : "memory");
    ++stats.mapped_pages;
    return 1;
}

int vmm_unmap_page(u32 va) {
    if (va < PMM_LIMIT || va % PAGE_SIZE || !(directory[va >> 22] & 1)) {
        return 0;
    }
    u32* table = (u32*)(directory[va >> 22] & ~4095u);
    u32 t = (va >> 12) & 1023;
    if (!(table[t] & 1)) {
        return 0;
    }
    table[t] = 0;
    __asm__ volatile("invlpg (%0)" ::"r"(va) : "memory");
    --stats.mapped_pages;
    for (u32 i = 0; i < 1024; ++i) {
        if (table[i] & 1) {
            return 1;
        }
    }
    u32 physical = directory[va >> 22] & ~4095u;
    directory[va >> 22] = 0;
    pmm_free_page(physical);
    return 1;
}

u32 vmm_translate(u32 va) {
    if (!(directory[va >> 22] & 1)) {
        return 0;
    }
    u32* table = (u32*)(directory[va >> 22] & ~4095u);
    u32 entry = table[(va >> 12) & 1023];
    return entry & 1 ? (entry & ~4095u) | (va & 4095) : 0;
}

void memory_init(void) {
    memset(used, 255, sizeof(used));
    u32 count = *(volatile u32*)0x5000;
    if (!count || count > 64) {
        panic("BIOS E820 map unavailable", NULL);
    }
    stats.map_entries = count;
    memcpy(regions, (void*)0x5004, count * sizeof(memory_region_t));
    /* First release usable full pages, then reserve all overlapping firmware ranges. */
    for (u32 pass = 0; pass < 2; ++pass) {
        for (u32 i = 0; i < count; ++i) {
            memory_region_t* r = &regions[i];
            if (!(r->attributes & 1) || !r->length) {
                continue;
            }
            unsigned long long end = r->base + r->length;
            if (end < r->base) {
                continue;
            }
            if (!pass && r->type == 1) {
                unsigned long long kib = r->length >> 10;
                stats.usable_kib +=
                    kib > 0xffffffffu - stats.usable_kib ? 0xffffffffu - stats.usable_kib : (u32)kib;
            }
            if (r->base >= PMM_LIMIT) {
                continue;
            }
            if (end > PMM_LIMIT) {
                end = PMM_LIMIT;
            }
            u32 first, last;
            if (!pass) {
                if (r->type != 1) {
                    continue;
                }
                first = (u32)((r->base + 4095) >> 12);
                last = (u32)(end >> 12);
            } else {
                if (r->type == 1) {
                    continue;
                }
                first = (u32)(r->base >> 12);
                last = (u32)((end + 4095) >> 12);
            }
            for (u32 n = first; n < last; ++n) {
                if (pass) {
                    set(used, n);
                } else {
                    clear(used, n);
                }
            }
        }
    }
    /* Includes kernel/BSS, BIOS handoff, VGA, boot stack and firmware low memory. */
    for (u32 n = 0; n < 256; ++n) {
        set(used, n);
    }
    stats.managed_pages = FRAMES;
    for (u32 n = 0; n < FRAMES; ++n) {
        if (!bit(used, n)) {
            ++stats.free_pages;
        }
    }
    stats.reserved_pages = FRAMES - stats.free_pages;
    for (u32 d = 0; d < 64; ++d) {
        for (u32 t = 0; t < 1024; ++t) {
            direct[d][t] = ((d * 1024 + t) * PAGE_SIZE) | 3;
        }
        directory[d] = (u32)direct[d] | 3;
    }
    direct[0][0] = 0; /* Null dereferences fault. */
    __asm__ volatile("mov %0, %%cr3" ::"r"((u32)directory) : "memory");
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80010000u; /* PG and supervisor write protection. */
    __asm__ volatile("mov %0, %%cr0" ::"r"(cr0) : "memory");
    if (!stats.free_pages) {
        panic("No usable physical pages", NULL);
    }
    log_write("[INFO] E820 PMM VMM paging ready\n");
}

memory_stats_t memory_stats(void) {
    return stats;
}

u32 vmm_space_create(void) {
    u32 space = pmm_alloc_page();
    if (!space) {
        return 0;
    }
    memcpy((void*)space, directory, sizeof(directory));
    memset((u32*)space + USER_BASE / 0x400000, 0, (USER_TOP - USER_BASE) / 0x400000 * 4);
    return space;
}

void vmm_space_switch(u32 space) {
    if (!space) {
        space = (u32)directory;
    }
    __asm__ volatile("mov %0, %%cr3" ::"r"(space) : "memory");
}

u32 vmm_user_physical(u32 space, u32 address, int write) {
    if (!space || address < USER_BASE || address >= USER_TOP) {
        return 0;
    }
    u32 pde = ((u32*)space)[address >> 22];
    if ((pde & 5) != 5) {
        return 0;
    }
    u32 pte = ((u32*)(pde & ~4095u))[(address >> 12) & 1023];
    if ((pte & 5) != 5 || (write && !(pte & 2))) {
        return 0;
    }
    return (pte & ~4095u) | (address & 4095);
}

int vmm_user_page(u32 space, u32 address, int writable) {
    if (!space || address < USER_BASE || address >= USER_TOP || (address & 4095)) {
        return 0;
    }
    u32* pd = (u32*)space;
    if (!(pd[address >> 22] & 1)) {
        u32 table = pmm_alloc_page();
        if (!table) {
            return 0;
        }
        pd[address >> 22] = table | 7;
    }
    u32* entry = (u32*)(pd[address >> 22] & ~4095u) + ((address >> 12) & 1023);
    if (*entry & 1) {
        return 0;
    }
    u32 page = pmm_alloc_page();
    if (!page) {
        return 0;
    }
    *entry = page | 5 | (writable ? 2 : 0);
    return 1;
}

void vmm_space_destroy(u32 space) {
    if (!space) {
        return;
    }
    u32* pd = (u32*)space;
    for (u32 d = USER_BASE >> 22; d < USER_TOP >> 22; ++d) {
        if (!(pd[d] & 1)) {
            continue;
        }
        u32* table = (u32*)(pd[d] & ~4095u);
        for (u32 t = 0; t < 1024; ++t) {
            if (table[t] & 1) {
                pmm_free_page(table[t] & ~4095u);
            }
        }
        pmm_free_page(pd[d] & ~4095u);
    }
    pmm_free_page(space);
}

void memory_print_info(void) {
    printf("RAM usable (E820): %u KiB; map entries: %u\n", stats.usable_kib, stats.map_entries);
    printf("PMM limit: %u MiB; page size: 4096\n", PMM_LIMIT >> 20);
    printf(
        "PMM pages: free=%u allocated=%u unavailable/reserved=%u\n",
        stats.free_pages,
        stats.allocated_pages,
        stats.reserved_pages);
    printf("Paging: enabled, supervisor; dynamic mappings=%u\n", stats.mapped_pages);
}

void memory_print_map(void) {
    for (u32 i = 0; i < stats.map_entries; ++i) {
        memory_region_t* r = &regions[i];
        printf(
            "%u base=%08x:%08x size=%08x:%08x type=%u attr=%x\n",
            i,
            (u32)(r->base >> 32),
            (u32)r->base,
            (u32)(r->length >> 32),
            (u32)r->length,
            r->type,
            r->attributes);
    }
}
