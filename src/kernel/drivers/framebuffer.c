#include "framebuffer.h"

#include "../../user/desktopabi.h"
#include "../kklibc/stdio.h"
#include "../kklibc/stdlib.h"
#include "../mm/memory.h"
#include "font.h"
#include "lowlevel_io.h"
#include "mouse.h"

/* QEMU standard VGA / Bochs DISPI. Unsupported adapters retain VGA text mode. */
#define FB_WINDOW 0xe0000000u
static framebuffer_info_t info;
static u32 generation, mode_index = 1, scale_index = 1, font_index, font_size_index = 1;
static const u32 font_percent[KOS_FRAMEBUFFER_FONT_SIZES] = { 75, 100, 125, 150 };
static u32 origin_x, origin_y, cell_width, cell_height;
static u32 theme_colors[16];
static u32 active_theme;
static u32 saved_theme_colors[16];
static int game_palette;
static int mouse_visible;

void framebuffer_game_palette(int enabled) {
    if (enabled == game_palette) {
        return;
    }
    if (enabled) {
        for (u32 i = 0; i < 16; ++i) {
            saved_theme_colors[i] = theme_colors[i];
        }
        /* Classic DOS VGA: dark navy, blue, green, cyan, red, magenta, brown,
         * silver, then bright variants. Predictable road/grass/sky colors. */
        static const u32 dos[16] = { 0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa,
                                     0xaa5500, 0xaaaaaa, 0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
                                     0xff5555, 0xff55ff, 0xffff55, 0xffffff };
        for (u32 i = 0; i < 16; ++i) {
            theme_colors[i] = dos[i];
        }
        game_palette = 1;
    } else {
        for (u32 i = 0; i < 16; ++i) {
            theme_colors[i] = saved_theme_colors[i];
        }
        game_palette = 0;
    }
    ++generation;
}

u32 framebuffer_generation(void) {
    return generation;
}

u32 framebuffer_mode_index(void) {
    return mode_index;
}

u32 framebuffer_theme_index(void) {
    return active_theme;
}

u32 framebuffer_scale_index(void) {
    return scale_index;
}

u32 framebuffer_font_index(void) {
    return font_index;
}

u32 framebuffer_font_size_index(void) {
    return font_size_index;
}

static const u32 colors[16] = { 0x141820, 0x304c78, 0x69a879, 0x66adb5, 0xc66e72, 0xa88bbd,
                                0xc5a36a, 0xcbd0d8, 0x687383, 0x729bd0, 0x95c69c, 0x94d5dc,
                                0xed9696, 0xc6ade0, 0xebce91, 0xf3f2ed };

void framebuffer_theme(u32 theme) {
    static const u32 ocean[16] = { 0x0b1220, 0x12395a, 0x1f6f8b, 0x2aa198, 0xd95f59, 0x9b6b9e,
                                   0xc9a227, 0xe8edf2, 0x405064, 0x4aa3df, 0x54d6a0, 0x63d9d0,
                                   0xff8278, 0xd8a8ef, 0xffd166, 0xffffff };
    static const u32 amber[16] = { 0x17120d, 0x52351f, 0x566b3f, 0x8c8f5b, 0x9e493f, 0x77536e,
                                   0xb17c3f, 0xd9c7a1, 0x554238, 0x8b6f47, 0x7fa15a, 0xaab878,
                                   0xd36c4e, 0xae83a8, 0xe3af62, 0xfff1c2 };
    static const u32 forest[16] = { 0x101a15, 0x234b35, 0x39734c, 0x5d9b62, 0xb85c5c, 0x87679b,
                                    0xb5a642, 0xd7e8c5, 0x44584b, 0x61b878, 0x7bd58a, 0x93d9a0,
                                    0xe47d72, 0xc79bde, 0xe3d268, 0xf4fae9 };
    static const u32 sakura[16] = { 0x1b1420, 0x54304f, 0x89506e, 0xc2799c, 0xd95d78, 0xa77bbd,
                                    0xd0a05e, 0xf6e6ed, 0x594457, 0xc078a5, 0xf096b7, 0xedafd0,
                                    0xff8d9d, 0xd6b1ed, 0xffd47c, 0xfff5fb };
    /* Minimal grayscale: no hue, dark background, clean ramp. Default theme. */
    static const u32 mono[16] = { 0x0a0a0a, 0x262626, 0x3c3c3c, 0x545454, 0x6e6e6e, 0x8a8a8a,
                                  0xa8a8a8, 0xc8c8c8, 0x4a4a4a, 0x9a9a9a, 0xb4b4b4, 0xc4c4c4,
                                  0xd8d8d8, 0xe6e6e6, 0xf2f2f2, 0xffffff };
    const u32* source = theme == 1 ? ocean
        : theme == 2               ? amber
        : theme == 3               ? forest
        : theme == 4               ? sakura
        : theme == 5               ? mono
                                   : colors;
    for (u32 i = 0; i < 16; ++i) {
        theme_colors[i] = source[i];
    }
    active_theme = theme;
    ++generation;
}

static void dispi(u16 index, u16 value) {
    port_word_out(0x1ce, index);
    port_word_out(0x1cf, value);
}

int framebuffer_mode(u32 mode) {
    if (!info.available || mode >= KOS_VIDEO_MODES) {
        return -1;
    }
    /* Geometry is about to change: the saved cursor pixels belong to the old
     * frame layout, so drop them instead of restoring them at bogus offsets. */
    mouse_visible = 0;
    info.width = kos_video_modes[mode].width;
    info.height = kos_video_modes[mode].height;
    info.pitch = info.width * 4;
    mode_index = mode;
    u32 usable_width = info.width > 64 ? info.width - 64 : info.width;
    u32 usable_height = info.height > 118 ? info.height - 118 : info.height;
    u32 base_width = usable_width / 80, base_height = usable_height / 25;
    u32 percent = font_percent[font_size_index];
    cell_width = base_width * percent / 100;
    cell_height = base_height * percent / 100;
    if (cell_width < 8) {
        cell_width = 8;
    }
    if (cell_height < 12) {
        cell_height = 12;
    }
    /* Never allow an oversized cell grid to wrap unsigned coordinates. */
    if (cell_width * 80 > info.width) {
        cell_width = info.width / 80;
    }
    if (cell_height * 25 > info.height - 88) {
        cell_height = (info.height - 88) / 25;
    }
    if (cell_width < 1) {
        cell_width = 1;
    }
    if (cell_height < 1) {
        cell_height = 1;
    }
    origin_x = cell_width * 80 < info.width ? (info.width - cell_width * 80) / 2 : 0;
    origin_y = 44;
    dispi(4, 0);
    dispi(1, info.width);
    dispi(2, info.height);
    dispi(3, 32);
    dispi(6, info.width);
    dispi(8, 0);
    dispi(9, 0);
    dispi(4, 0x41);
    ++generation;
    return 0;
}

void framebuffer_scale(u32 scale) {
    if (scale >= KOS_FRAMEBUFFER_SCALES) {
        return;
    }
    scale_index = scale;
    font_size_index = scale; /* legacy alias: small/normal/large */
    framebuffer_mode(mode_index);
}

void framebuffer_font_size(u32 size) {
    if (size >= KOS_FRAMEBUFFER_FONT_SIZES) {
        return;
    }
    font_size_index = size;
    scale_index = size < KOS_FRAMEBUFFER_SCALES ? size : KOS_FRAMEBUFFER_SCALES - 1;
    framebuffer_mode(mode_index);
}

void framebuffer_font(u32 font) {
    if (font >= KOS_FRAMEBUFFER_FONTS) {
        return;
    }
    font_index = font;
    ++generation;
}

void framebuffer_geometry(u32 out[4]) {
    out[0] = cell_width;
    out[1] = cell_height;
    out[2] = origin_x;
    out[3] = origin_y;
}

void framebuffer_init(void) {
    port_word_out(0x1ce, 0);
    u16 id = port_word_in(0x1cf);
    if (id < 0xb0c0 || id > 0xb0c5) {
        return;
    }
    for (u32 slot = 0; slot < 32; ++slot) {
        u32 config = 0x80000000u | (slot << 11);
        port_dword_out(0xcf8, config);
        u32 device = port_dword_in(0xcfc);
        if (device != 0x11111234 && device != 0xbeef80ee) {
            continue;
        }
        port_dword_out(0xcf8, config | 0x10);
        u32 bar = port_dword_in(0xcfc);
        if ((bar & 1) || !(bar & ~15u)) {
            return;
        }
        info.address = bar & ~15u;
        info.bpp = 32;
        info.available = 1;
        framebuffer_theme(5);
        framebuffer_mode(mode_index);
        return;
    }
}

const framebuffer_info_t* framebuffer_get_info(void) {
    return &info;
}

void framebuffer_pixel(u32 x, u32 y, u32 rgb) {
    if (info.available && x < info.width && y < info.height) {
        ((volatile u32*)FB_WINDOW)[y * info.width + x] = rgb;
    }
}

void framebuffer_rect(u32 x, u32 y, u32 width, u32 height, u32 rgb) {
    if (!info.available || x >= info.width || y >= info.height) {
        return;
    }
    if (width > info.width - x) {
        width = info.width - x;
    }
    if (height > info.height - y) {
        height = info.height - y;
    }
    for (u32 row = y; row < y + height; ++row) {
        for (u32 col = x; col < x + width; ++col) {
            ((volatile u32*)FB_WINDOW)[row * info.width + col] = rgb;
        }
    }
}

void framebuffer_clear(u32 rgb) {
    framebuffer_rect(0, 0, info.width, info.height, rgb);
}

void framebuffer_blit(u32 x, u32 y, u32 width, u32 height, u32 stride, const u32* pixels) {
    if (!info.available || !pixels || !width || !height || x >= info.width || y >= info.height) {
        return;
    }
    if (width > info.width - x) {
        width = info.width - x;
    }
    if (height > info.height - y) {
        height = info.height - y;
    }
    /* The source rows are packed with `stride` pixels, not with the width of
     * the clipped destination rectangle. Using the latter smears unrelated
     * heap pixels into every row (visible as a block following the cursor). */
    if (!stride || stride < width) {
        stride = width;
    }
    volatile u32* target = (volatile u32*)FB_WINDOW;
    for (u32 row = 0; row < height; ++row) {
        volatile u32* destination = target + (y + row) * info.width + x;
        const u32* source = pixels + row * stride;
        for (u32 col = 0; col < width; ++col) {
            destination[col] = source[col];
        }
    }
}

void framebuffer_cell(u32 x, u32 y, u8 glyph, u8 attr, int cursor) {
    if (!info.available || x >= 80 || y >= 25) {
        return;
    }
    u32 fg = theme_colors[attr & 15], bg = theme_colors[(attr >> 4) & 15];
    for (u32 row = 0; row < cell_height; ++row) {
        u32 font_row = row * 20 / cell_height;
        u16 bits = console_font_row(glyph, font_row);
        if (font_index == 1) {
            bits &= 0x777; /* compact, lighter strokes */
        }
        if (font_index == 2) {
            bits |= (bits >> 1); /* bold terminal */
        }
        volatile u32* pixels = (volatile u32*)FB_WINDOW + (origin_y + y * cell_height + row) * info.width
            + origin_x + x * cell_width;
        for (u32 col = 0; col < cell_width; ++col) {
            pixels[col] = cursor && row >= cell_height - 2      ? theme_colors[14]
                : (bits & (1u << (11 - col * 12 / cell_width))) ? fg
                                                                : bg;
        }
    }
}

static void label(u32 x, u32 y, const char* text, u32 color) {
    while (*text) {
        u8 glyph = *text++;
        for (u32 row = 0; row < 20; ++row) {
            for (u32 col = 0; col < 12; ++col) {
                if (console_font_row(glyph, row) & (1u << (11 - col))) {
                    framebuffer_pixel(x + col, y + row, color);
                }
            }
        }
        x += 12;
    }
}

static u32 label_width(const char* text) {
    u32 n = 0;
    while (text[n]) {
        ++n;
    }
    return n * 12;
}

static void label_right(u32 right, u32 y, const char* text, u32 color) {
    u32 width = label_width(text);
    label(right > width ? right - width : 0, y, text, color);
}

static void label_clipped(u32 x, u32 y, const char* text, u32 max_width, u32 color) {
    char clipped[160];
    u32 n = 0;
    while (text[n] && (n + 1) * 12 <= max_width && n + 1 < sizeof(clipped)) {
        ++n;
    }
    for (u32 i = 0; i < n; ++i) {
        clipped[i] = text[i];
    }
    clipped[n] = 0;
    label(x, y, clipped, color);
}

static void format_clock(char* out, u32 seconds) {
    u32 hours = seconds / 3600, minutes = (seconds / 60) % 60, secs = seconds % 60;
    out[0] = '0' + (hours / 10) % 10;
    out[1] = '0' + hours % 10;
    out[2] = ':';
    out[3] = '0' + minutes / 10;
    out[4] = '0' + minutes % 10;
    out[5] = ':';
    out[6] = '0' + secs / 10;
    out[7] = '0' + secs % 10;
    out[8] = 0;
}

void framebuffer_chrome(int russian, int scrolled) {
    static int last_russian = -1, last_scrolled = -1;
    static u32 last_generation, last_second = ~0u;
    extern u32 tick;
    u32 seconds = tick / 50;
    if (last_russian == russian && last_scrolled == scrolled && last_generation == generation
        && last_second == seconds) {
        return;
    }
    last_russian = russian;
    last_scrolled = scrolled;
    last_generation = generation;
    last_second = seconds;

    /* Top bar: title on the left, fixed video mode on the right. */
    framebuffer_rect(0, 0, info.width, 44, 0x202733);
    framebuffer_rect(32, 42, info.width - 64, 2, theme_colors[6]);
    label(32, 12, "KintsugiOS / Keramika", theme_colors[14]);
    char text[64];
    snprintf(text, sizeof(text), "%ux%u | ring3", info.width, info.height);
    label_right(info.width - 32, 12, text, theme_colors[7]);

    /* Bottom bar: status on the left, reserved right-aligned group with the
     * keyboard layout and the clock. Each group has its own fixed slot so no
     * label can ever be drawn on top of another. */
    u32 bottom = info.height - 44;
    u32 clock_right = info.width - 32;
    u32 lang_right = clock_right - 112; /* separator + clock width */
    framebuffer_rect(0, bottom, info.width, 44, 0x202733);
    framebuffer_rect(32, bottom + 2, info.width - 64, 2, theme_colors[6]);

    snprintf(
        text,
        sizeof(text),
        "F12 EN/RU | RAM %u KiB | %s",
        memory_stats().free_pages * 4,
        scrolled ? "SCROLL" : "READY");
    label_clipped(32, bottom + 12, text, (lang_right - 56) - 40, theme_colors[7]);

    framebuffer_rect(lang_right - 56, bottom + 9, 2, 26, 0x3d4756);
    framebuffer_rect(lang_right - 48, bottom + 8, 46, 28, 0x2c3542);
    label(lang_right - 36, bottom + 12, russian ? "RU" : "EN", theme_colors[14]);
    framebuffer_rect(clock_right - 108, bottom + 9, 2, 26, 0x3d4756);

    char clock[9];
    format_clock(clock, seconds);
    label_right(clock_right, bottom + 12, clock, theme_colors[15]);
}

/* Arrow sprite: 'X' black outline, 'B' white body, space transparent. Only
 * the visible pixels are saved and restored, so moving the pointer can never
 * smear a solid background block over freshly drawn text. */
#define CURSOR_W 12
#define CURSOR_H 16
static const char* const cursor_art[CURSOR_H] = {
    "X           ", "XX          ", "XBX         ", "XBBX        ", "XBBBX       ", "XBBBBX      ",
    "XBBBBBX     ", "XBBBBBBX    ", "XBBBBBBBX   ", "XBBBBBBBBX  ", "XBBXBBBBBBX ", "XBX XBBBBBX ",
    "X    XBBBX  ", "     XBBX   ", "     XBBX   ", "      XX    "
};
static u32 mouse_saved[CURSOR_W * CURSOR_H], mouse_x, mouse_y;

static int cursor_pixel(u32 x, u32 y) {
    if (x >= CURSOR_W || y >= CURSOR_H) {
        return 0;
    }
    char c = cursor_art[y][x];
    return c == 'X' ? 1 : c == 'B' ? 2 : 0;
}

void framebuffer_mouse_hide(void) {
    if (!mouse_visible) {
        return;
    }
    for (u32 row = 0; row < CURSOR_H; ++row) {
        for (u32 col = 0; col < CURSOR_W; ++col) {
            if (!cursor_pixel(col, row)) {
                continue;
            }
            int px = (int)mouse_x + (int)col, py = (int)mouse_y + (int)row;
            if (px >= 0 && py >= 0 && (u32)px < info.width && (u32)py < info.height) {
                framebuffer_pixel((u32)px, (u32)py, mouse_saved[row * CURSOR_W + col]);
            }
        }
    }
    mouse_visible = 0;
}

void framebuffer_mouse_show(void) {
    const kos_mouse* m = mouse_state();
    if (!info.available || !m->available) {
        return;
    }
    mouse_x = (u32)m->x;
    mouse_y = (u32)m->y;
    if (mouse_x >= info.width || mouse_y >= info.height) {
        return;
    }
    for (u32 row = 0; row < CURSOR_H; ++row) {
        for (u32 col = 0; col < CURSOR_W; ++col) {
            int kind = cursor_pixel(col, row);
            if (!kind) {
                continue;
            }
            int px = (int)mouse_x + (int)col, py = (int)mouse_y + (int)row;
            if (px < 0 || py < 0 || (u32)px >= info.width || (u32)py >= info.height) {
                continue;
            }
            mouse_saved[row * CURSOR_W + col] = ((volatile u32*)FB_WINDOW)[(u32)py * info.width + (u32)px];
            framebuffer_pixel((u32)px, (u32)py, kind == 1 ? theme_colors[0] : theme_colors[15]);
        }
    }
    mouse_visible = 1;
}
