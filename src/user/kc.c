#include "ktuilib.h"
static tui_entry entries[512];

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    char directory[256];
    syscall(4, (u32)directory, sizeof(directory), 0, 0);
    u32 selected = 0, top = 0;
    const char* status = "Ready";
    for (;;) {
        u32 cursor = 0, count = 0;
        int result = 0;
        while (count < 512
               && (result = syscall(3, (u32)directory, (u32)&cursor, (u32)&entries[count], 0)) > 0) {
            ++count;
        }
        if (result < 0) {
            status = "Directory read failed";
        }
        if (selected >= count) {
            selected = count ? count - 1 : 0;
        }
        if (selected < top) {
            top = selected;
        }
        if (selected >= top + 20) {
            top = selected - 19;
        }
        tui_clear(0x17);
        tui_bar(0, "KC - Kintsugi Commander");
        tui_text(30, 0, directory, 0x70);
        tui_text(2, 1, "Name             Type       Size", 0x1e);
        for (u32 i = top; i < count && i < top + 20; ++i) {
            int y = i - top + 2;
            unsigned char color = i == selected ? 0x70 : 0x17;
            tui_text(2, y, entries[i].name, color);
            tui_text(19, y, entries[i].stat.directory ? "<DIR>" : "file", color);
            tui_number(30, y, entries[i].stat.size, color);
        }
        tui_bar(23, status);
        tui_bar(24, "Enter open dir | Backspace parent | C copy M move D delete N mkdir Q quit");
        tui_present(79, 24);
        int key = tui_key(1);
        if (key == 'q' || key == 'Q' || key == 27 || key == 3) {
            break;
        }
        if (key == TUI_UP && selected) {
            --selected;
        } else if (key == TUI_DOWN && selected + 1 < count) {
            ++selected;
        } else if (key == 8) {
            u32 n = tui_len(directory);
            while (n > 1 && directory[n - 1] != '/') {
                --n;
            }
            if (n > 1) {
                --n;
            }
            directory[n] = 0;
            selected = top = 0;
        } else {
            char source[256], destination[256], input[256];
            if (count) {
                tui_path(source, directory, entries[selected].name);
            }
            if (key == '\n' && count && entries[selected].stat.directory) {
                tui_copy(directory, source, sizeof(directory));
                selected = top = 0;
            } else if (key == 'n' || key == 'N') {
                if (!tui_prompt("New directory name:", input, sizeof(input))) {
                    continue;
                }
                tui_path(destination, directory, input);
                status = syscall(11, (u32)destination, 0, 0, 0) ? "mkdir failed" : "Directory created";
            } else if ((key == 'd' || key == 'D') && count) {
                if (!tui_confirm("Delete selected entry? y/N (directories must be empty)")) {
                    continue;
                }
                status = syscall(11, (u32)source, entries[selected].stat.directory ? 2 : 1, 0, 0)
                    ? "Delete failed"
                    : "Deleted";
            } else if ((key == 'c' || key == 'C' || key == 'm' || key == 'M') && count) {
                if (!tui_prompt(
                        "Destination (absolute path or name in current panel):", input, sizeof(input))) {
                    continue;
                }
                if (*input == '/') {
                    tui_copy(destination, input, sizeof(destination));
                } else {
                    tui_path(destination, directory, input);
                }
                status = syscall(12, (u32)source, (u32)destination, key == 'm' || key == 'M', 0)
                    ? "Operation failed (no overwrite; no recursive copy)"
                    : "Done";
            }
        }
    }
    return 0;
}
