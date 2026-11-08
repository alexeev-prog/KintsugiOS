#include "ktuilib.h"
#include "sysabi.h"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    for (;;) {
        tui_clear(0x17);
#if APP == 1
        tui_bar(0, "KPROCM - Kintsugi Process Manager");
        kos_process_info list[16];
        int count = syscall(16, (u32)list, 16, 0, 0);
        if (count < 0) {
            return 1;
        }
        tui_text(2, 2, "PID       PPID      STATE       TICKS      HEAP bytes", 0x1e);
        const char* states[] = { "unused", "ready", "running", "waiting", "zombie" };
        for (int i = 0; i < count; ++i) {
            tui_number(2, i + 4, list[i].pid, 0x1f);
            tui_number(12, i + 4, list[i].parent, 0x1f);
            tui_text(22, i + 4, list[i].state < 5 ? states[list[i].state] : "?", 0x1f);
            tui_number(34, i + 4, list[i].ticks, 0x1f);
            tui_number(45, i + 4, list[i].heap_bytes, 0x1f);
        }
        tui_bar(23, "Read-only snapshot; ticks are accounting, not CPU load percentage.");
#elif APP == 2
        tui_bar(0, "KDISKMAN - Kintsugi Disk Manager (read only)");
        kos_disk_info info;
        if (syscall(19, (u32)&info, 0, 0, 0)) {
            return 1;
        }
        tui_text(
            3, 3, info.mounted ? "Primary ATA volume: FAT12 mounted at /" : "No mounted FAT12 volume", 0x1f);
        const char* labels[] = { "Sectors (512 bytes)", "Cluster bytes", "Total clusters", "Free clusters" };
        u32 values[] = { info.sectors, info.cluster_bytes, info.clusters, info.free_clusters };
        for (int i = 0; i < 4; ++i) {
            tui_text(3, 6 + i * 2, labels[i], 0x1f);
            tui_number(35, 6 + i * 2, values[i], 0x1e);
        }
        tui_gauge(3, 15, 70, info.clusters - info.free_clusters, info.clusters);
        tui_bar(23, "C check filesystem | No partitioning, formatting or repair operations");
#else
        tui_bar(0, "KTUI - Kintsugi application launcher");
        tui_box(8, 4, 64, 16, "Applications", 0x17);
        tui_text(11, 7, "1 Editor     2 Commander     3 Resource monitor", 0x1f);
        tui_text(11, 10, "4 Processes  5 Disks        H Library demo", 0x1f);
        tui_text(11, 14, "Shared panels, modal dialogs, settings and gauges", 0x1e);
        tui_bar(23, "Choose application by number");
#endif
        tui_bar(24, "Q / Esc quit");
        tui_present(79, 24);
        int key = tui_key(2);
        if (key == 'q' || key == 27) {
            return 0;
        }
#if APP == 2
        if (key == 'c') {
            tui_message(
                "Filesystem check",
                syscall(18, 0, 0, 0, 0) ? "Check failed: corrupt/unavailable filesystem or I/O error"
                                        : "FAT12 allocation and directory structure are consistent");
        }
#elif APP == 3
        const char* programs[] = { "KUNED", "KC", "KBSM", "KPROCM", "KDISKMAN" };
        const char* paths[] = {
            "/BIN/KUNED.ELF", "/BIN/KC.ELF", "/BIN/KBSM.ELF", "/BIN/KPROCM.ELF", "/BIN/KDISKMAN.ELF"
        };
        if (key >= '1' && key <= '5') {
            char* args[] = { (char*)programs[key - '1'], 0 };
            int pid = spawn(paths[key - '1'], 1, args);
            if (pid < 0 || wait(pid)) {
                tui_message("Launcher", "Application failed");
            }
        }
        if (key == 'h') {
            tui_message("KTUI demo", "Clipped text, boxes, modal input and persistent settings.");
        }
#endif
    }
}
