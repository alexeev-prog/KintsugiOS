#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H
#include "../kklibc/ctypes.h"

typedef struct {
    u32 address, width, height, pitch, bpp;
    u8 available;
} framebuffer_info_t;

void framebuffer_init(void);
const framebuffer_info_t* framebuffer_get_info(void);
void framebuffer_clear(u32 rgb);
void framebuffer_pixel(u32 x, u32 y, u32 rgb);
void framebuffer_rect(u32 x, u32 y, u32 width, u32 height, u32 rgb);
void framebuffer_cell(u32 x, u32 y, u8 glyph, u8 attr, int cursor);
/* Pixel graphics ABI: blit a 32-bit RGB surface into the linear framebuffer. */
void framebuffer_blit(u32 x, u32 y, u32 width, u32 height, u32 stride, const u32* pixels);
void framebuffer_chrome(int russian, int scrolled);
int framebuffer_mode(u32 mode);
u32 framebuffer_generation(void);
u32 framebuffer_mode_index(void);
void framebuffer_mouse_hide(void);
void framebuffer_mouse_show(void);
void framebuffer_theme(u32 theme);
void framebuffer_scale(u32 scale);
void framebuffer_font(u32 font);
void framebuffer_font_size(u32 size);
u32 framebuffer_theme_index(void);
u32 framebuffer_scale_index(void);
u32 framebuffer_font_index(void);
u32 framebuffer_font_size_index(void);
/* Actual cell geometry: width, height, origin x, origin y (pixels). */
void framebuffer_geometry(u32 out[4]);
/* Fixed DOS-era palette for games, independent of user themes. */
void framebuffer_game_palette(int enabled);
#endif
