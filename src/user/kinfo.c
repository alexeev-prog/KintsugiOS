#include "ktuilib.h"
#include "sysabi.h"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    for (;;) {
        kos_desktop_info info = { 0 };
        kos_mouse mouse = { 0 };
        syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
        syscall(KOS_SYS_MOUSE, (u32)&mouse, 0, 0, 0);
        tui_clear(TUI_DESKTOP);
        tui_bar(0, "KInfo - desktop and input monitor");
        tui_box(4, 3, 72, 18, "System overview", TUI_BORDER);
        tui_text(7, 6, "Free memory KiB", 0x1f);
        tui_number(40, 6, info.free_kib, TUI_NUMBER);
        tui_text(7, 8, "Processes / uptime seconds", 0x1f);
        tui_number(40, 8, info.processes, TUI_NUMBER);
        tui_number(48, 8, info.uptime, TUI_NUMBER);
        tui_gauge(7, 10, 64, info.ram_kib - info.free_kib, info.ram_kib);
        tui_text(7, 12, "Display pixels", 0x1f);
        tui_number(40, 12, info.width, TUI_NUMBER);
        tui_number(48, 12, info.height, TUI_NUMBER);
        tui_text(7, 14, mouse.available ? "PS/2 mouse online" : "PS/2 mouse unavailable", TUI_SUCCESS);
        tui_number(40, 14, mouse.x, TUI_NUMBER);
        tui_number(48, 14, mouse.y, TUI_NUMBER);
        tui_text(7, 16, info.russian ? "Keyboard RU / F12 switches" : "Keyboard EN / F12 switches", 0x1f);
        tui_text(7, 18, "User ELF32 / ring3 / supervisor-only framebuffer", 0x1f);
        tui_bar(23, "Q quit | S settings | T themes");
        tui_status(24);
        tui_present(79, 24);
        int key = tui_key(2);
        if (key == 'q' || key == 27) {
            return 0;
        }
        if (key == 's') {
            tui_launch("/bin/KSET.ELF", 0);
        }
        if (key == 't') {
            tui_theme_choose();
        }
        if (key == 'f') {
            syscall(KOS_SYS_FRAMEBUFFER_THEME, (info.mode + 1) % KOS_FRAMEBUFFER_THEMES, 0, 0, 0);
        }
    }
}
