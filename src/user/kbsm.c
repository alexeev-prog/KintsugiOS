#include "ktuilib.h"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    for (;;) {
        ktui_stats stats;
        if (syscall(9, (u32)&stats, 0, 0, 0)) {
            return 1;
        }
        tui_clear(0x17);
        tui_bar(0, "kbsm - Kintsugi Basic System Monitor");
        const char* labels[] = { "Usable RAM (KiB)",         "Free physical pages",
                                 "Allocated physical pages", "Kernel heap used (bytes)",
                                 "Kernel heap free (bytes)", "Kernel heap committed (bytes)" };
        u32 values[] = { stats.ram_kib,   stats.free_pages, stats.allocated_pages,
                         stats.heap_used, stats.heap_free,  stats.heap_committed };
        for (u32 i = 0; i < 6; ++i) {
            tui_text(3, 3 + i * 2, labels[i], 0x1f);
            tui_number(40, 3 + i * 2, values[i], 0x1e);
        }
        tui_text(3, 17, "Foreground process: KBSM.ELF (ring 3)", 0x1f);
        tui_text(3, 19, "Single-process system; CPU load is not measured.", 0x17);
        tui_bar(24, "Live memory statistics | Q / Esc quit");
        tui_present(79, 24);
        int key = tui_key(2);
        if (key == 'q' || key == 'Q' || key == 27 || key == 3) {
            break;
        }
    }
    return 0;
}
