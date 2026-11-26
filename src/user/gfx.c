#include "gfx.h"

#include "tuiabi.h"

typedef unsigned char u8;
typedef unsigned short u16;
#include "../kernel/drivers/font.h"

static int clamp_value(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

gfx_surface* gfx_surface_create(int width, int height) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    gfx_surface* surface = malloc(sizeof(gfx_surface));
    if (!surface) {
        return 0;
    }
    surface->pixels = malloc((u32)(width * height * 4));
    if (!surface->pixels) {
        free(surface);
        return 0;
    }
    surface->width = width;
    surface->height = height;
    return surface;
}

void gfx_surface_destroy(gfx_surface* surface) {
    if (!surface) {
        return;
    }
    free(surface->pixels);
    free(surface);
}

void gfx_clear(gfx_surface* surface, u32 color) {
    if (!surface) {
        return;
    }
    for (int i = 0; i < surface->width * surface->height; ++i) {
        surface->pixels[i] = color;
    }
}

void gfx_fill(gfx_surface* surface, int x, int y, int width, int height, u32 color) {
    if (!surface || width <= 0 || height <= 0) {
        return;
    }
    x = clamp_value(x, 0, surface->width);
    y = clamp_value(y, 0, surface->height);
    if (x + width > surface->width) {
        width = surface->width - x;
    }
    if (y + height > surface->height) {
        height = surface->height - y;
    }
    for (int row = 0; row < height; ++row) {
        u32* line = surface->pixels + (y + row) * surface->width + x;
        for (int col = 0; col < width; ++col) {
            line[col] = color;
        }
    }
}

void gfx_hline(gfx_surface* surface, int x, int y, int width, u32 color) {
    gfx_fill(surface, x, y, width, 1, color);
}

void gfx_vline(gfx_surface* surface, int x, int y, int height, u32 color) {
    gfx_fill(surface, x, y, 1, height, color);
}

void gfx_line(gfx_surface* surface, int x0, int y0, int x1, int y1, int size, u32 color) {
    if (!surface || size < 1) {
        return;
    }
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        gfx_fill(surface, x0 - size / 2, y0 - size / 2, size, size, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_blit(
    gfx_surface* destination,
    int x,
    int y,
    const gfx_surface* source,
    int sx,
    int sy,
    int width,
    int height) {
    if (!destination || !source || width <= 0 || height <= 0) {
        return;
    }
    /* Clip both rectangles. Filling a row with the first source pixel before
     * copying it smeared a solid bar whenever a window was partly off-screen. */
    if (x < 0) {
        sx -= x;
        width += x;
        x = 0;
    }
    if (y < 0) {
        sy -= y;
        height += y;
        y = 0;
    }
    if (sx < 0) {
        x -= sx;
        width += sx;
        sx = 0;
    }
    if (sy < 0) {
        y -= sy;
        height += sy;
        sy = 0;
    }
    if (sx >= source->width || sy >= source->height) {
        return;
    }
    if (x >= destination->width || y >= destination->height) {
        return;
    }
    int limit = source->width - sx;
    if (width > limit) {
        width = limit;
    }
    limit = destination->width - x;
    if (width > limit) {
        width = limit;
    }
    limit = source->height - sy;
    if (height > limit) {
        height = limit;
    }
    limit = destination->height - y;
    if (height > limit) {
        height = limit;
    }
    if (width <= 0 || height <= 0) {
        return;
    }
    for (int row = 0; row < height; ++row) {
        const u32* line = source->pixels + (sy + row) * source->width + sx;
        u32* target = destination->pixels + (y + row) * destination->width + x;
        for (int col = 0; col < width; ++col) {
            target[col] = line[col];
        }
    }
}

static const u8* glyph_bits(char character) {
    unsigned char c = (unsigned char)character;
    /* CP866 box drawing used by TUI borders. */
    static const u8 hline[7] = { 0, 0, 31, 0, 31, 0, 0 };
    static const u8 vline[7] = { 4, 4, 4, 4, 4, 4, 4 };
    static const u8 dvline[7] = { 5, 5, 5, 5, 5, 5, 5 };
    static const u8 tl[7] = { 0, 0, 31, 4, 4, 4, 4 };
    static const u8 tr[7] = { 0, 0, 31, 16, 16, 16, 16 };
    static const u8 bl[7] = { 4, 4, 4, 4, 31, 0, 0 };
    static const u8 br[7] = { 16, 16, 16, 16, 31, 0, 0 };
    static const u8 shade1[7] = { 10, 0, 10, 0, 10, 0, 10 };
    static const u8 shade2[7] = { 21, 0, 21, 0, 21, 0, 21 };
    if (c == 0xc4 || c == 0xcd) {
        return hline;
    }
    if (c == 0xb3) {
        return vline;
    }
    if (c == 0xba) {
        return dvline;
    }
    if (c == 0xda) {
        return tl;
    }
    if (c == 0xbf) {
        return tr;
    }
    if (c == 0xc0) {
        return bl;
    }
    if (c == 0xd9) {
        return br;
    }
    if (c == 0xb0) {
        return shade1;
    }
    if (c == 0xb1) {
        return shade2;
    }
    if (c >= 32 && c < 128) {
        return latin_font[c - 32];
    }
    /* CP866 Russian letters produced by text_key() when the keyboard layout
     * is Russian. The kernel font has the same bitmap table. */
    if (c >= 0x80 && c <= 0xaf) {
        return cyrillic_font[(c - 0x80) % 32];
    }
    if (c >= 0xe0 && c <= 0xef) {
        return cyrillic_font[c - 0xe0 + 16];
    }
    if (c == 0xf0 || c == 0xf1) {
        return cyrillic_font[32];
    }
    return latin_font['?' - 32];
}

void gfx_text(gfx_surface* surface, int x, int y, const char* text, u32 foreground, u32 background) {
    if (!surface || !text) {
        return;
    }
    (void)console_font_row(0, 0);
    int start = x;
    for (const char* p = text; *p; ++p) {
        const u8* bits = glyph_bits(*p);
        if (background) {
            gfx_fill(surface, x, y, 11, 15, background);
        }
        for (int row = 0; row < 7; ++row) {
            u8 bits_row = bits[row];
            /* font.h stores bit 0 at the RIGHT: reverse the bit order. */
            for (int col = 0; col < 5; ++col) {
                if (bits_row & (1u << (4 - col))) {
                    gfx_fill(surface, x + col * 2, y + row * 2, 2, 2, foreground);
                }
            }
        }
        x += 12;
        if (x > surface->width) {
            break;
        }
    }
    (void)start;
}

void gfx_text_wrapped(
    gfx_surface* surface,
    int x,
    int y,
    int width,
    int line_height,
    const char* text,
    u32 foreground,
    u32 background) {
    if (!surface || !text) {
        return;
    }
    int limit = width / 12;
    if (limit < 1) {
        return;
    }
    int row = 0;
    while (*text && row * line_height < surface->height - y) {
        int n = 0;
        while (text[n] && text[n] != '\n' && n < limit) {
            ++n;
        }
        char line[128];
        int i = 0;
        for (; i < n && i + 1 < (int)sizeof(line); ++i) {
            line[i] = text[i];
        }
        line[i] = 0;
        gfx_text(surface, x, y + row * line_height, line, foreground, background);
        if (text[n] == '\n') {
            ++n;
        }
        text += n;
        ++row;
    }
}

void gfx_cell(gfx_surface* surface, int x, int y, unsigned char character, unsigned char attribute) {
    if (!surface) {
        return;
    }
    static const u32 palette[16] = { GFX_RGB(0, 0, 0),      GFX_RGB(0, 0, 170),     GFX_RGB(0, 170, 0),
                                     GFX_RGB(0, 170, 170),  GFX_RGB(170, 0, 0),     GFX_RGB(170, 0, 170),
                                     GFX_RGB(170, 85, 0),   GFX_RGB(170, 170, 170), GFX_RGB(85, 85, 85),
                                     GFX_RGB(85, 85, 255),  GFX_RGB(85, 255, 85),   GFX_RGB(85, 255, 255),
                                     GFX_RGB(255, 85, 85),  GFX_RGB(255, 85, 255),  GFX_RGB(255, 255, 85),
                                     GFX_RGB(255, 255, 255) };
    u32 foreground = palette[attribute & 15];
    u32 background = palette[(attribute >> 4) & 15];
    gfx_fill(surface, x, y, 12, 15, background);
    const u8* bits = glyph_bits((char)character);
    for (int row = 0; row < 7; ++row) {
        u8 bits_row = bits[row];
        for (int col = 0; col < 5; ++col) {
            if (bits_row & (1u << (4 - col))) {
                gfx_fill(surface, x + col * 2, y + row * 2, 2, 2, foreground);
            }
        }
    }
}

int gfx_text_width(const char* text) {
    int n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n * 12;
}

void gfx_bevel(gfx_surface* surface, int x, int y, int width, int height, int raised) {
    u32 light = raised ? GFX_WHITE : GFX_SHADOW;
    u32 dark = raised ? GFX_SHADOW : GFX_WHITE;
    gfx_hline(surface, x, y, width, light);
    gfx_vline(surface, x, y, height, light);
    gfx_hline(surface, x, y + height - 1, width, dark);
    gfx_vline(surface, x + width - 1, y, height, dark);
    gfx_hline(surface, x + 1, y + 1, width - 2, raised ? GFX_FACE : GFX_FACE);
    gfx_vline(surface, x + 1, y + 1, height - 2, GFX_FACE);
    gfx_hline(surface, x + 1, y + height - 2, width - 2, GFX_FACE);
    gfx_vline(surface, x + width - 2, y + 1, height - 2, GFX_FACE);
}

void gfx_button(gfx_surface* surface, int x, int y, int width, int height, const char* label, int pressed) {
    gfx_fill(surface, x, y, width, height, GFX_FACE);
    gfx_bevel(surface, x, y, width, height, !pressed);
    int text_width = gfx_text_width(label);
    gfx_text(surface, x + (width - text_width) / 2, y + (height - 14) / 2, label, GFX_BLACK, 0);
}

void gfx_titlebar(gfx_surface* surface, int x, int y, int width, const char* title, int active) {
    gfx_fill(surface, x, y, width, 20, active ? GFX_DARK : GFX_SHADOW);
    gfx_text(surface, x + 4, y + 3, title, GFX_WHITE, 0);
    gfx_button(surface, x + width - 56, y + 3, 14, 14, "_", 0);
    gfx_button(surface, x + width - 38, y + 3, 14, 14, "#", 0);
    gfx_button(surface, x + width - 20, y + 3, 14, 14, "x", 0);
}

void gfx_scrollbar(gfx_surface* surface, int x, int y, int height, int position, int total) {
    gfx_fill(surface, x, y, 14, height, GFX_FACE);
    gfx_bevel(surface, x, y, 14, height, 0);
    int thumb = height > 40 ? height / 4 : 12;
    int maximum = total > 1 ? height - thumb - 28 : 0;
    int offset = maximum && total > 1 ? position * maximum / (total - 1) : 0;
    gfx_fill(surface, x + 2, y + 14 + offset, 10, thumb, GFX_FACE);
    gfx_bevel(surface, x + 2, y + 14 + offset, 10, thumb, 1);
    gfx_text(surface, x + 1, y + 1, "^", GFX_BLACK, 0);
    gfx_text(surface, x + 1, y + height - 13, "v", GFX_BLACK, 0);
}

void gfx_dirty_add(gfx_dirty* dirty, int x, int y, int width, int height) {
    if (!dirty || width <= 0 || height <= 0) {
        return;
    }
    if (!dirty->valid) {
        dirty->left = x;
        dirty->top = y;
        dirty->right = x + width;
        dirty->bottom = y + height;
        dirty->valid = 1;
        return;
    }
    if (x < dirty->left) {
        dirty->left = x;
    }
    if (y < dirty->top) {
        dirty->top = y;
    }
    if (x + width > dirty->right) {
        dirty->right = x + width;
    }
    if (y + height > dirty->bottom) {
        dirty->bottom = y + height;
    }
}

void gfx_present(gfx_surface* surface, int x, int y) {
    if (!surface) {
        return;
    }
    kos_gfx_blit request = { (u32)surface->pixels, (u32)x, (u32)y, (u32)surface->width, (u32)surface->height,
                             (u32)surface->width };
    syscall(KOS_SYS_GFX_BLIT, (u32)&request, 0, 0, 0);
}

void gfx_present_dirty(gfx_surface* surface, int x, int y, gfx_dirty* dirty) {
    if (!surface || !dirty || !dirty->valid) {
        return;
    }
    int left = clamp_value(dirty->left, 0, surface->width);
    int top = clamp_value(dirty->top, 0, surface->height);
    int right = clamp_value(dirty->right, 0, surface->width);
    int bottom = clamp_value(dirty->bottom, 0, surface->height);
    int width = right - left, height = bottom - top;
    if (width <= 0 || height <= 0) {
        dirty->valid = 0;
        return;
    }
    kos_gfx_blit request = { (u32)(surface->pixels + top * surface->width + left),
                             (u32)(x + left),
                             (u32)(y + top),
                             (u32)width,
                             (u32)height,
                             (u32)surface->width };
    syscall(KOS_SYS_GFX_BLIT, (u32)&request, 0, 0, 0);
    dirty->valid = 0;
}

void gfx_screen_fill(int x, int y, int width, int height, u32 color) {
    kos_gfx_rect rect = { x, y, (u32)width, (u32)height, color };
    syscall(KOS_SYS_GFX_FILL, (u32)&rect, 0, 0, 0);
}

int gfx_screen_info(kos_gfx_info* info) {
    return syscall(KOS_SYS_GFX_INFO, (u32)info, 0, 0, 0);
}

void gfx_exit_to_terminal(void) {
    ktui_cell blank[25][80];
    for (int y = 0; y < 25; ++y) {
        for (int x = 0; x < 80; ++x) {
            blank[y][x].character = ' ';
            blank[y][x].color = 0x07;
        }
    }
    syscall(KOS_SYS_GFX_MODE, 0, 0, 0, 0);
    syscall(7, (u32)blank, 0, 0, 0);
}

int gfx_window_create(int width, int height) {
    return syscall(KOS_SYS_GFX_WINDOW_CREATE, (u32)width, (u32)height, 0, 0);
}

int gfx_window_close(int id) {
    return syscall(KOS_SYS_GFX_WINDOW_CLOSE, (u32)id, 0, 0, 0);
}

int gfx_window_fill(int id, int x, int y, int width, int height, u32 color) {
    u32 vals[6] = { (u32)id, (u32)x, (u32)y, (u32)width, (u32)height, color };
    return syscall(KOS_SYS_GFX_WINDOW_FILL, (u32)vals, 0, 0, 0);
}

int gfx_window_blit(int id, const gfx_surface* source, int x, int y, int width, int height) {
    if (!source) {
        return -1;
    }
    u32 vals[6] = { (u32)id, (u32)source->pixels, (u32)x, (u32)y, (u32)width, (u32)height };
    return syscall(KOS_SYS_GFX_WINDOW_BLIT, (u32)vals, 0, 0, 0);
}

int gfx_window_title(int id, const char* title) {
    return syscall(KOS_SYS_GFX_WINDOW_TITLE, (u32)id, (u32)title, 0, 0);
}

int gfx_window_move(int id, int x, int y) {
    u32 vals[3] = { (u32)id, (u32)x, (u32)y };
    return syscall(KOS_SYS_GFX_WINDOW_MOVE, (u32)vals, 0, 0, 0);
}

int gfx_window_resize(int id, int width, int height) {
    u32 vals[3] = { (u32)id, (u32)width, (u32)height };
    return syscall(KOS_SYS_GFX_WINDOW_RESIZE, (u32)vals, 0, 0, 0);
}

int gfx_window_state(int id, int state) {
    return syscall(KOS_SYS_GFX_WINDOW_STATE, (u32)id, (u32)state, 0, 0);
}

int gfx_window_raise(int id) {
    return syscall(KOS_SYS_GFX_WINDOW_RAISE, (u32)id, 0, 0, 0);
}

int gfx_window_list(kos_gui_window_info* out, int count) {
    return syscall(KOS_SYS_GFX_WINDOW_LIST, (u32)out, (u32)count, 0, 0);
}

int gfx_window_read(int id, u32* out, int x, int y, int width, int height) {
    u32 vals[6] = { (u32)id, (u32)out, (u32)x, (u32)y, (u32)width, (u32)height };
    return syscall(KOS_SYS_GFX_WINDOW_READ, (u32)vals, 0, 0, 0);
}

int gfx_window_post(int id, const kos_gui_event* event) {
    return syscall(KOS_SYS_GFX_WINDOW_POST, (u32)event, (u32)id, 0, 0);
}

int gfx_window_event(int id, kos_gui_event* event) {
    return syscall(KOS_SYS_GFX_WINDOW_EVENT, (u32)event, (u32)id, 0, 0);
}
