#ifndef KERNEL_GFXWM_H
#define KERNEL_GFXWM_H
#include "../../user/desktopabi.h"
#include "../kklibc/ctypes.h"

void gfxwm_init(void);

int gfxwm_create(u32 width, u32 height);
int gfxwm_close(int id, u32 owner_pid, int privileged_uid);
int gfxwm_fill(int id, int x, int y, u32 width, u32 height, u32 color);
int gfxwm_blit(int id, const u32* source, int x, int y, u32 width, u32 height);
int gfxwm_title(int id, const char* title);
int gfxwm_move(int id, int x, int y);
int gfxwm_resize(int id, u32 width, u32 height);
int gfxwm_state(int id, int state);
int gfxwm_raise(int id);
int gfxwm_list(kos_gui_window_info* out, u32 capacity);
int gfxwm_read(int id, u32* out, int x, int y, u32 width, u32 height);
int gfxwm_post(int id, const kos_gui_event* event);
int gfxwm_event(int id, kos_gui_event* out);

#endif
