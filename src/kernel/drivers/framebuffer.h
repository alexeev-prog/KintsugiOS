#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H
#include "../kklibc/ctypes.h"
typedef struct { u32 address, width, height, pitch, bpp; u8 available; } framebuffer_info_t;
void framebuffer_init(void);
const framebuffer_info_t* framebuffer_get_info(void);
void framebuffer_clear(u32 rgb);
void framebuffer_pixel(u32 x, u32 y, u32 rgb);
void framebuffer_rect(u32 x, u32 y, u32 width, u32 height, u32 rgb);
#endif
