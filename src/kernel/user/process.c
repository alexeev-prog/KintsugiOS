#include "process.h"

#include "../../shared/signal.h"
#include "../../user/authabi.h"
#include "../../user/envabi.h"
#include "../../user/ioabi.h"
#include "../../user/shellabi.h"
#include "../../user/sysabi.h"
#include "../../user/tuiabi.h"
#include "../cpu/idt.h"
#include "../drivers/ata_pio.h"
#include "../drivers/audio.h"
#include "../drivers/framebuffer.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/terminal.h"
#include "../fs/ext2.h"
#include "../fs/fat12.h"
#include "../fs/kfs2.h"
#include "../fs/kfs3.h"
#include "../fs/vfs.h"
#include "../kernel/log.h"
#include "../kernel/power.h"
#include "../kklibc/kklibc.h"
#include "../mm/memory.h"
#include "../shell/shell.h"
#include "auth.h"
#include "fd.h"
#include "gfxwm.h"
#include "vfs_meta.h"

#define STACK_BOTTOM (USER_TOP - 9 * PAGE_SIZE)
#define STACK_START (STACK_BOTTOM + PAGE_SIZE)
static u8 kernel_stack[16384] __attribute__((aligned(16)));
static u32 gdt[12] __attribute__((aligned(8)));
/* Optional TUI capture target: syscall 7 from this process fills the buffer
 * instead of taking over the shared terminal (used by the pixel GUI). */
static u32 tui_capture_pid;
static ktui_cell tui_capture_cells[25][80];
static int tui_capture_dirty;
static u32 key_inject_pid;
static int key_injected;
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
    u32 group, quantum, priority;
    u32 uid, gid, euid, egid, pending_uid;
    int pending_elevation;
    int shell;
    char name[32];
    char cwd[VFS_PATH_MAX];
    kos_env_entry* environment;
} process_t;

static process_t processes[PROCESS_MAX];
static process_t* current;
static u32 next_pid = 1, shell_context, foreground;
static u32 boot_ticks;
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

u32 process_pid(void) {
    return current ? current->pid : 0;
}

u32 process_euid(void) {
    return current ? current->euid : 0;
}

u32 process_egid(void) {
    return current ? current->egid : 0;
}

u32 process_ticks(void) {
    return boot_ticks;
}

int process_set_credentials(u32 uid, u32 gid) {
    if (!current) {
        return KOS_EPERM;
    }
    current->uid = current->euid = uid;
    current->gid = current->egid = gid;
    return 0;
}

void process_set_pending_uid(u32 uid) {
    if (!current) {
        return;
    }
    current->pending_uid = uid;
    current->pending_elevation = 1;
}

int process_take_pending_uid(u32* uid) {
    if (!current || !current->pending_elevation) {
        return 0;
    }
    if (uid) {
        *uid = current->pending_uid;
    }
    current->pending_elevation = 0;
    return 1;
}

u32 process_pid_exists(u32 pid) {
    if (!pid) {
        return 0;
    }
    for (int i = 0; i < PROCESS_MAX; ++i) {
        if (processes[i].state && processes[i].pid == pid) {
            return pid;
        }
    }
    return 0;
}

u32 process_pid_at(u32 index) {
    for (int i = 0; i < PROCESS_MAX; ++i) {
        if (!processes[i].state) {
            continue;
        }
        if (!index--) {
            return processes[i].pid;
        }
    }
    return 0;
}

static process_t* process_by_pid(u32 pid) {
    for (int i = 0; i < PROCESS_MAX; ++i) {
        if (processes[i].state && processes[i].pid == pid) {
            return &processes[i];
        }
    }
    return NULL;
}

int process_proc_name(u32 pid, char* out, u32 capacity) {
    process_t* p = process_by_pid(pid);
    if (!p || !capacity) {
        return 0;
    }
    u32 n = 0;
    while (p->name[n] && n + 1 < capacity) {
        out[n] = p->name[n];
        ++n;
    }
    out[n] = 0;
    return (int)n;
}

int process_proc_status(u32 pid, char* out, u32 capacity) {
    process_t* p = process_by_pid(pid);
    if (!p) {
        return 0;
    }
    static const char* const states[] = { "unused", "ready", "running", "waiting", "zombie" };
    const char* state = p->state <= ZOMBIE ? states[p->state] : "unknown";
    return snprintf(
        out,
        capacity,
        "Name:\t%s\nState:\t%s\nPid:\t%u\nPPid:\t%u\nUid:\t%u\nGid:\t%u\nTicks:\t%u\n",
        p->name,
        state,
        p->pid,
        p->parent,
        p->euid,
        p->egid,
        p->ticks);
}

int process_proc_cmdline(u32 pid, char* out, u32 capacity) {
    process_t* p = process_by_pid(pid);
    if (!p || !capacity) {
        return 0;
    }
    return snprintf(out, capacity, "%s\n", p->name);
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
                /* Priority-aware: keep the quantum for high-priority tasks,
                 * but always yield after `priority` turns. */
                if (p->quantum > 1 && p->priority > 1) {
                    --p->quantum;
                } else {
                    p->quantum = p->priority ? p->priority : 1;
                }
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

/* Minimal OOM policy: when free pages drop below a low-water mark, select the
 * largest non-init process and ask it to exit at its next syscall/tick. This
 * is deliberately conservative — killing init or the current process would be
 * worse than the allocation failure. */
static void oom_check(void) {
    if (boot_ticks % 100 || !current) {
        return;
    }
    memory_stats_t memory = memory_stats();
    if (memory.free_pages > 256) {
        return;
    }
    int victim = -1;
    u32 largest = 0;
    for (int i = 0; i < PROCESS_MAX; ++i) {
        process_t* p = &processes[i];
        if (!p->state || p->pid == init_pid || p == current || p->state == ZOMBIE) {
            continue;
        }
        if (p->heap_end > largest) {
            largest = p->heap_end;
            victim = i;
        }
    }
    if (victim < 0) {
        return;
    }
    process_t* p = &processes[victim];
    p->status = SIG_EXIT_KILLED;
    p->interrupted = 1;
    if (p->state == WAITING) {
        p->state = READY;
    }
    char message[96];
    snprintf(
        message, sizeof(message), "[OOM] killed pid %u (%s), %u bytes heap\n", p->pid, p->name, p->heap_end);
    log_write(message);
}

void process_tick(int user_mode) {
    ++boot_ticks;
    oom_check();
    process_wake(NULL); /* Terminal input waiters recheck after an IRQ/tick. */
    if (!current) {
        return;
    }
    ++current->ticks;
    if (user_mode && current->interrupted) {
        process_exit(current->status ? current->status : SIG_EXIT_INTERRUPTED);
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
        log_event("DEBUG", "elf", "read failed: got=%d expected=%u", result, stat.size);
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
    current->shell = !vfs_normalize(path, executable) && !strcmp(executable, "/bin/SHELL.ELF");
    current->stack = sp;
    u32* context = (u32*)((u32)current->kernel_stack + 16384);
    *--context = (u32)process_start;
    for (int i = 0; i < 4; ++i) {
        *--context = 0;
    }
    current->context = (u32)context;
    current->priority = 1;
    current->quantum = 1;
    current->state = READY;
    return 0;
cleanup:
    vmm_space_destroy(space);
    space = 0;
    kfree(image);
    return result;
}

static void reap(process_t* p) {
    if (tui_capture_pid && tui_capture_pid == p->pid) {
        tui_capture_pid = 0;
        tui_capture_dirty = 1;
    }
    if (key_inject_pid && key_inject_pid == p->pid) {
        key_inject_pid = 0;
        key_injected = 0;
    }
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
        u32 elevated = 0;
        int elevate = parent ? process_take_pending_uid(&elevated) : 0;
        current = children[i] = slot;
        strcpy(current->cwd, inherited_cwd);
        current->pid = next_pid++;
        current->parent = parent ? parent->pid : 0;
        current->group = group;
        current->uid = current->euid = parent ? parent->euid : 0;
        current->gid = current->egid = parent ? parent->egid : 0;
        if (elevate) {
            current->uid = current->euid = elevated;
            current->gid = current->egid = elevated;
        }
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
    return process_run("/bin/INIT.ELF", 1, argv);
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

static int permit_path(const char* path, int write) {
    char absolute[VFS_PATH_MAX];
    if (vfs_normalize(path, absolute)) {
        return 0;
    }
    return kfs3_permit(absolute, write);
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
                    && user_range((u32)request.argv[i], argc[i] * 4, 0) && permit_path(paths[i], 0);
                for (int j = 0; valid && j < argc[i]; ++j) {
                    args[i][j] = strings[i * 33 + j + 1];
                    valid = user_path((u32)request.argv[i][j], args[i][j]);
                }
            }
            if (valid && request.output) {
                valid = user_path((u32)request.output, strings[264]) && permit_path(strings[264], 1);
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
            /* ebx=3: run one kernel builtin with a one-shot root credential.
             * sugo arms the pending elevation after verifying the password;
             * the elevation is consumed only when a builtin actually exists. */
            int elevated_call = r->ebx == 3;
            if (elevated_call && current->euid != 0 && !current->pending_elevation) {
                r->eax = KOS_EPERM;
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
            if (valid && !elevated_call) {
                /* Power operations are intentionally available to every
                 * logged-in user (the machine is a single-user workstation). */
                static const char* const privileged[] = { "memtest",   "memdump", "malloc", "free",
                                                          "panic",     "fault",   "fstest", "load",
                                                          "fat12info", "memmap" };
                if (current->euid != 0) {
                    for (u32 i = 0; i < sizeof(privileged) / sizeof(privileged[0]); ++i) {
                        if (!strcmp(argv[0], privileged[i])) {
                            valid = 0;
                            result = KOS_EPERM;
                        }
                    }
                }
            }
            if (valid) {
                const shell_command_t* found = 0;
                for (u32 i = 0; shell_command_at(i); ++i) {
                    const shell_command_t* command = shell_command_at(i);
                    if (!strcmp(command->name, argv[0])) {
                        found = command;
                        break;
                    }
                }
                u32 saved_uid = current->euid, saved_gid = current->egid;
                int use_root = elevated_call && found && saved_uid != 0;
                if (use_root) {
                    u32 target = 0;
                    if (!process_take_pending_uid(&target) || target != 0) {
                        result = KOS_EPERM;
                        use_root = 0;
                    } else {
                        current->uid = current->euid = 0;
                        current->gid = current->egid = 0;
                    }
                }
                if (found) {
                    vmm_space_switch(0);
                    __asm__ volatile("sti" ::: "memory");
                    result = found->run(r->ecx, argv);
                    __asm__ volatile("cli" ::: "memory");
                    vmm_space_switch(space);
                }
                if (use_root) {
                    current->uid = current->euid = saved_uid;
                    current->gid = current->egid = saved_gid;
                }
            }
            kfree(args);
            r->eax = result;
            return;
        }
        case KOS_SYS_OPEN:
            if (!user_path(r->ebx, path)) {
                break;
            }
            if (!permit_path(
                    path, (r->ecx & (KOS_O_WRITE | KOS_O_APPEND | KOS_O_CREATE | KOS_O_TRUNC)) != 0)) {
                r->eax = KOS_EPERM;
                return;
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
        case KOS_SYS_KILL: {
            /* kill(pid, sig): sig 0 is an existence check. Supported signals
             * are cooperative: the target exits at its next syscall/tick. */
            int signal = r->ecx ? (int)r->ecx : SIGTERM;
            if (signal != 0 && signal != SIGHUP && signal != SIGINT && signal != SIGQUIT && signal != SIGTERM
                && signal != SIGKILL && signal != SIGUSR1 && signal != SIGUSR2) {
                r->eax = VFS_EINVAL;
                return;
            }
            r->eax = KOS_ESRCH;
            for (int i = 0; i < PROCESS_MAX; ++i) {
                process_t* p = &processes[i];
                if (p->pid != r->ebx || !p->state || p->state == ZOMBIE) {
                    continue;
                }
                if (p->pid == init_pid || (current->euid != 0 && p->euid != current->euid)) {
                    r->eax = KOS_EPERM;
                } else if (signal == 0) {
                    r->eax = 0;
                } else if (p == current) {
                    process_exit(signal == SIGKILL ? SIG_EXIT_KILLED : SIG_EXIT_INTERRUPTED);
                } else {
                    p->status = signal == SIGKILL ? SIG_EXIT_KILLED : SIG_EXIT_INTERRUPTED;
                    p->interrupted = 1;
                    if (p->state == WAITING) {
                        p->state = READY;
                    }
                    r->eax = 0;
                }
                break;
            }
            return;
        }
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
        case KOS_SYS_DISPLAY:
            if (!process_foreground()) {
                r->eax = KOS_EPERM;
                return;
            }
            r->eax = framebuffer_mode(r->ebx);
            terminal_get_state()->dirty = 1;
            return;
        case KOS_SYS_LAYOUT:
            if (!process_foreground()) {
                r->eax = KOS_EPERM;
                return;
            }
            keyboard_set_layout(r->ebx);
            r->eax = 0;
            return;
        case KOS_SYS_FRAMEBUFFER_THEME:
            if (!process_foreground()) {
                r->eax = KOS_EPERM;
                return;
            }
            if (r->ebx >= KOS_FRAMEBUFFER_THEMES) {
                r->eax = VFS_EINVAL;
                return;
            }
            framebuffer_theme(r->ebx);
            terminal_get_state()->dirty = 1;
            r->eax = 0;
            return;
        case KOS_SYS_FRAMEBUFFER_INFO:
            /* mode, theme, scale, font, font size, then cell geometry. */
            if (!user_range(r->ebx, 9 * sizeof(u32), 1)) {
                break;
            }
            ((u32*)r->ebx)[0] = framebuffer_mode_index();
            ((u32*)r->ebx)[1] = framebuffer_theme_index();
            ((u32*)r->ebx)[2] = framebuffer_scale_index();
            ((u32*)r->ebx)[3] = framebuffer_font_index();
            ((u32*)r->ebx)[4] = framebuffer_font_size_index();
            framebuffer_geometry((u32*)r->ebx + 5);
            r->eax = 0;
            return;
        case KOS_SYS_FRAMEBUFFER_SCALE:
            if (!process_foreground() || r->ebx >= KOS_FRAMEBUFFER_SCALES) {
                r->eax = KOS_EPERM;
                return;
            }
            framebuffer_scale(r->ebx);
            terminal_get_state()->dirty = 1;
            r->eax = 0;
            return;
        case KOS_SYS_FRAMEBUFFER_FONT:
            if (!process_foreground() || r->ebx >= KOS_FRAMEBUFFER_FONTS) {
                r->eax = KOS_EPERM;
                return;
            }
            framebuffer_font(r->ebx);
            terminal_get_state()->dirty = 1;
            r->eax = 0;
            return;
        case KOS_SYS_FRAMEBUFFER_FONT_SIZE:
            if (!process_foreground() || r->ebx >= KOS_FRAMEBUFFER_FONT_SIZES) {
                r->eax = KOS_EPERM;
                return;
            }
            framebuffer_font_size(r->ebx);
            terminal_get_state()->dirty = 1;
            r->eax = 0;
            return;
        case KOS_SYS_GFX_INFO: {
            const framebuffer_info_t* fb = framebuffer_get_info();
            if (!user_range(r->ebx, sizeof(kos_gfx_info), 1)) {
                break;
            }
            kos_gfx_info info = { fb->width, fb->height, fb->pitch, fb->bpp };
            memcpy((void*)r->ebx, &info, sizeof(info));
            r->eax = fb->available ? 0 : VFS_EIO;
            return;
        }
        case KOS_SYS_GFX_BLIT: {
            /* ebx points to a kos_gfx_blit request in userspace. The explicit
             * source stride is what makes dirty-rect updates correct. */
            kos_gfx_blit request;
            if (!user_range(r->ebx, sizeof(request), 1)) {
                break;
            }
            memcpy(&request, (void*)r->ebx, sizeof(request));
            u32 stride = request.stride ? request.stride : request.width;
            if (!request.width || !request.height || stride < request.width || stride > 4096
                || (int)request.x < 0 || (int)request.y < 0) {
                r->eax = VFS_EINVAL;
                return;
            }
            u32 bytes = stride * request.height * 4;
            if (!bytes || bytes > 0x1000000u || !user_range(request.source, bytes, 0)) {
                r->eax = VFS_EINVAL;
                return;
            }
            framebuffer_blit(
                request.x, request.y, request.width, request.height, stride, (const u32*)request.source);
            r->eax = 0;
            return;
        }
        case KOS_SYS_GFX_MODE:
            /* 1 = pixel GUI owns the framebuffer, 0 = terminal mode. */
            terminal_set_gui_mode((int)r->ebx);
            r->eax = 0;
            return;
        case KOS_SYS_GFX_FILL: {
            if (!user_range(r->ebx, sizeof(kos_gfx_rect), 1)) {
                break;
            }
            kos_gfx_rect rect;
            memcpy(&rect, (void*)r->ebx, sizeof(rect));
            if (rect.x < 0 || rect.y < 0) {
                r->eax = VFS_EINVAL;
                return;
            }
            framebuffer_rect((u32)rect.x, (u32)rect.y, rect.width, rect.height, rect.color);
            r->eax = 0;
            return;
        }
        case KOS_SYS_GFX_WINDOW_CREATE: {
            if (r->ebx < 1 || r->ebx > 1920 || r->ecx < 1 || r->ecx > 1200) {
                r->eax = VFS_EINVAL;
                return;
            }
            r->eax = gfxwm_create(r->ebx, r->ecx);
            return;
        }
        case KOS_SYS_GFX_WINDOW_CLOSE:
            /* Windows are owned by the creating process (pid), not by its uid. */
            r->eax = gfxwm_close((int)r->ebx, current ? current->pid : 0, current ? current->euid : 0);
            return;
        case KOS_SYS_GFX_WINDOW_FILL: {
            if (!user_range(r->ebx, 24, 1)) {
                break;
            }
            u32 vals[6];
            memcpy(vals, (void*)r->ebx, 24);
            r->eax = gfxwm_fill((int)vals[0], (int)vals[1], (int)vals[2], vals[3], vals[4], vals[5]);
            return;
        }
        case KOS_SYS_GFX_WINDOW_BLIT: {
            /* pointer, id, x, y, w, h packed in two user words is awkward;
             * use ebx=pointer, ecx=id, edx=x, esi=(y<<16)|w, edi? no. Pack
             * y/w/h in one word via user buffer instead. */
            if (!user_range(r->ebx, 24, 1)) {
                break;
            }
            u32 vals[6];
            memcpy(vals, (void*)r->ebx, 24);
            r->eax =
                gfxwm_blit((int)vals[0], (const u32*)vals[1], (int)vals[2], (int)vals[3], vals[4], vals[5]);
            return;
        }
        case KOS_SYS_GFX_WINDOW_TITLE: {
            /* ebx = window id, ecx = user title string (NUL terminated). */
            if (!user_range(r->ecx, 1, 0)) {
                break;
            }
            char title[48];
            int n = 0;
            while (n < 47 && user_range(r->ecx + n, 1, 0) && ((const char*)r->ecx)[n]) {
                title[n] = ((const char*)r->ecx)[n];
                ++n;
            }
            title[n] = 0;
            r->eax = gfxwm_title((int)r->ebx, title);
            return;
        }
        case KOS_SYS_GFX_WINDOW_MOVE: {
            if (!user_range(r->ebx, 12, 1)) {
                break;
            }
            u32 vals[3];
            memcpy(vals, (void*)r->ebx, 12);
            r->eax = gfxwm_move((int)vals[0], (int)vals[1], (int)vals[2]);
            return;
        }
        case KOS_SYS_GFX_WINDOW_RESIZE: {
            if (!user_range(r->ebx, 12, 1)) {
                break;
            }
            u32 vals[3];
            memcpy(vals, (void*)r->ebx, 12);
            r->eax = gfxwm_resize((int)vals[0], vals[1], vals[2]);
            return;
        }
        case KOS_SYS_GFX_WINDOW_STATE:
            r->eax = gfxwm_state((int)r->ebx, (int)r->ecx);
            return;
        case KOS_SYS_GFX_WINDOW_RAISE:
            r->eax = gfxwm_raise((int)r->ebx);
            return;

        case KOS_SYS_GFX_WINDOW_LIST: {
            if (!user_range(r->ebx, r->ecx * sizeof(kos_gui_window_info), 1)) {
                break;
            }
            r->eax = gfxwm_list((kos_gui_window_info*)r->ebx, r->ecx);
            return;
        }
        case KOS_SYS_GFX_WINDOW_READ: {
            if (!user_range(r->ebx, 24, 1)) {
                break;
            }
            u32 vals[6];
            memcpy(vals, (void*)r->ebx, 24);
            u32 bytes = vals[4] * vals[5] * 4;
            if (!bytes || !vals[1] || !user_range(vals[1], bytes, 1)) {
                break;
            }
            r->eax = gfxwm_read((int)vals[0], (u32*)vals[1], (int)vals[2], (int)vals[3], vals[4], vals[5]);
            return;
        }
        case KOS_SYS_GFX_WINDOW_POST: {
            if (!user_range(r->ebx, sizeof(kos_gui_event), 1)) {
                break;
            }
            kos_gui_event event;
            memcpy(&event, (void*)r->ebx, sizeof(event));
            r->eax = gfxwm_post((int)r->ecx, &event);
            return;
        }
        case KOS_SYS_GFX_WINDOW_EVENT: {
            if (!user_range(r->ebx, sizeof(kos_gui_event), 1)) {
                break;
            }
            r->eax = gfxwm_event((int)r->ecx, (kos_gui_event*)r->ebx);
            return;
        }
        case KOS_SYS_TUI_ATTACH:
            tui_capture_pid = r->ebx;
            tui_capture_dirty = 1;
            r->eax = 0;
            return;
        case KOS_SYS_KEY_INJECT:
            key_inject_pid = r->ebx;
            key_injected = (int)r->ecx;
            r->eax = 0;
            return;
        case KOS_SYS_TUI_READ:
            if (!user_range(r->ebx, sizeof(tui_capture_cells), 1)) {
                break;
            }
            memcpy((void*)r->ebx, tui_capture_cells, sizeof(tui_capture_cells));
            r->eax = tui_capture_dirty;
            tui_capture_dirty = 0;
            return;
        case KOS_SYS_SOUND_TONE:
            /* A beep cannot corrupt another session's screen; background
             * services (music daemons, alarms) must be able to use it. */
            audio_tone_inst(r->ebx, r->ecx, r->edx);
            r->eax = 0;
            return;
        case KOS_SYS_SOUND_VOLUME:
            if (!process_foreground() || r->ebx > 100) {
                r->eax = VFS_EINVAL;
                return;
            }
            audio_volume(r->ebx);
            r->eax = 0;
            return;
        case KOS_SYS_SOUND_INFO:
            if (!user_range(r->ebx, 3 * sizeof(u32), 1)) {
                break;
            }
            ((u32*)r->ebx)[0] = audio_get_volume();
            ((u32*)r->ebx)[1] = audio_is_enabled();
            ((u32*)r->ebx)[2] = audio_is_opl2();
            r->eax = 0;
            return;
        case KOS_SYS_GAME_PALETTE:
            if (!process_foreground()) {
                r->eax = KOS_EPERM;
                return;
            }
            framebuffer_game_palette(r->ebx ? 1 : 0);
            terminal_get_state()->dirty = 1;
            r->eax = 0;
            return;
        case KOS_SYS_SETPRIORITY:
            if (!current) {
                r->eax = KOS_EPERM;
                return;
            }
            if (r->ebx == 0 || r->ebx > 4) {
                r->eax = VFS_EINVAL;
                return;
            }
            current->priority = r->ebx;
            current->quantum = r->ebx;
            r->eax = 0;
            return;
        case KOS_SYS_SERVICE:
            r->eax = kfs3_is_active() ? 1 : 0;
            return;
        case KOS_SYS_GETUID:
            r->eax = current ? current->euid : 0;
            return;
        case KOS_SYS_GETGID:
            r->eax = current ? current->egid : 0;
            return;
        case KOS_SYS_GETUSER: {
            if (!r->ebx || r->ecx < 2 || !user_range(r->ebx, r->ecx, 1)) {
                break;
            }
            const char* account = auth_name_for_uid(current ? current->euid : 0);
            u32 n = 0;
            while (account[n] && n + 1 < r->ecx) {
                ((char*)r->ebx)[n] = account[n];
                ++n;
            }
            ((char*)r->ebx)[n] = 0;
            r->eax = 0;
            return;
        }
        case KOS_SYS_LOGIN: {
            char account[VFS_PATH_MAX], password[VFS_PATH_MAX];
            if (!current || current->euid != 0 || !user_path(r->ebx, account)) {
                break;
            }
            if (!user_path(r->ecx, password)) {
                break;
            }
            const auth_user_t* user = auth_find(account);
            if (!user || auth_verify(account, password)) {
                r->eax = KOS_EPERM;
                return;
            }
            process_set_credentials(user->uid, user->gid);
            log_event("INFO", "auth", "login %s uid=%u pid=%u", account, user->uid, current->pid);
            r->eax = 0;
            return;
        }
        case KOS_SYS_ELEVATE: {
            char account[VFS_PATH_MAX], password[VFS_PATH_MAX];
            if (!current || !user_path(r->ebx, account) || !user_path(r->ecx, password)) {
                break;
            }
            const auth_user_t* target = auth_find(account);
            if (!target) {
                r->eax = VFS_ENOENT;
                return;
            }
            if (current->euid == 0) {
                /* Root may drop to any account for the next child (login). */
                process_set_pending_uid(target->uid);
                r->eax = 0;
                return;
            }
            if (target->uid != 0 || auth_verify("root", password)) {
                r->eax = KOS_EPERM;
                return;
            }
            process_set_pending_uid(0);
            r->eax = 0;
            return;
        }
        case KOS_SYS_CANCEL_ELEVATE:
            if (current) {
                current->pending_elevation = 0;
            }
            r->eax = 0;
            return;
        case KOS_SYS_AUTH: {
            char account[VFS_PATH_MAX], password[VFS_PATH_MAX];
            if (!user_path(r->ebx, account) || !user_path(r->ecx, password)) {
                break;
            }
            r->eax = auth_verify(account, password);
            return;
        }
        case KOS_SYS_NEEDAUTH: {
            char account[VFS_PATH_MAX];
            if (!user_path(r->ebx, account)) {
                break;
            }
            r->eax = auth_needs_password(account);
            return;
        }
        case KOS_SYS_CHMOD:
        case KOS_SYS_CHOWN: {
            char path[VFS_PATH_MAX];
            if (!user_path(r->ebx, path)) {
                break;
            }
            char absolute[VFS_PATH_MAX];
            if (vfs_normalize(path, absolute)) {
                r->eax = VFS_EINVAL;
                return;
            }
            u32 uid;
            u16 mode;
            int have = vfs_meta_get(absolute, &uid, &mode);
            if (!have) {
                uid = kfs3_owner(absolute);
                mode = 0644;
            }
            if (r->eax == KOS_SYS_CHOWN) {
                if (!current || current->euid != 0) {
                    r->eax = KOS_EPERM;
                    return;
                }
                uid = r->ecx;
            } else {
                if (!current || (current->euid != 0 && current->euid != uid)) {
                    r->eax = KOS_EPERM;
                    return;
                }
                if (r->ecx > 0777) {
                    r->eax = VFS_EINVAL;
                    return;
                }
                mode = (u16)r->ecx;
            }
            r->eax = vfs_meta_set(absolute, uid, mode);
            return;
        }
        case KOS_SYS_USERADD: {
            char account[VFS_PATH_MAX], password[VFS_PATH_MAX];
            if (!current || current->euid != 0 || !user_path(r->ebx, account)) {
                break;
            }
            if (r->ecx) {
                if (!user_path(r->ecx, password)) {
                    break;
                }
            } else {
                password[0] = 0;
            }
            int uid = auth_add(account, password, 0);
            r->eax = uid < 0 ? uid : 0;
            return;
        }
        case KOS_SYS_USERPASSWD: {
            char account[VFS_PATH_MAX], previous[VFS_PATH_MAX], password[VFS_PATH_MAX];
            if (!current || !user_path(r->ebx, account)) {
                break;
            }
            if (r->ecx) {
                if (!user_path(r->ecx, previous)) {
                    break;
                }
            } else {
                previous[0] = 0;
            }
            if (!user_path(r->edx, password)) {
                break;
            }
            const auth_user_t* user = auth_find(account);
            if (!user) {
                r->eax = VFS_ENOENT;
                return;
            }
            if (current->euid != 0) {
                if (current->euid != user->uid || auth_verify(account, previous)) {
                    r->eax = KOS_EPERM;
                    return;
                }
            }
            r->eax = auth_set_password(account, password);
            return;
        }
        case KOS_SYS_REBOOT:
            if (!current || current->euid != 0) {
                r->eax = KOS_EPERM;
                return;
            }
            log_write("[INFO] reboot requested by root\n");
            system_sync();
            process_shutdown();
            reboot();
            for (;;) {
                __asm__ volatile("cli; hlt");
            }
        case KOS_SYS_SLEEP: {
            extern u32 tick;
            u32 ms = r->ebx > 10000 ? 10000 : r->ebx;
            u32 deadline = tick + (ms + 19) / 20;
            while (tick < deadline) {
                current->state = WAITING;
                schedule();
                if (current->interrupted) {
                    process_exit(130);
                }
            }
            r->eax = 0;
            return;
        }
        case KOS_SYS_MOUSE:
            if (!user_range(r->ebx, sizeof(kos_mouse), 1)) {
                break;
            }
            /* Any GUI process may read the pointer; requiring foreground here
             * broke kgui, which is spawned directly by the shell builtin. */
            /* The mouse syscall consumes wheel notches so TUI apps see each
             * notch once; the shell consumes them directly in raw-key mode. */
            kos_mouse snapshot = *mouse_state();
            snapshot.wheel = mouse_take_wheel();
            memcpy((void*)r->ebx, &snapshot, sizeof(snapshot));
            r->eax = 0;
            return;
        case KOS_SYS_DESKTOP: {
            if (!user_range(r->ebx, sizeof(kos_desktop_info), 1)) {
                break;
            }
            const framebuffer_info_t* fb = framebuffer_get_info();
            memory_stats_t m = memory_stats();
            extern u32 tick;
            kos_desktop_info out = { fb->width,         fb->height,
                                     fb->available,     framebuffer_mode_index(),
                                     tick / 50,         m.free_pages * 4,
                                     m.usable_kib,      0,
                                     keyboard_russian() };
            for (int i = 0; i < PROCESS_MAX; ++i) {
                if (processes[i].state) {
                    ++out.processes;
                }
            }
            memcpy((void*)r->ebx, &out, sizeof(out));
            r->eax = 0;
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
                                              .uid = p->euid,
                                              .gid = p->egid,
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
            r->eax = ext2_is_mounted() ? 0 : kfs2_is_mounted() ? kfs2_check() : fat12_check();
            return;
        case 19: {
            if (!user_range(r->ebx, sizeof(kos_disk_info), 1)) {
                break;
            }
            kos_disk_info info = { 0 };
            int ext = ext2_is_mounted();
            int kfs = kfs2_is_mounted();
            info.mounted = ext || kfs || fat12_is_mounted();
            info.disk_sectors = ata_disks[0].size;
            memcpy(info.model, ata_disks[0].model, sizeof(info.model));
            if (ext) {
                ext2_usage(&info.sectors, &info.cluster_bytes, &info.clusters, &info.free_clusters);
                strcpy(info.fs, "ext2");
            } else if (kfs) {
                kfs2_usage(&info.sectors, &info.cluster_bytes, &info.clusters, &info.free_clusters);
                strcpy(info.fs, "KFS2");
            } else {
                fat12_usage(&info.sectors, &info.cluster_bytes, &info.clusters, &info.free_clusters);
                strcpy(info.fs, "FAT12");
            }
            memcpy((void*)r->ebx, &info, sizeof(info));
            r->eax = 0;
            return;
        }
        case 13: { /* spawn(path, argc, argv): copy all parent pointers first. */
            if (!user_path(r->ebx, path) || !r->ecx || r->ecx > 32 || !user_range(r->edx, r->ecx * 4, 0)) {
                break;
            }
            if (!permit_path(path, 0)) {
                r->eax = KOS_EPERM;
                return;
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
            child->uid = child->euid = parent->euid;
            child->gid = child->egid = parent->egid;
            u32 elevated = 0;
            if (process_take_pending_uid(&elevated)) {
                child->uid = child->euid = elevated;
                child->gid = child->egid = elevated;
            }
            if (parent->pid == init_pid) {
                child->group = child->pid;
                if (!strcmp(path, "/bin/SHELL.ELF")) {
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
            if (!permit_path(path, 0)) {
                r->eax = KOS_EPERM;
                return;
            }
            r->eax = vfs_read(path, r->ecx, (void*)r->edx, r->esi);
            return;
        case 3: /* directory iterator; cursor passed by pointer */
            if (!user_path(r->ebx, path) || !user_range(r->ecx, 4, 1)
                || !user_range(r->edx, sizeof(vfs_dirent_t), 1)) {
                break;
            }
            if (!permit_path(path, 0)) {
                r->eax = KOS_EPERM;
                return;
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
            if (tui_capture_pid && current && current->pid == tui_capture_pid) {
                memcpy(tui_capture_cells, (void*)r->ebx, sizeof(tui_capture_cells));
                tui_capture_dirty = 1;
                r->eax = 0;
                return;
            }
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
            if (key_inject_pid && current && current->pid == key_inject_pid && key_injected) {
                int forwarded = key_injected;
                key_injected = 0;
                key_inject_pid = 0;
                r->eax = forwarded;
                return;
            }
            /* The GUI process may be spawned directly by the shell builtin and
             * is not in the shell's foreground group. Key input is a shared
             * console resource in this single-user design. */
            int key;
            do {
                __asm__ volatile("cli" ::: "memory");
                process_check_interrupt();
                key = keyboard_read_event();
                if (current->shell) {
                    int wheel = mouse_take_wheel();
                    if (wheel) {
                        if (wheel > 0) {
                            terminal_scroll_up(3);
                        } else {
                            terminal_scroll_down(3);
                            u32 bottom = terminal_get_state()->cursor_y >= SCREEN_HEIGHT
                                ? terminal_get_state()->cursor_y - SCREEN_HEIGHT + 1
                                : 0;
                            if (terminal_get_scroll_offset() > bottom) {
                                terminal_scroll_to(bottom);
                            }
                        }
                        terminal_refresh();
                    }
                }
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
            if (!permit_path(path, 1)) {
                r->eax = KOS_EPERM;
                return;
            }
            r->eax = vfs_replace(path, (void*)r->ecx, r->edx);
            return;
        case 11: /* Small filesystem operations. */
            if (!user_path(r->ebx, path)) {
                break;
            }
            if (r->ecx <= 2 && !permit_path(path, 1)) {
                r->eax = KOS_EPERM;
                return;
            }
            if (r->ecx >= 3 && !permit_path(path, 0)) {
                r->eax = KOS_EPERM;
                return;
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
            if (!permit_path(path, r->edx ? 1 : 0) || !permit_path(destination, 1)) {
                r->eax = KOS_EPERM;
                return;
            }
            r->eax = r->edx ? vfs_rename(path, destination) : vfs_copy(path, destination);
            return;
        }
        case KOS_SYS_READDIR_LONG: {
            char directory[VFS_PATH_MAX];
            if (!user_path(r->ebx, directory) || !user_range(r->ecx, sizeof(u32), 1)
                || !user_range(r->edx, sizeof(kos_long_dirent), 1)) {
                break;
            }
            if (!permit_path(directory, 0)) {
                r->eax = KOS_EPERM;
                return;
            }
            vfs_long_dirent_t long_entry;
            int result = vfs_readdir_long(directory, (u32*)r->ecx, &long_entry);
            if (result > 0) {
                memcpy((void*)r->edx, &long_entry, sizeof(long_entry));
            }
            r->eax = result;
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
