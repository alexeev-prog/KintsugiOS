#include "ktuilib.h"

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    kos_desktop_info info = {0};
    syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
    u32 selected = info.mode, top = 0;
    const char* modes[] = {"1024 x 768 - compact", "1280 x 800 - default", "1440 x 900 - large", "1600 x 900 - wide"};
    const char* status = "Select a mode; Enter tests it for 10 seconds. Y keeps, Esc restores.";
    for (;;) {
        syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
        tui_clear(TUI_DESKTOP);
        tui_bar(0, "KSettings - Display / Keyboard / Appearance");
        tui_box(3, 2, 74, 13, "Display resolution", TUI_BORDER);
        tui_list_draw(5, 4, 68, 4, modes, 4, selected, 0, 1);
        tui_text(5, 10, "Large 80x25 compatibility viewport; hardware mouse cursor", 0x1f);
        tui_text(5, 12, info.graphics ? "Standard VGA framebuffer online" : "Graphics unavailable on this adapter", TUI_SUCCESS);
        tui_box(3, 16, 74, 6, "Desktop preferences", TUI_BORDER);
        tui_text(5, 18, "L switch EN/RU | F12 / Ctrl+Shift / Alt+Shift: keyboard", 0x1f);
        tui_text(5, 20, "T choose theme | E theme editor | S save display preference", 0x1f);
        tui_bar(23, status); tui_status(24); tui_present(79, 24);
        int key = tui_key(2);
        if (key == 'q' || key == 27 || key == TUI_F10) return 0;
        tui_list_key(key, &selected, &top, 4, 4);
        if (key == 'l') syscall(KOS_SYS_LAYOUT, !info.russian, 0, 0, 0);
        if (key == 't') tui_theme_choose();
        if (key == 'e') tui_launch("/BIN/KTTE.ELF", 0);
        if (key == 's') {
            unsigned char mode = info.mode;
            status = tui_save_settings("/ETC/VIDEO.CFG", &mode, 1) ? "Display preference saved" : "Cannot save preference";
        }
        if (key == '\n') {
            unsigned old = info.mode;
            if (syscall(KOS_SYS_DISPLAY, selected, 0, 0, 0)) { status = "Mode not supported"; continue; }
            unsigned deadline = info.uptime + 10;
            int keep = 0;
            do {
                tui_bar(23, "Keep this mode? Y keep / Esc revert (automatic revert after 10 seconds)");
                tui_present(79, 24);
                key = tui_key(2);
                if (key == 'y' || key == '\n') { keep = 1; break; }
                if (key == 27) break;
                syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
            } while (info.uptime < deadline);
            if (!keep) syscall(KOS_SYS_DISPLAY, old, 0, 0, 0);
            status = keep ? "Mode accepted. S saves for next boot." : "Previous mode restored";
        }
    }
}
