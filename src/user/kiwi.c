#include "ktuilib.h"
#include "sysabi.h"

/* Experimental in-process TUI window workspace. External ELF applications
 * run foreground and return here; they are NOT embedded process surfaces. */
typedef struct { tui_window frame; int kind, visible, minimized, maximized; } kiwi_window;
typedef struct { char path[256]; u32 selected, top, count; tui_long_entry entries[64]; } kiwi_files;
static kiwi_window windows[4] = {
    {{2, 3, 46, 12, 1, "Welcome / KiWi"}, 0, 1, 0, 0},
    {{34, 7, 43, 14, 0, "Processes"}, 1, 1, 0, 0},
    {{7, 5, 52, 15, 0, "Files /HOME"}, 2, 0, 0, 0},
    {{15, 4, 55, 17, 0, "Notes (UTF-8)"}, 3, 0, 0, 0}
};
static char note[4097];
static kiwi_files files;
static int notes_dirty;

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
    int result = syscall(11, (u32)path, files.entries[files.selected].stat.directory ? 2 : 1, 0, 0);
    if (result) tui_message("KiWi", "Delete failed");
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
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    int active = 0, dragging = 0, dx = 0, dy = 0;
    const char* tabs[] = {"Welcome", "Processes", "Files", "Notes"};
    tui_editor editor = {.text = note, .capacity = 4096};
    tui_copy(files.path, "/HOME", sizeof(files.path));
    files_refresh();
    const char* apps[] = {"KPaint", "Editor", "Vik", "Commander", "Settings", "Theme editor", "System monitor", "KRrawl"};
    const char* paths[] = {"/BIN/KPAINT.ELF", "/BIN/KUNED.ELF", "/BIN/VIK.ELF", "/BIN/KC.ELF", "/BIN/KSET.ELF", "/BIN/KTTE.ELF", "/BIN/KINFO.ELF", "/BIN/KRRAWL.ELF"};
    for (;;) {
        tui_clear(TUI_DESKTOP);
        /* A real menu strip, not only a keyboard hint.  The menu actions are
         * deliberately routed through the same modal menu used by buttons so
         * mouse and keyboard activation have identical behaviour. */
        tui_bar(0, "KiWi   File   View   Apps   Settings");
        tui_tabs(1, tabs, 4, active);
        for (int pass = 0; pass < 4; ++pass) {
            int i = pass == 3 ? active : pass >= active ? pass + 1 : pass;
            kiwi_window* w = &windows[i];
            if (!w->visible || w->minimized) continue;
            w->frame.focused = i == active;
            tui_window_draw(&w->frame);
            int x = w->frame.x + 2, y = w->frame.y + 2;
            tui_text(w->frame.x + w->frame.width - 8, w->frame.y, "[_]", TUI_STATUS);
            tui_text(w->frame.x + w->frame.width - 4, w->frame.y, "[x]", TUI_BORDER);
            if (w->kind == 0) {
                tui_window_text(&w->frame, 1, 1, "KiWi / Kintsugi Window Interface", TUI_KEYWORD);
                tui_window_text(&w->frame, 1, 3, "Drag title bars with the mouse.", 0x1f);
                tui_window_text(&w->frame, 1, 5, "Tab: focus | F4: show all windows", 0x1f);
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
        tui_bar(23, "F2 apps | Tab focus | F4 all | M minimize Z maximize | Files N/D/R/X/Enter | Ctrl+S");
        tui_status(24); tui_present(79, 24);
        tui_event e; tui_event_read(&e, 1);
        if (e.key == 27) {
            if (notes_dirty && !tui_confirm("Discard unsaved notes?")) continue;
            tui_editor_destroy(&editor); return 0;
        }
        if (e.key == '\t') { active = (active + 1) % 4; windows[active].visible = 1; windows[active].minimized = 0; }
        if (e.key == TUI_F4) for (int i = 0; i < 4; ++i) windows[i].visible = 1;
        if (e.key == 'm') windows[active].minimized = 1;
        if (e.key == 'z') {
            tui_window* w = &windows[active].frame;
            if (!windows[active].maximized) {
                w->x = 1; w->y = 2; w->width = 78; w->height = 20;
                windows[active].maximized = 1;
            } else {
                w->x = 2 + active * 3; w->y = 3 + active; w->width = 46; w->height = 12;
                windows[active].maximized = 0;
            }
        }
        if (e.key == TUI_F3) tui_theme_choose();
        if (e.key == TUI_F2) {
            int choice = tui_menu("KiWi Applications", apps, 8, 0);
            if (choice >= 0) { int result = tui_launch(paths[choice], 0); if (result < 0) tui_message("Launch failed", kos_error(result)); }
        }
        if (active == 2) {
            if (e.key == 'n') files_create(0);
            else if (e.key == 'd') files_create(1);
            else if (e.key == 'r') files_rename();
            else if (e.key == 'x' || e.key == TUI_DELETE) files_delete();
            else if (e.key == TUI_UP && files.selected) --files.selected;
            else if (e.key == TUI_DOWN && files.selected + 1 < files.count) ++files.selected;
            else if (e.key == TUI_LEFT) files_parent();
            else if (e.key == '\n' && files.count) {
                char selected[256]; files_selected_path(selected);
                if (files.entries[files.selected].stat.directory) {
                    tui_copy(files.path, selected, sizeof(files.path)); files.selected = files.top = 0; files_refresh();
                } else {
                    int result = tui_launch("/BIN/KUNED.ELF", selected);
                    if (result < 0) tui_message("KiWi", "Could not open file");
                    files_refresh();
                }
            }
            if (files.selected < files.top) files.top = files.selected;
            if (files.selected >= files.top + 10) files.top = files.selected - 9;
        }
        if (e.key == 19) {
            if (tui_save_settings("/HOME/KIWI.TXT", note, editor.length)) notes_dirty = 0;
            else tui_message("KiWi", "Could not save notes");
        } else if (active == 3 && e.key && e.key != '\t') {
            if (tui_editor_key(&editor, e.key) > 0) notes_dirty = 1;
        }
        if (e.pressed & 1) {
            if (e.mouse_y == 1 && e.mouse_x >= 0 && e.mouse_x < 80) { active = e.mouse_x / 20; windows[active].visible = 1; }
            else if (e.mouse_y == 0 && e.mouse_x >= 0 && e.mouse_x < 8) {
                const char* items[] = { "New note", "Open home", "Save note", "Exit" };
                int choice = tui_menu("File", items, 4, 0);
                if (choice == 0) { active = 3; windows[3].visible = 1; }
                else if (choice == 1) { active = 2; windows[2].visible = 1; }
                else if (choice == 2 && tui_save_settings("/HOME/KIWI.TXT", note, editor.length)) notes_dirty = 0;
                else if (choice == 3) { if (!notes_dirty || tui_confirm("Discard unsaved notes?")) { tui_editor_destroy(&editor); return 0; } }
            } else if (e.mouse_y == 0 && e.mouse_x >= 8 && e.mouse_x < 15) {
                const char* items[] = { "Show all windows", "Minimize active", "Maximize / restore" };
                int choice = tui_menu("View", items, 3, 0);
                if (choice == 0) for (int i = 0; i < 4; ++i) windows[i].visible = 1;
                else if (choice == 1) windows[active].minimized = 1;
                else if (choice == 2) { tui_window* w = &windows[active].frame; w->x = 1; w->y = 2; w->width = 78; w->height = 20; windows[active].maximized = 1; }
            } else if (e.mouse_y == 0 && e.mouse_x >= 15 && e.mouse_x < 22) {
                int choice = tui_menu("Apps", apps, 8, 0);
                if (choice >= 0) { int result = tui_launch(paths[choice], 0); if (result < 0) tui_message("Launch failed", kos_error(result)); }
            } else if (e.mouse_y == 0 && e.mouse_x >= 22 && e.mouse_x < 32) {
                const char* items[] = { "TUI theme", "Framebuffer settings", "System information" };
                int choice = tui_menu("Settings", items, 3, 0);
                if (choice == 0) tui_theme_choose();
                else if (choice == 1) { int result = tui_launch("/BIN/KSET.ELF", 0); if (result < 0) tui_message("Settings", "Could not launch settings"); }
                else if (choice == 2) { int result = tui_launch("/BIN/KINFO.ELF", 0); if (result < 0) tui_message("Info", "Could not launch system information"); }
            }
            else {
                for (int p = 0; p < 4; ++p) {
                    int i = p ? (active + p) % 4 : active;
                    tui_window* w = &windows[i].frame;
                    if (!windows[i].visible || !tui_hit(w->x, w->y, w->width, w->height, &e)) continue;
                    active = i;
                    if (i == 2 && e.mouse_y >= w->y + 2 && e.mouse_y < w->y + w->height - 3) {
                        u32 row = files.top + e.mouse_y - w->y - 2;
                        if (row < files.count) files.selected = row;
                    }
                    if (e.mouse_y == w->y && e.mouse_x >= w->x + w->width - 8
                        && e.mouse_x < w->x + w->width - 4) windows[i].minimized = 1;
                    else if (e.mouse_y == w->y && e.mouse_x >= w->x + w->width - 4) windows[i].visible = 0;
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
            if (w->y + w->height > 23) w->y = 23 - w->height;
        }
    }
}
