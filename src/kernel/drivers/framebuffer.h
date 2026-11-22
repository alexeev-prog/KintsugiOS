#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H
#include "../kklibc/ctypes.h"
typedef struct { u32 address, width, height, pitch, bpp; u8 available; } framebuffer_info_t;
void framebuffer_init(void);
const framebuffer_info_t* framebuffer_get_info(void);
void framebuffer_clear(u32 rgb);
void framebuffer_pixel(u32 x, u32 y, u32 rgb);
void framebuffer_rect(u32 x, u32 y, u32 width, u32 height, u32 rgb);
void framebuffer_cell(u32 x, u32 y, u8 glyph, u8 attr, int cursor);
void framebuffer_chrome(int russian, int scrolled);
int framebuffer_mode(u32 mode);
u32 framebuffer_generation(void);
u32 framebuffer_mode_index(void);
void framebuffer_mouse_hide(void);
void framebuffer_mouse_show(void);
#endif
