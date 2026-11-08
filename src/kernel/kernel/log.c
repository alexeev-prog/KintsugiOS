#include "log.h"

#include "../drivers/lowlevel_io.h"
#include "../kklibc/stdio.h"
#include "../kklibc/stdlib.h"

#define COM1 0x3f8
static int serial_ready;
static char journal[16384];
static u32 journal_start, journal_size;

int log_read(u32 offset, char* buffer, u32 capacity) {
    if (offset >= journal_size) return 0;
    u32 size = journal_size - offset;
    if (size > capacity) size = capacity;
    for (u32 i = 0; i < size; ++i)
        buffer[i] = journal[(journal_start + offset + i) % sizeof(journal)];
    return size;
}

void log_init(void) {
    port_byte_out(COM1 + 1, 0);
    port_byte_out(COM1 + 3, 0x80);
    port_byte_out(COM1, 1);
    port_byte_out(COM1 + 1, 0);
    port_byte_out(COM1 + 3, 3);
    port_byte_out(COM1 + 2, 0xc7);
    port_byte_out(COM1 + 4, 0x0b);
    serial_ready = 1;
    if (*(volatile u8*)0x4f00 == 3)
        log_write("[BOOT] BIOS loader: kernel loaded, E820 collected, protected mode entered\n");
    log_write("[INFO] serial initialized\n");
}

void log_write(const char* message) {
    if (!message) {
        return;
    }
    for (const char* p = message; *p; ++p) {
        if (journal_size == sizeof(journal)) {
            journal_start = (journal_start + 1) % sizeof(journal);
            --journal_size;
        }
        journal[(journal_start + journal_size++) % sizeof(journal)] = *p;
    }
    if (!serial_ready) return;
    while (*message) {
        u32 timeout = 100000;
        while (!(port_byte_in(COM1 + 5) & 0x20) && --timeout) {}
        if (!timeout) {
            return;
        }
        port_byte_out(COM1, *message++);
    }
}

__attribute__((noreturn)) void panic(const char* message, const registers_t* r) {
    __asm__ volatile("cli");
    /* No heap, terminal state or shared printf buffer is needed here. */
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "PANIC: %s\n", message ? message : "Unknown failure");
    log_write(buffer);
    volatile u16* vga = (volatile u16*)0xb8000;
    for (u32 i = 0; i < 2000; ++i) {
        vga[i] = 0x4f20;
    }
    u32 position = 0;
    for (const char* p = buffer; *p && position < 2000; ++p) {
        if (*p == '\n') {
            position = (position / 80 + 1) * 80;
        } else {
            vga[position++] = 0x4f00 | (u8)*p;
        }
    }
    if (r) {
        u32 cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        snprintf(buffer, sizeof(buffer),
            "vector=%x error=%x eip=%x cs=%x flags=%x cr2=%x\n"
            "eax=%x ebx=%x ecx=%x edx=%x\nesi=%x edi=%x ebp=%x esp=%x\n",
            r->int_no, r->err_code, r->eip, r->cs, r->eflags, cr2,
            r->eax, r->ebx, r->ecx, r->edx, r->esi, r->edi, r->ebp, r->esp);
        log_write(buffer);
        for (const char* p = buffer; *p && position < 2000; ++p) {
            if (*p == '\n') {
                position = (position / 80 + 1) * 80;
            } else {
                vga[position++] = 0x4f00 | (u8)*p;
            }
        }
    }
    for (;;) {
        __asm__ volatile("hlt");
    }
}
