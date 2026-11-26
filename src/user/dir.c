#include "ktuilib.h"

static int executable(const char* name) {
    u32 n = tui_len(name);
    return n > 4 && name[n - 4] == '.' && name[n - 3] == 'E' && name[n - 2] == 'L' && name[n - 1] == 'F';
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : ".";
    tui_entry entries[128];
    u32 cursor = 0, count = 0;
    while (count < 128 && tui_readdir(path, &cursor, &entries[count]) > 0) {
        ++count;
    }
    tui_clear(TUI_DESKTOP);
    tui_bar(0, "DIR - File Explorer");
    tui_text(2, 1, path, TUI_HEADER);
    for (u32 i = 0; i < count && i < 20; ++i) {
        unsigned char color = entries[i].stat.directory ? TUI_DIRECTORY
            : executable(entries[i].name)               ? TUI_EXECUTABLE_COLOR
                                                        : TUI_FILE_COLOR;
        tui_text(2, 3 + (int)i, entries[i].name, color);
        if (entries[i].stat.directory) {
            tui_text(24, 3 + (int)i, "<DIR>", color);
        } else {
            /* Windows-style explorer column: compact human-readable size. */
            if (entries[i].stat.size >= 1024 * 1024) {
                tui_number(24, 3 + (int)i, entries[i].stat.size / (1024 * 1024), color);
                tui_text(35, 3 + (int)i, "MB", color);
            } else if (entries[i].stat.size >= 1024) {
                tui_number(24, 3 + (int)i, entries[i].stat.size / 1024, color);
                tui_text(35, 3 + (int)i, "KB", color);
            } else {
                tui_number(24, 3 + (int)i, entries[i].stat.size, color);
                tui_text(35, 3 + (int)i, "B", color);
            }
        }
        tui_text(36, 3 + (int)i, entries[i].stat.directory ? "Directory" : "File", color);
    }
    tui_bar(24, "Enter directory   Q quit");
    tui_present(79, 24);
    while (1) {
        int key = tui_key(1);
        if (key == 'q' || key == 'Q' || key == 27) {
            break;
        }
    }
    return 0;
}
