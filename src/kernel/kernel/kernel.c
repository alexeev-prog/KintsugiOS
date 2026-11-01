#include "kernel.h"

#include "../cpu/isr.h"
#include "../drivers/ata_pio.h"
#include "../drivers/screen_output_switch.h"
#include "../drivers/terminal.h"
#include "../fs/fat12.h"
#include "../kklibc/kklibc.h"
#include "../mm/memory.h"
#include "../shell/shell.h"
#include "log.h"
#include "sysinfo.h"

void kmain(void) {
    log_init();
    log_write("[INFO] kernel entry\n");
    isr_install();
    memory_init();
    heap_init();
    terminal_init();
    output_set_mode(OUTPUT_MODE_TERMINAL);
    terminal_refresh();
    irq_install();
    detect_cpu();
    detect_memory();
    ata_pio_init();
    fat12_init();
    log_write(fat12_is_mounted() ? "[INFO] FAT12 mounted\n" : "[WARN] FAT12 unavailable\n");
    printf("KintsugiOS %s\n", VERSION);
    shell_init();
    for (;;) {
        shell_poll();
        __asm__ volatile("hlt");
    }
}
