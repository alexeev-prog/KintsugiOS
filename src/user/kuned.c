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
        u32 row = 0, col = 0;
        for (u32 i = 0; i < cursor; ++i) {
            if (text[i] == '\n' || ++col == 80) {
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
        tui_clear(0x07);
        tui_bar(0, "kuned - Kintsugi Universal Editor");
        tui_text(45, 0, *path ? path : "[new file]", 0x70);
        if (dirty) {
            tui_text(78, 0, "*", 0x70);
        }
        u32 r = 0, c = 0;
        for (u32 i = 0; i < length && r < top + 22; ++i) {
            unsigned char ch = text[i];
            if (ch == '\n') {
                ++r;
                c = 0;
                continue;
            }
            if (r >= top) {
                char s[2] = { ch >= 32 && ch < 127 ? ch : '.', 0 };
                tui_text(c, r - top + 1, s, 0x07);
            }
            if (++c == 80) {
                ++r;
                c = 0;
            }
        }
        tui_bar(23, status);
        tui_bar(24, "Ctrl+S save | Ctrl+Q quit (confirm unsaved changes)");
        tui_present(col, row - top + 1);
        int key = tui_key(1);
        if (key == 17 || key == 27) {
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
