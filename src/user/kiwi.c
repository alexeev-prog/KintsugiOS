#include "ktuilib.h"
#include "sysabi.h"

/* KiWi - in-process window workspace with a Windows-style shell:
 * menu bar, overlapping windows with z-order, and a taskbar with Start. */
typedef struct {
    tui_window frame;
    int kind, visible, minimized, maximized;
    int saved_x, saved_y, saved_w, saved_h;
} kiwi_window;
typedef struct { char path[256]; u32 selected, top, count; tui_long_entry entries[64]; } kiwi_files;

static kiwi_window windows[4] = {
    {{2, 2, 46, 12, 1, "Welcome to KiWi"}, 0, 1, 0, 0, 2, 2, 46, 12},
    {{34, 6, 43, 13, 0, "Processes"}, 1, 1, 0, 0, 34, 6, 43, 13},
    {{7, 4, 52, 14, 0, "Files - /HOME"}, 2, 0, 0, 0, 7, 4, 52, 14},
    {{15, 3, 55, 16, 0, "Notes - UTF-8"}, 3, 0, 0, 0, 15, 3, 55, 16}
};
static int zorder[4] = {0, 1, 2, 3};
static char note[4097];
static kiwi_files files;
static int notes_dirty;
static int want_exit;

/* Per-user notes live in the user's home. /home itself is root-owned in the
 * POSIX layout, so writing there would violate the same permission policy as
 * a normal Unix account. */
static void notes_path(char* out, u32 capacity) {
    const char* home = getenv("HOME");
    if (!home || !*home) home = "/home/user";
    u32 used = 0;
    while (home[used] && used + 1 < capacity) { out[used] = home[used]; ++used; }
    while (used && out[used - 1] == '/') --used;
    const char* tail = "/kiwi.txt";
    while (*tail && used + 1 < capacity) out[used++] = *tail++;
    out[used] = 0;
}

static void files_refresh(void) {
    files.count = 0;
    u32 cursor = 0;
    while (files.count < 64 && tui_readdir_long(files.path, &cursor, &files.entries[files.count]) > 0) ++files.count;
    if (files.selected >= files.count) files.selected = files.count ? files.count - 1 : 0;
    if (files.top > files.selected) files.top = files.selected;
}
static void files_parent(void) {
    u32 n = tui_len(files.path);
    while (n > 1 && files.path[n - 1] != '/') --n;
    if (n > 1) --n;
    files.path[n ? n : 1] = 0;
    if (!files.path[0]) tui_copy(files.path, "/", sizeof(files.path));
    files_refresh();
}
static void files_selected_path(char out[256]) {
    tui_path(out, files.path, files.count ? files.entries[files.selected].name : "");
}
static void files_create(int directory) {
    char name[256] = "";
    if (!tui_prompt(directory ? "New directory name:" : "New file name:", name, sizeof(name))) return;
    char path[256]; tui_path(path, files.path, name);
    int result = directory ? syscall(11, (u32)path, 0, 0, 0) : 0;
    if (!directory) {
        int fd = open(path, KOS_O_WRITE | KOS_O_CREATE | KOS_O_EXCL);
        result = fd < 0 ? fd : close(fd);
    }
    if (result) tui_message("KiWi", "Could not create item");
    files_refresh();
}
static void files_delete(void) {
    if (!files.count) return;
    char path[256]; files_selected_path(path);
    if (!tui_confirm("Delete selected item?")) return;
    if (syscall(11, (u32)path, files.entries[files.selected].stat.directory ? 2 : 1, 0, 0))
        tui_message("KiWi", "Delete failed");
    files_refresh();
}
static void files_rename(void) {
    if (!files.count) return;
    char name[256] = "";
    if (!tui_prompt("Rename selected item:", name, sizeof(name))) return;
    char source[256], target[256]; files_selected_path(source); tui_path(target, files.path, name);
    if (syscall(12, (u32)source, (u32)target, 1, 0)) tui_message("KiWi", "Rename failed");
    files_refresh();
}

static int focus_window(int idx) {
    if (idx < 0 || idx > 3) return 0;
    /* Restore minimized windows even when they are already on top of zorder. */
    windows[idx].visible = 1;
    windows[idx].minimized = 0;
    int pos = 0;
    while (pos < 4 && zorder[pos] != idx) ++pos;
    if (pos == 3) return idx;
    for (int i = pos; i < 3; ++i) zorder[i] = zorder[i + 1];
    zorder[3] = idx;
    return idx;
}
static int active_window(void) { return zorder[3]; }
/* Moving focus away from a hidden window prevents keystrokes from editing an
 * invisible editor or file list. */
static void hide_current(int index) {
    windows[index].visible = 0;
    windows[index].minimized = 1;
    for (int n = 1; n <= 4; ++n) {
        int candidate = (index + n) % 4;
        if (windows[candidate].visible && !windows[candidate].minimized) {
            focus_window(candidate);
            return;
        }
    }
    focus_window(index);
}

static const char* apps[] = { "KPaint", "Editor", "Vik", "Commander", "Settings",
                              "Theme editor", "System monitor", "KRrawl", "KMusa", "SoundCtl",
                              "KListen", "Services", "Service system", "CSV table", "KMines",
                              "K2048", "KLife", "KTmux", "kfetch" };
static const char* paths[] = { "/bin/KPAINT.ELF", "/bin/KUNED.ELF", "/bin/VIK.ELF", "/bin/KC.ELF",
                               "/bin/KSET.ELF", "/bin/KTTE.ELF", "/bin/KINFO.ELF", "/bin/KRRAWL.ELF",
                               "/bin/KMUSA.ELF", "/bin/SOUNDCTL.ELF", "/bin/KLISTEN.ELF",
                               "/bin/KSERVICES.ELF", "/bin/KSERSYS.ELF", "/bin/KCSV.ELF",
                               "/bin/KMINES.ELF", "/bin/K2048.ELF", "/bin/KLIFE.ELF",
                               "/bin/KTMUX.ELF", "/bin/KFETCH.ELF" };
#define APP_COUNT ((int)(sizeof(apps) / sizeof(apps[0])))
static void launch_app(int i) {
    if (i < 0 || i >= APP_COUNT) return;
    int result = tui_launch(paths[i], 0);
    if (result < 0) tui_message("Launch failed", kos_error(result));
}
static void start_menu(void) {
    const char* items[] = { "Programs", "Settings", "Display settings", "Sound", "Exit" };
    int c = tui_menu("Start", items, 5, 0);
    if (c == 0) { int a = tui_menu("Programs", apps, APP_COUNT, 0); launch_app(a); }
    else if (c == 1) launch_app(4);
    else if (c == 2) launch_app(4);
    else if (c == 3) launch_app(9);
    else if (c == 4) want_exit = 1;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    int dragging = 0, dx = 0, dy = 0;
    tui_editor editor = {.text = note, .capacity = 4096};
    char notes_file[256];
    notes_path(notes_file, sizeof(notes_file));
    int stored = syscall(2, (u32)notes_file, 0, (u32)note, 4096);
    if (stored > 0) {
        note[stored] = 0;
        editor.length = (u32)stored;
        editor.cursor = (u32)stored;
    }
    tui_copy(files.path, "/home", sizeof(files.path));
    files_refresh();

    for (;;) {
        kos_desktop_info desk = {0};
        syscall(KOS_SYS_DESKTOP, (u32)&desk, 0, 0, 0);
        tui_clear(TUI_DESKTOP);
        /* Subtle desktop wallpaper pattern, drawn before windows. */
        for (int row = 2; row < 22; ++row)
            for (int col = 0; col < 80; ++col)
                if (((row / 3) + (col / 8)) % 2 == 0) tui_text(col, row, ".", 0x18);
        tui_bar(0, "KiWi   File   View   Apps   Settings");
        for (int pass = 0; pass < 4; ++pass) {
            int i = zorder[pass];
            kiwi_window* w = &windows[i];
            if (!w->visible || w->minimized) continue;
            w->frame.focused = i == active_window();
            tui_window_draw(&w->frame);
            int x = w->frame.x + 2, y = w->frame.y + 2;
            tui_text(w->frame.x + w->frame.width - 8, w->frame.y, "[_]", TUI_STATUS);
            tui_text(w->frame.x + w->frame.width - 4, w->frame.y, "[x]", TUI_BORDER);
            if (w->kind == 0) {
                tui_window_text(&w->frame, 1, 1, "KiWi / Kintsugi Window Interface", TUI_KEYWORD);
                tui_window_text(&w->frame, 1, 3, "Drag title bars with the mouse.", 0x1f);
                tui_window_text(&w->frame, 1, 5, "Start menu: click [Start] on the taskbar.", 0x1f);
                tui_window_text(&w->frame, 1, 7, "ELF apps open fullscreen, then return.", 0x1f);
            } else if (w->kind == 1) {
                kos_process_info list[16]; int n = syscall(16, (u32)list, 16, 0, 0);
                for (int p = 0; p < n && p < w->frame.height - 4; ++p) {
                    tui_number(x, y + p, list[p].pid, TUI_NUMBER);
                    tui_textn(x + 5, y + p, list[p].name, w->frame.width - 10, 0x1f);
                }
            } else if (w->kind == 2) {
                tui_textn(x, y - 1, files.path, w->frame.width - 4, TUI_STATUS);
                for (int p = 0; p < w->frame.height - 5 && files.top + p < files.count; ++p) {
                    tui_long_entry* entry = &files.entries[files.top + p];
                    unsigned color = files.top + p == files.selected ? TUI_VISUAL :
                        entry->stat.directory ? TUI_DIRECTORY : 0x1f;
                    tui_textn(x, y + p, entry->name, w->frame.width - 4, color);
                }
            } else {
                tui_editor_draw(&editor, x, y, w->frame.width - 5, w->frame.height - 4);
            }
        }
        /* Windows-style taskbar */
        tui_fill(0, 23, 80, 2, TUI_HEADER);
        tui_button(0, 23, 10, "[Start]", 1, 1);
        int bx = 11;
        for (int pass = 3; pass >= 0; --pass) {
            int i = zorder[pass];
            if (!windows[i].visible) continue;
            int width = 12;
            if (bx + width > 68) break;
            tui_button(bx, 23, width, windows[i].kind == 0 ? "KiWi" :
                       windows[i].kind == 1 ? "Processes" :
                       windows[i].kind == 2 ? "Files" : "Notes",
                       i == active_window(), 1);
            bx += width + 1;
        }
        char clock[16];
        u32 minutes = (desk.uptime / 60) % 60, hours = (desk.uptime / 3600) % 24;
        clock[0] = '0' + hours / 10; clock[1] = '0' + hours % 10; clock[2] = ':';
        clock[3] = '0' + minutes / 10; clock[4] = '0' + minutes % 10; clock[5] = 0;
        tui_text(74, 23, clock, TUI_HEADER);
        tui_bar(22, "Tab focus | F2 apps | F3 theme | F4 all | Files N/D/R/X | Ctrl+S save");
        tui_present(79, 24);

        tui_event e; tui_event_read(&e, 1);
        int active = active_window();
        if (e.key == 27 || want_exit) {
            if (notes_dirty && !tui_confirm("Discard unsaved notes?")) { want_exit = 0; continue; }
            break;
        }
        if (e.key == '\t') { int n = (active + 1) % 4; focus_window(n); }
        if (e.key == TUI_F2) { int a = tui_menu("Programs", apps, APP_COUNT, 0); launch_app(a); }
        if (e.key == TUI_F3) tui_theme_choose();
        if (e.key == TUI_F4) {
            for (int i = 0; i < 4; ++i) { windows[i].visible = 1; windows[i].minimized = 0; }
        }
        /* Single letters are editor text: do not steal them from Notes. */
        if (e.key == 'm' && active != 3) hide_current(active);
        if (e.key == 'z' && active != 3) {
            tui_window* w = &windows[active].frame;
            if (!windows[active].maximized) {
                windows[active].saved_x = w->x; windows[active].saved_y = w->y;
                windows[active].saved_w = w->width; windows[active].saved_h = w->height;
                w->x = 1; w->y = 2; w->width = 78; w->height = 19;
                windows[active].maximized = 1;
            } else {
                w->x = windows[active].saved_x; w->y = windows[active].saved_y;
                w->width = windows[active].saved_w; w->height = windows[active].saved_h;
                windows[active].maximized = 0;
            }
        }
        if (active == 2) {
            if (e.key == 'n') files_create(0);
            else if (e.key == 'd') files_create(1);
            else if (e.key == 'r') files_rename();
            else if (e.key == 'x' || e.key == TUI_DELETE) files_delete();
            else if ((e.key == TUI_UP || e.key == TUI_SCROLL_UP) && files.selected) --files.selected;
            else if ((e.key == TUI_DOWN || e.key == TUI_SCROLL_DOWN) && files.selected + 1 < files.count) ++files.selected;
            else if (e.key == TUI_HOME) files.selected = 0;
            else if (e.key == TUI_END && files.count) files.selected = files.count - 1;
            else if (e.key == TUI_LEFT) files_parent();
            else if (e.key == '\n' && files.count) {
                char selected[256]; files_selected_path(selected);
                if (files.entries[files.selected].stat.directory) {
                    tui_copy(files.path, selected, sizeof(files.path)); files.selected = files.top = 0; files_refresh();
                } else {
                    int result = tui_launch("/bin/KUNED.ELF", selected);
                    if (result < 0) tui_message("KiWi", "Could not open file");
                    files_refresh();
                }
            }
            if (files.selected < files.top) files.top = files.selected;
            if (files.selected >= files.top + 10) files.top = files.selected - 9;
        }
        if (e.key == 19) {
            if (tui_save_settings(notes_file, note, editor.length)) notes_dirty = 0;
            else tui_message("KiWi", "Could not save notes");
        } else if (active == 3 && e.key && e.key != '\t') {
            if (tui_editor_key(&editor, e.key) > 0) notes_dirty = 1;
        }

        if (e.pressed & 1) {
            if (e.mouse_y == 23 && e.mouse_x < 10) { start_menu(); }
            else if (e.mouse_y == 23 && e.mouse_x >= 11 && e.mouse_x < 68) {
                int slot = (e.mouse_x - 11) / 13, found = 0, pass;
                for (pass = 3; pass >= 0; --pass) {
                    int i = zorder[pass];
                    if (!windows[i].visible) continue;
                    int width = 12;
                    if (e.mouse_x >= 11 + slot * 13 && e.mouse_x < 11 + slot * 13 + width) {
                        if (i == active_window() && !windows[i].minimized) windows[i].minimized = 1;
                        else focus_window(i);
                        found = 1; break;
                    }
                    if (11 + slot * 13 + width >= 68) break;
                }
                (void)found;
            }
            else if (e.mouse_y == 0 && e.mouse_x >= 0 && e.mouse_x < 8) {
                const char* items[] = { "New note", "Open home", "Save note", "Exit" };
                int c = tui_menu("File", items, 4, 0);
                if (c == 0) focus_window(3);
                else if (c == 1) focus_window(2);
                else if (c == 2 && tui_save_settings(notes_file, note, editor.length)) notes_dirty = 0;
                else if (c == 3) { if (!notes_dirty || tui_confirm("Discard unsaved notes?")) break; }
            }
            else if (e.mouse_y == 0 && e.mouse_x >= 8 && e.mouse_x < 15) {
                const char* items[] = { "Show all", "Minimize", "Maximize" };
                int c = tui_menu("View", items, 3, 0);
                if (c == 0) for (int i = 0; i < 4; ++i) { windows[i].visible = 1; windows[i].minimized = 0; }
                else if (c == 1) hide_current(active);
                else if (c == 2) { tui_window* w = &windows[active].frame; w->x = 1; w->y = 2; w->width = 78; w->height = 19; windows[active].maximized = 1; }
            }
            else if (e.mouse_y == 0 && e.mouse_x >= 15 && e.mouse_x < 22) {
                int a = tui_menu("Apps", apps, APP_COUNT, 0); launch_app(a);
            }
            else if (e.mouse_y == 0 && e.mouse_x >= 22 && e.mouse_x < 32) {
                const char* items[] = { "TUI theme", "Framebuffer settings", "System information" };
                int c = tui_menu("Settings", items, 3, 0);
                if (c == 0) tui_theme_choose();
                else if (c == 1) launch_app(4);
                else if (c == 2) launch_app(6);
            }
            else {
                for (int pass = 3; pass >= 0; --pass) {
                    int i = zorder[pass];
                    tui_window* w = &windows[i].frame;
                    if (!windows[i].visible || windows[i].minimized
                        || !tui_hit(w->x, w->y, w->width, w->height, &e)) continue;
                    if (i != active) { focus_window(i); active = i; }
                    if (i == 2 && e.mouse_y >= w->y + 2 && e.mouse_y < w->y + w->height - 3) {
                        u32 row = files.top + e.mouse_y - w->y - 2;
                        if (row < files.count) files.selected = row;
                    }
                    if (e.mouse_y == w->y && e.mouse_x >= w->x + w->width - 8
                        && e.mouse_x < w->x + w->width - 4) hide_current(i);
                    else if (e.mouse_y == w->y && e.mouse_x >= w->x + w->width - 4) hide_current(i);
                    else if (e.mouse_y == w->y) { dragging = 1; dx = e.mouse_x - w->x; dy = e.mouse_y - w->y; }
                    break;
                }
            }
        }
        if (!(e.buttons & 1)) dragging = 0;
        if (dragging) {
            tui_window* w = &windows[active].frame;
            w->x = e.mouse_x - dx; w->y = e.mouse_y - dy;
            if (w->x < 0) w->x = 0;
            if (w->x + w->width > 80) w->x = 80 - w->width;
            if (w->y < 2) w->y = 2;
            if (w->y + w->height > 22) w->y = 22 - w->height;
        }
    }
    tui_editor_destroy(&editor);
    return 0;
}