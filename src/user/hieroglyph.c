#include "ktuilib.h"
#define LIMIT 65536u
#define ROWS 20

static int save(tui_editor* editor, char path[256], int save_as) {
    char destination[256];
    tui_copy(destination, path, 256);
    if (save_as || !*path) {
        if (!tui_prompt("Save document as (8.3 path, e.g. /HOME/NOTE.MD):", destination, 256)) {
            return 0;
        }
        tui_stat stat;
        if (!tui_stat_path(destination, &stat) && !tui_confirm("Replace existing document?")) {
            return 0;
        }
    }
    int result = syscall(10, (u32)destination, (u32)editor->text, editor->length, 0);
    if (result) {
        tui_message("Save failed", kos_error(result));
        return 0;
    }
    tui_copy(path, destination, 256);
    return 1;
}

int main(int argc, char** argv) {
    if (argc > 2) {
        write_all(2, "Usage: hieroglyph [FILE.MD]\n", 27);
        return 2;
    }
    tui_editor editor = { 0 };
    editor.capacity = LIMIT;
    editor.text = malloc(LIMIT + 1);
    if (!editor.text) {
        return 1;
    }
    char path[256] = "";
    int writing = argc < 2, dirty = 0;
    if (argc == 2) {
        tui_copy(path, argv[1], 256);
        tui_stat stat;
        int result = tui_stat_path(path, &stat);
        if (result == -1) {
            writing = 1;
        } else if (result || stat.directory || stat.size > LIMIT) {
            tui_clear(0x07);
            tui_message(
                "Cannot open document",
                result ? kos_error(result) : "Not a regular document or exceeds 64 KiB");
            free(editor.text);
            return 1;
        } else {
            int fd = open(path, KOS_O_READ);
            if (fd < 0) {
                free(editor.text);
                return 1;
            }
            while (editor.length < stat.size) {
                u32 chunk = stat.size - editor.length;
                if (chunk > 4096) {
                    chunk = 4096;
                }
                int n = read(fd, editor.text + editor.length, chunk);
                if (n <= 0) {
                    close(fd);
                    free(editor.text);
                    return 1;
                }
                editor.length += n;
            }
            close(fd);
            /* Normalize CRLF; reject binary NUL rather than silently hiding data. */
            u32 out = 0;
            for (u32 i = 0; i < editor.length; ++i) {
                if (!editor.text[i]) {
                    free(editor.text);
                    return 1;
                }
                if (editor.text[i] == '\r' && i + 1 < editor.length && editor.text[i + 1] == '\n') {
                    continue;
                }
                editor.text[out++] = editor.text[i];
            }
            editor.length = out;
        }
    }
    editor.text[editor.length] = 0;
    u32 top = 0;
    const char* status = "Markdown document | F3 switches source / formatted view";
    for (;;) {
        tui_clear(TUI_DESKTOP);
        tui_bar(0, "Hieroglyph - documents");
        tui_textn(29, 0, *path ? path : "[New document]", 47, 0x70);
        if (dirty) {
            tui_text(78, 0, "*", 0x70);
        }
        tui_window paper = { 1,  1, 78,
                             22, 1, writing ? "WRITE - Markdown source" : "READ - formatted document" };
        tui_window_draw(&paper);
        u32 total = 1;
        if (writing) {
            tui_editor_draw(&editor, 3, 2, 73, ROWS);
        } else {
            total = tui_markdown(editor.text, editor.length, 3, 2, 73, 0, 0);
            tui_document_key(0, &top, total, ROWS);
            tui_markdown(editor.text, editor.length, 3, 2, 73, ROWS, top);
            tui_scrollbar(76, 2, ROWS, top, ROWS, total);
        }
        tui_bar(23, status);
        if (!writing) {
            tui_text(57, 23, "Page", 0x70);
            tui_number(62, 23, (top + ROWS - 1) / ROWS + 1, 0x70);
            tui_text(67, 23, "/", 0x70);
            tui_number(69, 23, (total + ROWS - 1) / ROWS, 0x70);
        }
        tui_bar(24, "F1 Help F2 Save F3 Read/Write F4 Save as F10 Quit | ^Z Undo ^R Redo ^T Theme");
        tui_present(79, 24);
        int key = tui_key(1);
        if (key == TUI_F10 || key == 27) {
            if (!dirty || tui_confirm("Discard unsaved document changes?")) {
                break;
            }
        } else if (key == TUI_F1) {
            const char* help[] = { "# Heading (levels 1-6)",
                                   "**strong** *emphasis* `inline code`",
                                   "- List item; numbered lists: 1. Item",
                                   "> Quoted paragraph",
                                   "--- Horizontal separator",
                                   "``` starts / ends a literal code block",
                                   "READ: arrows, PgUp/PgDn, Space, Home/End",
                                   "WRITE: arrows, Home/End, Tab, ^Z undo, ^R redo",
                                   "F3 previews the current buffer without saving",
                                   "ASCII text, 64 KiB; LF output; no HTML or images" };
            tui_menu("Hieroglyph - Markdown help", help, 10, 0);
        } else if (key == TUI_F3) {
            writing = !writing;
        } else if (key == TUI_F2 || key == TUI_F4) {
            if (save(&editor, path, key == TUI_F4)) {
                dirty = 0;
                status = "Document saved";
            }
        } else if (writing) {
            int result = tui_editor_key(&editor, key);
            if (result > 0) {
                dirty = 1;
                status = "Modified | F3 preview";
            }
            if (result < 0) {
                status = "Cannot edit: document full or insufficient memory";
            }
        } else {
            tui_document_key(key, &top, total, ROWS);
        }
    }
    tui_editor_destroy(&editor);
    free(editor.text);
    return 0;
}
