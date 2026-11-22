#include "ktuilib.h"
#include "sysabi.h"

/* Experimental in-process TUI window workspace. External ELF applications
 * run foreground and return here; they are NOT embedded process surfaces. */
typedef struct { tui_window frame; int kind, visible; } kiwi_window;
static kiwi_window windows[4] = {
    {{2, 3, 46, 12, 1, "Welcome / KiWi"}, 0, 1},
    {{34, 7, 43, 14, 0, "Processes"}, 1, 1},
    {{7, 5, 52, 15, 0, "Files /HOME"}, 2, 0},
    {{15, 4, 55, 17, 0, "Notes (UTF-8)"}, 3, 0}
};
static char note[4097];
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    int active = 0, dragging = 0, dx = 0, dy = 0;
    const char* tabs[] = {"Welcome", "Processes", "Files", "Notes"};
    tui_editor editor = {.text = note, .capacity = 4096};
    const char* apps[] = {"KPaint", "Editor", "Vik", "Commander", "Settings", "Theme editor", "System monitor"};
    const char* paths[] = {"/BIN/KPAINT.ELF", "/BIN/KUNED.ELF", "/BIN/VIK.ELF", "/BIN/KC.ELF", "/BIN/KSET.ELF", "/BIN/KTTE.ELF", "/BIN/KINFO.ELF"};
    for (;;) {
        tui_clear(TUI_DESKTOP); tui_bar(0, "KiWi - experimental window workspace | F2 Applications | F3 Themes");
        tui_tabs(1, tabs, 4, active);
        for (int pass = 0; pass < 4; ++pass) {
            int i = pass == 3 ? active : pass >= active ? pass + 1 : pass;
            kiwi_window* w = &windows[i];
            if (!w->visible) continue;
            w->frame.focused = i == active;
            tui_window_draw(&w->frame);
            int x = w->frame.x + 2, y = w->frame.y + 2;
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
                u32 cursor = 0; tui_entry entry;
                for (int p = 0; p < w->frame.height - 4 && tui_readdir("/HOME", &cursor, &entry) > 0; ++p)
                    tui_textn(x, y + p, entry.name, w->frame.width - 4, entry.stat.directory ? TUI_DIRECTORY : 0x1f);
            } else {
                tui_editor_draw(&editor, x, y, w->frame.width - 5, w->frame.height - 4);
            }
        }
        tui_bar(23, "F2 Apps | Tab window | F4 show all | Ctrl+S save notes | Esc quit");
        tui_status(24); tui_present(79, 24);
        tui_event e; tui_event_read(&e, 1);
        if (e.key == 27) { tui_editor_destroy(&editor); return 0; }
        if (e.key == '\t') { active = (active + 1) % 4; windows[active].visible = 1; }
        if (e.key == TUI_F4) for (int i = 0; i < 4; ++i) windows[i].visible = 1;
        if (e.key == TUI_F3) tui_theme_choose();
        if (e.key == TUI_F2) {
            int choice = tui_menu("KiWi Applications", apps, 7, 0);
            if (choice >= 0) { int result = tui_launch(paths[choice], 0); if (result < 0) tui_message("Launch failed", kos_error(result)); }
        }
        if (e.key == 19) tui_save_settings("/HOME/KIWI.TXT", note, editor.length);
        else if (active == 3 && e.key && e.key != '\t') tui_editor_key(&editor, e.key);
        if (e.pressed & 1) {
            if (e.mouse_y == 1 && e.mouse_x >= 0 && e.mouse_x < 80) { active = e.mouse_x / 20; windows[active].visible = 1; }
            else {
                for (int p = 0; p < 4; ++p) {
                    int i = p ? (active + p) % 4 : active;
                    tui_window* w = &windows[i].frame;
                    if (!windows[i].visible || !tui_hit(w->x, w->y, w->width, w->height, &e)) continue;
                    active = i;
                    if (e.mouse_y == w->y && e.mouse_x >= w->x + w->width - 4) windows[i].visible = 0;
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
