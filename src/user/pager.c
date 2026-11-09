#include "ktuilib.h"
#define LIMIT (512u * 1024u)
static u32 offsets[32768];

int main(int argc, char** argv) {
    int fd = argc > 1 ? open(argv[1], KOS_O_READ) : 0;
    if (fd < 0) {
        write_all(2, "pager: cannot open file\n", 24);
        return 1;
    }
    char* data = malloc(LIMIT);
    if (!data) {
        return 1;
    }
    u32 used = 0;
    int n = 0;
    while (used < LIMIT && (n = read(fd, data + used, LIMIT - used > 4096 ? 4096 : LIMIT - used)) > 0) {
        used += n;
    }
    if (fd) {
        close(fd);
    }
    if (n < 0) {
        free(data);
        return 1;
    }
    u32 lines = 1, column = 0;
    offsets[0] = 0;
    for (u32 i = 0; i < used && lines < 32768; ++i) {
        if (data[i] == '\n' || ++column == 78) {
            offsets[lines++] = i + 1;
            column = 0;
        }
    }
    u32 top = 0, selected = 0;
    for (;;) {
        tui_clear(0x07);
        tui_bar(0, argc > 1 ? argv[1] : "PAGER - standard input");
        for (u32 row = 0; row < 23 && top + row < lines; ++row) {
            u32 start = offsets[top + row], end = top + row + 1 < lines ? offsets[top + row + 1] : used;
            int x = 1;
            for (u32 i = start; i < end && x < 79; ++i) {
                char c[2] = { data[i] >= 32 && data[i] < 127 ? data[i] : ' ', 0 };
                if (data[i] == '\n' || data[i] == '\r') {
                    continue;
                }
                tui_text(x++, row + 1, c, 0x07);
            }
        }
        tui_bar(
            24,
            used == LIMIT ? "LIMIT 512 KiB | arrows/PgUp/PgDn Home/End | q quit"
                          : "Arrows/PgUp/PgDn Home/End | Space next page | q quit | Ctrl+T themes");
        tui_present(79, 24);
        int key = tui_key(1);
        if (key == 'q' || key == 27) {
            break;
        }
        u32 maximum = lines > 23 ? lines - 23 : 0;
        if (key == ' ') {
            key = TUI_PAGE_DOWN;
        }
        selected = top;
        tui_list_key(key, &selected, &top, maximum + 1, 23);
        top = selected;
    }
    free(data);
    return 0;
}
