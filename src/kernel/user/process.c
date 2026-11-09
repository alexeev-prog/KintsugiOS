#include "process.h"

#include "../../user/envabi.h"
#include "../../user/shellabi.h"
#include "../../user/sysabi.h"
#include "../../user/tuiabi.h"
#include "../cpu/idt.h"
#include "../drivers/ata_pio.h"
#include "../drivers/keyboard.h"
#include "../drivers/terminal.h"
#include "../fs/fat12.h"
#include "../fs/vfs.h"
#include "../kernel/log.h"
#include "../kernel/power.h"
#include "../kklibc/kklibc.h"
#include "../mm/memory.h"
#include "../shell/shell.h"
#include "fd.h"

#define STACK_BOTTOM (USER_TOP - 9 * PAGE_SIZE)
#define STACK_START (STACK_BOTTOM + PAGE_SIZE)
static u8 kernel_stack[16384] __attribute__((aligned(16)));
static u32 gdt[12] __attribute__((aligned(8)));
static u32 tss[26];
#define PROCESS_MAX 16

enum {
    UNUSED_PROCESS,
    READY,
    RUNNING,
    WAITING,
    ZOMBIE
};

typedef struct {
    u32 pid, parent, state, address_space, heap_end, context, entry, stack, ticks;
    int status, interrupted, orphan;
    terminal_state_t* terminal_snapshot;
    void* kernel_stack;
    fd_table_t descriptors;
    const void* waiting_on;
    u32 group;
    int shell;
    char name[32];
    char cwd[VFS_PATH_MAX];
    kos_env_entry* environment;
} process_t;

static process_t processes[PROCESS_MAX];
static process_t* current;
static u32 next_pid = 1, shell_context, foreground;
#define space (current->address_space)
#define saved_terminal (current->terminal_snapshot)
#define user_heap_end (current->heap_end)
extern void process_switch(u32* old, u32 next);
static void schedule(void);
static void reap(process_t* p);
static u32 init_pid;
static u32 shell_pid;

void process_shutdown(void) {
    /* Single CPU, interrupts disabled by the power path: no new userspace work. */
    for (int i = 0; i < PROCESS_MAX; ++i) {
        if (&processes[i] != current) {
            reap(&processes[i]);
        }
    }
    if (current) {
        fd_destroy(&current->descriptors);
    }
}

char* process_cwd(void) {
    return current ? current->cwd : NULL;
}

void process_wake(const void* object) {
    for (int i = 0; i < PROCESS_MAX; ++i) {
        process_t* p = &processes[i];
        if (p->state == WAITING && p->waiting_on == object) {
            p->waiting_on = NULL;
            p->state = READY;
        }
    }
}

void process_block(const void* object) {
    current->waiting_on = object;
    current->state = WAITING;
    schedule();
    process_check_interrupt();
}

#define USER_HEAP_BASE 0x60000000u
#define USER_HEAP_LIMIT 0x61000000u
extern int user_enter(u32 entry, u32 stack);
extern void user_leave(int status) __attribute__((noreturn));
extern void syscall_entry(void);

void process_init(void) {
    gdt[2] = 0xffff;
    gdt[3] = 0x00cf9a00;
    gdt[4] = 0xffff;
    gdt[5] = 0x00cf9200;
    gdt[6] = 0xffff;
    gdt[7] = 0x00cffa00;
    gdt[8] = 0xffff;
    gdt[9] = 0x00cff200;
    tss[1] = (u32)kernel_stack + sizeof(kernel_stack);
    tss[2] = 0x10;
    tss[25] = sizeof(tss) << 16; /* No I/O bitmap: all user port access denied. */
    u32 base = (u32)tss, limit = sizeof(tss) - 1;
    gdt[10] = (limit & 0xffff) | (base << 16);
    gdt[11] = ((base >> 16) & 255) | 0x8900 | (base & 0xff000000);

    struct __attribute__((packed)) {
        u16 limit;
        u32 base;
    } gdtr = { sizeof(gdt) - 1, (u32)gdt };

    __asm__ volatile("lgdt %0" ::"m"(gdtr) : "memory");
    u16 selector = 0x28;
    __asm__ volatile("ltr %0" ::"r"(selector));
    set_idt_gate(128, (u32)syscall_entry);
    idt[128].flags = 0xee;
    log_write("[INFO] ring3 TSS syscall ready\n");
}

int process_active(void) {
    return current != NULL;
}

int process_foreground(void) {
    return current && current->group == foreground;
}

void process_exit(int status) {
    char message[64];
    snprintf(message, sizeof(message), "[PROCESS] exit status=%d\n", status);
    log_write(message);
    __asm__ volatile("cli" ::: "memory");
    fd_destroy(&current->descriptors);
    current->status = status;
    current->state = ZOMBIE;
    for (int i = 0; i < PROCESS_MAX; ++i) {
        if (processes[i].pid == current->parent && processes[i].state == WAITING) {
            processes[i].state = READY;
        }
        if (processes[i].parent == current->pid) {
            processes[i].parent = processes[0].pid;
            processes[i].orphan = 1;
        }
    }
    schedule();
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

static void schedule(void) {
    process_t* old = current;
    /* A dead process cannot free the kernel stack it is still executing on. */
    for (int i = 1; i < PROCESS_MAX; ++i) {
        if (&processes[i] != old && processes[i].state == ZOMBIE && processes[i].orphan) {
            reap(&processes[i]);
        }
    }
    int start = old ? (int)(old - processes) + 1 : 0;
    for (;;) {
        if (old && processes[0].state == ZOMBIE) {
            current = NULL;
            vmm_space_switch(0);
            process_switch(&old->context, shell_context);
        }
        for (int n = 0; n < PROCESS_MAX; ++n) {
            process_t* p = &processes[(start + n) % PROCESS_MAX];
            if (p->state != READY) {
                continue;
            }
            if (p == old) {
                p->state = RUNNING;
                return;
            }
            if (old && old->state == RUNNING) {
                old->state = READY;
            }
            current = p;
            p->state = RUNNING;
            tss[1] = (u32)p->kernel_stack + 16384;
            vmm_space_switch(p->address_space);
            process_switch(old ? &old->context : &shell_context, p->context);
            return;
        }
        if (old && old->state == RUNNING) {
            return;
        }
        if (processes[0].state == ZOMBIE) {
            current = NULL;
            vmm_space_switch(0);
            process_switch(&old->context, shell_context);
        }
        __asm__ volatile("sti; hlt; cli" ::: "memory");
    }
}

void process_tick(int user_mode) {
    process_wake(NULL); /* Terminal input waiters recheck after an IRQ/tick. */
    if (!current) {
        return;
    }
    ++current->ticks;
    if (user_mode && current->interrupted) {
        process_exit(130);
    }
    if (user_mode && current->ticks % 5 == 0) {
        schedule();
    }
}

int process_interrupt(void) {
    if (!current || foreground == init_pid || foreground == shell_pid) {
        return 0;
    }
    for (int i = 0; i < PROCESS_MAX; ++i) {
        if (processes[i].group == foreground) {
            processes[i].interrupted = 1;
            if (processes[i].state == WAITING) {
                processes[i].state = READY;
            }
        }
    }
    return 1;
}

static void process_start(void) {
    user_enter(current->entry, current->stack);
    process_exit(0);
}

void process_check_interrupt(void) {
    if (current && current->interrupted) {
        process_exit(130);
    }
}

typedef struct __attribute__((packed)) {
    u8 ident[16];
    u16 type, machine;
    u32 version, entry, phoff, shoff, flags;
    u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} elf_header_t;

typedef struct {
    u32 type, offset, vaddr, paddr, filesz, memsz, flags, align;
} elf_segment_t;

static int user_copy(u32 address, const void* source, u32 length) {
    const u8* bytes = source;
    while (length) {
        u32 physical = vmm_user_physical(space, address, 0);
        if (!physical) {
            return 0;
        }
        u32 n = PAGE_SIZE - (address & 4095);
        if (n > length) {
            n = length;
        }
        memcpy((void*)physical, bytes, n);
        address += n;
        bytes += n;
        length -= n;
    }
    return 1;
}

static int process_load(const char* path, int argc, char** argv) {
    if (!current->environment) {
        current->environment = kmalloc(KOS_ENV_MAX * sizeof(kos_env_entry));
        if (!current->environment) {
            return VFS_ENOMEM;
        }
        memset(current->environment, 0, KOS_ENV_MAX * sizeof(kos_env_entry));
    }
    log_event("DEBUG", "elf", "Loading %s argc=%d", path, argc);
    if (argc < 1 || argc > 32) {
        return VFS_EINVAL;
    }
    vfs_stat_t stat;
    int result = vfs_stat(path, &stat);
    if (result) {
        return result;
    }
    if (stat.directory || stat.size < sizeof(elf_header_t) || stat.size > 1024 * 1024) {
        return VFS_EINVAL;
    }
    u8* image = kmalloc(stat.size);
    if (!image) {
        return VFS_ENOMEM;
    }
    result = vfs_read(path, 0, image, stat.size);
    if (result < 0 || (u32)result != stat.size) {
        kfree(image);
        return VFS_EIO;
    }
    elf_header_t* header = (elf_header_t*)image;
    if (memcmp(header->ident, "\177ELF\1\1\1", 7) || header->type != 2 || header->machine != 3
        || header->version != 1 || header->ehsize != sizeof(*header)
        || header->phentsize != sizeof(elf_segment_t) || !header->phnum || header->phnum > 32
        || header->phoff > stat.size || header->phnum * sizeof(elf_segment_t) > stat.size - header->phoff) {
        kfree(image);
        return VFS_EINVAL;
    }
    space = vmm_space_create();
    current->interrupted = 0;
    user_heap_end = USER_HEAP_BASE;
    if (!space) {
        kfree(image);
        return VFS_ENOMEM;
    }
    result = VFS_EINVAL;
    int executable_entry = 0;
    u32 total = 0;
    for (u32 i = 0; i < header->phnum; ++i) {
        elf_segment_t* s = (elf_segment_t*)(image + header->phoff) + i;
        if (s->type == 2 || s->type == 3) {
            goto cleanup; /* No dynamic linker. */
        }
        if (s->type != 1 || !s->memsz) {
            continue;
        }
        if (s->filesz > s->memsz || s->offset > stat.size || s->filesz > stat.size - s->offset
            || s->vaddr < USER_BASE || s->vaddr >= USER_HEAP_BASE || s->memsz > USER_HEAP_BASE - s->vaddr
            || (s->flags & 3) == 3 || s->memsz > 8 * 1024 * 1024 - total
            || (s->align > 1
                && ((s->align & (s->align - 1)) || (s->vaddr % s->align != s->offset % s->align)))) {
            goto cleanup;
        }
        total += s->memsz;
        /* Reject overlapping load pages, including conflicting permissions. */
        for (u32 j = 0; j < i; ++j) {
            elf_segment_t* previous = (elf_segment_t*)(image + header->phoff) + j;
            if (previous->type != 1 || !previous->memsz) {
                continue;
            }
            u32 begin = s->vaddr & ~(PAGE_SIZE - 1);
            u32 end = (s->vaddr + s->memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            u32 other_begin = previous->vaddr & ~(PAGE_SIZE - 1);
            u32 other_end = (previous->vaddr + previous->memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            if (begin < other_end && other_begin < end) {
                goto cleanup;
            }
        }
        if ((s->flags & 1) && header->entry >= s->vaddr && header->entry - s->vaddr < s->memsz) {
            executable_entry = 1;
        }
        u32 end = (s->vaddr + s->memsz + 4095) & ~4095u;
        for (u32 va = s->vaddr & ~4095u; va < end; va += PAGE_SIZE) {
            if (!vmm_user_page(space, va, !!(s->flags & 2))) {
                result = VFS_ENOMEM;
                goto cleanup;
            }
        }
        if (!user_copy(s->vaddr, image + s->offset, s->filesz)) {
            goto cleanup;
        }
    }
    if (!executable_entry) {
        goto cleanup;
    }
    for (u32 va = STACK_START; va < USER_TOP; va += PAGE_SIZE) {
        if (!vmm_user_page(space, va, 1)) {
            result = VFS_ENOMEM;
            goto cleanup;
        }
    }
    u32 sp = USER_TOP, arguments[33];
    for (int i = argc - 1; i >= 0; --i) {
        u32 n = strlen(argv[i]) + 1;
        if (n > 1024 || n > sp - STACK_START) {
            goto cleanup;
        }
        sp -= n;
        arguments[i] = sp;
        if (!user_copy(sp, argv[i], n)) {
            goto cleanup;
        }
    }
    arguments[argc] = 0;
    sp &= ~15u;
    if (sp - STACK_START < (u32)(argc + 4) * 4) {
        goto cleanup;
    }
    sp -= (argc + 1) * 4;
    user_copy(sp, arguments, (argc + 1) * 4);
    u32 initial[2] = { argc, sp };
    sp -= 8;
    user_copy(sp, initial, 8);
    u32 entry = header->entry;
    kfree(image);
    image = NULL;
    current->kernel_stack = kmalloc(16384);
    if (!current->kernel_stack) {
        result = VFS_ENOMEM;
        goto cleanup;
    }
    current->entry = entry;
    log_event(
        "INFO", "elf", "pid=%u loaded %s entry=%x mapped payload=%u bytes", current->pid, path, entry, total);
    const char* basename = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '/') {
            basename = p + 1;
        }
    }
    u32 name_length = strlen(basename);
    if (name_length >= sizeof(current->name)) {
        name_length = sizeof(current->name) - 1;
    }
    memcpy(current->name, basename, name_length);
    for (u32 i = 0; i < name_length; ++i) {
        current->name[i] = toupper(current->name[i]);
    }
    current->name[name_length] = 0;
    /* Capability is assigned by the loader, never by a user request. */
    char executable[VFS_PATH_MAX];
    current->shell = !vfs_normalize(path, executable) && !strcmp(executable, "/BIN/SHELL.ELF");
    current->stack = sp;
    u32* context = (u32*)((u32)current->kernel_stack + 16384);
    *--context = (u32)process_start;
    for (int i = 0; i < 4; ++i) {
        *--context = 0;
    }
    current->context = (u32)context;
    current->state = READY;
    return 0;
cleanup:
    vmm_space_destroy(space);
    space = 0;
    kfree(image);
    return result;
}

static void reap(process_t* p) {
    kfree(p->environment);
    fd_destroy(&p->descriptors);
    vmm_space_destroy(p->address_space);
    kfree(p->kernel_stack);
    if (p->terminal_snapshot) {
        *terminal_get_state() = *p->terminal_snapshot;
        terminal_get_state()->dirty = 1;
        terminal_refresh();
        kfree(p->terminal_snapshot);
    }
    memset(p, 0, sizeof(*p));
}

int process_run(const char* path, int argc, char** argv) {
    if (current && (current->pid == init_pid || current->shell)) {
        char* paths[] = { (char*)path };
        int counts[] = { argc };
        char** args[] = { argv };
        return process_pipeline(1, paths, counts, args, NULL, 0, 0);
    }
    if (current) {
        return VFS_EINVAL;
    }
    __asm__ volatile("cli" ::: "memory");
    current = &processes[0];
    strcpy(current->cwd, vfs_getcwd());
    current->pid = next_pid++;
    current->group = current->pid;
    int result = process_load(path, argc, argv);
    if (!result) {
        result = fd_init(&current->descriptors);
    }
    current = NULL;
    if (!result) {
        foreground = processes[0].pid;
        schedule();
        result = processes[0].status;
    }
    for (int i = PROCESS_MAX - 1; i >= 0; --i) {
        reap(&processes[i]);
    }
    foreground = 0;
    __asm__ volatile("sti" ::: "memory");
    return result;
}

int process_pipeline(
    int count, char** paths, int* argc, char*** argv, const char* output, int append, int background) {
    if ((current && current->pid != init_pid && !current->shell) || count < 1 || count > 8) {
        return VFS_EINVAL;
    }
    __asm__ volatile("cli" ::: "memory");
    process_t* parent = current;
    process_t* children[8] = { 0 };
    char inherited_cwd[VFS_PATH_MAX];
    strcpy(inherited_cwd, vfs_getcwd());
    fd_table_t setup;
    int result = fd_init(&setup), previous = -1;
    u32 group = next_pid;
    for (int i = 0; !result && i < count; ++i) {
        int pair[2] = { -1, -1 };
        if (i + 1 < count) {
            result = fd_pipe(&setup, pair);
        }
        if (result) {
            break;
        }
        process_t* slot = NULL;
        for (int n = parent ? 1 : 0; n < PROCESS_MAX; ++n) {
            if (!processes[n].state && !processes[n].pid) {
                slot = &processes[n];
                break;
            }
        }
        if (!slot) {
            result = VFS_ENOMEM;
            break;
        }
        current = children[i] = slot;
        strcpy(current->cwd, inherited_cwd);
        current->pid = next_pid++;
        current->parent = parent ? parent->pid : 0;
        current->group = group;
        result = process_load(paths[i], argc[i], argv[i]);
        if (!result && parent) {
            memcpy(current->environment, parent->environment, KOS_ENV_MAX * sizeof(kos_env_entry));
        }
        if (!result) {
            fd_clone(&current->descriptors, &setup);
            if (previous >= 0) {
                fd_dup2(&current->descriptors, previous, 0);
            }
            if (pair[1] >= 0) {
                fd_dup2(&current->descriptors, pair[1], 1);
            }
            for (int fd = 3; fd < KOS_FD_MAX; ++fd) {
                if (current->descriptors.entries[fd]) {
                    fd_close(&current->descriptors, fd);
                }
            }
        }
        current = NULL;
        if (previous >= 0) {
            fd_close(&setup, previous);
        }
        if (pair[1] >= 0) {
            fd_close(&setup, pair[1]);
        }
        previous = pair[0];
    }
    current = parent;
    if (!result && output) {
        vfs_stat_t stat;
        int exists = vfs_stat(output, &stat);
        if (exists && exists != VFS_ENOENT) {
            result = exists;
        } else if (!append || exists == VFS_ENOENT) {
            result = vfs_replace(output, "", 0);
        }
        if (!result) {
            fd_table_t* table = &children[count - 1]->descriptors;
            int fd = fd_open(table, output, KOS_O_WRITE | (append ? KOS_O_APPEND : 0));
            if (fd < 0) {
                result = fd;
            } else {
                fd_dup2(table, fd, 1);
                fd_close(table, fd);
            }
        }
    }
    fd_destroy(&setup);
    if (parent) {
        current = parent;
        if (!result && background) {
            printf("[job %d] started\n", group);
        } else if (!result) {
            foreground = group;
            for (int i = 0; i < count; ++i) {
                while (children[i]->state != ZOMBIE) {
                    parent->state = WAITING;
                    schedule();
                }
            }
            result = children[count - 1]->status;
            foreground = parent->group;
        }
        if (result < 0 || !background) {
            for (int i = 0; i < count; ++i) {
                if (children[i]) {
                    reap(children[i]);
                }
            }
        }
        __asm__ volatile("sti" ::: "memory");
        return result;
    }
    if (!result) {
        foreground = group;
        /* Root is last stage so session cleanup cannot kill its producers early. */
        process_t swap = processes[0];
        processes[0] = processes[count - 1];
        processes[count - 1] = swap;
        schedule();
        result = processes[0].status;
    }
    current = NULL;
    vmm_space_switch(0);
    for (int i = PROCESS_MAX - 1; i >= 0; --i) {
        reap(&processes[i]);
    }
    foreground = 0;
    __asm__ volatile("sti" ::: "memory");
    return result;
}

void process_jobs(void) {
    printf("PID GROUP STATE STATUS\n");
    for (int i = 1; i < PROCESS_MAX; ++i) {
        process_t* p = &processes[i];
        if (p->state && current && p->parent == current->pid) {
            printf("%d %d %s %d\n", p->pid, p->group, p->state == ZOMBIE ? "done" : "running", p->status);
        }
    }
}

int process_wait_jobs(void) {
    int status = 0;
    if (!current || (current->pid != init_pid && !current->shell)) {
        return VFS_EINVAL;
    }
    __asm__ volatile("cli" ::: "memory");
    for (int i = 1; i < PROCESS_MAX; ++i) {
        process_t* p = &processes[i];
        if (!p->state || p->parent != current->pid) {
            continue;
        }
        while (p->state != ZOMBIE) {
            current->state = WAITING;
            schedule();
        }
        status = p->status;
        reap(p);
    }
    __asm__ volatile("sti" ::: "memory");
    return status;
}

int process_boot_init(void) {
    char* argv[] = { "init", NULL };
    init_pid = next_pid;
    return process_run("/BIN/INIT.ELF", 1, argv);
}

/* ABI: eax=number, ebx/ecx/edx/esi=arguments, eax=result. */
typedef struct {
    u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;
} syscall_regs_t;

static int user_range(u32 address, u32 length, int write) {
    if (address < USER_BASE || address >= USER_TOP || length > USER_TOP - address) {
        return 0;
    }
    while (length) {
        if (!vmm_user_physical(space, address, write)) {
            return 0;
        }
        u32 n = PAGE_SIZE - (address & 4095);
        if (n > length) {
            n = length;
        }
        address += n;
        length -= n;
    }
    return 1;
}

static int user_path(u32 address, char path[VFS_PATH_MAX]) {
    for (u32 i = 0; i < VFS_PATH_MAX; ++i) {
        if (!user_range(address + i, 1, 0)) {
            return 0;
        }
        path[i] = *(const char*)(address + i);
        if (!path[i]) {
            return 1;
        }
    }
    return 0;
}

void syscall_dispatch(syscall_regs_t* r) {
    process_check_interrupt();
    if (!space) {
        r->eax = VFS_EINVAL;
        return;
    }
    char path[VFS_PATH_MAX];
    switch (r->eax) {
        case KOS_SYS_ENV_GET:
            if (!user_range(r->ebx, KOS_ENV_MAX * sizeof(kos_env_entry), 1)) {
                break;
            }
            memcpy((void*)r->ebx, current->environment, KOS_ENV_MAX * sizeof(kos_env_entry));
            r->eax = 0;
            return;
        case KOS_SYS_ENV_SET: {
            if (!user_range(r->ebx, sizeof(kos_env_entry), 0)) {
                break;
            }
            kos_env_entry entry = *(kos_env_entry*)r->ebx;
            u32 n = 0, v = 0;
            while (n < KOS_ENV_NAME && entry.name[n]) {
                char c = entry.name[n];
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'
                      || (n && c >= '0' && c <= '9'))) {
                    break;
                }
                ++n;
            }
            while (v < KOS_ENV_VALUE && entry.value[v]) {
                ++v;
            }
            if (!n || n == KOS_ENV_NAME || entry.name[n] || v == KOS_ENV_VALUE) {
                break;
            }
            int slot = -1;
            for (int i = 0; i < KOS_ENV_MAX; ++i) {
                if (!strcmp(current->environment[i].name, entry.name)) {
                    slot = i;
                    break;
                }
                if (slot < 0 && !current->environment[i].name[0]) {
                    slot = i;
                }
            }
            if (slot < 0) {
                r->eax = VFS_ENOSPC;
                return;
            }
            current->environment[slot] = entry;
            if (r->ecx) {
                memset(&current->environment[slot], 0, sizeof(entry));
            }
            r->eax = 0;
            return;
        }
        case KOS_SYS_SESSION:
            if (!current->shell && current->pid != init_pid) {
                break;
            }
            if (r->ebx == 1 && current->pid == shell_pid) {
                log_write("[INFO] shell ready\n");
            }
            for (int i = 1; i < PROCESS_MAX; ++i) {
                if (processes[i].state == ZOMBIE && processes[i].parent == current->pid) {
                    reap(&processes[i]);
                }
            }
            r->eax = 0;
            return;
        case KOS_SYS_LAUNCH: {
            if (!current->shell || !user_range(r->ebx, sizeof(kos_launch), 0)) {
                break;
            }
            kos_launch request = *(kos_launch*)r->ebx;
            if (!request.count || request.count > 8) {
                break;
            }
            /* Copy all user pointers before process_load changes current. */
            char (*strings)[VFS_PATH_MAX] = kmalloc(265 * VFS_PATH_MAX);
            if (!strings) {
                r->eax = VFS_ENOMEM;
                return;
            }
            char* paths[8];
            char* args[8][32];
            char** argv[8];
            int argc[8], valid = 1;
            for (u32 i = 0; valid && i < request.count; ++i) {
                paths[i] = strings[i * 33];
                argv[i] = args[i];
                argc[i] = request.argc[i];
                valid = argc[i] > 0 && argc[i] <= 32 && user_path((u32)request.paths[i], paths[i])
                    && user_range((u32)request.argv[i], argc[i] * 4, 0);
                for (int j = 0; valid && j < argc[i]; ++j) {
                    args[i][j] = strings[i * 33 + j + 1];
                    valid = user_path((u32)request.argv[i][j], args[i][j]);
                }
            }
            if (valid && request.output) {
                valid = user_path((u32)request.output, strings[264]);
            }
            r->eax = valid ? process_pipeline(
                                 request.count,
                                 paths,
                                 argc,
                                 argv,
                                 request.output ? strings[264] : NULL,
                                 !!request.append,
                                 !!request.background)
                           : VFS_EINVAL;
            kfree(strings);
            return;
        }
        case KOS_SYS_BUILTIN: {
            if (!current->shell) {
                break;
            }
            if (r->ebx == 1) {
                process_jobs();
                r->eax = 0;
                return;
            }
            if (r->ebx == 2) {
                r->eax = process_wait_jobs();
                return;
            }
            if (!r->ecx || r->ecx > 32 || !user_range(r->edx, r->ecx * 4, 0)) {
                break;
            }
            char (*args)[VFS_PATH_MAX] = kmalloc(32 * VFS_PATH_MAX);
            if (!args) {
                r->eax = VFS_ENOMEM;
                return;
            }
            char* argv[32];
            int valid = 1;
            for (u32 i = 0; valid && i < r->ecx; ++i) {
                argv[i] = args[i];
                valid = user_path(((u32*)r->edx)[i], args[i]);
            }
            int result = 127;
            if (valid) {
                vmm_space_switch(0);
                __asm__ volatile("sti" ::: "memory");
                for (u32 i = 0; shell_command_at(i); ++i) {
                    const shell_command_t* command = shell_command_at(i);
                    if (!strcmp(command->name, argv[0])) {
                        result = command->run(r->ecx, argv);
                        break;
                    }
                }
                __asm__ volatile("cli" ::: "memory");
                vmm_space_switch(space);
            }
            kfree(args);
            r->eax = result;
            return;
        }
        case KOS_SYS_OPEN:
            if (!user_path(r->ebx, path)) {
                break;
            }
            r->eax = fd_open(&current->descriptors, path, r->ecx);
            return;
        case KOS_SYS_CLOSE:
            r->eax = fd_close(&current->descriptors, r->ebx);
            return;
        case KOS_SYS_DUP2:
            r->eax = fd_dup2(&current->descriptors, r->ebx, r->ecx);
            return;
        case KOS_SYS_SEEK:
            r->eax = fd_seek(&current->descriptors, r->ebx, r->ecx);
            return;
        case KOS_SYS_FTRUNCATE:
            r->eax = fd_truncate(&current->descriptors, r->ebx, r->ecx);
            return;
        case KOS_SYS_FSTAT:
            if (!user_range(r->ecx, sizeof(kos_stat), 1)) {
                break;
            }
            r->eax = fd_stat(&current->descriptors, r->ebx, (kos_stat*)r->ecx);
            return;
        case KOS_SYS_SYNC:
            r->eax = system_sync();
            return;
        case KOS_SYS_KILL:
            r->eax = KOS_ESRCH;
            for (int i = 0; i < PROCESS_MAX; ++i) {
                process_t* p = &processes[i];
                if (p->pid != r->ebx || !p->state || p->state == ZOMBIE) {
                    continue;
                }
                if (p == current || p->pid == init_pid || p->shell) {
                    r->eax = KOS_EPERM;
                } else {
                    p->interrupted = 1;
                    if (p->state == WAITING) {
                        p->state = READY;
                    }
                    r->eax = 0;
                }
                break;
            }
            return;
        case KOS_SYS_PIPE: {
            if (!user_range(r->ebx, 8, 1)) {
                break;
            }
            int pair[2];
            int result = fd_pipe(&current->descriptors, pair);
            if (!result) {
                memcpy((void*)r->ebx, pair, sizeof(pair));
            }
            r->eax = result;
            return;
        }
        case KOS_SYS_READ:
        case KOS_SYS_WRITE: {
            int reading = r->eax == KOS_SYS_READ;
            if (r->edx > 4096 || !user_range(r->ecx, r->edx, reading)) {
                break;
            }
            int result;
            do {
                process_check_interrupt();
                result = reading ? fd_read(&current->descriptors, r->ebx, (void*)r->ecx, r->edx)
                                 : fd_write(&current->descriptors, r->ebx, (void*)r->ecx, r->edx);
            } while (result == KOS_EAGAIN);
            r->eax = result;
            return;
        }
        case 16: { /* Bounded process table snapshot. */
            if (r->ecx > PROCESS_MAX || !user_range(r->ebx, r->ecx * sizeof(kos_process_info), 1)) {
                break;
            }
            kos_process_info* out = (void*)r->ebx;
            u32 n = 0;
            for (int i = 0; i < PROCESS_MAX && n < r->ecx; ++i) {
                process_t* p = &processes[i];
                if (p->state) {
                    kos_process_info info = { .pid = p->pid,
                                              .parent = p->parent,
                                              .state = p->state,
                                              .ticks = p->ticks,
                                              .group = p->group,
                                              .heap_bytes = p->heap_end >= USER_HEAP_BASE
                                                  ? p->heap_end - USER_HEAP_BASE
                                                  : 0 };
                    memcpy(info.name, p->name, sizeof(info.name));
                    for (int fd = 0; fd < KOS_FD_MAX; ++fd) {
                        if (p->descriptors.entries[fd]) {
                            ++info.open_fds;
                        }
                    }
                    out[n++] = info;
                }
            }
            r->eax = n;
            return;
        }
        case 17:
            if (r->edx > 4096 || !user_range(r->ecx, r->edx, 1)) {
                break;
            }
            r->eax = log_read(r->ebx, (char*)r->ecx, r->edx);
            return;
        case 18:
            r->eax = fat12_check();
            return;
        case 19: {
            if (!user_range(r->ebx, sizeof(kos_disk_info), 1)) {
                break;
            }
            kos_disk_info info = { 0 };
            info.mounted = fat12_is_mounted();
            info.disk_sectors = ata_disks[0].size;
            memcpy(info.model, ata_disks[0].model, sizeof(info.model));
            fat12_usage(&info.sectors, &info.cluster_bytes, &info.clusters, &info.free_clusters);
            memcpy((void*)r->ebx, &info, sizeof(info));
            r->eax = 0;
            return;
        }
        case 13: { /* spawn(path, argc, argv): copy all parent pointers first. */
            if (!user_path(r->ebx, path) || !r->ecx || r->ecx > 32 || !user_range(r->edx, r->ecx * 4, 0)) {
                break;
            }
            char (*args)[VFS_PATH_MAX] = kmalloc(32 * VFS_PATH_MAX);
            if (!args) {
                r->eax = VFS_ENOMEM;
                return;
            }
            char* argv[32];
            for (u32 i = 0; i < r->ecx; ++i) {
                argv[i] = args[i];
                if (!user_path(((u32*)r->edx)[i], args[i])) {
                    kfree(args);
                    r->eax = VFS_EINVAL;
                    return;
                }
            }
            process_t* parent = current;
            process_t* child = NULL;
            for (int i = 1; i < PROCESS_MAX; ++i) {
                if (!processes[i].state) {
                    child = &processes[i];
                    break;
                }
            }
            if (!child) {
                kfree(args);
                r->eax = VFS_ENOMEM;
                return;
            }
            child->pid = next_pid++;
            child->parent = parent->pid;
            child->group = parent->group;
            if (parent->pid == init_pid) {
                child->group = child->pid;
                if (!strcmp(path, "/BIN/SHELL.ELF")) {
                    shell_pid = child->pid;
                }
            }
            strcpy(child->cwd, parent->cwd);
            current = child;
            int result = process_load(path, r->ecx, argv);
            if (!result) {
                memcpy(child->environment, parent->environment, KOS_ENV_MAX * sizeof(kos_env_entry));
            }
            current = parent;
            if (!result) {
                fd_clone(&child->descriptors, &parent->descriptors);
            }
            kfree(args);
            if (result) {
                reap(child);
                r->eax = result;
            } else {
                r->eax = child->pid;
            }
            return;
        }
        case 14: { /* wait(pid): exit status, direct children only. */
            process_t* child = NULL;
            for (int i = 1; i < PROCESS_MAX; ++i) {
                if (processes[i].state && processes[i].pid == r->ebx && processes[i].parent == current->pid) {
                    child = &processes[i];
                }
            }
            if (!child) {
                r->eax = KOS_ECHILD;
                return;
            }
            u32 previous_foreground = foreground;
            foreground = child->group;
            while (child->state != ZOMBIE) {
                current->state = WAITING;
                schedule();
                if (current->interrupted) {
                    process_exit(130);
                }
            }
            foreground = previous_foreground;
            r->eax = child->status;
            reap(child);
            return;
        }
        case 15:
            r->eax = current->pid;
            return;
        case 0:
            process_exit(r->ebx & 255);
        case 1: /* console write */
            if (r->ecx > 4096 || !user_range(r->ebx, r->ecx, 0)) {
                break;
            }
            /* VGA/serial output can take many timer ticks. Keep IRQ1 alive
             * while printing; kernel-mode timer IRQs do not switch tasks. */
            __asm__ volatile("sti" ::: "memory");
            for (u32 i = 0; i < r->ecx; ++i) {
                char text[2] = { ((char*)r->ebx)[i], 0 };
                printf("%s", text);
            }
            __asm__ volatile("cli" ::: "memory");
            r->eax = r->ecx;
            return;
        case 2: /* bounded file read */
            if (r->esi > 4096 || !user_path(r->ebx, path) || !user_range(r->edx, r->esi, 1)) {
                break;
            }
            r->eax = vfs_read(path, r->ecx, (void*)r->edx, r->esi);
            return;
        case 3: /* directory iterator; cursor passed by pointer */
            if (!user_path(r->ebx, path) || !user_range(r->ecx, 4, 1)
                || !user_range(r->edx, sizeof(vfs_dirent_t), 1)) {
                break;
            }
            r->eax = vfs_readdir(path, (u32*)r->ecx, (vfs_dirent_t*)r->edx);
            return;
        case 4:
            if (r->ecx < (u32)strlen(vfs_getcwd()) + 1 || !user_range(r->ebx, r->ecx, 1)) {
                break;
            }
            strcpy((char*)r->ebx, vfs_getcwd());
            r->eax = 0;
            return;
        case 7: /* Full screen presentation, restored automatically on exit. */
            if (!process_foreground()) {
                r->eax = VFS_EBUSY;
                return;
            }
            if (!user_range(r->ebx, 80 * 25 * sizeof(ktui_cell), 0)) {
                break;
            }
            if (!saved_terminal) {
                saved_terminal = kmalloc(sizeof(*saved_terminal));
                if (!saved_terminal) {
                    r->eax = VFS_ENOMEM;
                    return;
                }
                *saved_terminal = *terminal_get_state();
            }
            terminal_clear();
            memcpy(terminal_get_state()->buffer, (void*)r->ebx, 80 * 25 * sizeof(ktui_cell));
            terminal_get_state()->cursor_x = r->ecx < 80 ? r->ecx : 0;
            terminal_get_state()->cursor_y = r->edx < 25 ? r->edx : 24;
            terminal_get_state()->dirty = 1;
            terminal_refresh();
            r->eax = 0;
            return;
        case 8: { /* Raw key: 0 poll, 1 wait, 2 wait at most one timer tick. */
            if (!process_foreground()) {
                r->eax = VFS_EBUSY;
                return;
            }
            int key;
            do {
                __asm__ volatile("cli" ::: "memory");
                process_check_interrupt();
                key = keyboard_read_event();
                if (current->shell
                    && (key == KEY_SCROLL_UP || key == KEY_SCROLL_DOWN || key == KEY_PAGE_UP
                        || key == KEY_PAGE_DOWN)) {
                    u32 lines = key == KEY_PAGE_UP || key == KEY_PAGE_DOWN ? 23 : 1;
                    if (key == KEY_SCROLL_UP || key == KEY_PAGE_UP) {
                        terminal_scroll_up(lines);
                    } else {
                        terminal_scroll_down(lines);
                        u32 bottom = terminal_get_state()->cursor_y >= SCREEN_HEIGHT
                            ? terminal_get_state()->cursor_y - SCREEN_HEIGHT + 1
                            : 0;
                        if (terminal_get_scroll_offset() > bottom) {
                            terminal_scroll_to(bottom);
                        }
                    }
                    terminal_refresh();
                    key = 0;
                } else if (
                    current->shell && key
                    && terminal_get_scroll_offset()
                        != (terminal_get_state()->cursor_y >= SCREEN_HEIGHT
                                ? terminal_get_state()->cursor_y - SCREEN_HEIGHT + 1
                                : 0)) {
                    terminal_scroll_to_bottom();
                    terminal_refresh();
                }
                if (key || !r->ebx) {
                    break;
                }
                schedule();
                __asm__ volatile("sti; hlt; cli" ::: "memory");
                if (r->ebx == 2) {
                    key = keyboard_read_event();
                    break;
                }
            } while (1);
            r->eax = key;
            return;
        }
        case 9: {
            if (!user_range(r->ebx, sizeof(ktui_stats), 1)) {
                break;
            }
            memory_stats_t m = memory_stats();
            meminfo_t h = get_meminfo();
            ktui_stats snapshot = { m.usable_kib, m.free_pages, m.allocated_pages,
                                    h.total_used, h.total_free, h.heap_size };
            memcpy((void*)r->ebx, &snapshot, sizeof(snapshot));
            r->eax = 0;
            return;
        }
        case 10: /* Replace file, bounded to the editor's maximum size. */
            if (!user_path(r->ebx, path) || r->edx > 65536 || !user_range(r->ecx, r->edx, 0)) {
                break;
            }
            r->eax = vfs_replace(path, (void*)r->ecx, r->edx);
            return;
        case 11: /* Small filesystem operations. */
            if (!user_path(r->ebx, path)) {
                break;
            }
            switch (r->ecx) {
                case 0:
                    r->eax = vfs_mkdir(path);
                    return;
                case 1:
                    r->eax = vfs_unlink(path);
                    return;
                case 2:
                    r->eax = vfs_rmdir(path);
                    return;
                case 3:
                    r->eax = vfs_chdir(path);
                    return;
                case 4: {
                    if (!user_range(r->edx, sizeof(vfs_stat_t), 1)) {
                        break;
                    }
                    r->eax = vfs_stat(path, (vfs_stat_t*)r->edx);
                    return;
                }
            }
            break;
        case 12: {
            char destination[VFS_PATH_MAX];
            if (!user_path(r->ebx, path) || !user_path(r->ecx, destination)) {
                break;
            }
            r->eax = r->edx ? vfs_rename(path, destination) : vfs_copy(path, destination);
            return;
        }
        case 5: { /* Grow-only page-backed user heap. malloc/free live in userspace. */
            u32 size = r->ebx;
            if (!size || size > USER_HEAP_LIMIT - user_heap_end) {
                r->eax = 0;
                return;
            }
            size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            u32 start = user_heap_end;
            for (u32 n = 0; n < size; n += PAGE_SIZE) {
                if (!vmm_user_page(space, user_heap_end, 1)) {
                    /* Failed growth must not consume invisible heap/pages. */
                    vmm_user_unmap(space, user_heap_end);
                    while (user_heap_end > start) {
                        user_heap_end -= PAGE_SIZE;
                        vmm_user_unmap(space, user_heap_end);
                    }
                    vmm_space_switch(space);
                    r->eax = 0;
                    return;
                }
                user_heap_end += PAGE_SIZE;
            }
            vmm_space_switch(space); /* Flush cached translations after adding pages. */
            r->eax = start;
            return;
        }
        case 6: { /* Canonical stdin: one line, no shared shell editor state. */
            if (!process_foreground()) {
                r->eax = VFS_EBUSY;
                return;
            }
            if (!r->ecx || r->ecx > 4096 || !user_range(r->ebx, r->ecx, 1)) {
                break;
            }
            char* buffer = (char*)r->ebx;
            u32 length = 0;
            for (;;) {
                __asm__ volatile("cli" ::: "memory");
                process_check_interrupt();
                int key = keyboard_read_event();
                if (!key) {
                    schedule();
                    __asm__ volatile("sti; hlt; cli" ::: "memory");
                    continue;
                }
                if (key == 4) {
                    break; /* Ctrl+D: EOF / return pending bytes. */
                }
                if (key == 3) {
                    process_exit(130);
                }
                if (key == 8) {
                    if (length) {
                        --length;
                        terminal_handle_backspace();
                        terminal_refresh();
                    }
                    continue;
                }
                if (key == '\n' || key == '\r') {
                    buffer[length++] = '\n';
                    terminal_putchar('\n');
                    terminal_refresh();
                    break;
                }
                if (key >= 32 && key < 127) {
                    buffer[length++] = key;
                    terminal_putchar(key);
                    terminal_refresh();
                    if (length == r->ecx) {
                        break;
                    }
                }
            }
            r->eax = length;
            return;
        }
        default:
            break;
    }
    r->eax = VFS_EINVAL;
}
