#ifndef KTUILIB_H
#define KTUILIB_H
#include "runtime.h"
#include "tuiabi.h"
#include "fsabi.h"

enum {
    TUI_UP = 256,
    TUI_DOWN,
    TUI_LEFT,
    TUI_RIGHT,
    TUI_HOME,
    TUI_END,
    TUI_DELETE
};

typedef kos_stat tui_stat;
typedef kos_dirent tui_entry;

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
void tui_fill(int x, int y, int width, int height, unsigned char color);
void tui_textn(int x, int y, const char* text, u32 width, unsigned char color);
void tui_box(int x, int y, int width, int height, const char* title, unsigned char color);
void tui_message(const char* title, const char* message);
void tui_gauge(int x, int y, int width, u32 used, u32 total);
/* Small per-application settings file, stored under /ETC (8.3 names). */
int tui_load_settings(const char* path, void* data, u32 size);
int tui_save_settings(const char* path, const void* data, u32 size);
#endif
