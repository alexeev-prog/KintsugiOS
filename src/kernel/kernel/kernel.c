#include "kernel.h"

#include "../cpu/isr.h"
#include "../drivers/ata_pio.h"
#include "../drivers/audio.h"
#include "../drivers/framebuffer.h"
#include "../drivers/mouse.h"
#include "../drivers/screen_output_switch.h"
#include "../drivers/terminal.h"
#include "../fs/ext2.h"
#include "../fs/fat12.h"
#include "../fs/kfs2.h"
#include "../fs/kfs3.h"
#include "../kklibc/kklibc.h"
#include "../mm/memory.h"
#include "../shell/shell.h"
#include "../user/auth.h"
#include "../user/gfxwm.h"
#include "../user/process.h"
#include "../user/vfs_meta.h"
#include "log.h"
#include "sysinfo.h"

void kmain(void) {
    int fs_ready = 0;
    framebuffer_init();
    framebuffer_theme(5);
    log_init();
    log_write("[INFO] kernel entry\n");
    audio_init();
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
    log_event(
        "INFO",
        "terminal",
        "%s; UTF-8 Cyrillic; scrollback 200 lines",
        framebuffer_get_info()->available ? "Framebuffer 1280x800, 80x25 large cells" : "VGA text 80x25");
    output_set_mode(OUTPUT_MODE_TERMINAL);
    terminal_refresh();
    mouse_init();
    irq_install();
    log_event("INFO", "drivers", "PIT 50 Hz, PC speaker audio and PS/2 keyboard IRQ handlers installed");
    detect_cpu();
    detect_memory();
    log_event("DEBUG", "ata", "Probing primary legacy ATA channel, polling LBA28");
    ata_pio_init();
    fs_ready = ext2_init();
    if (!fs_ready) {
        log_event("WARN", "filesystem", "ext2 unavailable; trying KFS2 native backend");
        fs_ready = kfs2_init();
    }
    if (!fs_ready) {
        log_event("WARN", "filesystem", "KFS2 unavailable; trying legacy FAT12 backend");
        fat12_init();
    }
    log_write(
        ext2_is_mounted()        ? "[INFO] ext2 mounted as root\n"
            : kfs2_is_mounted()  ? "[INFO] KFS2 mounted\n"
            : fat12_is_mounted() ? "[INFO] FAT12 compatibility backend mounted\n"
                                 : "[WARN] filesystem unavailable\n");
    if (fs_ready) {
        auth_init();
        kfs3_init();
        vfs_meta_load();
        gfxwm_init();
    }
    printf("KintsugiOS %s\n", VERSION);
    if (fs_ready) {
        log_event("INFO", "init", "Starting /bin/INIT.ELF");
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
