#include "gfx.h"

#include "tuiabi.h"

typedef unsigned char u8;
typedef unsigned short u16;
#include "../kernel/drivers/font.h"

static int clamp_value(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

gui_theme_t gui_theme = {
    "Classic",
    GFX_RGB(192, 192, 192), GFX_RGB(96, 96, 96), GFX_RGB(224, 224, 224),
    GFX_RGB(0, 0, 128), GFX_RGB(255, 255, 255), GFX_RGB(0, 0, 0), GFX_RGB(0, 0, 160),
    GFX_RGB(0, 128, 128), GFX_RGB(0, 0, 128), GFX_RGB(40, 160, 40),
    GFX_RGB(180, 60, 60), GFX_RGB(160, 60, 60), 0, 0
};

static const gui_theme_t themes[GUI_THEME_COUNT] = {
    { "Classic", GFX_RGB(192,192,192), GFX_RGB(96,96,96), GFX_RGB(224,224,224),
      GFX_RGB(0,0,128), GFX_RGB(255,255,255), GFX_RGB(0,0,0), GFX_RGB(0,0,160),
      GFX_RGB(0,128,128), GFX_RGB(0,0,128), GFX_RGB(40,160,40),
      GFX_RGB(180,60,60), GFX_RGB(160,60,60), 0, 0 },
    { "NeXTSTEP", GFX_RGB(206,206,206), GFX_RGB(72,76,84), GFX_RGB(250,250,250),
      GFX_RGB(32,38,48), GFX_RGB(245,245,235), GFX_RGB(20,24,32), GFX_RGB(40,90,200),
      GFX_RGB(32,38,48), GFX_RGB(30,80,180), GFX_RGB(60,190,110),
      GFX_RGB(230,90,70), GFX_RGB(210,150,50), 1, 1 },
    { "Millennium", GFX_RGB(212,208,200), GFX_RGB(90,90,90), GFX_RGB(255,255,255),
      GFX_RGB(10,36,106), GFX_RGB(255,255,255), GFX_RGB(0,0,0), GFX_RGB(49,106,197),
      GFX_RGB(58,110,165), GFX_RGB(10,36,106), GFX_RGB(48,140,64),
      GFX_RGB(170,50,50), GFX_RGB(150,110,40), 0, 0 },
    { "Humanity", GFX_RGB(242,241,239), GFX_RGB(130,126,120), GFX_RGB(255,255,255),
      GFX_RGB(92,74,61), GFX_RGB(60,48,40), GFX_RGB(45,40,36), GFX_RGB(221,72,20),
      GFX_RGB(122,59,46), GFX_RGB(221,72,20), GFX_RGB(78,140,60),
      GFX_RGB(190,60,55), GFX_RGB(180,120,40), 0, 1 },
    { "Fruit", GFX_RGB(245,240,225), GFX_RGB(120,116,104), GFX_RGB(255,255,255),
      GFX_RGB(48,52,58), GFX_RGB(40,40,40), GFX_RGB(32,32,32), GFX_RGB(74,144,217),
      GFX_RGB(120,160,190), GFX_RGB(74,144,217), GFX_RGB(80,170,90),
      GFX_RGB(210,80,80), GFX_RGB(210,170,70), 1, 1 },
    { "Graphite", GFX_RGB(72,76,82), GFX_RGB(32,34,38), GFX_RGB(110,116,124),
      GFX_RGB(24,26,30), GFX_RGB(230,232,236), GFX_RGB(222,226,232), GFX_RGB(150,170,200),
      GFX_RGB(34,38,44), GFX_RGB(90,130,190), GFX_RGB(90,180,120),
      GFX_RGB(220,100,90), GFX_RGB(210,170,80), 1, 0 },
    { "Ocean", GFX_RGB(214,232,238), GFX_RGB(70,110,130), GFX_RGB(250,252,255),
      GFX_RGB(12,62,92), GFX_RGB(255,255,255), GFX_RGB(10,40,60), GFX_RGB(28,130,170),
      GFX_RGB(20,90,130), GFX_RGB(28,130,170), GFX_RGB(60,170,140),
      GFX_RGB(200,80,70), GFX_RGB(190,150,50), 0, 1 },
    { "Solar", GFX_RGB(236,228,206), GFX_RGB(110,96,72), GFX_RGB(255,248,226),
      GFX_RGB(80,54,26), GFX_RGB(60,44,24), GFX_RGB(48,36,20), GFX_RGB(200,120,30),
      GFX_RGB(120,76,34), GFX_RGB(190,110,30), GFX_RGB(120,150,60),
      GFX_RGB(190,70,50), GFX_RGB(170,120,40), 0, 1 }
};

void gfx_theme_set(int id) {
    if (id < 0 || id >= GUI_THEME_COUNT) {
        id = GUI_THEME_CLASSIC;
    }
    gui_theme = themes[id];
}

int gfx_theme_id(void) {
    for (int i = 0; i < GUI_THEME_COUNT; ++i) {
        if (gui_theme.name == themes[i].name) {
            return i;
        }
    }
    return GUI_THEME_CLASSIC;
}

/* String instructions move four bytes per micro-op instead of a C loop per
 * pixel. The compositor fills and copies megabytes every frame, so this is
 * the cheapest single optimisation available. */
static void fast_fill(u32* destination, u32 value, u32 count) {
    __asm__ volatile("cld; rep stosl" : "+D"(destination), "+c"(count) : "a"(value) : "memory");
}

static void fast_copy(u32* destination, const u32* source, u32 count) {
    __asm__ volatile("cld; rep movsl" : "+D"(destination), "+S"(source), "+c"(count) : : "memory");
}

/* Text that must never cross a widget boundary (menus, buttons, titles). */
void gfx_text_clipped(
    gfx_surface* surface, int x, int y, int max_width, const char* text, u32 foreground, u32 background) {
    if (!surface || !text || max_width < 12) {
        return;
    }
    int limit = max_width / 12;
    char line[64];
    int n = 0;
    while (text[n] && n < limit && n + 1 < (int)sizeof(line)) {
        line[n] = text[n];
        ++n;
    }
    line[n] = 0;
    gfx_text(surface, x, y, line, foreground, background);
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
    fast_fill(surface->pixels, color, (u32)(surface->width * surface->height));
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
        fast_fill(surface->pixels + (y + row) * surface->width + x, color, (u32)width);
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
        fast_copy(destination->pixels + (y + row) * destination->width + x,
                  source->pixels + (sy + row) * source->width + sx, (u32)width);
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

static void gfx_put2(gfx_surface* surface, int x, int y, u32 color) {
    if (x < 0 || y < 0 || x + 1 >= surface->width || y + 1 >= surface->height) return;
    u32* row = surface->pixels + y * surface->width + x;
    row[0] = color;
    row[1] = color;
    row += surface->width;
    row[0] = color;
    row[1] = color;
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
                    gfx_put2(surface, x + col * 2, y + row * 2, foreground);
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
                gfx_put2(surface, x + col * 2, y + row * 2, foreground);
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
    u32 light = raised ? gui_theme.light : GFX_SHADOW;
    u32 dark = raised ? GFX_SHADOW : gui_theme.light;
    gfx_hline(surface, x, y, width, light);
    gfx_vline(surface, x, y, height, light);
    gfx_hline(surface, x, y + height - 1, width, dark);
    gfx_vline(surface, x + width - 1, y, height, dark);
    if (gui_theme.flat) {
        return;
    }
    /* Inner bevel gives Win2000/Humanity controls their volume. */
    gfx_hline(surface, x + 1, y + 1, width - 2, GFX_FACE);
    gfx_vline(surface, x + 1, y + 1, height - 2, GFX_FACE);
    gfx_hline(surface, x + 1, y + height - 2, width - 2, GFX_FACE);
    gfx_vline(surface, x + width - 2, y + 1, height - 2, GFX_FACE);
}

void gfx_groupbox(gfx_surface* surface, int x, int y, int width, int height, const char* label) {
    gfx_fill(surface, x, y, width, height, GFX_FACE);
    gfx_bevel(surface, x, y, width, height, 0);
    if (!label || !*label) {
        return;
    }
    int label_width = gfx_text_width(label) + 12;
    if (label_width > width - 8) {
        label_width = width - 8;
    }
    gfx_fill(surface, x + 6, y, label_width, 16, GFX_FACE);
    gfx_text(surface, x + 12, y + 1, label, gui_theme.title, GFX_FACE);
}

void gfx_slider(gfx_surface* surface, int x, int y, int width, int height, int value, int maximum, int active) {
    if (width < 20 || height < 8) {
        return;
    }
    gfx_fill(surface, x, y + height / 2 - 3, width, 6, GFX_SHADOW);
    gfx_bevel(surface, x, y + height / 2 - 3, width, 6, 0);
    int fill = maximum ? value * (width - 8) / maximum : 0;
    if (fill > width - 8) {
        fill = width - 8;
    }
    gfx_fill(surface, x + 2, y + height / 2 - 1, fill, 2, gui_theme.accent);
    int knob_x = x + 2 + fill - 4;
    if (knob_x < x) {
        knob_x = x;
    }
    gfx_fill(surface, knob_x, y, 10, height, GFX_FACE);
    gfx_bevel(surface, knob_x, y, 10, height, active ? 0 : 1);
}

void gfx_tab(gfx_surface* surface, int x, int y, int width, int height, const char* label, int active) {
    gfx_fill(surface, x, y, width, height, GFX_FACE);
    gfx_bevel(surface, x, y, width, height, active ? 0 : 1);
    int text_width = gfx_text_width(label);
    gfx_text(surface, x + (width - text_width) / 2, y + (height - 14) / 2, label,
             active ? gui_theme.title : gui_theme.shadow, GFX_FACE);
}

void gfx_progress(gfx_surface* surface, int x, int y, int width, int height, int value, int maximum, u32 color) {
    gfx_fill(surface, x, y, width, height, GFX_WHITE);
    gfx_bevel(surface, x, y, width, height, 0);
    int filled = maximum ? value * (width - 4) / maximum : 0;
    if (filled > width - 4) {
        filled = width - 4;
    }
    gfx_fill(surface, x + 2, y + 2, filled, height - 4, color);
}

void gfx_button(gfx_surface* surface, int x, int y, int width, int height, const char* label, int pressed) {
    gfx_fill(surface, x, y, width, height, GFX_FACE);
    gfx_bevel(surface, x, y, width, height, !pressed);
    int text_width = gfx_text_width(label);
    int tx = x + (width - text_width) / 2;
    if (tx < x + 2) {
        tx = x + 2;
    }
    gfx_text_clipped(surface, tx, y + (height - 14) / 2, width - 4, label, GFX_BLACK, 0);
}

void gfx_titlebar(gfx_surface* surface, int x, int y, int width, const char* title, int active) {
    gfx_fill(surface, x, y, width, 20, active ? GFX_DARK : GFX_SHADOW);
    gfx_text(surface, x + 4, y + 3, title, GFX_WHITE, 0);
    gfx_button(surface, x + width - 56, y + 3, 14, 14, "_", 0);
    gfx_button(surface, x + width - 38, y + 3, 14, 14, "#", 0);
    gfx_button(surface, x + width - 20, y + 3, 14, 14, "x", 0);
}

void gfx_titlebar_next(gfx_surface* surface, int x, int y, int width, const char* title, int active) {
    if (!surface || width < 80) {
        return;
    }
    /* Raised strip with a recessed centre plate — the NeXTSTEP look. */
    gfx_fill(surface, x, y, width, 24, active ? GFX_RGB(208, 208, 208) : GFX_FACE);
    gfx_bevel(surface, x, y, width, 24, 1);
    gfx_fill(surface, x + 60, y + 3, width - 68, 18, active ? GFX_WHITE : GFX_FACE);
    gfx_bevel(surface, x + 60, y + 3, width - 68, 18, 0);
    /* Clip the title to the sunken plate: long names never spill over the
     * traffic lights or the window border. */
    int plate_width = width - 72;
    int glyphs = plate_width / 12 - 1;
    char clipped[64];
    int length = 0;
    while (title[length] && length + 1 < (int)sizeof(clipped) && length < glyphs) {
        clipped[length] = title[length];
        ++length;
    }
    clipped[length] = 0;
    int title_width = gfx_text_width(clipped);
    int tx = x + 64 + (plate_width - title_width) / 2;
    if (tx < x + 64) {
        tx = x + 64;
    }
    gfx_text(surface, tx, y + 6, clipped, active ? GFX_DARK : GFX_SHADOW, 0);
    /* NeXTSTEP controls: three quiet beveled squares, no bright dots. */
    static const char marks[3] = { 'x', '-', '+' };
    for (int i = 0; i < 3; ++i) {
        int bx = x + 8 + i * 18;
        gfx_fill(surface, bx, y + 5, 14, 14, GFX_FACE);
        gfx_bevel(surface, bx, y + 5, 14, 14, 1);
        char mark[2] = { marks[i], 0 };
        gfx_text_clipped(surface, bx + 1, y + 7, 12, mark, GFX_DARK, GFX_FACE);
    }
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
