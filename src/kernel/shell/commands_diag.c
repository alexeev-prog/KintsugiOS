#include "../kernel/log.h"
#include "../kernel/sysinfo.h"
#include "../kklibc/kklibc.h"
#include "shell.h"

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
    { "info", "diag", "info", "System information", info },
    { "memdump", "memory", "memdump", "Inspect heap", memory },
    { "malloc", "memory", "malloc <bytes>", "Allocate tracked diagnostic block", allocate },
    { "free", "memory", "free <allocation-id>", "Free tracked diagnostic block", release },
    { "status", "diag", "status", "Previous command exit code", status },
    { "panic", "diag", "panic confirm", "Test fatal diagnostics (halts CPU)", panic_test },
    { "fault", "diag", "fault confirm", "Test invalid-opcode exception", exception_test },
    { 0 }
};
