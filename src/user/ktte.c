#include "ktuilib.h"

static unsigned char colors[256];
static const char digits[] = "0123456789ABCDEF";
static const char* names[] = { "Black",      "Blue",           "Green",        "Cyan",
                               "Red",        "Magenta",        "Brown",        "Light gray",
                               "Dark gray",  "Bright blue",    "Bright green", "Bright cyan",
                               "Bright red", "Bright magenta", "Yellow",       "White" };

static const char* roles[] = { "Desktop background",
                               "Panel background",
                               "Panel text",
                               "Panel accents",
                               "Editor text / background",
                               "Selection",
                               "Popup windows",
                               "Shadows",
                               "Gauges",
                               "Input fields",
                               "Buttons",
                               "Keyboard focus",
                               "Disabled controls",
                               "Errors",
                               "Warnings",
                               "Success",
                               "Marked files",
                               "Directories",
                               "Window borders",
                               "Syntax comments",
                               "Syntax strings",
                               "Syntax numbers",
                               "Syntax keywords",
                               "Visual selection",
                               "Status text" };
static const unsigned char attributes[] = { TUI_DESKTOP, 0x17,       0x1f,          0x1e,       0x07,
                                            0x70,        TUI_POPUP,  0x08,          0x2f,       TUI_INPUT,
                                            TUI_BUTTON,  TUI_FOCUS,  TUI_DISABLED,  TUI_ERROR,  TUI_WARNING,
                                            TUI_SUCCESS, TUI_MARKED, TUI_DIRECTORY, TUI_BORDER, TUI_COMMENT,
                                            TUI_STRING,  TUI_NUMBER, TUI_KEYWORD,   TUI_VISUAL, TUI_STATUS };

static int choose_color(int value, int background) {
    if (!tui_modal_begin(background ? "Choose background color" : "Choose text color", 64, 21)) {
        return -1;
    }
    for (;;) {
        for (int i = 0; i < 16; ++i) {
            int y = 3 + i;
            tui_choice_row(10, y, 58, names[i], i == value, TUI_POPUP);
            tui_sample(40, y, " Aa ", background ? (i << 4) | 15 : i);
        }
        tui_text(11, 20, "Arrows choose | Enter accept | Esc cancel", TUI_POPUP);
        tui_present(79, 24);
        int key = tui_key(1);
        if (key == 27) {
            tui_modal_end();
            return -1;
        }
        if (key == '\n') {
            tui_modal_end();
            return value;
        }
        u32 selected = value, top = 0;
        if (key == TUI_LEFT) {
            key = TUI_UP;
        }
        if (key == TUI_RIGHT) {
            key = TUI_DOWN;
        }
        tui_list_key(key, &selected, &top, 16, 16);
        value = selected;
    }
}

static void byte(char out[3], unsigned int value) {
    out[0] = digits[(value >> 4) & 15];
    out[1] = digits[value & 15];
    out[2] = 0;
}

static int save(const char* path) {
    char text[1536];
    u32 n = 0;
    for (int i = 0; i < 256; ++i) {
        /* Keep all mappings, including values outside the usual TUI palette. */
        text[n++] = digits[i >> 4];
        text[n++] = digits[i & 15];
        text[n++] = '=';
        text[n++] = digits[colors[i] >> 4];
        text[n++] = digits[colors[i] & 15];
        text[n++] = '\n';
    }
    return syscall(10, (u32)path, (u32)text, n, 0) == 0;
}

static void help(void) {
    if (!tui_modal_begin("VGA color guide", 72, 23)) {
        return;
    }
    tui_text(7, 3, "XX=YY: source attribute = target attribute", 0x1e);
    tui_text(7, 4, "High digit: background. Low digit: foreground.", 0x1f);
    for (int i = 0; i < 16; ++i) {
        int x = i < 8 ? 7 : 40, y = 6 + i % 8;
        char label[3];
        byte(label, i);
        tui_sample(x, y, label + 1, 0x07);
        tui_sample(x + 3, y, " Aa ", i);
        tui_text(x + 9, y, names[i], 0x1f);
    }
    tui_text(7, 15, "Backgrounds 8-F may blink on legacy VGA; prefer 0-7.", 0x1e);
    tui_text(7, 17, "07 editor | 17 panels | 1F text | 1E accent", 0x1f);
    tui_text(7, 18, "70 selection/dialog | 08 shadow | 2F gauge", 0x1f);
    tui_text(7, 20, "Enter / Esc: close", 0x1e);
    tui_present(79, 24);
    int key;
    do {
        key = tui_key(1);
    } while (key != '\n' && key != 27);
    tui_modal_end();
}

int main(int argc, char** argv) {
    char path[256] = "";
    const char* status = "N new | O open | H color guide";
    u32 selected = 0, top = 0;
    int background = 0, dirty = 0, simple = 1;
    tui_clear(0x17);
    tui_theme_get(colors);
    if (argc > 1) {
        if (tui_theme_load(argv[1])) {
            tui_theme_get(colors);
            tui_copy(path, argv[1], sizeof(path));
            tui_theme_load("/ETC/TUI.THM");
        } else {
            status = "Cannot open theme";
        }
    }
    for (;;) {
        tui_clear(0x17);
        tui_bar(0, "KTTE - Kintsugi TUI Theme Editor");
        if (simple) {
            tui_text(2, 2, "Simple mode: select an interface element", 0x1e);
            tui_list_key(0, &selected, &top, sizeof(attributes), 16);
            for (u32 i = top; i < sizeof(attributes) && i < top + 16; ++i) {
                tui_choice_row(1, 4 + i - top, 46, roles[i], i == selected, 0x1f);
                tui_sample(36, 4 + i - top, " Aa 123 ", colors[attributes[i]]);
            }
            tui_scrollbar(47, 4, 16, top, 16, sizeof(attributes));
        } else {
            tui_text(2, 2, "Source  Target  Sample   Role / attribute", 0x1e);
            tui_list_key(0, &selected, &top, 256, 16);
            for (u32 i = top; i < top + 16 && i < 256; ++i) {
                int y = 4 + i - top;
                unsigned char c = i == selected ? 0x70 : 0x1f;
                char label[3];
                tui_fill(1, y, 46, 1, c);
                tui_text(1, y, i == selected ? ">" : " ", c);
                byte(label, i);
                tui_text(2, y, label, c);
                byte(label, colors[i]);
                tui_text(10, y, label, c);
                tui_sample(18, y, " Aa 123 ", colors[i]);
                const char* role = i == 7 ? "Editor"
                    : i == 0x17           ? "Panel"
                    : i == 0x1f           ? "Text"
                    : i == 0x1e           ? "Accent"
                    : i == 0x70           ? "Selection"
                    : i == 8              ? "Shadow"
                    : i == 0x2f           ? "Gauge"
                                          : "VGA attribute";
                tui_text(28, y, role, c);
            }
            tui_scrollbar(47, 4, 16, top, 16, 256);
        }
        unsigned int attribute = simple ? attributes[selected] : selected;
        tui_text(50, 3, "Edit selected target", 0x1e);
        tui_text(
            50,
            5,
            background || (simple && selected < 2) ? "[Background]  Foreground" : " Background  [Foreground]",
            0x1f);
        tui_text(50, 7, "Tab: switch digit", 0x1f);
        tui_text(50, 8, "Left/Right: change color", 0x1f);
        tui_text(50, 9, "0-9 / a-f: set digit", 0x1f);
        tui_text(50, 11, "H: color names and guide", 0x1e);
        tui_text(50, 13, "Samples show real VGA colors", 0x1f);
        tui_text(50, 15, "Live preview:", 0x1e);
        tui_sample(50, 17, " Normal text / sample 123 ", colors[attribute]);
        tui_text(50, 19, "m: simple / advanced", 0x1e);
        tui_bar(21, *path ? path : "[New theme - save under /ETC/THEMES/NAME.THM]");
        tui_bar(22, dirty ? "Modified" : "Unmodified");
        tui_bar(23, status);
        tui_bar(24, "n new o open s save w save as Shift+A apply h guide q quit | ^T themes");
        tui_present(79, 24);
        int key = tui_key(1);
        if (key == 'm') {
            simple = !simple;
            selected = top = 0;
        } else if (key == 'q' || key == 27) {
            if (!dirty || tui_confirm("Discard unsaved theme changes?")) {
                break;
            }
        } else if (key == 'h') {
            help();
        } else if (key == '\t') {
            background = !background;
        } else if (key == 'n') {
            if (dirty && !tui_confirm("Discard unsaved theme changes?")) {
                continue;
            }
            for (int i = 0; i < 256; ++i) {
                colors[i] = i;
            }
            colors[TUI_POPUP] = 0x70;
            colors[TUI_DESKTOP] = 0x17;
            static const unsigned char defaults[] = { 7,    0x70, 0x70, 8,    0x4f, 0x1e, 0x2f, 0x1e,
                                                      0x1f, 0x1f, 8,    0x0a, 0x0e, 0x0b, 0x70, 0x70 };
            for (u32 i = 0; i < sizeof(defaults); ++i) {
                colors[TUI_INPUT + i] = defaults[i];
            }
            path[0] = 0;
            dirty = 1;
            status = "New identity palette";
        } else if (key == 'o') {
            char next[256];
            if (dirty && !tui_confirm("Discard unsaved theme changes?")) {
                continue;
            }
            char input[256];
            if (tui_prompt("Open theme: name or path", input, sizeof(input))) {
                if (!tui_theme_path(next, input)) {
                    status = "Use a 1-8 character name and .THM";
                    continue;
                }
                if (tui_theme_load(next)) {
                    tui_theme_get(colors);
                    tui_copy(path, next, sizeof(path));
                    dirty = 0;
                    tui_theme_load("/ETC/TUI.THM");
                    status = "Theme loaded";
                } else {
                    status = "Cannot load theme (invalid file or path)";
                }
            }
        } else if (key == 's' || key == 'w') {
            char next[256];
            tui_copy(next, path, sizeof(next));
            if (key == 'w' || !*next) {
                char input[256];
                if (!tui_prompt("Save theme: name (stored in /ETC/THEMES)", input, sizeof(input))) {
                    continue;
                }
                if (!tui_theme_path(next, input)) {
                    status = "Use a 1-8 character name and .THM";
                    continue;
                }
            }
            tui_stat st;
            int same = 1;
            for (u32 i = 0; next[i] || path[i]; ++i) {
                if (next[i] != path[i]) {
                    same = 0;
                    break;
                }
            }
            if (!same && !tui_stat_path(next, &st) && !tui_confirm("Overwrite existing file?")) {
                continue;
            }
            if (save(next)) {
                tui_copy(path, next, sizeof(path));
                dirty = 0;
                status = "Theme saved";
            } else {
                status = "Save failed; changes kept in editor";
            }
        } else if (key == 'A') {
            if (save("/ETC/TUI.THM")) {
                tui_theme_load("/ETC/TUI.THM");
                status = "Shared theme applied";
            } else {
                status = "Cannot save active theme";
            }
        } else {
            int value = key >= '0' && key <= '9' ? key - '0' : key >= 'a' && key <= 'f' ? key - 'a' + 10 : -1;
            int shift = background ? 4 : 0;
            if (simple && selected < 2) {
                shift = 4;
            }
            if (simple && key == '\n') {
                value = choose_color((colors[attribute] >> shift) & 15, shift == 4);
            }
            if (key == TUI_LEFT || key == TUI_RIGHT) {
                value = (((colors[attribute] >> shift) & 15) + (key == TUI_RIGHT ? 1 : 15)) & 15;
            }
            if (value >= 0) {
                colors[attribute] = (colors[attribute] & ~(15 << shift)) | (value << shift);
                if (simple && selected == 1 && shift == 4) {
                    for (int i = 0x10; i <= 0x1f; ++i) {
                        colors[i] = (colors[i] & 15) | (value << 4);
                    }
                }
                dirty = 1;
            } else {
                tui_list_key(key, &selected, &top, simple ? sizeof(attributes) : 256, 16);
            }
        }
    }
    return 0;
}
