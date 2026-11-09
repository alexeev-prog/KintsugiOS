#include "ktuilib.h"
static ktui_cell screen[25][80];
#define MODALS 4
static ktui_cell backgrounds[MODALS][25][80];
static int modal_depth;
static unsigned char palette[256];
static int theme_ready;
static ktui_cell themed[25][80];
static unsigned char raw_cells[25][80];
static unsigned char raw_backgrounds[MODALS][25][80];

void tui_theme_get(unsigned char out[256]) {
    for (int i = 0; i < 256; ++i) {
        out[i] = palette[i];
    }
}

void tui_sample(int x, int y, const char* text, unsigned char color) {
    if (y < 0 || y >= 25) {
        return;
    }
    while (*text && x < 80) {
        if (x >= 0) {
            screen[y][x] = (ktui_cell){ *text, color };
            raw_cells[y][x] = 1;
        }
        ++x;
        ++text;
    }
}

static int hex(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

int tui_theme_parse(const char* text, u32 length) {
    unsigned char next[256];
    int popup = 0, desktop = 0;
    unsigned char specified[256] = { 0 };
    for (int i = 0; i < 256; ++i) {
        next[i] = i;
    }
    for (u32 i = 0; i < length;) {
        u32 start = i;
        while (i < length && text[i] != '\n') {
            ++i;
        }
        u32 end = i;
        if (i < length) {
            ++i;
        }
        if (end > start && text[end - 1] == '\r') {
            --end;
        }
        if (end == start || text[start] == '#') {
            continue;
        }
        if (end - start != 5 || text[start + 2] != '=') {
            return 0;
        }
        int a = hex(text[start]), b = hex(text[start + 1]);
        int c = hex(text[start + 3]), d = hex(text[start + 4]);
        if (a < 0 || b < 0 || c < 0 || d < 0) {
            return 0;
        }
        next[a * 16 + b] = c * 16 + d;
        specified[a * 16 + b] = 1;
        if (a * 16 + b == TUI_POPUP) {
            popup = 1;
        }
        if (a * 16 + b == TUI_DESKTOP) {
            desktop = 1;
        }
    }
    if (!popup) {
        next[TUI_POPUP] = next[0x70];
    }
    if (!desktop) {
        next[TUI_DESKTOP] = next[0x17];
    }
    static const unsigned char fallback[] = { 0x07, 0x70, 0x70, 0x08, 0x4f, 0x1e, 0x2f, 0x1e,
                                              0x1f, 0x1f, 0x08, 0x0a, 0x0e, 0x0b, 0x70, 0x70 };
    for (u32 i = 0; i < sizeof(fallback); ++i) {
        if (!specified[TUI_INPUT + i]) {
            next[TUI_INPUT + i] = next[fallback[i]];
        }
    }
    for (int i = 0; i < 256; ++i) {
        palette[i] = next[i];
    }
    theme_ready = 1;
    return 1;
}

int tui_theme_load(const char* path) {
    char text[2048];
    tui_stat stat;
    if (tui_stat_path(path, &stat) || stat.directory || stat.size > sizeof(text)) {
        return 0;
    }
    if (syscall(2, (u32)path, 0, (u32)text, stat.size) != (int)stat.size) {
        return 0;
    }
    return tui_theme_parse(text, stat.size);
}

int tui_theme_choose(void) {
    tui_entry entry;
    u32 cursor = 0;
    int count = 0;
    char names[64][13];
    const char* items[64];
    while (count < 64 && syscall(3, (u32) "/ETC/THEMES", (u32)&cursor, (u32)&entry, 0) > 0) {
        u32 n = tui_len(entry.name);
        if (entry.stat.directory || n < 4 || entry.name[n - 4] != '.' || entry.name[n - 3] != 'T'
            || entry.name[n - 2] != 'H' || entry.name[n - 1] != 'M') {
            continue;
        }
        tui_copy(names[count], entry.name, 13);
        items[count] = names[count];
        ++count;
    }
    if (!count) {
        tui_message("Themes", "No themes in /ETC/THEMES");
        return 0;
    }
    int choice = tui_menu("Choose shared TUI theme", items, count, 0);
    if (choice < 0) {
        return 0;
    }
    char path[256], text[2048];
    tui_stat st;
    tui_path(path, "/ETC/THEMES", names[choice]);
    if (tui_stat_path(path, &st) || st.size > sizeof(text)
        || syscall(2, (u32)path, 0, (u32)text, st.size) != (int)st.size) {
        return 0;
    }
    unsigned char old[256];
    for (int i = 0; i < 256; ++i) {
        old[i] = palette[i];
    }
    if (!tui_theme_parse(text, st.size)) {
        tui_message("Themes", "Invalid theme file");
        return 0;
    }
    if (syscall(10, (u32) "/ETC/TUI.THM", (u32)text, st.size, 0)) {
        for (int i = 0; i < 256; ++i) {
            palette[i] = old[i];
        }
        tui_message("Themes", "Cannot save active theme");
        return 0;
    }
    return 1;
}

int tui_theme_path(char out[256], const char* name) {
    const char* base = name;
    for (const char* p = name; *p; ++p) {
        if (*p == '/') {
            base = p + 1;
        }
    }
    char file[13];
    u32 n = 0;
    while (base[n] && base[n] != '.') {
        char c = base[n];
        if (n == 8
            || !(
                (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
                || c == '-')) {
            return 0;
        }
        file[n++] = c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
    }
    if (!n) {
        return 0;
    }
    if (base[n]) {
        const char* ext = base + n;
        if (tui_len(ext) != 4 || ext[0] != '.' || (ext[1] != 't' && ext[1] != 'T')
            || (ext[2] != 'h' && ext[2] != 'H') || (ext[3] != 'm' && ext[3] != 'M')) {
            return 0;
        }
    }
    tui_copy(file + n, ".THM", sizeof(file) - n);
    tui_path(out, "/ETC/THEMES", file);
    return 1;
}

int tui_command(const char* prefix, char* buffer, u32 capacity) {
    if (capacity < 2) {
        return 0;
    }
    u32 n = 0;
    buffer[0] = 0;
    for (;;) {
        tui_bar(24, prefix);
        tui_textn(2, 24, buffer, 76, 0x70);
        tui_present(n < 76 ? n + 2 : 78, 24);
        int key = tui_key(1);
        if (key == 27) {
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

void tui_window_draw(const tui_window* w) {
    tui_fill(w->x + 1, w->y + 1, w->width, w->height, 0x08);
    tui_box(w->x, w->y, w->width, w->height, w->title, w->focused ? TUI_BORDER : 0x17);
}

void tui_window_text(const tui_window* w, int x, int y, const char* text, unsigned char color) {
    if (y < 0 || y >= w->height - 2) {
        return;
    }
    while (x < 0 && *text) {
        ++x;
        ++text;
    }
    if (x < w->width - 2) {
        tui_textn(w->x + 1 + x, w->y + 1 + y, text, w->width - 2 - x, color);
    }
}

int tui_modal_begin(const char* title, int width, int height) {
    if (modal_depth == MODALS || width < 4 || width > 78 || height < 4 || height > 23) {
        return 0;
    }
    for (int y = 0; y < 25; ++y) {
        for (int x = 0; x < 80; ++x) {
            backgrounds[modal_depth][y][x] = screen[y][x];
            raw_backgrounds[modal_depth][y][x] = raw_cells[y][x];
        }
    }
    ++modal_depth;
    tui_window w = { (80 - width) / 2, (25 - height) / 2, width, height, 1, title };
    tui_fill(w.x + 1, w.y + 1, w.width, w.height, 0x08);
    tui_box(w.x, w.y, w.width, w.height, w.title, TUI_POPUP);
    return 1;
}

void tui_modal_end(void) {
    if (!modal_depth) {
        return;
    }
    --modal_depth;
    for (int y = 0; y < 25; ++y) {
        for (int x = 0; x < 80; ++x) {
            screen[y][x] = backgrounds[modal_depth][y][x];
            raw_cells[y][x] = raw_backgrounds[modal_depth][y][x];
        }
    }
}

int tui_list_key(int key, u32* selected, u32* top, u32 count, u32 visible) {
    if (!visible) {
        return 0;
    }
    if (!count) {
        *selected = *top = 0;
        return 0;
    }
    if (*selected >= count) {
        *selected = count - 1;
    }
    u32 old = *selected;
    if ((key == TUI_UP || key == TUI_SCROLL_UP) && *selected) {
        --*selected;
    } else if ((key == TUI_DOWN || key == TUI_SCROLL_DOWN) && *selected + 1 < count) {
        ++*selected;
    } else if (key == TUI_HOME) {
        *selected = 0;
    } else if (key == TUI_END) {
        *selected = count - 1;
    } else if (key == TUI_PAGE_UP) {
        *selected = *selected > visible ? *selected - visible : 0;
    } else if (key == TUI_PAGE_DOWN) {
        *selected += count - 1 - *selected < visible ? count - 1 - *selected : visible;
    }
    if (*selected < *top) {
        *top = *selected;
    }
    if (*selected >= *top + visible) {
        *top = *selected - visible + 1;
    }
    return old != *selected;
}

void tui_scrollbar(int x, int y, int height, u32 top, u32 visible, u32 total) {
    if (height < 1 || height > 25) {
        return;
    }
    tui_fill(x, y, 1, height, 0x08);
    if (total <= visible) {
        return;
    }
    if (top > total - visible) {
        top = total - visible;
    }
    u32 range = total - visible;
    while (range > 0x07ffffffu) {
        range >>= 1;
        top >>= 1;
    }
    u32 position = top * (height - 1) / range;
    tui_text(x, y + position, "#", 0x70);
}

void tui_choice_row(int x, int y, int width, const char* label, int selected, unsigned char normal) {
    unsigned char base = palette[normal];
    unsigned char color = selected ? palette[0x70] : base;
    /* A theme may make selection identical to the popup or hide its text. */
    if (selected && (color == base || (color & 15) == ((color >> 4) & 7))) {
        color = (base & 0x70) == 0x70 ? 0x0f : 0x70;
    }
    if ((color & 15) == ((color >> 4) & 7)) {
        color = (color & 0x70) | (((color >> 4) & 7) < 4 ? 15 : 0);
    }
    tui_fill(x, y, width, 1, normal);
    tui_textn(x + 2, y, label, width > 2 ? width - 2 : 0, normal);
    tui_text(x, y, selected ? ">" : " ", normal);
    if (y < 0 || y >= 25) {
        return;
    }
    for (int col = x; col < x + width && col < 80; ++col) {
        if (col >= 0) {
            screen[y][col].color = color;
            raw_cells[y][col] = 1;
        }
    }
}

int tui_menu(const char* title, const char* const* items, int count, int initial) {
    int visible = count < 16 ? count : 16;
    if (count < 1 || count > 64 || !tui_modal_begin(title, 64, visible + 4)) {
        return -1;
    }
    u32 selected = initial >= 0 && initial < count ? initial : 0, top = 0;
    int y = (25 - visible - 4) / 2;
    for (;;) {
        tui_list_key(0, &selected, &top, count, visible);
        tui_list_draw(10, y + 1, 61, visible, items, count, selected, top, 1);
        tui_text(11, y + visible + 2, "Arrows/PgDn select | Enter accept | Esc cancel", TUI_POPUP);
        tui_scrollbar(70, y + 1, visible, top, visible, count);
        tui_present(79, 24);
        int key = tui_key(1);
        if (key == 27 || key == 3) {
            tui_modal_end();
            return -1;
        }
        if (key == '\n') {
            tui_modal_end();
            return selected;
        }
        tui_list_key(key, &selected, &top, count, visible);
    }
}

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
                raw_cells[y + row][x + col] = 0;
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
    if (!tui_modal_begin(title, 70, 7)) {
        return;
    }
    tui_textn(7, 11, message, 66, TUI_POPUP);
    tui_button(7, 13, 22, "OK - press any key", 1, 1);
    tui_present(79, 24);
    tui_key(1);
    tui_modal_end();
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
    if (color == 0x17) {
        color = TUI_DESKTOP;
    }
    if (!theme_ready) {
        tui_theme_parse("", 0);
        tui_theme_load("/ETC/TUI.THM");
    }
    for (u32 y = 0; y < 25; ++y) {
        for (u32 x = 0; x < 80; ++x) {
            screen[y][x] = (ktui_cell){ ' ', color };
            raw_cells[y][x] = 0;
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
            raw_cells[y][x] = 0;
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
        raw_cells[y][x] = 0;
    }
    tui_text(1, y, text, 0x70);
}

void tui_present(int x, int y) {
    for (int row = 0; row < 25; ++row) {
        for (int col = 0; col < 80; ++col) {
            themed[row][col] = screen[row][col];
            if (!raw_cells[row][col]) {
                themed[row][col].color = palette[screen[row][col].color];
            }
        }
    }
    syscall(7, (u32)themed, x, y, 0);
}

int tui_key(int wait) {
    int key = syscall(8, wait, 0, 0, 0);
    if (key == 20) {
        tui_theme_choose();
        tui_present(79, 24);
        return 0;
    }
    return key;
}

int tui_prompt(const char* label, char* buffer, u32 capacity) {
    if (!capacity) {
        return 0;
    }
    tui_input input = { buffer, capacity, 0 };
    buffer[0] = 0;
    if (!tui_modal_begin("Input - Enter accept / Esc cancel", 74, 7)) {
        return 0;
    }
    for (;;) {
        tui_fill(4, 10, 72, 4, TUI_POPUP);
        tui_textn(5, 10, label, 70, TUI_POPUP);
        tui_input_draw(&input, 5, 12, 69, 1);
        tui_present(5 + (input.cursor < 69 ? input.cursor : 68), 12);
        int key = tui_key(1);
        if (key == 27 || key == 3) {
            tui_modal_end();
            return 0;
        }
        if (key == '\n') {
            tui_modal_end();
            return buffer[0] != 0;
        }
        tui_input_key(&input, key);
    }
}

int tui_confirm(const char* label) {
    if (!tui_modal_begin("Confirm", 74, 7)) {
        return 0;
    }
    tui_fill(4, 10, 72, 4, TUI_POPUP);
    tui_textn(5, 10, label, 70, TUI_POPUP);
    tui_text(5, 12, "Y yes / any other key cancels", TUI_POPUP);
    tui_button(51, 12, 10, "Yes [Y]", 0, 1);
    tui_button(62, 12, 12, "Cancel", 1, 1);
    tui_present(79, 24);
    int key = tui_key(1);
    tui_modal_end();
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

int tui_input_key(tui_input* input, int key) {
    if (!input || !input->text || !input->capacity) {
        return 0;
    }
    u32 n = 0;
    while (n + 1 < input->capacity && input->text[n]) {
        ++n;
    }
    input->text[n] = 0;
    if (input->cursor > n) {
        input->cursor = n;
    }
    u32* cursor = &input->cursor;
    if (key == TUI_LEFT && *cursor) {
        --*cursor;
    } else if (key == TUI_RIGHT && *cursor < n) {
        ++*cursor;
    } else if (key == TUI_HOME) {
        *cursor = 0;
    } else if (key == TUI_END) {
        *cursor = n;
    } else if ((key == 8 && *cursor) || (key == TUI_DELETE && *cursor < n)) {
        if (key == 8) {
            --*cursor;
        }
        for (u32 i = *cursor; i < n; ++i) {
            input->text[i] = input->text[i + 1];
        }
        return 1;
    } else if (key >= 32 && key < 127 && n + 1 < input->capacity) {
        for (u32 i = n + 1; i > *cursor; --i) {
            input->text[i] = input->text[i - 1];
        }
        input->text[(*cursor)++] = key;
        return 1;
    }
    return 0;
}

void tui_input_draw(tui_input* input, int x, int y, int width, int focused) {
    if (!input || !input->text || width < 1 || width > 80) {
        return;
    }
    u32 n = tui_len(input->text);
    if (input->cursor > n) {
        input->cursor = n;
    }
    u32 start = input->cursor >= (u32)width ? input->cursor - width + 1 : 0;
    tui_fill(x, y, width, 1, TUI_INPUT);
    tui_textn(x, y, input->text + start, width, TUI_INPUT);
    if (focused) {
        char ch[2] = { input->cursor < n ? input->text[input->cursor] : ' ', 0 };
        tui_text(x + input->cursor - start, y, ch, TUI_FOCUS);
    }
}

void tui_button(int x, int y, int width, const char* label, int focused, int enabled) {
    if (width < 2 || width > 80) {
        return;
    }
    unsigned char color = !enabled ? TUI_DISABLED : focused ? TUI_FOCUS : TUI_BUTTON;
    tui_fill(x, y, width, 1, color);
    tui_text(x, y, "[", color);
    tui_textn(x + 1, y, label, width - 2, color);
    tui_text(x + width - 1, y, "]", color);
}

void tui_checkbox(int x, int y, const char* label, int checked, int focused) {
    unsigned char color = focused ? TUI_FOCUS : TUI_POPUP;
    tui_text(x, y, checked ? "[x] " : "[ ] ", color);
    tui_textn(x + 4, y, label, 76, color);
}

void tui_radio(int x, int y, const char* label, int checked, int focused) {
    unsigned char color = focused ? TUI_FOCUS : TUI_POPUP;
    tui_text(x, y, checked ? "(*) " : "( ) ", color);
    tui_textn(x + 4, y, label, 76, color);
}

int tui_toggle_key(int key, int* checked) {
    if (key != ' ' || !checked) {
        return 0;
    }
    *checked = !*checked;
    return 1;
}

int tui_focus_key(int key, u32* focus, u32 count) {
    if (!focus || !count) {
        return 0;
    }
    if (*focus >= count) {
        *focus = 0;
    }
    if (key != '\t') {
        return 0;
    }
    *focus = (*focus + 1) % count;
    return 1;
}

void tui_list_draw(
    int x,
    int y,
    int width,
    int height,
    const char* const* items,
    u32 count,
    u32 selected,
    u32 top,
    int focused) {
    if (width < 3 || width > 80 || height < 1 || height > 25) {
        return;
    }
    for (int row = 0; row < height; ++row) {
        u32 i = top + row;
        tui_choice_row(
            x,
            y + row,
            width - 1,
            i < count ? items[i] : "",
            focused && i < count && i == selected,
            TUI_POPUP);
    }
    tui_scrollbar(x + width - 1, y, height, top, height, count);
}
