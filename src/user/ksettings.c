#include "ktuilib.h"

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    kos_desktop_info info = {0};
    syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
    u32 selected = info.mode, top = 0;
    u32 framebuffer_theme = 1, scale = 1, font = 0;
    u32 display_info[8] = {0};
    if (syscall(KOS_SYS_FRAMEBUFFER_INFO, (u32)display_info, 0, 0, 0) == 0) {
        framebuffer_theme = display_info[1];
        scale = display_info[2];
        font = display_info[3];
    }
    const char* modes[] = {"1024 x 768 - compact", "1280 x 800 - default", "1440 x 900 - large", "1600 x 900 - wide", "1680 x 1050 - HD+", "1920 x 1080 - Full HD", "1920 x 1200 - WUXGA"};
    const char* scales[] = {"Small cells / smaller font", "Normal cells", "Large cells"};
    const char* fonts[] = {"Classic", "Thin compact", "Bold terminal"};
    const char* framebuffer_themes[] = {"Classic", "Ocean", "Amber", "Forest", "Sakura"};
    const char* status = "Select a mode; Enter tests it for 10 seconds. Y keeps, Esc restores.";
    for (;;) {
        syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
        if (syscall(KOS_SYS_FRAMEBUFFER_INFO, (u32)display_info, 0, 0, 0) == 0) {
            framebuffer_theme = display_info[1];
            scale = display_info[2];
            font = display_info[3];
        }
        tui_clear(TUI_DESKTOP);
        tui_bar(0, "KSettings - Display / Keyboard / Appearance");
        tui_box(3, 2, 74, 11, "Display resolution", TUI_BORDER);
        tui_list_draw(5, 4, 68, 7, modes, KOS_VIDEO_MODES, selected, top, 1);
        tui_text(5, 11, info.graphics ? "Framebuffer online" : "Graphics unavailable", TUI_SUCCESS);
        tui_box(3, 13, 74, 9, "Desktop preferences", TUI_BORDER);
        tui_text(5, 15, "L language | F12 / Ctrl+Shift / Alt+Shift: keyboard", 0x1f);
        tui_text(5, 17, "T TUI theme | F framebuffer theme | Z cell size", 0x1f);
        tui_text(5, 19, "G font | E theme editor | S save profile", 0x1f);
        tui_bar(22, status); tui_status(24); tui_present(79, 24);
        int key = tui_key(2);
        if (key == 'q' || key == 27 || key == TUI_F10) return 0;
        tui_list_key(key, &selected, &top, KOS_VIDEO_MODES, 7);
        if (key == 'l') syscall(KOS_SYS_LAYOUT, !info.russian, 0, 0, 0);
        if (key == 't') {
            int result = tui_theme_choose();
            status = result ? "TUI theme selected" : "TUI theme unchanged";
        }
        if (key == 'f') {
            int choice = tui_menu("Choose framebuffer theme", framebuffer_themes, KOS_FRAMEBUFFER_THEMES, framebuffer_theme);
            if (choice >= 0) {
                framebuffer_theme = choice;
                syscall(KOS_SYS_FRAMEBUFFER_THEME, framebuffer_theme, 0, 0, 0);
                status = framebuffer_themes[framebuffer_theme];
            }
        }
        if (key == 'z') {
            scale = (scale + 1) % KOS_FRAMEBUFFER_SCALES;
            syscall(KOS_SYS_FRAMEBUFFER_SCALE, scale, 0, 0, 0);
            status = scales[scale];
        }
        if (key == 'g') {
            int choice = tui_menu("Choose framebuffer font", fonts, KOS_FRAMEBUFFER_FONTS, font);
            if (choice >= 0) {
                font = choice;
                syscall(KOS_SYS_FRAMEBUFFER_FONT, font, 0, 0, 0);
                status = fonts[font];
            }
        }
        if (key == 'e') tui_launch("/BIN/KTTE.ELF", 0);
        if (key == 's') {
            unsigned char mode = info.mode;
            unsigned char settings[4] = { (unsigned char)mode, (unsigned char)framebuffer_theme,
                (unsigned char)scale, (unsigned char)font };
            status = tui_save_settings("/ETC/VIDEO.CFG", settings, sizeof(settings)) ? "Display profile saved" : "Cannot save preference";
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
