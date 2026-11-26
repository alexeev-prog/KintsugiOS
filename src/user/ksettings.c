#include "ktuilib.h"

typedef struct {
    const char* name;
    const char* const* options;
    int count;
} setting_group;

static const char* modes[] = { "1024 x 768 - compact", "1280 x 800 - default", "1440 x 900 - large",
                               "1600 x 900 - wide",    "1680 x 1050 - HD+",    "1920 x 1080 - Full HD",
                               "1920 x 1200 - WUXGA" };
/* Indices must match framebuffer_theme(): 0 Classic, 1 Ocean, 2 Amber,
 * 3 Forest, 4 Sakura, 5 Mono (default). */
static const char* themes[] = { "Classic", "Ocean", "Amber", "Forest", "Sakura", "Mono (dark minimal)" };
static const char* fonts[] = { "Classic", "Thin compact", "Bold terminal" };
static const char* scales[] = { "Small cells", "Normal cells", "Large cells" };
static const char* font_sizes[] = { "75% compact", "100% normal", "125% large", "150% extra large" };
static const char* actions[] = { "Apply now", "Open theme editor", "Save profile" };

static const setting_group groups[] = {
    { "Display resolution", modes,      KOS_VIDEO_MODES            },
    { "Framebuffer theme",  themes,     KOS_FRAMEBUFFER_THEMES     },
    { "Font",               fonts,      KOS_FRAMEBUFFER_FONTS      },
    { "Cell size",          scales,     KOS_FRAMEBUFFER_SCALES     },
    { "Font size",          font_sizes, KOS_FRAMEBUFFER_FONT_SIZES },
    { "TUI theme",          0,          0                          },
    { "Actions",            actions,    3                          }
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    kos_desktop_info info = { 0 };
    syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
    u32 fb[9] = { 0 };
    syscall(KOS_SYS_FRAMEBUFFER_INFO, (u32)fb, 0, 0, 0);
    u32 selected_mode = fb[0], theme = fb[1], scale = fb[2], font = fb[3], font_size = fb[4];
    u32 cat = 0;
    const char* status = "Up/Down select category | Enter open list | Q exit";

    for (;;) {
        syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
        tui_clear(TUI_DESKTOP);
        tui_bar(0, "KSettings - System appearance and display");
        tui_box(2, 2, 25, 18, "Categories", TUI_BORDER);
        tui_box(29, 2, 49, 18, "Settings", TUI_BORDER);
        for (u32 i = 0; i < 7; ++i) {
            tui_choice_row(4, 4 + i, 21, groups[i].name, i == cat, TUI_STATUS);
        }
        tui_text(31, 4, "Current configuration", TUI_KEYWORD);
        tui_text(31, 6, "Resolution:", TUI_STATUS);
        tui_textn(44, 6, modes[selected_mode], 32, TUI_NUMBER);
        tui_text(31, 7, "Framebuffer:", TUI_STATUS);
        tui_textn(44, 7, themes[theme], 32, TUI_NUMBER);
        tui_text(31, 8, "Font:", TUI_STATUS);
        tui_textn(44, 8, fonts[font], 32, TUI_NUMBER);
        tui_text(31, 9, "Cells:", TUI_STATUS);
        tui_textn(44, 9, scales[scale], 32, TUI_NUMBER);
        tui_text(31, 10, "Font size:", TUI_STATUS);
        tui_textn(44, 10, font_sizes[font_size], 32, TUI_NUMBER);
        tui_text(31, 11, "TUI theme:", TUI_STATUS);
        tui_text(44, 11, "stored in /etc/TUI.THM", TUI_NUMBER);
        tui_text(
            31,
            12,
            info.graphics ? "Framebuffer: online" : "Framebuffer: unavailable",
            info.graphics ? TUI_SUCCESS : TUI_ERROR);
        tui_text(31, 13, "Adapter:", TUI_STATUS);
        tui_text(40, 13, "Bochs / standard VGA", TUI_NUMBER);
        tui_text(31, 15, "Keyboard: F12 / Ctrl+Shift / Alt+Shift", TUI_COMMENT);
        tui_bar(21, status);
        tui_status(24);
        tui_present(79, 24);

        int key = tui_key(1);
        if (key == 'q' || key == 27 || key == TUI_F10) {
            return 0;
        }
        if (key == TUI_UP && cat) {
            --cat;
        }
        if (key == TUI_DOWN && cat < 6) {
            ++cat;
        }
        if (key == 'l') {
            syscall(KOS_SYS_LAYOUT, !info.russian, 0, 0, 0);
            status = "Keyboard layout switched";
        }
        if (key != '\n') {
            continue;
        }

        if (cat == 5) {
            tui_theme_choose();
            status = "TUI theme applied";
            continue;
        }
        if (cat == 6) {
            int choice = tui_menu("Actions", actions, 3, 0);
            if (choice == 0) {
                if (syscall(KOS_SYS_DISPLAY, selected_mode, 0, 0, 0)) {
                    status = "Display mode not supported";
                } else {
                    status = "Display mode applied; S in profile saves it";
                }
            } else if (choice == 1) {
                tui_launch("/bin/KTTE.ELF", 0);
                status = "Theme editor closed";
            } else if (choice == 2) {
                unsigned char cfg[5] = { (unsigned char)selected_mode,
                                         (unsigned char)theme,
                                         (unsigned char)scale,
                                         (unsigned char)font,
                                         (unsigned char)font_size };
                status = tui_save_settings("/etc/VIDEO.CFG", cfg, sizeof(cfg))
                    ? "Display profile saved"
                    : "Cannot save display profile";
            }
            continue;
        }

        int current = cat == 0 ? (int)selected_mode
            : cat == 1         ? (int)theme
            : cat == 2         ? (int)font
            : cat == 3         ? (int)scale
                               : (int)font_size;
        int choice = tui_menu(groups[cat].name, groups[cat].options, groups[cat].count, current);
        if (choice < 0) {
            status = "Selection cancelled";
            continue;
        }
        if (cat == 0) {
            unsigned old = info.mode;
            if (syscall(KOS_SYS_DISPLAY, choice, 0, 0, 0)) {
                status = "Mode not supported";
                continue;
            }
            selected_mode = choice;
            tui_bar(21, "Keep this mode? Y keep / Esc revert (auto-revert in 10 seconds)");
            tui_present(79, 24);
            unsigned deadline = info.uptime + 10;
            int keep = 0;
            for (;;) {
                int k = tui_key(2);
                if (k == 'y' || k == '\n') {
                    keep = 1;
                    break;
                }
                if (k == 27) {
                    break;
                }
                syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
                if (info.uptime >= deadline) {
                    break;
                }
            }
            if (!keep) {
                syscall(KOS_SYS_DISPLAY, old, 0, 0, 0);
                selected_mode = old;
            }
            status = keep ? "Mode accepted; save via Actions" : "Previous mode restored";
        } else if (cat == 1) {
            theme = choice;
            syscall(KOS_SYS_FRAMEBUFFER_THEME, theme, 0, 0, 0);
            status = "Framebuffer theme applied";
        } else if (cat == 2) {
            font = choice;
            syscall(KOS_SYS_FRAMEBUFFER_FONT, font, 0, 0, 0);
            status = "Font applied";
        } else if (cat == 3) {
            scale = choice;
            syscall(KOS_SYS_FRAMEBUFFER_SCALE, scale, 0, 0, 0);
            status = "Cell size applied";
        } else {
            font_size = choice;
            syscall(KOS_SYS_FRAMEBUFFER_FONT_SIZE, font_size, 0, 0, 0);
            status = "Font size applied";
        }
    }
}
