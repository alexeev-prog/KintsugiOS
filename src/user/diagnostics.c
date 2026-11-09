#include "ktuilib.h"
#include "sysabi.h"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    u32 selected = 0, top = 0;
    int paused = 0;
#if APP == 1
    kos_process_info list[16];
    int count = 0, sort_ticks = 0;
    u32 selected_pid = 0;
#endif
    for (;;) {
        tui_clear(0x17);
#if APP == 1
        tui_bar(0, "KPROCM - Kintsugi Process Manager");
        if (!paused || !count) {
            count = syscall(16, (u32)list, 16, 0, 0);
        }
        if (count < 0) {
            return 1;
        }
        for (int i = 1; i < count; ++i) {
            kos_process_info value = list[i];
            int j = i;
            while (j && (sort_ticks ? list[j - 1].ticks < value.ticks : list[j - 1].pid > value.pid)) {
                list[j] = list[j - 1];
                --j;
            }
            list[j] = value;
        }
        for (int i = 0; i < count; ++i) {
            if (list[i].pid == selected_pid) {
                selected = i;
            }
        }
        tui_text(2, 2, "PID   PPID  STATE     TICKS      HEAP       PROGRAM", 0x1e);
        const char* states[] = { "unused", "ready", "running", "waiting", "zombie" };
        tui_list_key(0, &selected, &top, count, 14);
        for (int i = top; i < count && i < (int)top + 14; ++i) {
            int y = i - top + 4;
            unsigned char color = i == (int)selected ? 0x70 : 0x1f;
            tui_choice_row(1, y, 77, "", i == (int)selected, 0x1f);
            tui_number(2, y, list[i].pid, color);
            tui_number(8, y, list[i].parent, color);
            tui_text(14, y, list[i].state < 5 ? states[list[i].state] : "?", color);
            tui_number(24, y, list[i].ticks, color);
            tui_number(35, y, list[i].heap_bytes, color);
            tui_textn(46, y, list[i].name, 31, color);
        }
        tui_scrollbar(78, 4, 14, top, 14, count);
        tui_bar(23, "S sort PID/ticks | Enter details | PgUp/PgDn scroll | Space pause");
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
        tui_text(
            3,
            18,
            info.mounted ? "Volume online" : "Volume unavailable",
            info.mounted ? TUI_SUCCESS : TUI_ERROR);
        tui_bar(23, "C check filesystem | No partitioning, formatting or repair operations");
#else
        tui_bar(0, "KTUI - Kintsugi application launcher");
        tui_window applications = { 8, 4, 64, 16, 1, "Applications" };
        tui_window_draw(&applications);
        tui_text(11, 7, "1 Editor     2 Commander     3 Resource monitor", 0x1f);
        tui_text(11, 10, "4 Processes  5 Disks  6 Vik  7 Themes  H Library demo", 0x1f);
        tui_text(11, 14, "Shared panels, modal dialogs, settings and gauges", 0x1e);
        tui_bar(23, "Choose application by number | Enter opens application menu");
#endif
        tui_bar(
            24,
            paused ? "Paused | Space resume | H help | Q / Esc quit" : "Space pause | H help | Q / Esc quit");
        tui_present(79, 24);
        int key = tui_key(paused ? 1 : 2);
        if (key == TUI_F1) {
            key = 'h';
        }
        if (key == TUI_F10) {
            key = 'q';
        }
        if (key == 'q' || key == 27) {
            return 0;
        }
        if (key == ' ') {
            paused = !paused;
        }
#if APP == 1
        tui_list_key(key, &selected, &top, count, 14);
        if (count) {
            selected_pid = list[selected].pid;
        }
        if (key == 's') {
            sort_ticks = !sort_ticks;
        }
        if (key == '\n' && count) {
            if (tui_modal_begin("Process details", 64, 13)) {
                kos_process_info* p = &list[selected];
                tui_text(11, 8, p->name, 0x1f);
                tui_text(11, 10, "PID / parent / process group", 0x1e);
                tui_number(11, 11, p->pid, 0x1f);
                tui_number(24, 11, p->parent, 0x1f);
                tui_number(37, 11, p->group, 0x1f);
                tui_text(11, 13, "Scheduled ticks / committed user heap bytes", 0x1e);
                tui_number(11, 14, p->ticks, 0x1f);
                tui_number(30, 14, p->heap_bytes, 0x1f);
                tui_text(11, 16, "Press any key", 0x1e);
                tui_present(79, 24);
                tui_key(1);
                tui_modal_end();
            }
        }
        if (key == 'h') {
            tui_message("Process manager", "Arrows/Home/End select. Read-only; no unsafe kill operation.");
        }
#elif APP == 2
        (void)selected;
        (void)top;
        if (key == 'h') {
            tui_message("Disk manager", "C validates FAT chains and directories without modifying disk.");
        }
#endif
#if APP == 2
        if (key == 'c') {
            tui_message(
                "Filesystem check",
                syscall(18, 0, 0, 0, 0) ? "Check failed: corrupt/unavailable filesystem or I/O error"
                                        : "FAT12 allocation and directory structure are consistent");
        }
#elif APP == 3
        const char* programs[] = { "KUNED", "KC", "KBSM", "KPROCM", "KDISKMAN", "VIK", "KTTE" };
        const char* paths[] = { "/BIN/KUNED.ELF",    "/BIN/KC.ELF",  "/BIN/KBSM.ELF", "/BIN/KPROCM.ELF",
                                "/BIN/KDISKMAN.ELF", "/BIN/VIK.ELF", "/BIN/KTTE.ELF" };
        if (key == '\n') {
            const char* items[] = { "Editor", "Commander", "Resource monitor", "Processes",
                                    "Disks",  "Vik",       "Theme editor" };
            int choice = tui_menu("Launch application", items, 7, selected);
            if (choice >= 0) {
                selected = choice;
                key = '1' + choice;
            }
        }
        (void)top;
        if (key >= '1' && key <= '7') {
            char* args[] = { (char*)programs[key - '1'], 0 };
            int pid = spawn(paths[key - '1'], 1, args);
            if (pid < 0 || wait(pid)) {
                tui_message("Launcher", "Application failed");
            }
        }
        if (key == 'h') {
            int checked = 1;
            u32 focus = 0;
            if (tui_modal_begin("Shared widgets - Tab focus / Space toggle / Esc close", 70, 13)) {
                for (;;) {
                    tui_checkbox(8, 9, "Example option", checked, focus == 0);
                    tui_radio(8, 11, "Selected radio option", 1, focus == 1);
                    tui_button(8, 14, 18, "Close", focus == 2, 1);
                    tui_button(30, 14, 18, "Unavailable", 0, 0);
                    tui_present(79, 24);
                    int event = tui_key(1);
                    if (event == 27 || (event == '\n' && focus == 2)) {
                        break;
                    }
                    tui_focus_key(event, &focus, 3);
                    if (focus == 0) {
                        tui_toggle_key(event, &checked);
                    }
                }
                tui_modal_end();
            }
        }
#endif
    }
}
