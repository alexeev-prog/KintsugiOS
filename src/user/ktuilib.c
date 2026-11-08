#include "ktuilib.h"
static ktui_cell screen[25][80];

void tui_parent(char path[256]) {
    u32 n = tui_len(path);
    while (n > 1 && path[n - 1] != '/') {
        --n;
    }
    if (n > 1) {
        --n;
    }
    path[n] = 0;
}

void tui_fill(int x, int y, int width, int height, unsigned char color) {
    for (int row = 0; row < height && row < 25; ++row) {
        for (int col = 0; col < width && col < 80; ++col) {
            if (x + col >= 0 && x + col < 80 && y + row >= 0 && y + row < 25) {
                screen[y + row][x + col] = (ktui_cell){ ' ', color };
            }
        }
    }
}

void tui_textn(int x, int y, const char* text, u32 width, unsigned char color) {
    for (u32 i = 0; i < width && i < 80 && text[i]; ++i) {
        char ch[2] = { text[i], 0 };
        tui_text(x + i, y, ch, color);
    }
}

void tui_box(int x, int y, int width, int height, const char* title, unsigned char color) {
    if (width < 2 || height < 2 || width > 80 || height > 25) {
        return;
    }
    tui_fill(x, y, width, height, color);
    for (int i = 0; i < width; ++i) {
        tui_text(x + i, y, i && i < width - 1 ? "-" : "+", color);
        tui_text(x + i, y + height - 1, i && i < width - 1 ? "-" : "+", color);
    }
    for (int i = 1; i < height - 1; ++i) {
        tui_text(x, y + i, "|", color);
        tui_text(x + width - 1, y + i, "|", color);
    }
    if (width > 4 && title) {
        tui_textn(x + 2, y, title, width - 4, color);
    }
}

void tui_message(const char* title, const char* message) {
    tui_box(5, 8, 70, 7, title, 0x70);
    tui_textn(7, 10, message, 66, 0x70);
    tui_text(7, 12, "Press any key", 0x70);
    tui_present(79, 24);
    tui_key(1);
}

void tui_gauge(int x, int y, int width, u32 used, u32 total) {
    if (width < 1 || width > 80) {
        return;
    }
    if (used > total) {
        used = total;
    }
    while (total > 0x01ffffffu) {
        total >>= 1;
        used >>= 1;
    }
    u32 filled = total ? used * (u32)width / total : 0;
    tui_fill(x, y, width, 1, 0x08);
    tui_fill(x, y, filled, 1, 0x2f);
}

int tui_load_settings(const char* path, void* data, u32 size) {
    tui_stat stat;
    if (size > 4096 || tui_stat_path(path, &stat) || stat.size != size) {
        return 0;
    }
    return syscall(2, (u32)path, 0, (u32)data, size) == (int)size;
}

int tui_save_settings(const char* path, const void* data, u32 size) {
    return syscall(10, (u32)path, (u32)data, size, 0) == 0;
}

u32 tui_len(const char* s) {
    u32 n = 0;
    while (s[n]) {
        ++n;
    }
    return n;
}

void tui_copy(char* out, const char* s, u32 capacity) {
    if (!capacity) {
        return;
    }
    u32 i = 0;
    while (i + 1 < capacity && s[i]) {
        out[i] = s[i];
        ++i;
    }
    out[i] = 0;
}

void tui_clear(unsigned char color) {
    for (u32 y = 0; y < 25; ++y) {
        for (u32 x = 0; x < 80; ++x) {
            screen[y][x] = (ktui_cell){ ' ', color };
        }
    }
}

void tui_text(int x, int y, const char* s, unsigned char color) {
    if (y < 0 || y >= 25) {
        return;
    }
    while (*s && x < 80) {
        if (x >= 0) {
            screen[y][x] = (ktui_cell){ (unsigned char)*s, color };
        }
        ++s;
        ++x;
    }
}

void tui_number(int x, int y, u32 n, unsigned char color) {
    char text[11];
    u32 i = 10;
    text[i] = 0;
    do {
        text[--i] = '0' + n % 10;
        n /= 10;
    } while (n);
    tui_text(x, y, text + i, color);
}

void tui_bar(int y, const char* text) {
    if (y < 0 || y >= 25) {
        return;
    }
    for (u32 x = 0; x < 80; ++x) {
        screen[y][x] = (ktui_cell){ ' ', 0x70 };
    }
    tui_text(1, y, text, 0x70);
}

void tui_present(int x, int y) {
    syscall(7, (u32)screen, x, y, 0);
}

int tui_key(int wait) {
    return syscall(8, wait, 0, 0, 0);
}

int tui_prompt(const char* label, char* buffer, u32 capacity) {
    if (!capacity) {
        return 0;
    }
    u32 n = 0, cursor = 0;
    buffer[0] = 0;
    for (;;) {
        tui_box(3, 8, 74, 7, "Input - Enter accept / Esc cancel", 0x70);
        tui_textn(5, 10, label, 70, 0x70);
        u32 start = cursor > 68 ? cursor - 68 : 0;
        tui_textn(5, 12, buffer + start, 69, 0x70);
        tui_present(5 + cursor - start, 12);
        int key = tui_key(1);
        if (key == 27 || key == 3) {
            return 0;
        }
        if (key == '\n') {
            return n != 0;
        }
        if (key == TUI_LEFT && cursor) {
            --cursor;
        } else if (key == TUI_RIGHT && cursor < n) {
            ++cursor;
        } else if (key == TUI_HOME) {
            cursor = 0;
        } else if (key == TUI_END) {
            cursor = n;
        } else if ((key == 8 && cursor) || (key == TUI_DELETE && cursor < n)) {
            if (key == 8) {
                --cursor;
            }
            for (u32 i = cursor; i < n; ++i) {
                buffer[i] = buffer[i + 1];
            }
            --n;
        } else if (key >= 32 && key < 127 && n + 1 < capacity) {
            for (u32 i = n + 1; i > cursor; --i) {
                buffer[i] = buffer[i - 1];
            }
            buffer[cursor++] = key;
            ++n;
        }
    }
}

int tui_confirm(const char* label) {
    tui_box(3, 8, 74, 7, "Confirm", 0x70);
    tui_textn(5, 10, label, 70, 0x70);
    tui_text(5, 12, "Y yes / any other key cancels", 0x70);
    tui_present(79, 24);
    int key = tui_key(1);
    return key == 'y' || key == 'Y';
}

void tui_path(char out[256], const char* directory, const char* name) {
    tui_copy(out, directory, 256);
    u32 n = tui_len(out);
    if (n > 1 && n < 255) {
        out[n++] = '/';
    }
    tui_copy(out + n, name, 256 - n);
}

int tui_stat_path(const char* path, tui_stat* stat) {
    return syscall(11, (u32)path, 4, (u32)stat, 0);
}
