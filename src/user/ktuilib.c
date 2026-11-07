#include "ktuilib.h"
static ktui_cell screen[25][80];

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
    u32 n = 0;
    buffer[0] = 0;
    for (;;) {
        tui_bar(23, label);
        tui_bar(24, buffer);
        tui_present(n < 78 ? n + 1 : 79, 24);
        int key = tui_key(1);
        if (key == 27 || key == 3) {
            return 0;
        }
        if (key == '\n') {
            return n != 0;
        }
        if (key == 8 && n) {
            buffer[--n] = 0;
        } else if (key >= 32 && key < 127 && n + 1 < capacity) {
            buffer[n++] = key;
            buffer[n] = 0;
        }
    }
}

int tui_confirm(const char* label) {
    tui_bar(24, label);
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
