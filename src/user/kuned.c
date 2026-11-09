#include "ktuilib.h"
#define CAPACITY 65536u
#ifndef VIK
#    define VIK 0
#endif
/* Bounded undo journal: eight complete buffers, allocated in user memory. */
#define HISTORY 8
static char* undo[HISTORY];
static u32 undo_length[HISTORY], undo_cursor[HISTORY], undos;
static char* redo[HISTORY];
static u32 redo_length[HISTORY], redo_cursor[HISTORY], redos;

static void remember(const char* text, u32 length, u32 cursor) {
    while (redos) {
        free(redo[--redos]);
    }
    char* snapshot = malloc(length + 1);
    if (!snapshot) {
        return;
    }
    for (u32 i = 0; i < length; ++i) {
        snapshot[i] = text[i];
    }
    if (undos == HISTORY) {
        free(undo[0]);
        for (u32 i = 1; i < HISTORY; ++i) {
            undo[i - 1] = undo[i];
            undo_length[i - 1] = undo_length[i];
            undo_cursor[i - 1] = undo_cursor[i];
        }
        --undos;
    }
    undo[undos] = snapshot;
    undo_length[undos] = length;
    undo_cursor[undos++] = cursor;
}

#include "syntax.h"
#include "vikmode.h"
static vik_state vim;

static void history_swap(char* text, u32* length, u32* cursor, int forward) {
    char** source = forward ? redo : undo;
    char** target = forward ? undo : redo;
    u32 *sizes = forward ? redo_length : undo_length, *positions = forward ? redo_cursor : undo_cursor;
    u32 *out_sizes = forward ? undo_length : redo_length,
        *out_positions = forward ? undo_cursor : redo_cursor;
    u32 *count = forward ? &redos : &undos, *out_count = forward ? &undos : &redos;
    if (!*count || *out_count >= HISTORY) {
        return;
    }
    char* copy = malloc(*length + 1);
    if (!copy) {
        return;
    }
    for (u32 i = 0; i < *length; ++i) {
        copy[i] = text[i];
    }
    target[*out_count] = copy;
    out_sizes[*out_count] = *length;
    out_positions[(*out_count)++] = *cursor;
    --*count;
    *length = sizes[*count];
    *cursor = positions[*count];
    for (u32 i = 0; i < *length; ++i) {
        text[i] = source[*count][i];
    }
    free(source[*count]);
}

static int find(const char* text, u32 length, const char* query, u32 start) {
    u32 n = tui_len(query);
    if (!n || n > length) {
        return -1;
    }
    for (u32 step = 0; step <= length; ++step) {
        u32 i = (start + step) % (length + 1), j = 0;
        if (n > length - i) {
            continue;
        }
        while (j < n && text[i + j] == query[j]) {
            ++j;
        }
        if (j == n) {
            return i;
        }
    }
    return -1;
}

int main(int argc, char** argv) {
    int readonly = 0;
    if (argc > 2) {
        const char* option = "--readonly";
        u32 i = 0;
        while (option[i] && option[i] == argv[2][i]) {
            ++i;
        }
        readonly = !option[i] && !argv[2][i];
    }
    char path[256];
    tui_copy(path, argc > 1 ? argv[1] : "", sizeof(path));
    char* text = malloc(CAPACITY + 1);
    if (!text) {
        return 1;
    }
    u32 length = 0, cursor = 0, top = 0;
    char query[128] = "";
    int insert = !VIK, pending = 0;
    int dirty = 0;
    unsigned char settings[2] = { 1, 0 }; /* line numbers, dark blue theme */
    tui_load_settings("/ETC/KUNED.CFG", settings, sizeof(settings));
    settings[0] = !!settings[0];
    settings[1] = !!settings[1];
    const char* status =
        VIK ? "VIK | :w save :q quit :q! discard" : "Ctrl+S save | Ctrl+Q quit | arrows/Home/End edit";
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
        u32 logical_line = 1, logical_col = 1;
        for (u32 i = 0; i < cursor; ++i) {
            if (text[i] == '\n') {
                ++logical_line;
                logical_col = 1;
            } else {
                ++logical_col;
            }
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
        if (!VIK) {
            tui_bar(0, "kuned - Kintsugi Universal Editor");
        }
        tui_text(45, 0, *path ? path : "[new file]", 0x70);
        if (dirty) {
            tui_text(78, 0, "*", 0x70);
        }
        u32 r = 0, c = 0;
        syntax_state syntax = { .language = syntax_language(path) };
        u32 line = 1;
        if (settings[0] && !top) {
            tui_number(0, 1, line, 0x08);
        }
        int row_number_drawn = !top;
        for (u32 i = 0; i < length && r < top + 22; ++i) {
            unsigned char ch = text[i];
            unsigned char ink = syntax_color(&syntax, text, length, i, color);
            if (VIK && vim.visual) {
                u32 a = vim.anchor < cursor ? vim.anchor : cursor;
                u32 b = vim.anchor > cursor ? vim.anchor : cursor;
                if (vim.linewise) {
                    a = vik_start(text, a);
                    b = vik_end(text, length, b);
                }
                if (i >= a && i <= b) {
                    ink = TUI_VISUAL;
                }
            }
            if (ch == '\n') {
                ++r;
                ++line;
                c = 0;
                if (settings[0] && r >= top && r < top + 22) {
                    tui_number(0, r - top + 1, line, 0x08);
                }
                row_number_drawn = r >= top;
                continue;
            }
            if (r >= top) {
                if (settings[0] && !row_number_drawn && c == 0) {
                    tui_number(0, r - top + 1, line, 0x08);
                    row_number_drawn = 1;
                }
                char s[2] = { ch >= 32 && ch < 127 ? ch : '.', 0 };
                tui_text(c + gutter, r - top + 1, s, ink);
            }
            if (++c == width) {
                ++r;
                c = 0;
            }
        }
        tui_bar(
            23,
            readonly ? (VIK ? "Read-only | / find :q quit" : "Read-only viewer | Ctrl+F find | Ctrl+Q quit")
                     : status);
        tui_text(38, 23, "Ln", 0x70);
        tui_number(41, 23, logical_line, 0x70);
        tui_text(48, 23, "Col", 0x70);
        tui_number(52, 23, logical_col, 0x70);
        tui_number(60, 23, length, 0x70);
        tui_text(68, 23, "bytes", 0x70);
        tui_bar(24, "^S save ^Q quit ^F find ^N next ^R replace ^Z undo ^G line ^A save as ^E help");
        if (VIK) {
            tui_bar(0, "");
            tui_text(1, 0, *path ? path : "[No Name]", 0x70);
            tui_bar(
                24,
                insert           ? "-- INSERT -- | Esc normal | :w save in normal mode"
                    : vim.visual ? "-- VISUAL -- | motions select | y yank d delete c change | Esc cancel"
                                 : "-- NORMAL -- | hjkl i/a/o | d/c/y+motion | v/V p/P u ^R | / :w :q");
        }
        tui_present(col + gutter, row - top + 1);
        int key = tui_key(1);
        if (VIK && !insert && key == 18) {
            if (!readonly) {
                history_swap(text, &length, &cursor, 1);
                dirty = 1;
            }
            continue;
        }
        if (VIK && key > 0 && key < 32 && key != 27 && key != '\n' && key != '\t' && key != 8) {
            continue;
        }
        if (VIK && insert && key == 27) {
            insert = 0;
            if (cursor && text[cursor - 1] != '\n') {
                --cursor;
            }
            continue;
        }
        if (VIK && !insert) {
            if (vik_key(&vim, text, &length, &cursor, key, readonly, &insert, &dirty)) {
                continue;
            }
            if (key != 'g' && key != 'd') {
                pending = 0;
            }
            if (key == '\n' || key == '\t' || key == 8) {
                continue;
            }
            if (key == ':') {
                char command[256];
                if (!tui_command(":", command, sizeof(command))) {
                    continue;
                }
                if (command[0] == 't' && command[1] == 'h' && command[2] == 'e' && command[3] == 'm'
                    && command[4] == 'e' && !command[5]) {
                    tui_theme_choose();
                    continue;
                }
                if (command[0] == 'q' && (!command[1] || (command[1] == '!' && !command[2]))) {
                    if (!dirty || command[1] == '!') {
                        break;
                    }
                    status = "Unsaved changes; :w or :q!";
                    continue;
                }
                if (command[0] == 'w'
                    && (!command[1] || command[1] == ' ' || (command[1] == 'q' && !command[2]))) {
                    char destination[256];
                    tui_copy(destination, command[1] == ' ' ? command + 2 : path, 256);
                    if (readonly) {
                        status = "Read-only";
                    } else if (!*destination) {
                        status = "Use :w PATH";
                    } else if (syscall(10, (u32)destination, (u32)text, length, 0)) {
                        status = "Save failed";
                    } else {
                        tui_copy(path, destination, 256);
                        dirty = 0;
                        status = "Saved";
                        if (command[1] == 'q') {
                            break;
                        }
                    }
                } else {
                    status = "Unknown command";
                }
                continue;
            }
            if (key == 'w' || key == 'b') {
                if (key == 'w') {
                    while (cursor < length && text[cursor] > ' ') {
                        ++cursor;
                    }
                    while (cursor < length && text[cursor] <= ' ') {
                        ++cursor;
                    }
                } else {
                    while (cursor && text[cursor - 1] <= ' ') {
                        --cursor;
                    }
                    while (cursor && text[cursor - 1] > ' ') {
                        --cursor;
                    }
                }
                continue;
            }
            if (key == 'd') {
                if (pending == 'd' && !readonly) {
                    remember(text, length, cursor);
                    u32 begin = cursor, end = cursor;
                    while (begin && text[begin - 1] != '\n') {
                        --begin;
                    }
                    while (end < length && text[end] != '\n') {
                        ++end;
                    }
                    if (end < length) {
                        ++end;
                    }
                    for (u32 i = end; i < length; ++i) {
                        text[begin + i - end] = text[i];
                    }
                    length -= end - begin;
                    cursor = begin;
                    dirty = 1;
                    pending = 0;
                } else {
                    pending = 'd';
                }
                continue;
            }
            if (key == 'i' || key == 'a' || key == 'o' || key == 'I' || key == 'A') {
                if (readonly) {
                    status = "Read-only";
                    continue;
                }
                insert = 1;
                if (key == 'I') {
                    while (cursor && text[cursor - 1] != '\n') {
                        --cursor;
                    }
                }
                if (key == 'A') {
                    while (cursor < length && text[cursor] != '\n') {
                        ++cursor;
                    }
                }
                if (key == 'a' && cursor < length && text[cursor] != '\n') {
                    ++cursor;
                }
                if (key != 'o') {
                    continue;
                }
                while (cursor < length && text[cursor] != '\n') {
                    ++cursor;
                }
                key = '\n';
                /* Insert before the line separator, then land on the new line. */
            } else if (key == 'h') {
                key = TUI_LEFT;
            } else if (key == 'j') {
                key = TUI_DOWN;
            } else if (key == 'k') {
                key = TUI_UP;
            } else if (key == 'l') {
                key = TUI_RIGHT;
            } else if (key == '0') {
                key = TUI_HOME;
            } else if (key == '$') {
                key = TUI_END;
            } else if (key == 'x') {
                key = TUI_DELETE;
            } else if (key == 'u') {
                key = 26;
            } else if (key == '/') {
                if (tui_command("/", query, sizeof(query))) {
                    int at = find(text, length, query, cursor + (cursor < length));
                    if (at >= 0) {
                        cursor = at;
                    } else {
                        status = "Pattern not found";
                    }
                }
                continue;
            } else if (key == 'n') {
                key = 14;
            } else if (key == 'G') {
                cursor = length;
                continue;
            } else if (key == 'g') {
                if (pending == 'g') {
                    cursor = 0;
                    pending = 0;
                } else {
                    pending = 'g';
                }
                continue;
            } else if (key >= 32 && key < 127) {
                pending = 0;
                continue;
            } else if (key == 27) {
                pending = 0;
                continue;
            }
        }
        if (key == 5) {
            tui_message("Editor help", "8 edits undo; Tab inserts 4 spaces; find wraps; ^O settings");
        } else if (key == 7) {
            char value[12];
            if (tui_prompt("Go to line:", value, sizeof(value))) {
                u32 target = 0;
                int valid = 1;
                for (u32 i = 0; value[i]; ++i) {
                    if (value[i] < '0' || value[i] > '9' || target > 65536) {
                        valid = 0;
                        break;
                    }
                    target = target * 10 + value[i] - '0';
                }
                if (valid && target) {
                    cursor = 0;
                    for (u32 line = 1; cursor < length && line < target; ++cursor) {
                        if (text[cursor] == '\n') {
                            ++line;
                        }
                    }
                    status = "Line selected";
                } else {
                    status = "Invalid line number";
                }
            }
        } else if (key == 26 && !readonly) {
            if (undos) {
                history_swap(text, &length, &cursor, 0);
                dirty = 1;
                status = "Undo";
            } else {
                status = "No more undo (8 edits retained)";
            }
        } else if (key == 18 && !readonly) {
            char replacement[128];
            if (tui_prompt("Find text to replace:", query, sizeof(query))
                && tui_prompt("Replacement text (nonempty):", replacement, sizeof(replacement))) {
                int at = find(text, length, query, cursor);
                u32 old = tui_len(query), n = tui_len(replacement);
                if (at < 0) {
                    status = "Not found";
                } else if (length - old + n > CAPACITY) {
                    status = "Buffer full";
                } else {
                    remember(text, length, cursor);
                    if (n > old) {
                        for (u32 i = length; i > (u32)at + old; --i) {
                            text[i - 1 + n - old] = text[i - 1];
                        }
                    } else {
                        for (u32 i = at + old; i < length; ++i) {
                            text[i - old + n] = text[i];
                        }
                    }
                    for (u32 i = 0; i < n; ++i) {
                        text[at + i] = replacement[i];
                    }
                    length = length - old + n;
                    cursor = at + n;
                    dirty = 1;
                    status = "Replaced one match";
                }
            }
        } else if (key == 14) {
            int at = find(text, length, query, cursor + (cursor < length));
            if (at >= 0) {
                cursor = at;
                status = "Found (wrap search)";
            } else {
                status = "Not found";
            }
        } else if (key == 15) {
            const char* items[] = { "Toggle line numbers", "Toggle theme", "Save settings" };
            int choice = tui_menu("Editor settings", items, 3, 0);
            if (choice == 0) {
                settings[0] ^= 1;
            }
            if (choice == 1) {
                settings[1] ^= 1;
            }
            if (choice == 2) {
                status = tui_save_settings("/ETC/KUNED.CFG", settings, sizeof(settings))
                    ? "Settings saved"
                    : "Cannot save settings";
            }
        } else if (key == 6) {
            if (tui_prompt("Find literal text (after cursor):", query, sizeof(query))) {
                int at = find(text, length, query, cursor + (cursor < length));
                if (at >= 0) {
                    cursor = at;
                    status = "Found";
                } else {
                    status = "Not found";
                }
            }
        } else if (key == 17 || key == 27) {
            if (!dirty || tui_confirm("Discard unsaved changes? y/N")) {
                break;
            }
        } else if ((key == 19 || key == 1) && !readonly) {
            char destination[256];
            tui_copy(destination, path, sizeof(destination));
            if ((key == 1 || !*path)
                && !tui_prompt("Save as (8.3 path):", destination, sizeof(destination))) {
                continue;
            }
            int result = syscall(10, (u32)destination, (u32)text, length, 0);
            if (!result) {
                tui_copy(path, destination, sizeof(path));
                dirty = 0;
                status = "Saved";
            } else {
                status = "Save failed - file unchanged or disk unavailable";
            }
        } else if (
            key == TUI_PAGE_UP || key == TUI_PAGE_DOWN || key == TUI_SCROLL_UP || key == TUI_SCROLL_DOWN) {
            u32 steps = key == TUI_PAGE_UP || key == TUI_PAGE_DOWN ? 21 : 1;
            int down = key == TUI_PAGE_DOWN || key == TUI_SCROLL_DOWN;
            while (steps--) {
                if (down) {
                    while (cursor < length && text[cursor] != '\n') {
                        ++cursor;
                    }
                    if (cursor < length) {
                        ++cursor;
                    }
                } else {
                    while (cursor && text[cursor - 1] != '\n') {
                        --cursor;
                    }
                    if (cursor) {
                        --cursor;
                        while (cursor && text[cursor - 1] != '\n') {
                            --cursor;
                        }
                    }
                }
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
            u32 n = logical_col - 1;
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
            u32 n = logical_col - 1;
            while (cursor < length && text[cursor] != '\n') {
                ++cursor;
            }
            if (cursor < length) {
                ++cursor;
            }
            while (n-- && cursor < length && text[cursor] != '\n') {
                ++cursor;
            }
        } else if (!readonly && ((key == 8 && cursor) || (key == TUI_DELETE && cursor < length))) {
            remember(text, length, cursor);
            if (key == 8) {
                --cursor;
            }
            for (u32 i = cursor; i + 1 < length; ++i) {
                text[i] = text[i + 1];
            }
            --length;
            dirty = 1;
        } else if (!readonly && key == '\t' && length + 4 <= CAPACITY) {
            remember(text, length, cursor);
            for (u32 i = length; i > cursor; --i) {
                text[i + 3] = text[i - 1];
            }
            for (u32 i = 0; i < 4; ++i) {
                text[cursor++] = ' ';
            }
            length += 4;
            dirty = 1;
        } else if (!readonly && (key == '\n' || (key >= 32 && key < 127)) && length < CAPACITY) {
            remember(text, length, cursor);
            for (u32 i = length; i > cursor; --i) {
                text[i] = text[i - 1];
            }
            text[cursor++] = key;
            ++length;
            dirty = 1;
        }
    }
    free(text);
    while (undos) {
        free(undo[--undos]);
    }
    while (redos) {
        free(redo[--redos]);
    }
    for (int i = 0; i < 27; ++i) {
        free(vim.registers[i]);
    }
    return 0;
}
