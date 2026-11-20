#include "framebuffer.h"
#include "screen.h"

/* Drawing boundary for the future VBE handoff. The current boot protocol has
 * no linear framebuffer address, so the backend safely remains unavailable. */
static framebuffer_info_t info;
static u32 shadow[80 * 25];
void framebuffer_init(void) {
    info.address = VIDEO_ADDRESS; info.width = MAX_COLS; info.height = MAX_ROWS;
    info.pitch = MAX_COLS * 2; info.bpp = 16; info.available = 0;
}
const framebuffer_info_t* framebuffer_get_info(void) { return &info; }
void framebuffer_clear(u32 rgb) { for (u32 i = 0; i < 80 * 25; ++i) shadow[i] = rgb; }
void framebuffer_pixel(u32 x, u32 y, u32 rgb) { if (x < 80 && y < 25) shadow[y * 80 + x] = rgb; }
void framebuffer_rect(u32 x, u32 y, u32 width, u32 height, u32 rgb) {
    for (u32 row = 0; row < height; ++row)
        for (u32 col = 0; col < width; ++col) framebuffer_pixel(x + col, y + row, rgb);
}
