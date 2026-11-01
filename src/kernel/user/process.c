#include "process.h"

#include "../cpu/idt.h"
#include "../drivers/keyboard.h"
#include "../drivers/terminal.h"
#include "../fs/vfs.h"
#include "../kernel/log.h"
#include "../kklibc/kklibc.h"
#include "../mm/memory.h"

#define STACK_BOTTOM (USER_TOP - 9 * PAGE_SIZE)
#define STACK_START (STACK_BOTTOM + PAGE_SIZE)
static u8 kernel_stack[16384] __attribute__((aligned(16)));
static u32 gdt[12] __attribute__((aligned(8)));
static u32 tss[26];
static u32 space;
static u32 user_heap_end;
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
    return space != 0;
}

void process_exit(int status) {
    user_leave(status);
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

int process_run(const char* path, int argc, char** argv) {
    if (space || argc < 1 || argc > 32) {
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
        if ((s->flags & 1) && header->entry >= s->vaddr && header->entry - s->vaddr < s->memsz) {
            executable_entry = 1;
        }
        u32 end = (s->vaddr + s->memsz + 4095) & ~4095u;
        for (u32 va = s->vaddr & ~4095u; va < end; va += PAGE_SIZE) {
            if (!vmm_user_page(space, va, !!(s->flags & 2))) {
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
    log_write("[INFO] entering user ELF\n");
    __asm__ volatile("cli" ::: "memory");
    vmm_space_switch(space);
    result = user_enter(entry, sp);
    vmm_space_switch(0);
cleanup:
    vmm_space_destroy(space);
    space = 0;
    kfree(image);
    __asm__ volatile("sti" ::: "memory");
    return result;
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
    if (!space) {
        r->eax = VFS_EINVAL;
        return;
    }
    char path[VFS_PATH_MAX];
    switch (r->eax) {
        case 0:
            process_exit(r->ebx & 255);
        case 1: /* console write */
            if (r->ecx > 4096 || !user_range(r->ebx, r->ecx, 0)) {
                break;
            }
            for (u32 i = 0; i < r->ecx; ++i) {
                char text[2] = { ((char*)r->ebx)[i], 0 };
                printf("%s", text);
            }
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
                    /* Pages remain owned by the process and are reclaimed on exit. */
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
            if (!r->ecx || r->ecx > 4096 || !user_range(r->ebx, r->ecx, 1)) {
                break;
            }
            char* buffer = (char*)r->ebx;
            u32 length = 0;
            for (;;) {
                __asm__ volatile("cli" ::: "memory");
                int key = keyboard_read_event();
                if (!key) {
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
