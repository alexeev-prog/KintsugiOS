#include "ktuilib.h"
#include "sysabi.h"
#ifndef KMEMMAN
#    define KMEMMAN 0
#endif

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    int paused = 0, detail = 0;
    ktui_stats stats = { 0 };
    kos_process_info processes[16];
    int count = 0, fresh = 0;
    for (;;) {
        if (!paused || !fresh) {
            if (syscall(9, (u32)&stats, 0, 0, 0)) {
                return 1;
            }
            count = syscall(16, (u32)processes, 16, 0, 0);
            fresh = 1;
        }
        tui_clear(0x17);
        tui_bar(0, KMEMMAN ? "KMEMMAN - Kintsugi Memory Manager" : "kbsm - Kintsugi Basic System Monitor");
        tui_window metrics = { 1, 1, 78, 22, 1, detail ? "Memory accounting" : "System resources" };
        tui_window_draw(&metrics);
        const char* labels[] = { "Usable RAM (KiB)",         "Free physical pages",
                                 "Allocated physical pages", "Kernel heap used (bytes)",
                                 "Kernel heap free (bytes)", "Kernel heap committed (bytes)" };
        u32 values[] = { stats.ram_kib,   stats.free_pages, stats.allocated_pages,
                         stats.heap_used, stats.heap_free,  stats.heap_committed };
        for (u32 i = 0; i < 6; ++i) {
            tui_text(3, 3 + i * 2, labels[i], 0x1f);
            tui_number(40, 3 + i * 2, values[i], 0x1e);
        }
        tui_gauge(3, 16, 70, stats.allocated_pages, stats.free_pages + stats.allocated_pages);
        u32 user_heap = 0, descriptors = 0;
        for (int i = 0; i < count; ++i) {
            user_heap += processes[i].heap_bytes;
            descriptors += processes[i].open_fds;
        }
        tui_text(3, 18, "User heap committed / open descriptors:", 0x1f);
        tui_number(44, 18, user_heap, 0x1e);
        tui_number(60, 18, descriptors, 0x1e);
        tui_text(3, 19, "Processes:", 0x1f);
        if (count >= 0) {
            tui_number(16, 19, count, 0x1e);
        }
        tui_text(
            3,
            21,
            detail ? "Pages are 4096 bytes. Heap is kernel-only; PMM includes userspace."
                   : "Physical allocation gauge; CPU percentage is not measured.",
            0x1f);
        tui_bar(
            24,
            paused ? "Paused | Space resume | D details | Q / Esc quit"
                   : "Live memory statistics | Space pause | D details | Q / Esc quit");
        tui_present(79, 24);
        int key = tui_key(paused ? 1 : 2);
        if (key == ' ') {
            paused = !paused;
        }
        if (key == 'h') {
            tui_message(
                "Memory accounting",
                "Read-only: no unsafe page release. User heap is committed, not live malloc usage.");
        }
        if (key == 'd') {
            detail = !detail;
        }
        if (key == 'q' || key == 'Q' || key == 27 || key == 3) {
            break;
        }
    }
    return 0;
}
