#include "framebuffer.h"
#include "lowlevel_io.h"
#include "font.h"
#include "mouse.h"
#include "../mm/memory.h"
#include "../kklibc/stdio.h"
#include "../kklibc/stdlib.h"
#include "../../user/desktopabi.h"

/* QEMU standard VGA / Bochs DISPI. Unsupported adapters retain VGA text mode. */
#define FB_WINDOW 0xe0000000u
static framebuffer_info_t info;
static u32 generation, mode_index = 1;
static u32 origin_x, origin_y, cell_width, cell_height;
u32 framebuffer_generation(void) { return generation; }
u32 framebuffer_mode_index(void) { return mode_index; }
static const u32 colors[16] = {
    0x141820, 0x304c78, 0x69a879, 0x66adb5, 0xc66e72, 0xa88bbd, 0xc5a36a, 0xcbd0d8,
    0x687383, 0x729bd0, 0x95c69c, 0x94d5dc, 0xed9696, 0xc6ade0, 0xebce91, 0xf3f2ed
};
static void dispi(u16 index, u16 value) {
    port_word_out(0x1ce, index); port_word_out(0x1cf, value);
}
int framebuffer_mode(u32 mode) {
    if (!info.available || mode >= KOS_VIDEO_MODES) return -1;
    framebuffer_mouse_hide();
    info.width = kos_video_modes[mode].width;
    info.height = kos_video_modes[mode].height;
    info.pitch = info.width * 4;
    mode_index = mode;
    cell_width = (info.width - 64) / 80;
    cell_height = (info.height - 118) / 25;
    origin_x = (info.width - cell_width * 80) / 2;
    origin_y = 58;
    dispi(4, 0); dispi(1, info.width); dispi(2, info.height); dispi(3, 32);
    dispi(6, info.width); dispi(8, 0); dispi(9, 0); dispi(4, 0x41);
    ++generation;
    return 0;
}
void framebuffer_init(void) {
    port_word_out(0x1ce, 0);
    u16 id = port_word_in(0x1cf);
    if (id < 0xb0c0 || id > 0xb0c5) return;
    for (u32 slot = 0; slot < 32; ++slot) {
        u32 config = 0x80000000u | (slot << 11);
        port_dword_out(0xcf8, config);
        u32 device = port_dword_in(0xcfc);
        if (device != 0x11111234 && device != 0xbeef80ee) continue;
        port_dword_out(0xcf8, config | 0x10);
        u32 bar = port_dword_in(0xcfc);
        if ((bar & 1) || !(bar & ~15u)) return;
        info.address = bar & ~15u;
        info.bpp = 32;
        info.available = 1;
        framebuffer_mode(mode_index);
        return;
    }
}
const framebuffer_info_t* framebuffer_get_info(void) { return &info; }
void framebuffer_pixel(u32 x, u32 y, u32 rgb) {
    if (info.available && x < info.width && y < info.height)
        ((volatile u32*)FB_WINDOW)[y * info.width + x] = rgb;
}
void framebuffer_rect(u32 x, u32 y, u32 width, u32 height, u32 rgb) {
    if (!info.available || x >= info.width || y >= info.height) return;
    if (width > info.width - x) width = info.width - x;
    if (height > info.height - y) height = info.height - y;
    for (u32 row = y; row < y + height; ++row)
        for (u32 col = x; col < x + width; ++col)
            ((volatile u32*)FB_WINDOW)[row * info.width + col] = rgb;
}
void framebuffer_clear(u32 rgb) { framebuffer_rect(0, 0, info.width, info.height, rgb); }
void framebuffer_cell(u32 x, u32 y, u8 glyph, u8 attr, int cursor) {
    u32 fg = colors[attr & 15], bg = colors[(attr >> 4) & 15];
    for (u32 row = 0; row < cell_height; ++row) {
        u32 font_row = row * 20 / cell_height;
        u16 bits = console_font_row(glyph, font_row);
        volatile u32* pixels = (volatile u32*)FB_WINDOW + (origin_y + y * cell_height + row) * info.width + origin_x + x * cell_width;
        for (u32 col = 0; col < cell_width; ++col)
            pixels[col] = cursor && row >= cell_height - 2 ? colors[14]
                : (bits & (1u << (11 - col * 12 / cell_width))) ? fg : bg;
    }
}
static void label(u32 x, u32 y, const char* text, u32 color) {
    while (*text) {
        u8 glyph = *text++;
        for (u32 row = 0; row < 20; ++row)
            for (u32 col = 0; col < 12; ++col)
                if (console_font_row(glyph, row) & (1u << (11 - col))) framebuffer_pixel(x + col, y + row, color);
        x += 12;
    }
}
void framebuffer_chrome(int russian, int scrolled) {
    static int last_russian = -1, last_scrolled = -1;
    static u32 last_generation, last_second = ~0u;
    extern u32 tick;
    if (last_russian == russian && last_scrolled == scrolled && last_generation == generation
        && last_second == tick / 50) return;
    last_russian = russian; last_scrolled = scrolled;
    last_generation = generation; last_second = tick / 50;
    framebuffer_rect(0, 0, info.width, 44, 0x202733);
    framebuffer_rect(32, 42, info.width - 64, 2, colors[6]);
    label(32, 12, "KintsugiOS / Keramika", colors[14]);
    char text[96];
    snprintf(text, sizeof(text), "%ux%u | ring3", info.width, info.height);
    label(info.width - 300, 12, text, colors[7]);
    framebuffer_rect(0, info.height - 44, info.width, 44, 0x202733);
    snprintf(text, sizeof(text), "F12: EN/RU | RAM free %u KiB | UP %us | %s",
        memory_stats().free_pages * 4, tick / 50, scrolled ? "SCROLL" : "READY");
    label(32, info.height - 32, text, colors[7]);
    label(info.width - 80, info.height - 32, russian ? "RU" : "EN", colors[14]);
}

static u32 mouse_saved[16 * 20], mouse_x, mouse_y;
static int mouse_visible;
void framebuffer_mouse_hide(void) {
    if (!mouse_visible) return;
    for (u32 y = 0; y < 20 && mouse_y + y < info.height; ++y)
        for (u32 x = 0; x < 16 && mouse_x + x < info.width; ++x)
            ((volatile u32*)FB_WINDOW)[(mouse_y + y) * info.width + mouse_x + x] = mouse_saved[y * 16 + x];
    mouse_visible = 0;
}
void framebuffer_mouse_show(void) {
    const kos_mouse* m = mouse_state();
    if (!info.available || !m->available) return;
    mouse_x = m->x; mouse_y = m->y;
    if (mouse_x >= info.width || mouse_y >= info.height) return;
    for (u32 y = 0; y < 20 && mouse_y + y < info.height; ++y)
        for (u32 x = 0; x < 16 && mouse_x + x < info.width; ++x) {
            mouse_saved[y * 16 + x] = ((volatile u32*)FB_WINDOW)[(mouse_y + y) * info.width + mouse_x + x];
            if (x <= y / 2 && y < 18)
                framebuffer_pixel(mouse_x + x, mouse_y + y, x == 0 || x == y / 2 || y == 17 ? 0x141820 : 0xf3f2ed);
        }
    mouse_visible = 1;
}
