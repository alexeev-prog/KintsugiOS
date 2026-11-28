#ifndef USER_GFX_H
#define USER_GFX_H
#include "desktopabi.h"
#include "runtime.h"

/* Per-process pixel surface. The surface is allocated in the calling
 * process's heap; the kernel only sees it during a validated blit syscall. */
typedef struct {
    int width, height;
    u32* pixels;
} gfx_surface;

typedef struct {
    int left, top, right, bottom;
    int valid;
} gfx_dirty;

#define GFX_RGB(r, g, b) ((u32)(((r) << 16) | ((g) << 8) | (b)))
#define GFX_WHITE GFX_RGB(255, 255, 255)
#define GFX_BLACK GFX_RGB(0, 0, 0)

/* Shared look: every GUI application and every primitive reads this theme. */
typedef struct {
    const char* name;
    u32 face, shadow, light, title, title_text, text, accent;
    u32 desktop, selection, success, warning, error;
    int flat;
    int bright_icons;
} gui_theme_t;

extern gui_theme_t gui_theme;

enum {
    GUI_THEME_CLASSIC = 0,
    GUI_THEME_NEXTSTEP,
    GUI_THEME_MILLENNIUM,
    GUI_THEME_HUMANITY,
    GUI_THEME_FRUIT,
    GUI_THEME_GRAPHITE,
    GUI_THEME_OCEAN,
    GUI_THEME_SOLAR,
    GUI_THEME_COUNT
};

void gfx_theme_set(int id);
int gfx_theme_id(void);
/* Font face weight: synthesized bold from the 5x7 bitmaps when enabled. */
void gfx_font_set_bold(int enabled);
int gfx_font_bold(void);

#define GFX_FACE (gui_theme.face)
#define GFX_SHADOW (gui_theme.shadow)
#define GFX_LIGHT (gui_theme.light)
#define GFX_DARK (gui_theme.title)
#define GFX_TEAL (gui_theme.desktop)
#define GFX_RED GFX_RGB(180, 40, 40)
#define GFX_GREEN GFX_RGB(40, 180, 40)
#define GFX_BLUE GFX_RGB(40, 40, 180)

gfx_surface* gfx_surface_create(int width, int height);
void gfx_surface_destroy(gfx_surface* surface);
void gfx_clear(gfx_surface* surface, u32 color);
void gfx_fill(gfx_surface* surface, int x, int y, int width, int height, u32 color);
void gfx_hline(gfx_surface* surface, int x, int y, int width, u32 color);
void gfx_vline(gfx_surface* surface, int x, int y, int height, u32 color);
void gfx_line(gfx_surface* surface, int x0, int y0, int x1, int y1, int size, u32 color);
void gfx_blit(
    gfx_surface* destination, int x, int y, const gfx_surface* source, int sx, int sy, int width, int height);
void gfx_blit_scale(
    gfx_surface* destination,
    int x,
    int y,
    const gfx_surface* source,
    int sx,
    int sy,
    int width,
    int height,
    int num,
    int den);
void gfx_text(gfx_surface* surface, int x, int y, const char* text, u32 foreground, u32 background);
void gfx_text_scaled(
    gfx_surface* surface, int x, int y, const char* text, u32 foreground, u32 background, int num, int den);
void gfx_text_clipped(
    gfx_surface* surface, int x, int y, int max_width, const char* text, u32 foreground, u32 background);
void gfx_text_wrapped(
    gfx_surface* surface,
    int x,
    int y,
    int width,
    int line_height,
    const char* text,
    u32 foreground,
    u32 background);
/* One 80x25 TUI cell (12x15 pixels, scaled 2x font) for window terminals. */
void gfx_cell(gfx_surface* surface, int x, int y, unsigned char character, unsigned char attribute);
int gfx_text_width(const char* text);
void gfx_bevel(gfx_surface* surface, int x, int y, int width, int height, int raised);
void gfx_groupbox(gfx_surface* surface, int x, int y, int width, int height, const char* label);
void gfx_slider(
    gfx_surface* surface, int x, int y, int width, int height, int value, int maximum, int active);
void gfx_tab(gfx_surface* surface, int x, int y, int width, int height, const char* label, int active);
void gfx_progress(
    gfx_surface* surface, int x, int y, int width, int height, int value, int maximum, u32 color);
void gfx_button(gfx_surface* surface, int x, int y, int width, int height, const char* label, int pressed);
void gfx_titlebar(gfx_surface* surface, int x, int y, int width, const char* title, int active);
/* NeXTSTEP/macOS style titlebar: sunken title plate, leading traffic lights. */
void gfx_titlebar_next(gfx_surface* surface, int x, int y, int width, const char* title, int active);
void gfx_scrollbar(gfx_surface* surface, int x, int y, int height, int position, int total);

void gfx_dirty_add(gfx_dirty* dirty, int x, int y, int width, int height);
void gfx_present(gfx_surface* surface, int x, int y);
void gfx_present_dirty(gfx_surface* surface, int x, int y, gfx_dirty* dirty);
void gfx_screen_fill(int x, int y, int width, int height, u32 color);
int gfx_screen_info(kos_gfx_info* info);
void gfx_exit_to_terminal(void);

/* External GUI client protocol. Windows are owned by the kernel; a process
 * may draw into its own window and the compositor (kgui) reads/blits them. */
int gfx_window_create(int width, int height);
int gfx_window_close(int id);
int gfx_window_fill(int id, int x, int y, int width, int height, u32 color);
int gfx_window_blit(int id, const gfx_surface* source, int x, int y, int width, int height);
int gfx_window_title(int id, const char* title);
int gfx_window_move(int id, int x, int y);
int gfx_window_resize(int id, int width, int height);
int gfx_window_state(int id, int state);
int gfx_window_raise(int id);
int gfx_window_list(kos_gui_window_info* out, int count);
int gfx_window_read(int id, u32* out, int x, int y, int width, int height);
int gfx_window_post(int id, const kos_gui_event* event);
int gfx_window_event(int id, kos_gui_event* event);

#endif
