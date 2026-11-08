#include "ktuilib.h"
#define CAPACITY 65536u

int main(int argc, char** argv) {
    char path[256];
    tui_copy(path, argc > 1 ? argv[1] : "", sizeof(path));
    char* text = malloc(CAPACITY + 1);
    if (!text) {
        return 1;
    }
    u32 length = 0, cursor = 0, top = 0;
    int dirty = 0;
    unsigned char settings[2] = {1, 0}; /* line numbers, dark blue theme */
    tui_load_settings("/ETC/KUNED.CFG", settings, sizeof(settings));
    settings[0] = !!settings[0]; settings[1] = !!settings[1];
    const char* status = "Ctrl+S save | Ctrl+Q quit | arrows/Home/End edit";
    if (*path) {
        tui_stat stat;
        int result = tui_stat_path(path, &stat);
        if (!result) {
            if (stat.directory || stat.size > CAPACITY) {
                free(text);
                return 1;
            }
            while (length < stat.size) {
                u32 n = stat.size - length;
                if (n > 4096) {
                    n = 4096;
                }
                int got = syscall(2, (u32)path, length, (u32)(text + length), n);
                if (got <= 0) {
                    free(text);
                    return 1;
                }
                length += got;
            }
        } else if (result != -1) {
            free(text);
            return 1;
        }
    }
    for (;;) {
        u32 gutter = settings[0] ? 6 : 0, width = 80 - gutter;
        unsigned char color = settings[1] ? 0x17 : 0x07;
        u32 row = 0, col = 0;
        for (u32 i = 0; i < cursor; ++i) {
            if (text[i] == '\n' || ++col == width) {
                ++row;
                col = 0;
            }
        }
        if (row < top) {
            top = row;
        }
        if (row >= top + 22) {
            top = row - 21;
        }
        tui_clear(color);
        tui_bar(0, "kuned - Kintsugi Universal Editor");
        tui_text(45, 0, *path ? path : "[new file]", 0x70);
        if (dirty) {
            tui_text(78, 0, "*", 0x70);
        }
        u32 r = 0, c = 0;
        u32 line = 1;
        if (settings[0] && !top) tui_number(0, 1, line, 0x08);
        int row_number_drawn = !top;
        for (u32 i = 0; i < length && r < top + 22; ++i) {
            unsigned char ch = text[i];
            if (ch == '\n') {
                ++r;
                ++line;
                c = 0;
                if (settings[0] && r >= top && r < top + 22)
                    tui_number(0, r - top + 1, line, 0x08);
                row_number_drawn = r >= top;
                continue;
            }
            if (r >= top) {
                if (settings[0] && !row_number_drawn && c == 0) {
                    tui_number(0, r - top + 1, line, 0x08);
                    row_number_drawn = 1;
                }
                char s[2] = { ch >= 32 && ch < 127 ? ch : '.', 0 };
                tui_text(c + gutter, r - top + 1, s, color);
            }
            if (++c == width) {
                ++r;
                c = 0;
            }
        }
        tui_bar(23, status);
        tui_bar(24, "Ctrl+S save | Ctrl+Q quit | Ctrl+O settings | Ctrl+F find");
        tui_present(col + gutter, row - top + 1);
        int key = tui_key(1);
        if (key == 15) {
            tui_box(10, 7, 60, 10, "Editor settings", 0x70);
            tui_text(12, 9, "N toggle line numbers / T toggle theme", 0x70);
            tui_text(12, 11, "S save settings / Esc close", 0x70);
            tui_present(79, 24);
            int choice = tui_key(1);
            if (choice == 'n') settings[0] ^= 1;
            if (choice == 't') settings[1] ^= 1;
            if (choice == 's') status = tui_save_settings("/ETC/KUNED.CFG", settings, sizeof(settings))
                ? "Settings saved" : "Cannot save settings";
        } else if (key == 6) {
            char query[128];
            if (tui_prompt("Find literal text (after cursor):", query, sizeof(query))) {
                u32 n = tui_len(query), i = cursor + (cursor < length);
                for (; i <= length && n <= length - i; ++i) {
                    u32 j = 0;
                    while (j < n && text[i+j] == query[j]) ++j;
                    if (j == n) { cursor = i; break; }
                }
                status = i <= length && n <= length - i ? "Found" : "Not found";
            }
        } else if (key == 17 || key == 27) {
            if (!dirty || tui_confirm("Discard unsaved changes? y/N")) {
                break;
            }
        } else if (key == 19) {
            if (!*path && !tui_prompt("Save as (8.3 path):", path, sizeof(path))) {
                continue;
            }
            int result = syscall(10, (u32)path, (u32)text, length, 0);
            if (!result) {
                dirty = 0;
                status = "Saved";
            } else {
                status = "Save failed - file unchanged or disk unavailable";
            }
        } else if (key == TUI_LEFT && cursor) {
            --cursor;
        } else if (key == TUI_RIGHT && cursor < length) {
            ++cursor;
        } else if (key == TUI_HOME) {
            while (cursor && text[cursor - 1] != '\n') {
                --cursor;
            }
        } else if (key == TUI_END) {
            while (cursor < length && text[cursor] != '\n') {
                ++cursor;
            }
        } else if (key == TUI_UP) {
            u32 n = col;
            while (cursor && text[cursor - 1] != '\n') {
                --cursor;
            }
            if (cursor) {
                --cursor;
                while (cursor && text[cursor - 1] != '\n') {
                    --cursor;
                }
                while (n-- && cursor < length && text[cursor] != '\n') {
                    ++cursor;
                }
            }
        } else if (key == TUI_DOWN) {
            u32 n = col;
            while (cursor < length && text[cursor] != '\n') {
                ++cursor;
            }
            if (cursor < length) {
                ++cursor;
            }
            while (n-- && cursor < length && text[cursor] != '\n') {
                ++cursor;
            }
        } else if ((key == 8 && cursor) || (key == TUI_DELETE && cursor < length)) {
            if (key == 8) {
                --cursor;
            }
            for (u32 i = cursor; i + 1 < length; ++i) {
                text[i] = text[i + 1];
            }
            --length;
            dirty = 1;
        } else if ((key == '\n' || (key >= 32 && key < 127)) && length < CAPACITY) {
            for (u32 i = length; i > cursor; --i) {
                text[i] = text[i - 1];
            }
            text[cursor++] = key;
            ++length;
            dirty = 1;
        }
    }
    free(text);
    return 0;
}
