#include "../kernel/log.h"
#include "../kernel/sysinfo.h"
#include "../kklibc/kklibc.h"
#include "shell.h"
#include "../mm/memory.h"

static int meminfo(int argc, char** argv) {
    UNUSED(argv);
    if (argc != 1) return SHELL_USAGE;
    memory_print_info();
    meminfo_t h = get_meminfo();
    printf("Heap committed=%u used=%u free=%u largest=%u live=%u\n", h.heap_size, h.total_used, h.total_free, h.largest_free, h.leak_count);
    return SHELL_OK;
}
static int memmap(int argc, char** argv) {
    UNUSED(argv);
    if (argc != 1) return SHELL_USAGE;
    memory_print_map(); return SHELL_OK;
}
static int memtest(int argc, char** argv) {
    UNUSED(argv);
    if (argc != 1) return SHELL_USAGE;
    memory_stats_t before = memory_stats();
    u32 physical = pmm_alloc_page();
    if (!physical) return SHELL_ERROR;
    const u32 test_va = 0xd0000000u;
    if (!vmm_map_page(test_va, physical, VMM_WRITE)) { pmm_free_page(physical); return SHELL_ERROR; }
    *(volatile u32*)test_va = 0x1234abcd;
    int ok = *(volatile u32*)physical == 0x1234abcd && vmm_translate(test_va + 12) == physical + 12;
    ok &= vmm_unmap_page(test_va);
    ok &= !vmm_translate(test_va);
    ok &= pmm_free_page(physical);
    ok &= !pmm_free_page(physical);
    ok &= memory_stats().free_pages == before.free_pages;
    meminfo_t heap_before = get_meminfo();
    u8* a = kmalloc(100000);
    if (!a) return SHELL_ERROR;
    for (u32 i = 0; i < 100000; ++i) a[i] = (u8)i;
    u8* b = krealloc(a, 200000);
    if (!b) { kfree(a); return SHELL_ERROR; }
    for (u32 i = 0; i < 100000; ++i) if (b[i] != (u8)i) ok = 0;
    ok &= !((u32)b & 15);
    kfree(b);
    void* aligned = kmalloc_aligned(5000, PAGE_SIZE);
    ok &= aligned && !((u32)aligned & (PAGE_SIZE - 1));
    kfree_aligned(aligned);
    meminfo_t after = get_meminfo();
    ok &= after.total_used == heap_before.total_used && after.leak_count == heap_before.leak_count;
    printf("Memory self-test: %s\n", ok ? "PASS" : "FAIL");
    return ok ? SHELL_OK : SHELL_ERROR;
}

static int memory(int argc, char** argv) {
    UNUSED(argv);
    if (argc != 1) {
        return SHELL_USAGE;
    }
    kmemdump();
    return SHELL_OK;
}

static int info(int argc, char** argv) {
    UNUSED(argv);
    if (argc != 1) {
        return SHELL_USAGE;
    }
    system_info_t* info = get_system_info();
    printf(
        "KintsugiOS %s\nCPU: %s\nHeap: %d used, %d free\n",
        VERSION,
        info->cpu_vendor,
        info->heap_used,
        info->heap_free);
    memory_print_info();
    return SHELL_OK;
}

static int status(int argc, char** argv) {
    UNUSED(argv);
    if (argc != 1) {
        return SHELL_USAGE;
    }
    printf("status=%d\n", shell_last_status());
    return SHELL_OK;
}

static int panic_test(int argc, char** argv) {
    if (argc != 2 || strcmp(argv[1], "confirm")) {
        return SHELL_USAGE;
    }
    panic("Requested from shell", NULL);
}

/* Only explicitly tracked diagnostic allocations may be freed by the shell. */
static void* allocations[16];

static int allocate(int argc, char** argv) {
    if (argc != 2) {
        return SHELL_USAGE;
    }
    int size = strtoint(argv[1]);
    if (size <= 0) {
        return SHELL_USAGE;
    }
    for (u32 i = 0; i < sizeof(allocations) / sizeof(allocations[0]); ++i) {
        if (!allocations[i]) {
            allocations[i] = kmalloc(size);
            if (!allocations[i]) {
                return SHELL_ERROR;
            }
            printf("allocation=%d address=%x size=%d\n", i, (u32)allocations[i], size);
            return SHELL_OK;
        }
    }
    return SHELL_ERROR;
}

static int release(int argc, char** argv) {
    if (argc != 2) {
        return SHELL_USAGE;
    }
    int slot = strtoint(argv[1]);
    if (slot < 0 || slot >= 16 || !allocations[slot]) {
        return SHELL_ERROR;
    }
    kfree(allocations[slot]);
    allocations[slot] = NULL;
    return SHELL_OK;
}

static int exception_test(int argc, char** argv) {
    if (argc != 2 || strcmp(argv[1], "confirm")) {
        return SHELL_USAGE;
    }
    __asm__ volatile("ud2");
    return SHELL_ERROR;
}

const shell_command_t shell_diag_commands[] = {
    { "memtest", "memory", "memtest", "Test PMM, VMM and growing heap", memtest },
    { "meminfo", "memory", "meminfo", "Physical pages, paging and heap statistics", meminfo },
    { "memmap", "memory", "memmap", "BIOS E820 physical memory map", memmap },
    { "info", "diag", "info", "System information", info },
    { "memdump", "memory", "memdump", "Inspect heap", memory },
    { "malloc", "memory", "malloc <bytes>", "Allocate tracked diagnostic block", allocate },
    { "free", "memory", "free <allocation-id>", "Free tracked diagnostic block", release },
    { "status", "diag", "status", "Previous command exit code", status },
    { "panic", "diag", "panic confirm", "Test fatal diagnostics (halts CPU)", panic_test },
    { "fault", "diag", "fault confirm", "Test invalid-opcode exception", exception_test },
    { 0 }
};
