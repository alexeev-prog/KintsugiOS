#ifndef KTUILIB_H
#define KTUILIB_H
#include "fsabi.h"
#include "runtime.h"
#include "tuiabi.h"

enum {
    TUI_UP = 256,
    TUI_DOWN,
    TUI_LEFT,
    TUI_RIGHT,
    TUI_HOME,
    TUI_END,
    TUI_DELETE,
    TUI_SCROLL_UP,
    TUI_SCROLL_DOWN,
    TUI_PAGE_UP,
    TUI_PAGE_DOWN
};

typedef kos_stat tui_stat;
typedef kos_dirent tui_entry;

/* Dedicated popup role; older themes inherit their selection color. */
#define TUI_POPUP 0x71
#define TUI_DESKTOP 0x72
/* Normalize a short name or path to /ETC/THEMES/NAME.THM; ASCII 8.3 only. */
int tui_theme_path(char out[256], const char* name);

/* Windows use screen coordinates; content coordinates are clipped to the client area. */
typedef struct {
    int x, y, width, height, focused;
    const char* title;
} tui_window;

void tui_window_draw(const tui_window* window);
void tui_window_text(const tui_window* window, int x, int y, const char* text, unsigned char color);
int tui_modal_begin(const char* title, int width, int height);
void tui_modal_end(void);
/* Returns selected item or -1 on cancellation. */
int tui_menu(const char* title, const char* const* items, int count, int selected);
void tui_scrollbar(int x, int y, int height, u32 top, u32 visible, u32 total);
int tui_list_key(int key, u32* selected, u32* top, u32 count, u32 visible);
/* Theme-aware choice row with a persistent marker and readable selection. */
void tui_choice_row(int x, int y, int width, const char* label, int selected, unsigned char normal);
/* Theme lines: XX=YY (hex VGA attributes), # comments. Atomic validation. */
int tui_theme_parse(const char* text, u32 length);
int tui_theme_load(const char* path);
void tui_theme_get(unsigned char out[256]);
/* Draw an unthemed VGA sample, useful for palette editors and color guides. */
void tui_sample(int x, int y, const char* text, unsigned char color);
int tui_theme_choose(void);
int tui_command(const char* prefix, char* buffer, u32 capacity);

u32 tui_len(const char* text);
void tui_copy(char* out, const char* text, u32 capacity);
void tui_clear(unsigned char color);
void tui_text(int x, int y, const char* text, unsigned char color);
void tui_number(int x, int y, u32 value, unsigned char color);
void tui_bar(int y, const char* text);
void tui_present(int x, int y);
int tui_key(int wait);
int tui_prompt(const char* label, char* buffer, u32 capacity);
int tui_confirm(const char* label);
void tui_path(char out[256], const char* directory, const char* name);
int tui_stat_path(const char* path, tui_stat* stat);
void tui_parent(char path[256]);
void tui_fill(int x, int y, int width, int height, unsigned char color);
void tui_textn(int x, int y, const char* text, u32 width, unsigned char color);
void tui_box(int x, int y, int width, int height, const char* title, unsigned char color);
void tui_message(const char* title, const char* message);
void tui_gauge(int x, int y, int width, u32 used, u32 total);
/* Small per-application settings file, stored under /ETC (8.3 names). */
int tui_load_settings(const char* path, void* data, u32 size);
int tui_save_settings(const char* path, const void* data, u32 size);
#endif
