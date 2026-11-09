#include "kernel.h"

#include "../cpu/isr.h"
#include "../drivers/ata_pio.h"
#include "../drivers/screen_output_switch.h"
#include "../drivers/terminal.h"
#include "../fs/fat12.h"
#include "../kklibc/kklibc.h"
#include "../mm/memory.h"
#include "../shell/shell.h"
#include "../user/process.h"
#include "log.h"
#include "sysinfo.h"

void kmain(void) {
    log_init();
    log_write("[INFO] kernel entry\n");
    log_event("INFO", "boot", "KintsugiOS %s i386; debug journal enabled", VERSION);
    log_event("DEBUG", "cpu", "Installing GDT/IDT exception vectors and PIC IRQ routing");
    isr_install();
    log_event("INFO", "cpu", "IDT and PIC initialized");
    memory_init();
    memory_stats_t memory = memory_stats();
    log_event(
        "DEBUG",
        "memory",
        "E820 entries=%u usable=%u KiB free=%u pages PMM limit=%u MiB",
        memory.map_entries,
        memory.usable_kib,
        memory.free_pages,
        PMM_LIMIT >> 20);
    heap_init();
    log_event("INFO", "heap", "Kernel heap initialized");
    process_init();
    log_event("INFO", "process", "Ring3, TSS and int80 ready; scheduler capacity=16");
    terminal_init();
    log_event("INFO", "terminal", "VGA text 80x25; scrollback 200 lines");
    output_set_mode(OUTPUT_MODE_TERMINAL);
    terminal_refresh();
    irq_install();
    log_event("INFO", "drivers", "PIT 50 Hz and PS/2 keyboard IRQ handlers installed");
    detect_cpu();
    detect_memory();
    log_event("DEBUG", "ata", "Probing primary legacy ATA channel, polling LBA28");
    ata_pio_init();
    log_event("DEBUG", "filesystem", "Mounting FAT12 backend at /");
    fat12_init();
    log_write(fat12_is_mounted() ? "[INFO] FAT12 mounted\n" : "[WARN] FAT12 unavailable\n");
    printf("KintsugiOS %s\n", VERSION);
    if (fat12_is_mounted()) {
        log_event("INFO", "init", "Starting /BIN/INIT.ELF");
        int status = process_boot_init();
        printf("init unavailable or exited (%d); rescue shell\n", status);
    }
    shell_init();
    log_event("WARN", "boot", "Userspace unavailable; rescue shell active");
    for (;;) {
        shell_poll();
        __asm__ volatile("hlt");
    }
}
