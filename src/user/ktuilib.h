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
    TUI_PAGE_DOWN,
    TUI_INSERT,
    TUI_F1,
    TUI_F2,
    TUI_F3,
    TUI_F4,
    TUI_F5,
    TUI_F6,
    TUI_F7,
    TUI_F8,
    TUI_F9,
    TUI_F10
};

typedef kos_stat tui_stat;
typedef kos_dirent tui_entry;

/* Bounded multiline editor. Capacity excludes the required trailing NUL byte.
 * Caller owns text[capacity+1]; widget owns up to eight undo/redo snapshots. */
typedef struct {
    char* text;
    u32 length, capacity, cursor, top;
    char* history[8];
    u32 lengths[8], cursors[8], history_count, history_position;
} tui_editor;

void tui_editor_destroy(tui_editor* editor);
/* 1 changed, 0 navigation/unhandled, -1 full/allocation failure. */
int tui_editor_key(tui_editor* editor, int key);
void tui_editor_draw(tui_editor* editor, int x, int y, int width, int height);
int tui_document_key(int key, u32* top, u32 total, u32 visible);
/* Same layout pass measures and draws: no separate wrapping implementation.
 * Returns total rendered rows; height=0 measures without drawing. */
u32 tui_markdown(const char* text, u32 length, int x, int y, int width, int height, u32 top);

/* Dedicated popup role; older themes inherit their selection color. */
#define TUI_POPUP 0x71
#define TUI_DESKTOP 0x72

/* Semantic slots; omitted mappings inherit the corresponding legacy color. */
enum {
    TUI_INPUT = 0x73,
    TUI_BUTTON,
    TUI_FOCUS,
    TUI_DISABLED,
    TUI_ERROR,
    TUI_WARNING,
    TUI_SUCCESS,
    TUI_MARKED,
    TUI_DIRECTORY,
    TUI_BORDER,
    TUI_COMMENT,
    TUI_STRING,
    TUI_NUMBER,
    TUI_KEYWORD,
    TUI_VISUAL,
    TUI_STATUS
};

typedef struct {
    char* text;
    u32 capacity, cursor;
} tui_input;

int tui_input_key(tui_input* input, int key);
void tui_input_draw(tui_input* input, int x, int y, int width, int focused);
void tui_button(int x, int y, int width, const char* label, int focused, int enabled);
void tui_checkbox(int x, int y, const char* label, int checked, int focused);
void tui_radio(int x, int y, const char* label, int checked, int focused);
/* Space toggles a checkbox; arrows select a radio member. */
int tui_toggle_key(int key, int* checked);
int tui_focus_key(int key, u32* focus, u32 count);
void tui_list_draw(
    int x,
    int y,
    int width,
    int height,
    const char* const* items,
    u32 count,
    u32 selected,
    u32 top,
    int focused);
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
