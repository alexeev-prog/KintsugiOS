#include "../shared/text.h"
#include "authabi.h"
#include "errors.h"
#include "gfx.h"
#include "sysabi.h"
#include "tuiabi.h"

/* Foreground for surfaces filled with the theme face. A dark theme has a pale
 * text colour, so hardcoded black would become unreadable there. */
#define GUI_INK (gui_theme.text)
/* KGUI - pixel window manager, Win95-like desktop and three graphic apps:
 * KGFM (files), KGED (editor), KGTERM (terminal). */

#define MAX_WINDOWS 12
#define MENUBAR_HEIGHT 24
#define DOCK_HEIGHT 40
#define TASKBAR_HEIGHT DOCK_HEIGHT
#define TITLE_HEIGHT 24
#define STATUS_HEIGHT 20
#define MAX_RESULTS 24
#define TRAY_WIDTH 300
/* NeXTSTEP places the three window controls at the leading (left) edge. */
#define TITLE_CLOSE_X 10
#define TITLE_MIN_X 28
#define TITLE_ZOOM_X 46
#define TITLE_BUTTON_Y 5
#define TITLE_BUTTON_SIZE 14
#define NOTE_CAPACITY 4096
#define TERM_ROWS 48
#define TERM_COLS 96
#define MAX_PROCESS_ROWS 16
#define POLL_INTERVAL 5
#define WALLPAPER_COUNT 8
#define DOCK_SLOT 44
#define DOCK_TILE 38
/* Title-bar buttons, window-relative. Keep these in sync with gfx_titlebar(),
 * which draws at (2,2) with width (w->width - 4). */
#define TITLE_BUTTON_W TITLE_BUTTON_SIZE
#define GUI_CURSOR_HIT_MARGIN 2

static int zvalid(int index) {
    return index >= 0 && index < MAX_WINDOWS;
}

typedef struct {
    int used, x, y, width, height;
    int saved_x, saved_y, saved_width, saved_height;
    int minimized, maximized;
    int anim_open, anim_progress; /* open animation: scale 0.75 -> 1.0 */
    int anim_close; /* closing animation: scale 1.0 -> 0.75 */
    char title[48];
    gfx_surface* surface;
    int kind; /* 0 KGFM, 1 KGED, 2 KGTERM, 3 About, 4 Notepad */
    /* KGFM */
    char path[160];
    kos_long_dirent entries[80];
    int entry_count, selected, top;
    char shelf[8][160], shelf_label[8][16];
    int shelf_count, shelf_selected;
    /* KGED */
    gfx_surface* canvas;
    int tool;
    u32 color;
    int drawing, last_paint_x, last_paint_y;
    /* KGTERM - in-window shell */
    char output[TERM_ROWS][TERM_COLS + 1];
    u32 output_color[TERM_ROWS];
    int out_count, out_top;
    /* Partially received line: program output may arrive in several reads. */
    char pending[TERM_COLS + 1];
    int pending_length;
    u32 pending_ink;
    char input[128];
    int input_length;
    char history[16][120];
    int history_count, history_pos;
    /* Notepad */
    char text[NOTE_CAPACITY];
    int text_length;
    int text_top;
    char file_path[160];
    /* Administration window */
    int admin_field;
    char admin_password[64];
    char admin_command[128];
    char admin_output[256];
    /* KSystem - task and service manager */
    int sys_tab, sys_selected;
    /* Settings document */
    int settings_tab;
} gui_window;

static gui_window windows[MAX_WINDOWS];
static int zorder[MAX_WINDOWS] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static int window_count;
static u32 desktop_color = GFX_RGB(0, 128, 128);
/* Settings document and wallpaper engine. */
static gfx_surface* wallpaper_surface;
static int wallpaper_kind, wallpaper_color_a, wallpaper_color_b, wallpaper_scale = 32;
static int dock_size = 18, dock_autohide;
static int dock_hover_key = -1, dock_mag_key = -1, dock_mag;
static int volume_master = 70, volume_music = 70, volume_effects = 70;
static const char* const wallpaper_names[WALLPAPER_COUNT] = { "Solid", "Grid",    "Diagonal", "Checker",
                                                              "Dots",  "Stripes", "Weave",    "Ripple" };
static int gui_language; /* 0 English, 1 Russian */
static char gui_toast[64];
static int gui_toast_until;
static char settings_users[8][32];
static int settings_user_count;
static int context_open, context_x, context_y, context_window, fm_action, context_kind;
static char context_items[10][32];
static int context_count, context_hover = -1;
/* Rectangle of the last composited overlay. It must be invalidated when the
 * overlay closes or moves, otherwise its pixels survive the next partial
 * repaint (the classic "leftover menu pieces" artifact). */
static gfx_dirty overlay_prev = { 0, 0, 0, 0, 0 };
/* Teardown is deferred until compose has finished walking zorder: destroying a
 * window inside that walk corrupts the iteration and can skip a redraw. */
static int pending_close[MAX_WINDOWS], pending_close_count;
static int global_menu_column = -1;
static gfx_surface* desktop;
static int screen_width, screen_height;
static int focused = -1;
static int menu; /* 0 none, 1 Start, 2 Programs, 3 Search, 4 Run, 5 Help */
static char dialog_input[160];
static int dialog_length;
static char search_results[MAX_RESULTS][160];
static int search_count;
static int dragging, resizing, drag_dx, drag_dy;
static int dialog_window_index = -1;
/* Set when the focused window moved through the keyboard: the old position
 * must be repainted, so the frame cannot use a single-window dirty rect. */
static int gui_keyboard_move;
static kos_mouse gui_mouse;
static unsigned previous_buttons;
static int program_category = -1;
static int external_focus = -1;
static int last_click_index = -1, last_click_window = -1;
static u32 last_click_tick, gui_frame;
/* A TUI child renders its 80x25 presentation into the active KGTERM window. */
static int tui_child_pid = -1;
static gui_window* tui_window;
static ktui_cell tui_cells[25][80];
static int tui_saved_x, tui_saved_y, tui_saved_w, tui_saved_h;
static gui_window* paint_window;
static int paint_dirty;
/* Non-blocking CLI child: the GUI keeps repainting while a program runs. */
static int cli_child_pid = -1, cli_fd = -1, cli_frames;
static gui_window* cli_window;
static gfx_surface* drag_backup;
static int drag_index = -1;
static int keyboard_resize;
static int last_title_click = -1;
static u32 last_title_tick;
/* Cached external-client surfaces: allocating one per composited frame made
 * pointer movement noticeably stutter. */
static gfx_surface* external_surface[8];
/* Resource monitor samples. */
static u32 monitor_cpu, monitor_previous_ticks;
static int monitor_frame;
/* Task manager snapshot, refreshed a few times per second rather than per frame. */
static kos_process_info sys_processes[MAX_PROCESS_ROWS];
static int sys_process_count, sys_refresh_frame;
static u32 sys_service_pids[8];

static int external_locate(int id, kos_gui_window_info* out) {
    kos_gui_window_info list[8];
    int count = gfx_window_list(list, 8);
    for (int i = 0; i < count; ++i) {
        if (list[i].id == id) {
            *out = list[i];
            return 1;
        }
    }
    return 0;
}

static void close_window(gui_window* w);
static void window_finish(gui_window* w);
static void open_help_document(void);
static gui_window* activate_window_kind(int kind, const char* title);
static gui_window* create_window(const char* title, int x, int y, int width, int height, int kind);
static void term_prompt_string(gui_window* w, char* out, u32 cap);
static void term_shell_banner(gui_window* w);
static void draw_window(gui_window* w);
static void toggle_maximize(gui_window* w);
static void window_surface_rebuilt(gui_window* w);
static void tui_launch(gui_window* w, const char* name);
static void tui_finish(void);
static void term_launch(gui_window* w, const char* name, char** argv, int argc);
static void launch_category_item(int category, int item);
static void draw_monitor(gui_window* w, gfx_surface* s, int content_y, int status_y);
static void draw_system(gui_window* w, gfx_surface* s, int content_y, int status_y);

/* Single source of truth for the Programs submenu: drawing, hit testing and
 * keyboard shortcuts all use these tables, so no entry can be unreachable. */
static const char* const program_names[5][9] = {
    { "Notepad", "kfetch",  0,          0,         0,           0,         0,       0, 0 },
    { "KPaint",  "KGED",    0,          0,         0,           0,         0,       0, 0 },
    { "KGFM",    "KGTERM",  "KMonitor", "KSystem", "KServices", "KSersys", "KInfo", 0, 0 },
    { "KMusa",   "KListen", "SoundCtl", 0,         0,           0,         0,       0, 0 },
    { "KRrawl",  "KMines",  "K2048",    "KLife",   "KTmux",     0,         0,       0, 0 }
};
static const int program_counts[5] = { 2, 2, 7, 3, 5 };

static int program_count(int category) {
    return category >= 0 && category < 5 ? program_counts[category] : 0;
}

static int start_menu_height(void) {
    if (menu == 2) {
        return 220;
    }
    if (menu == 6) {
        int height = 38 + (program_count(program_category) + 1) * 28;
        return height > screen_height - TASKBAR_HEIGHT - 8 ? screen_height - TASKBAR_HEIGHT - 8 : height;
    }
    return 272;
}

static int text_length(const char* s) {
    int n = 0;
    while (s[n]) {
        ++n;
    }
    return n;
}

static void toast(const char* text) {
    int n = 0;
    while (text[n] && n + 1 < (int)sizeof(gui_toast)) {
        gui_toast[n] = text[n];
        ++n;
    }
    gui_toast[n] = 0;
    gui_toast_until = 80;
}

static int text_equal(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int text_equal_ci(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
        char cb = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (ca != cb) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

static int text_has(const char* haystack, const char* needle) {
    if (!*needle) {
        return 1;
    }
    /* File names are upper-case in the image; queries are typed lower-case. */
    for (const char* p = haystack; *p; ++p) {
        const char *h = p, *n = needle;
        while (*h && *n) {
            char a = *h, b = *n;
            if (a >= 'a' && a <= 'z') {
                a -= 32;
            }
            if (b >= 'a' && b <= 'z') {
                b -= 32;
            }
            if (a != b) {
                break;
            }
            ++h;
            ++n;
        }
        if (!*n) {
            return 1;
        }
    }
    return 0;
}

static int number_text(char* out, int cap, unsigned value) {
    char digits[12];
    int d = 11;
    digits[d] = 0;
    do {
        digits[--d] = '0' + value % 10;
        value /= 10;
    } while (value);
    int n = 0;
    while (d < 12 && digits[d] && n + 1 < cap) {
        out[n++] = digits[d++];
    }
    out[n] = 0;
    return n;
}

static void text_copy(char* out, int capacity, const char* in) {
    int n = 0;
    while (in[n] && n + 1 < capacity) {
        out[n] = in[n];
        ++n;
    }
    out[n] = 0;
}

static void dirty_full(gfx_dirty* dirty) {
    dirty->left = 0;
    dirty->top = 0;
    dirty->right = screen_width;
    dirty->bottom = screen_height;
    dirty->valid = 1;
}

static int dirty_rect_hit(gfx_dirty* dirty, int x, int y, int width, int height) {
    if (!dirty || !dirty->valid || width <= 0 || height <= 0) {
        return 1; /* No explicit region: everything must be painted. */
    }
    return x < dirty->right && x + width > dirty->left && y < dirty->bottom && y + height > dirty->top;
}

static void dirty_window(gfx_dirty* dirty, gui_window* w) {
    dirty->valid = 0;
    gfx_dirty_add(dirty, w->x, w->y, w->width, w->height);
}

static int window_at(int px, int py) {
    for (int i = MAX_WINDOWS - 1; i >= 0; --i) {
        int index = zorder[i];
        if (!zvalid(index)) {
            continue;
        }
        gui_window* w = &windows[index];
        if (!w->used || w->minimized) {
            continue;
        }
        if (px >= w->x && px < w->x + w->width && py >= w->y && py < w->y + w->height) {
            return index;
        }
    }
    return -1;
}

static void raise_window(int index) {
    if (!zvalid(index) || !windows[index].used) {
        return;
    }
    int position = 0;
    while (position < MAX_WINDOWS && zorder[position] != index) {
        ++position;
    }
    if (position == MAX_WINDOWS) {
        /* Reopened windows are not in the z-order yet: insert them at the top. */
        int slot = 0;
        while (slot < MAX_WINDOWS && zvalid(zorder[slot])) {
            ++slot;
        }
        if (slot >= MAX_WINDOWS) {
            slot = MAX_WINDOWS - 1;
        }
        zorder[slot] = index;
        position = slot;
    }
    for (int i = position; i < MAX_WINDOWS - 1; ++i) {
        zorder[i] = zorder[i + 1];
    }
    zorder[MAX_WINDOWS - 1] = index;
    focused = index;
}

static void fm_refresh(gui_window* w) {
    w->entry_count = 0;
    u32 cursor = 0;
    while (w->entry_count < 80
           && syscall(KOS_SYS_READDIR_LONG, (u32)w->path, (u32)&cursor, (u32)&w->entries[w->entry_count], 0)
               > 0) {
        ++w->entry_count;
    }
    if (w->selected >= w->entry_count) {
        w->selected = w->entry_count ? w->entry_count - 1 : 0;
    }
    if (w->top > w->selected) {
        w->top = w->selected;
    }
}

static void fm_up(gui_window* w) {
    int n = text_length(w->path);
    while (n > 1 && w->path[n - 1] == '/') {
        --n;
    }
    while (n > 1 && w->path[n - 1] != '/') {
        --n;
    }
    if (n > 1) {
        --n;
    }
    w->path[n] = 0;
    w->selected = w->top = 0;
    fm_refresh(w);
}

static void fm_selected_path(gui_window* w, char* out, u32 capacity) {
    int n = 0;
    while (w->path[n] && n + 1 < (int)capacity) {
        out[n] = w->path[n];
        ++n;
    }
    if (w->path[n - 1] != '/' && n + 1 < (int)capacity) {
        out[n++] = '/';
    }
    if (w->selected < w->entry_count) {
        const char* name = w->entries[w->selected].name;
        int i = 0;
        while (name[i] && n + i + 1 < (int)capacity) {
            out[n + i] = name[i];
            ++i;
        }
        n += i;
    }
    out[n] = 0;
}

static void fm_join(gui_window* w, const char* name, char* out, u32 capacity) {
    int n = 0;
    while (w->path[n] && n + 1 < (int)capacity) {
        out[n] = w->path[n];
        ++n;
    }
    if (out[n - 1] != '/' && n + 1 < (int)capacity) {
        out[n++] = '/';
    }
    int i = 0;
    while (name[i] && n + i + 1 < (int)capacity) {
        out[n + i] = name[i];
        ++i;
    }
    out[n + i] = 0;
}

static void fm_enter(gui_window* w);

static void fm_open(gui_window* w) {
    if (w->selected >= w->entry_count) {
        return;
    }
    if (w->entries[w->selected].stat.directory) {
        fm_enter(w);
        return;
    }
    char path[256];
    fm_selected_path(w, path, sizeof(path));
    int fd = open(path, KOS_O_READ);
    if (fd < 0) {
        toast("Cannot open file");
        return;
    }
    gui_window* note = create_window("Notepad", 680, 400, 520, 320, 4);
    if (note) {
        int n = read(fd, note->text, sizeof(note->text) - 1);
        if (n > 0) {
            note->text[n] = 0;
            note->text_length = n;
        }
        text_copy(note->file_path, sizeof(note->file_path), path);
    } else {
        toast("Cannot create window");
    }
    close(fd);
}

static void fm_enter(gui_window* w) {
    if (w->selected >= w->entry_count) {
        return;
    }
    kos_long_dirent* entry = &w->entries[w->selected];
    if (!entry->stat.directory) {
        return;
    }
    int n = text_length(w->path);
    if (w->path[n - 1] != '/') {
        w->path[n++] = '/';
    }
    int i = 0;
    while (entry->name[i] && n + i + 1 < (int)sizeof(w->path)) {
        w->path[n + i] = entry->name[i];
        ++i;
    }
    w->path[n + i] = 0;
    w->selected = w->top = 0;
    fm_refresh(w);
}

#define TERM_DEFAULT_INK GFX_RGB(120, 255, 120)
static u32 term_ink = TERM_DEFAULT_INK;

static u32 term_sgr_color(unsigned code, u32 current) {
    if (code == 0 || code == 39) {
        return TERM_DEFAULT_INK;
    }
    if (code == 1) {
        return GFX_WHITE;
    }
    if (code == 8 || code == 90) {
        return GFX_RGB(128, 128, 128);
    }
    if (code == 31 || code == 91) {
        return GFX_RGB(255, 120, 120);
    }
    if (code == 32 || code == 92) {
        return GFX_RGB(120, 255, 120);
    }
    if (code == 33 || code == 93) {
        return GFX_RGB(255, 255, 120);
    }
    if (code == 34 || code == 94) {
        return GFX_RGB(120, 180, 255);
    }
    if (code == 35 || code == 95) {
        return GFX_RGB(255, 120, 255);
    }
    if (code == 36 || code == 96) {
        return GFX_RGB(120, 255, 255);
    }
    if (code == 37 || code == 97) {
        return GFX_WHITE;
    }
    return current;
}

static void term_push_row(gui_window* w, const char* text, u32 color) {
    int index;
    if (w->out_count < TERM_ROWS) {
        index = w->out_count++;
    } else {
        for (int i = 1; i < TERM_ROWS; ++i) {
            text_copy(w->output[i - 1], sizeof(w->output[0]), w->output[i]);
            w->output_color[i - 1] = w->output_color[i];
        }
        index = TERM_ROWS - 1;
    }
    text_copy(w->output[index], sizeof(w->output[0]), text);
    w->output_color[index] = color;
}

/* Program output may contain ANSI SGR sequences (kfetch uses them). Render
 * the text instead of showing raw escape bytes. The partial line lives in the
 * window so a program whose output arrives in several reads is still joined
 * into single rows. */
static void term_color(gui_window* w, u32 color, u32* ink) {
    *ink = color;
    /* The first colour chosen for a row describes the whole line; kfetch
     * resets to default before its newline, which would otherwise make every
     * line green. */
    if (w->pending_length == 0 && color != TERM_DEFAULT_INK) {
        w->pending_ink = color;
    }
}

static void term_write(gui_window* w, const char* data, int size) {
    int limit = (w->width - 28) / 12;
    if (limit < 8) {
        limit = 8;
    }
    if (limit > TERM_COLS) {
        limit = TERM_COLS;
    }
    u32 ink = term_ink;
    char* row = w->pending;
    int length = w->pending_length;
    for (int i = 0; i < size; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (c == 27) {
            int j = i + 1;
            if (j < size && data[j] == '[') {
                ++j;
                unsigned value = 0;
                int have_value = 0;
                while (j < size && ((data[j] >= '0' && data[j] <= '9') || data[j] == ';')) {
                    if (data[j] == ';') {
                        if (have_value) {
                            term_color(w, term_sgr_color(value, ink), &ink);
                        }
                        value = 0;
                        have_value = 0;
                    } else {
                        value = value * 10 + (unsigned)(data[j] - '0');
                        have_value = 1;
                    }
                    ++j;
                }
                if (have_value) {
                    term_color(w, term_sgr_color(value, ink), &ink);
                }
                if (j < size) {
                    ++j; /* consume the final command byte (usually 'm') */
                }
                i = j - 1;
            } else {
                while (j < size && (unsigned char)data[j] >= 0x40 && (unsigned char)data[j] <= 0x7e) {
                    ++j;
                }
                i = j < size ? j : size - 1;
            }
            continue;
        }
        if (c == '\n') {
            row[length] = 0;
            term_push_row(w, row, w->pending_ink != TERM_DEFAULT_INK ? w->pending_ink : ink);
            length = 0;
            w->pending_ink = TERM_DEFAULT_INK;
            continue;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\b') {
            if (length) {
                --length;
            }
            continue;
        }
        if (c == '\t') {
            while (length < limit && length % 8) {
                row[length++] = ' ';
            }
            continue;
        }
        if (c < 32 || c == 127) {
            continue;
        }
        if (length >= limit) {
            row[length] = 0;
            term_push_row(w, row, w->pending_ink != TERM_DEFAULT_INK ? w->pending_ink : ink);
            length = 0;
            w->pending_ink = TERM_DEFAULT_INK;
        }
        row[length++] = (char)c;
    }
    row[length] = 0;
    w->pending_length = length;
    term_ink = ink;
}

static void term_flush(gui_window* w) {
    if (!w || !w->pending_length) {
        return;
    }
    term_push_row(w, w->pending, w->pending_ink != TERM_DEFAULT_INK ? w->pending_ink : term_ink);
    w->pending[0] = 0;
    w->pending_length = 0;
    w->pending_ink = TERM_DEFAULT_INK;
}

static void term_print(gui_window* w, const char* line) {
    term_ink = TERM_DEFAULT_INK;
    w->pending_ink = TERM_DEFAULT_INK;
    if (!*line) {
        term_push_row(w, "", term_ink);
        return;
    }
    term_write(w, line, text_length(line));
    term_flush(w);
    term_ink = TERM_DEFAULT_INK;
}

static void split_command(char* line, char** argv, int* argc) {
    *argc = 0;
    char* p = line;
    while (*p && *argc < 8) {
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (!*p) {
            break;
        }
        argv[(*argc)++] = p;
        while (*p && *p != ' ' && *p != '\t') {
            ++p;
        }
        if (*p) {
            *p++ = 0;
        }
    }
}

static void term_prompt_string(gui_window* w, char* out, u32 cap) {
    char user[32];
    user[0] = 0;
    syscall(KOS_SYS_GETUSER, (u32)user, sizeof(user), 0, 0);
    if (!user[0]) {
        text_copy(user, sizeof(user), "user");
    }
    int n = 0;
    const char* parts[3] = { "[", "@kintsugi ", "]$ " };
    for (int p = 0; p < 3; ++p) {
        const char* s = parts[p];
        if (p == 1) {
            for (int i = 0; user[i] && n + 1 < (int)cap; ++i) {
                out[n++] = user[i];
            }
        }
        while (*s && n + 1 < (int)cap) {
            out[n++] = *s++;
        }
        if (p == 1) {
            for (int i = 0; w->path[i] && n + 1 < (int)cap; ++i) {
                out[n++] = w->path[i];
            }
        }
    }
    out[n] = 0;
}

static void shell_path(gui_window* w, const char* arg, char* out, u32 cap) {
    if (arg[0] == '/') {
        text_copy(out, cap, arg);
    } else {
        text_copy(out, cap, w->path);
        int n = text_length(out);
        if (n == 0 || out[n - 1] != '/') {
            out[n++] = '/';
        }
        int i = 0;
        while (arg[i] && n + 1 < (int)cap) {
            out[n++] = arg[i++];
        }
        out[n] = 0;
    }
    if (!out[0]) {
        out[0] = '/';
        out[1] = 0;
    }
    char* parts[32];
    int count = 0;
    char* p = out;
    while (*p) {
        while (*p == '/') {
            ++p;
        }
        if (!*p) {
            break;
        }
        char* start = p;
        while (*p && *p != '/') {
            ++p;
        }
        if (*p) {
            *p++ = 0;
        }
        int len = text_length(start);
        if (len == 1 && start[0] == '.') {
            continue;
        }
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (count) {
                --count;
            }
            continue;
        }
        if (count < 32) {
            parts[count++] = start;
        }
    }
    int n = 1;
    out[0] = '/';
    for (int i = 0; i < count; ++i) {
        if (n > 1 && n + 1 < (int)cap) {
            out[n++] = '/';
        }
        for (int j = 0; parts[i][j] && n + 1 < (int)cap; ++j) {
            out[n++] = parts[i][j];
        }
    }
    out[n] = 0;
}

static void build_bin_path(const char* name, char* path, u32 cap) {
    if (name[0] == '/') {
        text_copy(path, cap, name);
        return;
    }
    text_copy(path, cap, "/bin/");
    int n = text_length(path), i = 0;
    while (name[i] && n + i + 4 < (int)cap) {
        path[n + i] = name[i];
        ++i;
    }
    path[n + i] = '.';
    path[n + i + 1] = 'E';
    path[n + i + 2] = 'L';
    path[n + i + 3] = 'F';
    path[n + i + 4] = 0;
}

static int cli_offset;
static int gui_child_pid = -1;

/* Read whatever the child has written so far. Polling instead of waiting for
 * exit makes fast commands appear immediately and long ones show progress. */
static int cli_poll(gui_window* w) {
    if (!w || !w->used || cli_fd < 0) {
        return 0;
    }
    char output[2048];
    int size = syscall(2, (u32) "/tmp/kgui-term.out", (u32)cli_offset, (u32)output, sizeof(output) - 1);
    if (size > 0) {
        cli_offset += size;
        term_write(w, output, size);
        return 1;
    }
    return 0;
}

static void cli_finish(int timeout) {
    int pid = cli_child_pid;
    cli_child_pid = -1;
    if (pid > 0) {
        if (timeout) {
            kill(pid);
        }
        wait(pid);
    }
    gui_window* w = cli_window;
    cli_window = 0;
    cli_poll(w);
    term_flush(w);
    if (cli_fd >= 0) {
        close(cli_fd);
        cli_fd = -1;
    }
    term_ink = TERM_DEFAULT_INK;
}

static void term_run_program(gui_window* w, const char* name, char** argv, int argc) {
    char path[160];
    build_bin_path(name, path, sizeof(path));
    /* A second command supersedes the first instead of being refused, so the
     * Start menu and the in-window shell never appear to "do nothing". */
    if (cli_child_pid > 0) {
        cli_finish(1);
        term_print(w, "[previous command stopped]");
    }
    int capture = open("/tmp/kgui-term.out", KOS_O_WRITE | KOS_O_CREATE | KOS_O_TRUNC);
    if (capture < 0) {
        term_print(w, "terminal: cannot capture output");
        return;
    }
    cli_offset = 0;
    dup2(capture, 1);
    int pid = spawn(path, argc, argv);
    int tty = open("/dev/tty", KOS_O_WRITE);
    if (tty >= 0) {
        dup2(tty, 1);
        close(tty);
    }
    if (pid < 0) {
        close(capture);
        term_print(w, kos_error(pid));
        return;
    }
    cli_child_pid = pid;
    cli_fd = capture;
    cli_window = w;
    cli_frames = 0;
    term_print(w, "[running]");
}

static int name_equal(const char* a, const char* b) {
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') {
            x += 32;
        }
        if (y >= 'A' && y <= 'Z') {
            y += 32;
        }
        if (x != y) {
            return 0;
        }
    }
    return !*a && !*b;
}

static int is_tui_name(const char* name) {
    static const char* const apps[] = { "kiwi",    "kpaint",  "kuned",   "vik",      "kc",      "kterm",
                                        "kset",    "ktte",    "k2048",   "kmines",   "klife",   "kcsv",
                                        "krrawl",  "kmusa",   "klisten", "soundctl", "ktmux",   "kservices",
                                        "ksersys", "kinfo",   "kman",    "pager",    "journal", "hiero",
                                        "kbsm",    "kmemman", "kprocm",  "kdiskman", "ktui",    "dir",
                                        "ksdaemon" };
    for (unsigned i = 0; i < sizeof(apps) / sizeof(apps[0]); ++i) {
        const char *a = apps[i], *b = name;
        while (*a && *b) {
            char x = *a, y = *b;
            if (x >= 'A' && x <= 'Z') {
                x += 32;
            }
            if (y >= 'A' && y <= 'Z') {
                y += 32;
            }
            if (x != y) {
                break;
            }
            ++a;
            ++b;
        }
        if (!*a && !*b) {
            return 1;
        }
    }
    return 0;
}

static int is_gui_name(const char* name) {
    return name_equal(name, "gdemo");
}

/* One entry point for every program launch, shared by the shell prompt, the
 * Run dialog and the Start menu. TUI programs need presentation capture, GUI
 * clients draw into their own kernel window, and everything else is a plain
 * command whose stdout is shown in the terminal. */
static void term_launch(gui_window* w, const char* name, char** argv, int argc) {
    if (!name || !*name) {
        return;
    }
    if (name_equal(name, "kgui")) {
        term_print(w, "kgui is already running");
        return;
    }
    if (is_gui_name(name)) {
        char path[160];
        build_bin_path(name, path, sizeof(path));
        if (gui_child_pid > 0) {
            kill(gui_child_pid);
            wait(gui_child_pid);
            gui_child_pid = -1;
        }
        gui_child_pid = spawn(path, argc, argv);
        if (gui_child_pid < 0) {
            term_print(w, kos_error(gui_child_pid));
            gui_child_pid = -1;
        } else {
            term_print(w, "[GUI client started]");
        }
        return;
    }
    if (is_tui_name(name)) {
        if (cli_child_pid > 0) {
            cli_finish(1);
        }
        if (tui_child_pid > 0) {
            kill(tui_child_pid);
            tui_finish();
        }
        tui_launch(w, name);
        return;
    }
    term_run_program(w, name, argv, argc);
}

/* Applications built on the TUI library present through syscall 7. The
 * kernel redirects that presentation to a capture buffer while they run,
 * and the compositor paints the cells into this window. */
static void tui_launch(gui_window* w, const char* name) {
    char path[160];
    build_bin_path(name, path, sizeof(path));
    char* args[8];
    args[0] = (char*)name;
    args[1] = 0;
    int pid = spawn(path, 1, args);
    if (pid < 0) {
        term_print(w, kos_error(pid));
        return;
    }
    /* Clear the presentation buffer before attaching: stale cells from an
     * earlier session must never flash in a newly opened TUI window. */
    for (int row = 0; row < 25; ++row) {
        for (int col = 0; col < 80; ++col) {
            tui_cells[row][col].character = ' ';
            tui_cells[row][col].color = 0x07;
        }
    }
    syscall(KOS_SYS_TUI_ATTACH, (u32)pid, 0, 0, 0);
    tui_child_pid = pid;
    tui_window = w;
    tui_saved_x = w->x;
    tui_saved_y = w->y;
    tui_saved_w = w->width;
    tui_saved_h = w->height;
    w->x = 2;
    w->y = MENUBAR_HEIGHT + 2;
    w->width = screen_width - 4;
    w->height = screen_height - MENUBAR_HEIGHT - TASKBAR_HEIGHT - 6;
    window_surface_rebuilt(w);
    raise_window((int)(w - windows));
    draw_window(w);
}

static void tui_finish(void) {
    if (tui_child_pid > 0) {
        wait(tui_child_pid);
    }
    syscall(KOS_SYS_TUI_ATTACH, 0, 0, 0, 0);
    tui_child_pid = -1;
    if (tui_window) {
        gui_window* w = tui_window;
        w->x = tui_saved_x;
        w->y = tui_saved_y;
        w->width = tui_saved_w;
        w->height = tui_saved_h;
        window_surface_rebuilt(w);
        term_print(w, "[TUI application finished]");
        tui_window = 0;
    }
}

static void term_shell_banner(gui_window* w) {
    w->out_count = w->out_top = 0;
    w->pending[0] = 0;
    w->pending_length = 0;
    w->pending_ink = TERM_DEFAULT_INK;
    w->input[0] = 0;
    w->input_length = 0;
    text_copy(w->path, sizeof(w->path), "/home/user");
    term_print(w, "KintsugiOS Shell 0.7 (KGTERM)");
    term_print(w, "Type help for the command list.");
}

static void term_shell_command(gui_window* w) {
    char line[128];
    text_copy(line, sizeof(line), w->input);
    char shown[200];
    term_prompt_string(w, shown, sizeof(shown));
    int n = text_length(shown);
    for (int i = 0; line[i] && n + 1 < (int)sizeof(shown); ++i) {
        shown[n++] = line[i];
    }
    shown[n] = 0;
    term_print(w, shown);
    if (w->history_count < 16) {
        text_copy(w->history[w->history_count], sizeof(w->history[0]), line);
        ++w->history_count;
    } else {
        for (int i = 1; i < 16; ++i) {
            text_copy(w->history[i - 1], sizeof(w->history[0]), w->history[i]);
        }
        text_copy(w->history[15], sizeof(w->history[0]), line);
    }
    w->history_pos = w->history_count;

    char* argv[8];
    int argc;
    split_command(line, argv, &argc);
    if (!argc) {
        return;
    }
    if (text_equal(argv[0], "help")) {
        term_print(w, "help clear pwd cd ls cat echo mkdir rmdir rm touch date uptime free ps history exit");
        term_print(w, "Other names run from /bin: TUI apps take over the window, others print here.");
        term_print(w, "Ctrl+C stops a running command; the wheel scrolls the output.");
        return;
    }
    if (text_equal(argv[0], "clear")) {
        w->out_count = w->out_top = 0;
        return;
    }
    if (text_equal(argv[0], "exit")) {
        close_window(w);
        return;
    }
    if (text_equal(argv[0], "pwd")) {
        term_print(w, w->path);
        return;
    }
    if (text_equal(argv[0], "cd")) {
        char target[160];
        shell_path(w, argc > 1 ? argv[1] : "/home/user", target, sizeof(target));
        kos_stat stat;
        if (syscall(11, (u32)target, 4, (u32)&stat, 0) || !stat.directory) {
            term_print(w, "cd: no such directory");
        } else {
            text_copy(w->path, sizeof(w->path), target);
        }
        return;
    }
    if (text_equal(argv[0], "ls")) {
        char target[160];
        shell_path(w, argc > 1 ? argv[1] : ".", target, sizeof(target));
        u32 cursor = 0;
        kos_long_dirent entry;
        char line_out[120];
        while (syscall(KOS_SYS_READDIR_LONG, (u32)target, (u32)&cursor, (u32)&entry, 0) > 0) {
            line_out[0] = 0;
            int used = 0;
            for (int i = 0; entry.name[i] && used + 2 < (int)sizeof(line_out); ++i) {
                line_out[used++] = entry.name[i];
            }
            if (entry.stat.directory && used + 1 < (int)sizeof(line_out)) {
                line_out[used++] = '/';
            }
            line_out[used] = 0;
            term_print(w, line_out);
        }
        return;
    }
    if (text_equal(argv[0], "cat")) {
        if (argc < 2) {
            term_print(w, "usage: cat <file>");
            return;
        }
        char target[160];
        shell_path(w, argv[1], target, sizeof(target));
        int fd = open(target, KOS_O_READ);
        if (fd < 0) {
            term_print(w, kos_error(fd));
            return;
        }
        char buffer[512];
        int size = read(fd, buffer, sizeof(buffer) - 1);
        close(fd);
        if (size > 0) {
            buffer[size] = 0;
            int start = 0;
            for (int i = 0; i <= size; ++i) {
                if (i == size || buffer[i] == '\n') {
                    buffer[i] = 0;
                    term_print(w, buffer + start);
                    start = i + 1;
                }
            }
        } else {
            term_print(w, "cat: empty file");
        }
        return;
    }
    if (text_equal(argv[0], "echo")) {
        char text[128];
        text[0] = 0;
        int used = 0;
        for (int i = 1; i < argc; ++i) {
            if (used) {
                text[used++] = ' ';
            }
            for (const char* p = argv[i]; *p && used + 1 < 128; ++p) {
                text[used++] = *p;
            }
        }
        text[used] = 0;
        term_print(w, text);
        return;
    }
    if (text_equal(argv[0], "mkdir") || text_equal(argv[0], "rmdir") || text_equal(argv[0], "rm")
        || text_equal(argv[0], "touch")) {
        if (argc < 2) {
            term_print(w, "usage: mkdir|rmdir|rm|touch <path>");
            return;
        }
        char target[160];
        shell_path(w, argv[1], target, sizeof(target));
        int result = text_equal(argv[0], "mkdir") ? syscall(11, (u32)target, 0, 0, 0)
            : text_equal(argv[0], "rmdir")        ? syscall(11, (u32)target, 2, 0, 0)
            : text_equal(argv[0], "rm")           ? syscall(11, (u32)target, 1, 0, 0)
                                                  : open(target, KOS_O_WRITE | KOS_O_CREATE);
        if (result < 0) {
            term_print(w, kos_error(result));
        } else if (text_equal(argv[0], "touch")) {
            close(result);
        }
        return;
    }
    if (text_equal(argv[0], "date") || text_equal(argv[0], "uptime")) {
        kos_desktop_info info = { 0 };
        syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
        char text[80];
        text_copy(text, sizeof(text), "uptime ");
        int m = text_length(text);
        unsigned v = info.uptime;
        char digits[11];
        int d = 10;
        digits[d] = 0;
        do {
            digits[--d] = '0' + v % 10;
            v /= 10;
        } while (v);
        while (d < 11 && digits[d] && m + 1 < (int)sizeof(text)) {
            text[m++] = digits[d++];
        }
        if (m + 2 < (int)sizeof(text)) {
            text[m++] = 's';
            text[m] = 0;
        }
        term_print(w, text);
        return;
    }
    if (text_equal(argv[0], "free")) {
        kos_desktop_info info = { 0 };
        syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
        char text[96];
        text_copy(text, sizeof(text), "free: ");
        int m = text_length(text);
        unsigned v = info.free_kib;
        char digits[11];
        int d = 10;
        digits[d] = 0;
        do {
            digits[--d] = '0' + v % 10;
            v /= 10;
        } while (v);
        while (d < 11 && digits[d] && m + 1 < (int)sizeof(text)) {
            text[m++] = digits[d++];
        }
        const char* tail = " KiB";
        while (*tail && m + 1 < (int)sizeof(text)) {
            text[m++] = *tail++;
        }
        text[m] = 0;
        term_print(w, text);
        return;
    }
    if (text_equal(argv[0], "ps")) {
        kos_process_info list[16];
        int count = syscall(16, (u32)list, 16, 0, 0);
        char text[100];
        for (int i = 0; i < count && i < 16; ++i) {
            text[0] = 0;
            int m = 0;
            unsigned v = list[i].pid;
            char digits[11];
            int d = 10;
            digits[d] = 0;
            do {
                digits[--d] = '0' + v % 10;
                v /= 10;
            } while (v);
            while (d < 11 && digits[d] && m + 1 < (int)sizeof(text)) {
                text[m++] = digits[d++];
            }
            text[m++] = ' ';
            for (int j = 0; list[i].name[j] && m + 1 < (int)sizeof(text); ++j) {
                text[m++] = list[i].name[j];
            }
            text[m] = 0;
            term_print(w, text);
        }
        return;
    }
    if (text_equal(argv[0], "history")) {
        for (int i = 0; i < w->history_count && i < 16; ++i) {
            term_print(w, w->history[i]);
        }
        return;
    }
    term_launch(w, argv[0], argv, argc);
}

static void term_execute(gui_window* w) {
    term_shell_command(w);
}

static void search_directory(const char* path, const char* query, int depth) {
    if (depth > 3 || search_count >= MAX_RESULTS) {
        return;
    }
    u32 cursor = 0;
    kos_long_dirent entry;
    while (syscall(KOS_SYS_READDIR_LONG, (u32)path, (u32)&cursor, (u32)&entry, 0) > 0) {
        if (entry.name[0] == '.') {
            continue;
        }
        if (text_has(entry.name, query)) {
            char line[160];
            int n = 0;
            while (path[n] && n + 1 < (int)sizeof(line)) {
                line[n] = path[n];
                ++n;
            }
            if (line[n - 1] != '/') {
                line[n++] = '/';
            }
            int i = 0;
            while (entry.name[i] && n + i + 1 < (int)sizeof(line)) {
                line[n + i] = entry.name[i];
                ++i;
            }
            line[n + i] = 0;
            text_copy(search_results[search_count++], sizeof(search_results[0]), line);
            if (search_count >= MAX_RESULTS) {
                return;
            }
        }
    }
    cursor = 0;
    while (syscall(KOS_SYS_READDIR_LONG, (u32)path, (u32)&cursor, (u32)&entry, 0) > 0) {
        if (!entry.stat.directory || entry.name[0] == '.') {
            continue;
        }
        char child[160];
        int n = 0;
        while (path[n] && n + 1 < (int)sizeof(child)) {
            child[n] = path[n];
            ++n;
        }
        if (child[n - 1] != '/') {
            child[n++] = '/';
        }
        int i = 0;
        while (entry.name[i] && n + i + 1 < (int)sizeof(child)) {
            child[n + i] = entry.name[i];
            ++i;
        }
        child[n + i] = 0;
        search_directory(child, query, depth + 1);
        if (search_count >= MAX_RESULTS) {
            return;
        }
    }
}

/* Notepad keeps a scrollable, wrapped view instead of dumping a fixed buffer
 * behind the status bar. Lines are laid out lazily so scrolling stays cheap. */
static int note_columns(gui_window* w) {
    int columns = (w->width - 32) / 12;
    return columns < 8 ? 8 : columns;
}

static void note_draw(gui_window* w, gfx_surface* s, int content_y, int status_y) {
    int columns = note_columns(w);
    int rows = (status_y - content_y - 10) / 15;
    if (rows < 1) {
        rows = 1;
    }
    int maximum = 1, column = 0;
    for (int i = 0; i < w->text_length; ++i) {
        if (w->text[i] == '\n') {
            ++maximum;
            column = 0;
        } else if (++column >= columns) {
            ++maximum;
            column = 0;
        }
    }
    if (w->text_top >= maximum) {
        w->text_top = maximum > rows ? maximum - rows : 0;
    }
    char line[TERM_COLS + 1];
    int length = 0, row = 0;
    for (int i = 0; i < w->text_length; ++i) {
        char c = w->text[i];
        if (c == '\n' || length >= columns) {
            if (row >= w->text_top && row - w->text_top < rows) {
                line[length] = 0;
                gfx_text(s, 14, content_y + 6 + (row - w->text_top) * 15, line, GFX_BLACK, 0);
            }
            ++row;
            length = 0;
            if (c == '\n') {
                continue;
            }
        }
        if (length < (int)sizeof(line) - 1) {
            line[length++] = c;
        }
    }
    if (length && row >= w->text_top && row - w->text_top < rows) {
        line[length] = 0;
        gfx_text(s, 14, content_y + 6 + (row - w->text_top) * 15, line, GFX_BLACK, 0);
    }
    /* Caret at the end of the document, visible only while editing. */
    if (row >= w->text_top && row - w->text_top < rows) {
        gfx_fill(s, 14 + length * 12, content_y + 6 + (row - w->text_top) * 15, 2, 14, GFX_DARK);
    }
}

/* Perceptual luminance (Rec. 601 weights, integer only) decides whether a
 * pictogram is drawn in white or near-black on its tile. */
static u32 icon_ink(u32 color) {
    unsigned r = (color >> 16) & 0xff, g = (color >> 8) & 0xff, b = color & 0xff;
    unsigned luminance = (r * 77 + g * 150 + b * 29) / 256;
    return luminance > 128 ? GFX_RGB(20, 24, 32) : GFX_WHITE;
}

/* Vector pictograms avoid unreadable letter soup at 14..26 px. Every shape is
 * built from the same gfx primitives as the rest of the GUI. */
static void draw_pictogram(gfx_surface* s, int x, int y, int size, int kind, u32 color) {
    if (size < 12) {
        return;
    }
    u32 ink = icon_ink(color);
    int m = size / 5;
    int w = size - 2 * m;
    gfx_fill(s, x, y, size, size, color);
    gfx_bevel(s, x, y, size, size, 1);
    switch (kind) {
        case 0: /* launcher: K monogram */
            gfx_vline(s, x + size / 2 - m, y + m, size - 2 * m, ink);
            gfx_line(s, x + size / 2 + 1, y + m, x + size - m - 1, y + size / 2 - 1, 1, ink);
            gfx_line(s, x + size / 2 + 1, y + size / 2 + 1, x + size - m - 1, y + size - m - 1, 1, ink);
            break;
        case 1: /* file manager: folder */
            gfx_fill(s, x + m, y + m + 1, w / 2, m, ink);
            gfx_fill(s, x + m, y + m + 2, w, w - 1, ink);
            gfx_fill(s, x + m + 2, y + m + 4, w - 4, w - 5, color);
            break;
        case 2: /* editor: page with a folded corner */
            gfx_fill(s, x + m, y + m, w, w, ink);
            gfx_fill(s, x + m + 2, y + m + 1, w - 4, w - 3, color);
            gfx_hline(s, x + m + 3, y + m + 4, w - 7, ink);
            gfx_hline(s, x + m + 3, y + m + 8, w - 7, ink);
            gfx_hline(s, x + m + 3, y + m + 12, w / 2, ink);
            break;
        case 3: /* terminal: prompt */
            gfx_fill(s, x + m, y + m, w, w, ink);
            gfx_fill(s, x + m + 2, y + m + 2, w - 4, w - 4, color);
            gfx_line(s, x + m + 3, y + m + 4, x + m + w / 3, y + m + 6, 1, ink);
            gfx_line(s, x + m + w / 3, y + m + 6, x + m + 3, y + m + 8, 1, ink);
            gfx_hline(s, x + m + w / 3 + 2, y + m + 9, w / 3, ink);
            break;
        case 4: /* monitor */
            gfx_fill(s, x + m, y + m, w, w * 2 / 3, ink);
            gfx_fill(s, x + m + 2, y + m + 2, w - 4, w * 2 / 3 - 4, color);
            gfx_fill(s, x + size / 2 - 2, y + m + w * 2 / 3, 4, m + 1, ink);
            gfx_hline(s, x + m + 1, y + size - m - 1, w - 2, ink);
            break;
        case 5: /* system: cross/gear */
            gfx_fill(s, x + size / 2 - m, y + m, 2 * m, size - 2 * m, ink);
            gfx_fill(s, x + m, y + size / 2 - m, size - 2 * m, 2 * m, ink);
            gfx_fill(s, x + size / 2 - m / 2, y + size / 2 - m / 2, m, m, color);
            break;
        default: /* trash */
            gfx_hline(s, x + m, y + m, w, ink);
            gfx_fill(s, x + m + 2, y + m + 3, w - 4, w - 2, ink);
            gfx_fill(s, x + m + 4, y + m + 5, w - 8, w - 6, color);
            gfx_vline(s, x + size / 2, y + m + 6, w - 8, ink);
            break;
    }
}

typedef struct {
    int x, y, kind; /* kind is a draw_pictogram() id */
    const char* label;
} desktop_icon;

/* Iconography follows the active theme so bright themes (Fruit, NeXTSTEP) and
 * muted ones (Graphite, Solar) keep the same icon language. */
static u32 desktop_icon_color(int kind) {
    switch (kind) {
        case 1:
            return gui_theme.accent;
        case 2:
            return gui_theme.success;
        case 3:
            return gui_theme.title;
        default:
            return gui_theme.shadow;
    }
}

static const desktop_icon desktop_icons[] = {
    { 22, 40,  1, "My Computer" },
    { 22, 130, 3, "Terminal"    },
    { 22, 220, 2, "Editor"      },
    { 22, 310, 6, "Trash"       }
};
#define DESKTOP_ICON_COUNT ((int)(sizeof(desktop_icons) / sizeof(desktop_icons[0])))
static int selected_icon = -1, last_icon_click = -1;
static u32 last_icon_tick;

/* Painter and hit-test share one rectangle: no more label clicks that miss. */
static void desktop_icon_rect(const desktop_icon* icon, int* x, int* y, int* width, int* height) {
    int label_width = gfx_text_width(icon->label) + 8;
    int total = label_width > 22 ? label_width : 22;
    *x = icon->x - 2;
    *y = icon->y - 2;
    *width = total;
    *height = 42;
}

static void draw_desktop_icon(gfx_surface* back, const desktop_icon* icon, int selected) {
    u32 color = desktop_icon_color(icon->kind);
    draw_pictogram(back, icon->x, icon->y, 18, icon->kind, color);
    int label_width = gfx_text_width(icon->label) + 8;
    int label_x = icon->x + 9 - label_width / 2;
    if (label_x < 2) {
        label_x = 2;
    }
    /* A recessed plate keeps labels legible on light and patterned wallpapers. */
    gfx_fill(back, label_x, icon->y + 21, label_width, 16, GFX_FACE);
    gfx_bevel(back, label_x, icon->y + 21, label_width, 16, selected ? 0 : 1);
    gfx_text(back, label_x + 4, icon->y + 23, icon->label, selected ? GUI_INK : gui_theme.text, GFX_FACE);
    if (selected) {
        int x, y, width, height;
        desktop_icon_rect(icon, &x, &y, &width, &height);
        gfx_bevel(back, x, y, width, height, 1);
    }
}

static void draw_meter(gfx_surface* s, int x, int y, int width, int height, u32 used, u32 total, u32 color) {
    gfx_fill(s, x, y, width, height, GFX_RGB(224, 224, 224));
    gfx_bevel(s, x, y, width, height, 0);
    u32 filled = total ? used * (width - 4) / total : 0;
    if (filled > (u32)(width - 4)) {
        filled = width - 4;
    }
    gfx_fill(s, x + 2, y + 2, (int)filled, height - 4, color);
}

static void draw_monitor(gui_window* w, gfx_surface* s, int content_y, int status_y) {
    kos_desktop_info info = { 0 };
    syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
    ktui_stats stats = { 0 };
    syscall(9, (u32)&stats, 0, 0, 0);
    kos_disk_info disk = { 0 };
    syscall(19, (u32)&disk, 0, 0, 0);
    kos_process_info list[MAX_PROCESS_ROWS];
    int count = syscall(16, (u32)list, MAX_PROCESS_ROWS, 0, 0);
    if (count < 0) {
        count = 0;
    }
    u32 ticks = 0;
    for (int i = 0; i < count; ++i) {
        ticks += list[i].ticks;
    }
    if (++monitor_frame % 10 == 0) {
        u32 delta = ticks > monitor_previous_ticks ? ticks - monitor_previous_ticks : 0;
        monitor_cpu = delta * 100 / 10;
        if (monitor_cpu > 100) {
            monitor_cpu = 100;
        }
        monitor_previous_ticks = ticks;
    }
    gfx_fill(s, 8, content_y, w->width - 16, status_y - content_y - 6, GFX_WHITE);
    gfx_bevel(s, 8, content_y, w->width - 16, status_y - content_y - 6, 0);
    int y = content_y + 8;
    gfx_text(s, 14, y, "KMonitor - live resource usage", GFX_DARK, 0);
    char text[96];
    text_copy(text, sizeof(text), "Uptime ");
    int n = text_length(text);
    n += number_text(text + n, sizeof(text) - n, info.uptime);
    const char* tail = "s   Tasks ";
    while (*tail && n + 1 < (int)sizeof(text)) {
        text[n++] = *tail++;
    }
    n += number_text(text + n, sizeof(text) - n, info.processes);
    tail = "   CPU ";
    while (*tail && n + 1 < (int)sizeof(text)) {
        text[n++] = *tail++;
    }
    n += number_text(text + n, sizeof(text) - n, monitor_cpu);
    if (n + 2 < (int)sizeof(text)) {
        text[n++] = '%';
        text[n] = 0;
    }
    gfx_text(s, 14, y + 22, text, GFX_BLACK, 0);

    u32 ram_total = stats.ram_kib ? stats.ram_kib : info.ram_kib;
    u32 ram_used = ram_total > stats.free_pages * 4 ? ram_total - stats.free_pages * 4 : 0;
    u32 disk_used = disk.clusters > disk.free_clusters ? disk.clusters - disk.free_clusters : 0;
    int bar_width = w->width - 60;
    gfx_text(s, 14, y + 46, "RAM", GFX_BLACK, 0);
    draw_meter(s, 62, y + 44, bar_width, 16, ram_used, ram_total ? ram_total : 1, GFX_GREEN);
    gfx_text(s, 14, y + 70, "Disk", GFX_BLACK, 0);
    draw_meter(s, 62, y + 68, bar_width, 16, disk_used, disk.clusters ? disk.clusters : 1, GFX_BLUE);
    text_copy(text, sizeof(text), "Free ");
    n = text_length(text);
    n += number_text(text + n, sizeof(text) - n, stats.free_pages * 4);
    tail = " KiB of ";
    while (*tail && n + 1 < (int)sizeof(text)) {
        text[n++] = *tail++;
    }
    n += number_text(text + n, sizeof(text) - n, ram_total);
    tail = " KiB   FS: ";
    while (*tail && n + 1 < (int)sizeof(text)) {
        text[n++] = *tail++;
    }
    for (int c = 0; disk.fs[c] && n + 1 < (int)sizeof(text); ++c) {
        text[n++] = disk.fs[c];
    }
    text[n] = 0;
    gfx_text(s, 14, y + 92, text, GFX_SHADOW, 0);

    gfx_text(s, 14, y + 116, "PID     NAME                     TICKS", GFX_DARK, 0);
    /* Most active processes first: a small insertion sort over <=16 entries. */
    for (int i = 1; i < count; ++i) {
        kos_process_info current_entry = list[i];
        int j = i - 1;
        while (j >= 0 && list[j].ticks < current_entry.ticks) {
            list[j + 1] = list[j];
            --j;
        }
        list[j + 1] = current_entry;
    }
    int rows = (status_y - (y + 134)) / 16;
    for (int i = 0; i < count && i < rows && i < 8; ++i) {
        text_copy(text, sizeof(text), "");
        n = number_text(text, sizeof(text), list[i].pid);
        while (n < 8 && n + 1 < (int)sizeof(text)) {
            text[n++] = ' ';
        }
        for (int c = 0; list[i].name[c] && n + 1 < (int)sizeof(text); ++c) {
            text[n++] = list[i].name[c];
        }
        while (n < 32 && n + 1 < (int)sizeof(text)) {
            text[n++] = ' ';
        }
        number_text(text + n, sizeof(text) - n, list[i].ticks);
        gfx_text(s, 14, y + 134 + i * 16, text, i == 0 ? GFX_DARK : GFX_BLACK, 0);
    }
    gfx_text(
        s,
        10,
        status_y + 2,
        "KMonitor - F5-like automatic refresh, wheel to scroll windows",
        GUI_INK,
        GFX_FACE);
}

typedef struct {
    const char* name;
    const char* path;
    const char* description;
    int system;
} sys_service_t;

static const sys_service_t sys_services[] = {
    { "login",     "/bin/INIT.ELF",     "Session supervisor",  1 },
    { "shell",     "/bin/SHELL.ELF",    "Command interpreter", 0 },
    { "sysmon",    "/bin/KINFO.ELF",    "System monitor",      0 },
    { "mixer",     "/bin/SOUNDCTL.ELF", "Audio mixer",         0 },
    { "music",     "/bin/KMUSA.ELF",    "Music generator",     0 },
    { "terminal",  "/bin/KTERM.ELF",    "Terminal session",    0 },
    { "clockd",    "/bin/KSDAEMON.ELF", "Clock daemon",        1 },
    { "workspace", "/bin/KIWI.ELF",     "Window workspace",    0 }
};
#define SYS_SERVICE_COUNT ((int)(sizeof(sys_services) / sizeof(sys_services[0])))

static void sys_refresh(void) {
    if (++sys_refresh_frame % POLL_INTERVAL) {
        return;
    }
    sys_process_count = syscall(16, (u32)sys_processes, MAX_PROCESS_ROWS, 0, 0);
    if (sys_process_count < 0) {
        sys_process_count = 0;
    }
    for (int i = 0; i < SYS_SERVICE_COUNT; ++i) {
        if (sys_service_pids[i] <= 0) {
            continue;
        }
        int alive = 0;
        for (int j = 0; j < sys_process_count; ++j) {
            if (sys_processes[j].pid == sys_service_pids[i] && sys_processes[j].state != 4) {
                alive = 1;
                break;
            }
        }
        if (!alive) {
            wait(sys_service_pids[i]);
            sys_service_pids[i] = 0;
        }
    }
}

static void draw_system(gui_window* w, gfx_surface* s, int content_y, int status_y) {
    sys_refresh();
    gfx_fill(s, 8, content_y, w->width - 16, status_y - content_y - 6, GFX_WHITE);
    gfx_bevel(s, 8, content_y, w->width - 16, status_y - content_y - 6, 0);
    int bar_y = content_y + 4;
    gfx_button(s, 14, bar_y, 116, 22, "Processes", w->sys_tab == 0);
    gfx_button(s, 136, bar_y, 104, 22, "Services", w->sys_tab == 1);
    gfx_button(s, w->width - 96, bar_y, 78, 22, "Refresh", 0);
    int list_y = bar_y + 30;
    int rows = (status_y - list_y - 36) / 18;
    if (rows < 1) {
        rows = 1;
    }
    if (w->sys_selected < 0) {
        w->sys_selected = 0;
    }
    char text[96];
    if (w->sys_tab == 0) {
        gfx_fill(s, 12, list_y, w->width - 28, 18, GFX_FACE);
        gfx_text(s, 16, list_y + 2, "PID   NAME              STATE  UID  TICKS", GFX_DARK, 0);
        if (w->sys_selected >= sys_process_count) {
            w->sys_selected = sys_process_count ? sys_process_count - 1 : 0;
        }
        for (int i = 0; i < sys_process_count && i < rows; ++i) {
            kos_process_info* p = &sys_processes[i];
            int y = list_y + 18 + i * 18;
            if (i == w->sys_selected) {
                gfx_fill(s, 12, y, w->width - 28, 18, GFX_DARK);
            }
            u32 color = i == w->sys_selected ? GFX_WHITE : GFX_BLACK;
            text_copy(text, sizeof(text), "");
            int n = number_text(text, sizeof(text), p->pid);
            while (n < 6 && n + 1 < (int)sizeof(text)) {
                text[n++] = ' ';
            }
            for (int c = 0; p->name[c] && n + 1 < (int)sizeof(text); ++c) {
                text[n++] = p->name[c];
            }
            while (n < 23 && n + 1 < (int)sizeof(text)) {
                text[n++] = ' ';
            }
            const char* states[] = { "-", "ready", "run", "wait", "zombie" };
            const char* state = p->state <= 4 ? states[p->state] : "?";
            while (*state && n + 1 < (int)sizeof(text)) {
                text[n++] = *state++;
            }
            while (n < 32 && n + 1 < (int)sizeof(text)) {
                text[n++] = ' ';
            }
            n += number_text(text + n, sizeof(text) - n, p->uid);
            while (n < 39 && n + 1 < (int)sizeof(text)) {
                text[n++] = ' ';
            }
            n += number_text(text + n, sizeof(text) - n, p->ticks);
            gfx_text(s, 16, y + 2, text, color, 0);
        }
        gfx_button(s, 14, status_y - 32, 96, 24, "End task", 0);
        gfx_text(s, 122, status_y - 26, "Select a process, then End task", GFX_SHADOW, 0);
    } else {
        gfx_fill(s, 12, list_y, w->width - 28, 18, GFX_FACE);
        gfx_text(s, 16, list_y + 2, "NAME      SCOPE    STATUS   DESCRIPTION", GFX_DARK, 0);
        if (w->sys_selected >= SYS_SERVICE_COUNT) {
            w->sys_selected = SYS_SERVICE_COUNT - 1;
        }
        for (int i = 0; i < SYS_SERVICE_COUNT && i < rows; ++i) {
            const sys_service_t* svc = &sys_services[i];
            int y = list_y + 18 + i * 18;
            if (i == w->sys_selected) {
                gfx_fill(s, 12, y, w->width - 28, 18, GFX_DARK);
            }
            u32 color = i == w->sys_selected ? GFX_WHITE : GFX_BLACK;
            text_copy(text, sizeof(text), svc->name);
            int n = text_length(text);
            while (n < 11 && n + 1 < (int)sizeof(text)) {
                text[n++] = ' ';
            }
            const char* scope = svc->system ? "system" : "user";
            while (*scope && n + 1 < (int)sizeof(text)) {
                text[n++] = *scope++;
            }
            while (n < 21 && n + 1 < (int)sizeof(text)) {
                text[n++] = ' ';
            }
            const char* status = sys_service_pids[i] > 0 ? "running" : "stopped";
            while (*status && n + 1 < (int)sizeof(text)) {
                text[n++] = *status++;
            }
            while (n < 31 && n + 1 < (int)sizeof(text)) {
                text[n++] = ' ';
            }
            for (int c = 0; svc->description[c] && n + 1 < (int)sizeof(text); ++c) {
                text[n++] = svc->description[c];
            }
            text[n] = 0;
            gfx_text(s, 16, y + 2, text, color, 0);
        }
        gfx_button(s, 14, status_y - 32, 72, 24, "Start", 0);
        gfx_button(s, 92, status_y - 32, 72, 24, "Stop", 0);
        gfx_text(s, 176, status_y - 26, "System services need root rights", GFX_SHADOW, 0);
    }
    gfx_text(s, 10, status_y + 2, "KSystem - processes and services", GUI_INK, GFX_FACE);
}

/* NeXTSTEP style: the menu bar belongs to the screen, not to each window.
 * Client area therefore starts immediately below the title bar. */
static int window_content_y(void) {
    return TITLE_HEIGHT + 6;
}

/* KGED canvas == full client area. It is retained across resize/zoom, and the
 * old bitmap is copied into the new surface so drawings are never lost. */
static void kged_sync_canvas(gui_window* w) {
    if (!w || w->kind != 1) {
        return;
    }
    int canvas_y = window_content_y() + 28;
    int cw = w->width - 20;
    int ch = w->height - STATUS_HEIGHT - canvas_y - 10;
    if (cw < 40) {
        cw = 40;
    }
    if (ch < 40) {
        ch = 40;
    }
    if (w->canvas && w->canvas->width == cw && w->canvas->height == ch) {
        return;
    }
    gfx_surface* next = gfx_surface_create(cw, ch);
    if (!next) {
        return;
    }
    gfx_clear(next, GFX_WHITE);
    if (w->canvas) {
        int copy_w = w->canvas->width < cw ? w->canvas->width : cw;
        int copy_h = w->canvas->height < ch ? w->canvas->height : ch;
        gfx_blit(next, 0, 0, w->canvas, 0, 0, copy_w, copy_h);
        gfx_surface_destroy(w->canvas);
    }
    w->canvas = next;
}

static void window_surface_rebuilt(gui_window* w) {
    if (!w) {
        return;
    }
    if (w->surface) {
        gfx_surface_destroy(w->surface);
    }
    w->surface = gfx_surface_create(w->width, w->height);
    kged_sync_canvas(w);
}

/* Compact MM/DD display derived from the UNIX timestamp in kos_stat. */
static void fm_date(u32 stamp, char* out, u32 cap) {
    if (!stamp || cap < 6) {
        text_copy(out, cap, "--/--");
        return;
    }
    u32 days = stamp / 86400;
    u32 month = (days / 30) % 12 + 1;
    u32 day = days % 30 + 1;
    out[0] = '0' + month / 10;
    out[1] = '0' + month % 10;
    out[2] = '/';
    out[3] = '0' + day / 10;
    out[4] = '0' + day % 10;
    out[5] = 0;
}

static void fm_init_shelf(gui_window* w) {
    static const char* const paths[5] = { "/home/user", "/bin", "/etc", "/var/log", "/" };
    static const char* const labels[5] = { "Home", "Bin", "Etc", "Logs", "Root" };
    w->shelf_count = 5;
    w->shelf_selected = 0;
    for (int i = 0; i < 5; ++i) {
        text_copy(w->shelf[i], sizeof(w->shelf[i]), paths[i]);
        text_copy(w->shelf_label[i], sizeof(w->shelf_label[i]), labels[i]);
    }
}

/* ---- Settings document: theme, wallpaper, Dock, mixer, persistence ---- */

/* Pure pattern function: no allocations, no state, identical result for the
 * full-size wallpaper and for the small preview tiles in Settings. */
static int wallpaper_pattern(int kind, int x, int y, int scale) {
    if (scale < 4) {
        scale = 4;
    }
    switch (kind) {
        case 1:
            return (x % scale) < 2 || (y % scale) < 2;
        case 2:
            return ((x + y) % scale) < scale / 2;
        case 3:
            return ((x / scale) + (y / scale)) & 1;
        case 4:
            return (x % scale) < 3 && (y % scale) < 3;
        case 5:
            return (x / (scale / 2)) & 1;
        case 6:
            return (((x / scale) + (y / scale)) & 1) ^ (((x / (scale / 2)) + (y / (scale / 2))) & 1);
        case 7: {
            int dx = x - screen_width / 2;
            int dy = y - screen_height / 2;
            int ring = (dx * dx + dy * dy) / (scale * scale * 2);
            return (ring & 1) && (((x + y) % (scale * 2)) < scale);
        }
        default:
            return 0;
    }
}

static void wallpaper_generate(void) {
    if (!wallpaper_surface || wallpaper_surface->width != screen_width
        || wallpaper_surface->height != screen_height) {
        if (wallpaper_surface) {
            gfx_surface_destroy(wallpaper_surface);
        }
        wallpaper_surface = gfx_surface_create(screen_width, screen_height);
    }
    if (!wallpaper_surface) {
        return;
    }
    int scale = wallpaper_scale < 4 ? 4 : wallpaper_scale;
    for (int y = 0; y < screen_height; ++y) {
        u32* row = wallpaper_surface->pixels + y * screen_width;
        for (int x = 0; x < screen_width; ++x) {
            row[x] = wallpaper_pattern(wallpaper_kind, x, y, scale) ? (u32)wallpaper_color_b
                                                                    : (u32)wallpaper_color_a;
        }
    }
}

static void settings_apply(void) {
    gfx_theme_set(gfx_theme_id());
    desktop_color = gui_theme.desktop;
    syscall(KOS_SYS_SOUND_MIXER, 0, 1, (u32)volume_master, 0);
    syscall(KOS_SYS_SOUND_MIXER, 1, 1, (u32)volume_music, 0);
    syscall(KOS_SYS_SOUND_MIXER, 2, 1, (u32)volume_effects, 0);
    wallpaper_generate();
}

static void settings_save(void) {
    char data[512];
    int n = 0;
    const char* head = "# KintsugiOS GUI settings\n";
    while (*head && n + 1 < (int)sizeof(data)) {
        data[n++] = *head++;
    }
    const char* keys[8] = {
        "theme", "wallpaper", "color_a", "color_b", "scale", "dock", "autohide", "volume"
    };
    int values[8] = { gfx_theme_id(),  wallpaper_kind, wallpaper_color_a, wallpaper_color_b,
                      wallpaper_scale, dock_size,      dock_autohide,     volume_master };
    for (int i = 0; i < 8; ++i) {
        const char* key = keys[i];
        while (*key && n + 1 < (int)sizeof(data)) {
            data[n++] = *key++;
        }
        data[n++] = '=';
        n += number_text(data + n, sizeof(data) - n, (unsigned)values[i]);
        data[n++] = '\n';
        data[n] = 0;
    }
    syscall(10, (u32) "/home/user/KGUI.CFG", (u32)data, (u32)n, 0);
}

static int config_value(const char* data, const char* key) {
    while (*data) {
        const char* line = data;
        while (*line && *line != '\n') {
            ++line;
        }
        const char* p = data;
        const char* k = key;
        while (*p && *k && *p == *k) {
            ++p;
            ++k;
        }
        if (!*k && *p == '=') {
            int value = 0;
            ++p;
            while (*p >= '0' && *p <= '9') {
                value = value * 10 + (*p++ - '0');
            }
            return value;
        }
        data = *line ? line + 1 : line;
    }
    return -1;
}

static void settings_load(void) {
    char data[512];
    int size = syscall(2, (u32) "/home/user/KGUI.CFG", 0, (u32)data, sizeof(data) - 1);
    if (size <= 0) {
        return;
    }
    data[size] = 0;
    int value;
    if ((value = config_value(data, "theme")) >= 0 && value < GUI_THEME_COUNT) {
        gfx_theme_set(value);
    }
    if ((value = config_value(data, "wallpaper")) >= 0 && value < WALLPAPER_COUNT) {
        wallpaper_kind = value;
    }
    if ((value = config_value(data, "color_a")) >= 0) {
        wallpaper_color_a = (u32)value;
    }
    if ((value = config_value(data, "color_b")) >= 0) {
        wallpaper_color_b = (u32)value;
    }
    if ((value = config_value(data, "scale")) >= 4) {
        wallpaper_scale = value;
    }
    if ((value = config_value(data, "dock")) >= 0) {
        dock_size = value > 28 ? 28 : value;
    }
    if ((value = config_value(data, "autohide")) >= 0) {
        dock_autohide = value;
    }
    if ((value = config_value(data, "volume")) >= 0) {
        volume_master = value > 100 ? 100 : value;
    }
    if (!wallpaper_color_a) {
        wallpaper_color_a = gui_theme.desktop;
    }
    if (!wallpaper_color_b) {
        wallpaper_color_b = gui_theme.accent;
    }
    settings_apply();
}

static const u32 settings_palette[10] = { GFX_RGB(0, 128, 128),  GFX_RGB(32, 38, 48),    GFX_RGB(10, 36, 106),
                                          GFX_RGB(122, 59, 46),  GFX_RGB(120, 160, 190), GFX_RGB(64, 64, 96),
                                          GFX_RGB(96, 64, 64),   GFX_RGB(48, 96, 48),    GFX_RGB(221, 72, 20),
                                          GFX_RGB(245, 240, 225) };

static int palette_index(u32 color) {
    for (int i = 0; i < 10; ++i) {
        if (settings_palette[i] == color) {
            return i;
        }
    }
    return -1;
}

/* Display mode and interface scale persist in /etc/VIDEO.CFG, which init
 * applies at boot; changing it live would resize every retained surface. */
static void apply_video_config(int mode, int font_scale) {
    unsigned char settings[5] = { 1, 5, 1, 0, 1 };
    int loaded = (int)syscall(2, (u32) "/etc/VIDEO.CFG", 0, (u32)settings, sizeof(settings));
    if (loaded < 5) {
        settings[0] = 1;
        settings[1] = 5;
        settings[2] = 1;
        settings[3] = 0;
        settings[4] = 1;
    }
    if (mode >= 0) {
        settings[0] = (unsigned char)mode;
    }
    if (font_scale >= 0) {
        settings[2] = (unsigned char)font_scale;
        settings[4] = (unsigned char)font_scale;
    }
    syscall(10, (u32) "/etc/VIDEO.CFG", (u32)settings, sizeof(settings), 0);
    toast("Display saved - applies at next start");
}

/* Shared geometry for the wallpaper chooser: the preview tile, palette
 * swatch and scale controls have exactly one definition for paint + hit-test. */
#define SET_PREVIEW_W 104
#define SET_PREVIEW_H 40
#define SET_SWATCH_W 24
#define SET_SWATCH_H 20

static void settings_preview_rect(int index, int panel_y, int* x, int* y, int* w, int* h) {
    *x = 24 + (index % 4) * 120;
    *y = panel_y + 112 + (index / 4) * 52;
    *w = SET_PREVIEW_W;
    *h = SET_PREVIEW_H;
}

static void settings_swatch_rect(int row, int index, int panel_y, int* x, int* y, int* w, int* h) {
    *x = 40 + index * 30;
    *y = panel_y + 222 + row * 30;
    *w = SET_SWATCH_W;
    *h = SET_SWATCH_H;
}

static void settings_click(gui_window* w, int lx, int ly) {
    if (!w || w->kind != 5) {
        return;
    }
    int content_y = window_content_y();
    int panel_y = content_y + 32;
    if (ly >= content_y + 4 && ly < content_y + 26) {
        for (int i = 0; i < 4; ++i) {
            if (lx >= 14 + i * 116 && lx < 14 + i * 116 + 114) {
                w->settings_tab = i;
                return;
            }
        }
    }
    if (w->settings_tab == 0) {
        for (int i = 0; i < GUI_THEME_COUNT; ++i) {
            int tx = 22 + (i % 4) * 120, ty = panel_y + 18 + (i / 4) * 28;
            if (lx >= tx && lx < tx + 114 && ly >= ty && ly < ty + 24) {
                gfx_theme_set(i);
                wallpaper_color_a = gui_theme.desktop;
                wallpaper_color_b = gui_theme.accent;
                desktop_color = gui_theme.desktop;
                settings_apply();
                settings_save();
                toast("Theme applied");
                return;
            }
        }
        for (int i = 0; i < WALLPAPER_COUNT; ++i) {
            int px, py, pw, ph;
            settings_preview_rect(i, panel_y, &px, &py, &pw, &ph);
            if (lx >= px && lx < px + pw && ly >= py && ly < py + ph) {
                wallpaper_kind = i;
                wallpaper_generate();
                settings_save();
                return;
            }
        }
        for (int row = 0; row < 2; ++row) {
            for (int i = 0; i < 10; ++i) {
                int px, py, pw, ph;
                settings_swatch_rect(row, i, panel_y, &px, &py, &pw, &ph);
                if (lx >= px && lx < px + pw && ly >= py && ly < py + ph) {
                    if (row == 0) {
                        wallpaper_color_a = settings_palette[i];
                    } else {
                        wallpaper_color_b = settings_palette[i];
                    }
                    wallpaper_generate();
                    settings_save();
                    return;
                }
            }
        }
        if (((lx >= 400 && lx < 424) || (lx >= 430 && lx < 454)) && ly >= panel_y + 258
            && ly < panel_y + 278) {
            wallpaper_scale += lx < 424 ? -4 : 4;
            if (wallpaper_scale < 4) {
                wallpaper_scale = 4;
            }
            if (wallpaper_scale > 128) {
                wallpaper_scale = 128;
            }
            wallpaper_generate();
            settings_save();
        }
    } else if (w->settings_tab == 1) {
        if (((lx >= 380 && lx < 404) || (lx >= 408 && lx < 432)) && ly >= panel_y + 20 && ly < panel_y + 44) {
            dock_size += lx < 400 ? -2 : 2;
            if (dock_size < 10) {
                dock_size = 10;
            }
            if (dock_size > 28) {
                dock_size = 28;
            }
            settings_save();
        } else if (lx >= 24 && lx < 174 && ly >= panel_y + 58 && ly < panel_y + 82) {
            dock_autohide = !dock_autohide;
            dock_size = dock_autohide ? 10 : 18;
            settings_save();
        } else if (lx >= 182 && lx < 312 && ly >= panel_y + 58 && ly < panel_y + 82) {
            dock_size = 18;
            dock_autohide = 0;
            settings_save();
            toast("Dock layout reset");
        }
    } else if (w->settings_tab == 2) {
        int* volumes[3] = { &volume_master, &volume_music, &volume_effects };
        for (int i = 0; i < 3; ++i) {
            int row_y = panel_y + 22 + i * 38;
            if (((lx >= 398 && lx < 422) || (lx >= 426 && lx < 450)) && ly >= row_y - 1 && ly < row_y + 23) {
                *volumes[i] += lx < 410 ? -5 : 5;
                if (*volumes[i] < 0) {
                    *volumes[i] = 0;
                }
                if (*volumes[i] > 100) {
                    *volumes[i] = 100;
                }
                settings_apply();
                settings_save();
                return;
            }
        }
        if (lx >= 24 && lx < 134 && ly >= panel_y + 140 && ly < panel_y + 164) {
            syscall(KOS_SYS_SOUND_TONE, 660, 120, 0, 0);
            toast("Test tone");
        }
    } else {
        if (ly >= panel_y + 50 && ly < panel_y + 72) {
            if (lx >= 272 && lx < 372) {
                apply_video_config(0, -1);
            } else if (lx >= 378 && lx < 478) {
                apply_video_config(1, -1);
            }
        } else if (ly >= panel_y + 78 && ly < panel_y + 100) {
            if (lx >= 272 && lx < 372) {
                apply_video_config(3, -1);
            } else if (lx >= 378 && lx < 478) {
                apply_video_config(5, -1);
            }
        } else if (ly >= panel_y + 104 && ly < panel_y + 126) {
            if (lx >= 356 && lx < 414) {
                apply_video_config(-1, 0);
            } else if (lx >= 420 && lx < 478) {
                apply_video_config(-1, 2);
            }
        } else if (lx >= 24 && lx < 124 && ly >= panel_y + 30 && ly < panel_y + 54) {
            gui_language = 0;
            syscall(KOS_SYS_LAYOUT, 0, 0, 0, 0);
        } else if (lx >= 132 && lx < 232 && ly >= panel_y + 30 && ly < panel_y + 54) {
            gui_language = 1;
            syscall(KOS_SYS_LAYOUT, 1, 0, 0, 0);
        } else if (lx >= 24 && lx < 164 && ly >= panel_y + 158 && ly < panel_y + 184) {
            settings_save();
            toast("Settings saved");
        }
    }
}

static void draw_window(gui_window* w) {
    gfx_surface* s = w->surface;
    int active = (w - windows) == focused;
    int content_y = window_content_y();
    int status_y = w->height - STATUS_HEIGHT;

    gfx_clear(s, GFX_FACE);
    gfx_bevel(s, 0, 0, w->width, w->height, 1);
    gfx_titlebar_next(s, 2, 2, w->width - 4, w->title, active);
    /* Status strips change (file name, counters, tabs): clear them before
     * writing so shorter strings cannot leave stale glyphs behind. */
    gfx_fill(s, 4, status_y + 1, w->width - 8, STATUS_HEIGHT - 2, GFX_FACE);

    if (w->kind == 0) {
        /* KGFM: toolbar, address bar, four-column list and a NeXTSTEP Shelf. */
        int shelf_x = w->width - 124;
        gfx_hline(s, 4, content_y + 25, w->width - 8, GFX_SHADOW);
        gfx_button(s, 10, content_y + 3, 52, 20, "Back", 0);
        gfx_button(s, 66, content_y + 3, 44, 20, "Up", 0);
        gfx_button(s, 114, content_y + 3, 52, 20, "New", 0);
        gfx_button(s, 170, content_y + 3, 52, 20, "View", 0);
        gfx_button(s, 226, content_y + 3, 52, 20, "Home", 0);
        int address_y = content_y + 29;
        gfx_text(s, 10, address_y + 5, "Address", GFX_BLACK, 0);
        int addr_x = 82;
        gfx_fill(s, addr_x, address_y, shelf_x - addr_x - 10, 20, GFX_WHITE);
        gfx_bevel(s, addr_x, address_y, shelf_x - addr_x - 10, 20, 0);
        gfx_text(s, addr_x + 5, address_y + 3, w->path, GFX_BLACK, 0);
        int list_y = address_y + 28;
        int list_h = status_y - list_y - 6;
        int list_right = shelf_x - 8;
        int size_x = list_right - 196, type_x = list_right - 118, date_x = list_right - 52;
        if (size_x < 120) {
            size_x = 120;
            type_x = 190;
            date_x = 250;
        }
        gfx_fill(s, 8, list_y, list_right - 4, 18, GFX_FACE);
        gfx_bevel(s, 8, list_y, list_right - 4, 18, 1);
        gfx_text(s, 38, list_y + 3, "Name", GFX_BLACK, 0);
        gfx_text(s, size_x, list_y + 3, "Size", GFX_BLACK, 0);
        gfx_text(s, type_x, list_y + 3, "Type", GFX_BLACK, 0);
        gfx_text(s, date_x, list_y + 3, "Date", GFX_BLACK, 0);
        gfx_hline(s, 8, list_y + 18, list_right - 4, GFX_SHADOW);
        int rows_y = list_y + 20;
        int visible = (list_h - 18) / 20;
        if (visible < 1) {
            visible = 1;
        }
        if (w->top > w->entry_count - visible && w->top > 0) {
            w->top = w->entry_count - visible;
        }
        for (int i = 0; i < visible && i + w->top < w->entry_count; ++i) {
            int index = i + w->top;
            kos_long_dirent* entry = &w->entries[index];
            int y = rows_y + i * 20;
            u32 ink = index == w->selected ? GFX_WHITE : GFX_BLACK;
            if (index == w->selected) {
                gfx_fill(s, 10, y, list_right - 12, 19, gui_theme.selection);
            }
            char size[16], date[8];
            number_text(size, sizeof(size), entry->stat.size);
            fm_date(entry->stat.mtime, date, sizeof(date));
            gfx_text_clipped(
                s,
                38,
                y + 2,
                size_x - 42,
                entry->name,
                ink,
                index == w->selected ? gui_theme.selection : GFX_FACE);
            gfx_text(s, size_x, y + 2, size, ink, 0);
            gfx_text(s, type_x, y + 2, entry->stat.directory ? "DIR" : "FILE", ink, 0);
            gfx_text(s, date_x, y + 2, date, ink, 0);
            draw_pictogram(
                s,
                14,
                y,
                18,
                entry->stat.directory ? 1 : 2,
                entry->stat.directory ? GFX_RGB(180, 150, 40) : GFX_RGB(60, 120, 180));
        }
        gfx_scrollbar(s, list_right - 8, list_y, list_h, w->top, w->entry_count ? w->entry_count : 1);
        gfx_groupbox(s, shelf_x, list_y, 116, list_h, "Shelf");
        for (int i = 0; i < w->shelf_count; ++i) {
            int y = list_y + 22 + i * 30;
            if (y + 26 > list_y + list_h - 4) {
                break;
            }
            int selected = i == w->shelf_selected;
            gfx_fill(s, shelf_x + 6, y, 104, 25, selected ? gui_theme.selection : GFX_FACE);
            gfx_bevel(s, shelf_x + 6, y, 104, 25, selected ? 0 : 1);
            draw_pictogram(s, shelf_x + 10, y + 3, 14, 1, gui_theme.accent);
            gfx_text(s, shelf_x + 34, y + 6, w->shelf_label[i], selected ? GFX_WHITE : GFX_BLACK, 0);
        }
        char status[120];
        text_copy(status, sizeof(status), w->path);
        int n = text_length(status);
        const char* tail = "   [Name / Size / Type / Date]";
        while (*tail && n + 1 < (int)sizeof(status)) {
            status[n++] = *tail++;
        }
        status[n] = 0;
        gfx_text(s, 10, status_y + 2, status, GUI_INK, GFX_FACE);
    } else if (w->kind == 1) {
        /* KGED: toolbar + canvas */
        int y = content_y;
        gfx_button(s, 10, y, 66, 20, "Pen", w->tool == 0);
        gfx_button(s, 82, y, 66, 20, "Erase", w->tool == 1);
        gfx_button(s, 154, y, 66, 20, "Clear", 0);
        const u32 palette[] = { GFX_BLACK, GFX_RED, GFX_RGB(0, 128, 0), gui_theme.title, GFX_RGB(255, 255, 0),
                                GFX_WHITE };
        for (int i = 0; i < 6; ++i) {
            gfx_fill(s, 228 + i * 22, y, 20, 20, palette[i]);
            gfx_bevel(s, 228 + i * 22, y, 20, 20, w->color == palette[i]);
        }
        int canvas_y = y + 28;
        gfx_fill(s, 8, canvas_y, w->width - 16, status_y - canvas_y - 6, GFX_WHITE);
        gfx_bevel(s, 8, canvas_y, w->width - 16, status_y - canvas_y - 6, 0);
        if (w->canvas) {
            gfx_blit(s, 10, canvas_y + 2, w->canvas, 0, 0, w->canvas->width, w->canvas->height);
        }
        gfx_text(s, 10, status_y + 2, "KGED - Kintsugi Graphic Editor", GUI_INK, GFX_FACE);
    } else if (w->kind == 2) {
        if (tui_window == w && tui_child_pid > 0) {
            /* TUI application presentation, drawn inside this window. */
            gfx_fill(s, 8, content_y, w->width - 16, status_y - content_y - 6, GFX_BLACK);
            int cols = 80, rows = 25;
            if (w->width - 20 < cols * 12) {
                cols = (w->width - 20) / 12;
            }
            if (status_y - content_y - 8 < rows * 15) {
                rows = (status_y - content_y - 8) / 15;
            }
            int offx = 10 + (w->width - 20 - cols * 12) / 2;
            int offy = content_y + 3 + (status_y - content_y - 8 - rows * 15) / 2;
            if (offx < 10) {
                offx = 10;
            }
            if (offy < content_y + 3) {
                offy = content_y + 3;
            }
            for (int row = 0; row < rows; ++row) {
                for (int col = 0; col < cols; ++col) {
                    gfx_cell(
                        s,
                        offx + col * 12,
                        offy + row * 15,
                        tui_cells[row][col].character,
                        tui_cells[row][col].color);
                }
            }
            gfx_text(s, 10, status_y + 2, "TUI session - q/Esc to exit", GUI_INK, GFX_FACE);
        } else {
            /* One contiguous terminal: output and the live prompt line. */
            gfx_fill(s, 8, content_y, w->width - 16, status_y - content_y - 6, GFX_BLACK);
            gfx_bevel(s, 8, content_y, w->width - 16, status_y - content_y - 6, 0);
            int line_height = 15;
            int room = (status_y - content_y - 8) / line_height;
            if (room < 2) {
                room = 2;
            }
            int visible = room - (active ? 1 : 0);
            if (w->out_top > w->out_count - visible && w->out_top > 0) {
                w->out_top = w->out_count - visible;
            }
            int drawn = 0;
            for (int i = 0; i < visible && i + w->out_top < w->out_count; ++i, ++drawn) {
                int row_y = content_y + 4 + i * line_height;
                /* Clear the whole cell row: gfx_text with a transparent
                 * background would otherwise leave tails of longer lines. */
                gfx_fill(s, 8, row_y - 1, w->width - 16, 16, GFX_BLACK);
                gfx_text(s, 14, row_y, w->output[i + w->out_top], w->output_color[i + w->out_top], 0);
            }
            if (active) {
                char line[200], typed[129];
                term_prompt_string(w, line, sizeof(line));
                text_copy(typed, sizeof(typed), w->input);
                int n = text_length(line);
                for (int i = 0; typed[i] && n + 1 < (int)sizeof(line); ++i) {
                    line[n++] = typed[i];
                }
                line[n] = 0;
                int py = content_y + 4 + drawn * line_height;
                gfx_fill(s, 8, py - 1, w->width - 16, 16, GFX_BLACK);
                gfx_text(s, 14, py, line, GFX_RGB(200, 255, 200), 0);
                gfx_fill(s, 14 + gfx_text_width(line), py, 2, 14, GFX_RGB(200, 255, 200));
            }
            gfx_text(s, 10, status_y + 2, "KGTERM - shell", GUI_INK, GFX_FACE);
        }
    } else if (w->kind == 3) {
        gfx_text(s, 20, content_y + 12, "KintsugiOS", GFX_DARK, 0);
        gfx_text(s, 20, content_y + 36, "NeXTSTEP-style workspace", GFX_BLACK, 0);
        gfx_text(s, 20, content_y + 60, "Surfaces + compositor", GFX_BLACK, 0);
        gfx_text(s, 20, content_y + 84, "KGFM KGED KGTERM KMonitor KSystem", GFX_BLACK, 0);
        gfx_text(s, 20, content_y + 108, "Drag title, resize corner", GFX_BLACK, 0);
        gfx_text(s, 10, status_y + 2, "About KGUI", GUI_INK, GFX_FACE);
    } else if (w->kind == 5) {
        static const char* const short_themes[GUI_THEME_COUNT] = { "Classic", "NeXT",  "Millen", "Human",
                                                                   "Fruit",   "Graph", "Ocean",  "Solar" };
        static const char* const setting_tabs[4] = { "Appearance", "Dock", "Sound", "System" };
        for (int i = 0; i < 4; ++i) {
            gfx_tab(s, 14 + i * 116, content_y + 4, 114, 22, setting_tabs[i], w->settings_tab == i);
        }
        int panel_y = content_y + 32;
        if (w->settings_tab == 0) {
            gfx_groupbox(s, 14, panel_y, w->width - 28, 78, "Theme");
            for (int i = 0; i < GUI_THEME_COUNT; ++i) {
                gfx_button(
                    s,
                    22 + (i % 4) * 120,
                    panel_y + 18 + (i / 4) * 28,
                    114,
                    24,
                    short_themes[i],
                    gfx_theme_id() == i);
            }
            gfx_groupbox(s, 14, panel_y + 86, w->width - 28, 300, "Wallpaper chooser");
            for (int i = 0; i < WALLPAPER_COUNT; ++i) {
                int px, py, pw, ph;
                settings_preview_rect(i, panel_y, &px, &py, &pw, &ph);
                for (int ty = 0; ty < ph - 14; ++ty) {
                    u32* row = s->pixels + (py + ty) * s->width + px;
                    for (int tx = 0; tx < pw; ++tx) {
                        row[tx] = wallpaper_pattern(i, tx, ty, wallpaper_scale) ? (u32)wallpaper_color_b
                                                                                : (u32)wallpaper_color_a;
                    }
                }
                gfx_fill(s, px, py + ph - 14, pw, 14, GFX_FACE);
                gfx_text_clipped(s, px + 2, py + ph - 13, pw - 4, wallpaper_names[i], GUI_INK, GFX_FACE);
                gfx_bevel(s, px, py, pw, ph, wallpaper_kind == i ? 0 : 1);
                if (wallpaper_kind == i) {
                    gfx_fill(s, px + 2, py + ph - 16, pw - 4, 2, gui_theme.selection);
                }
            }
            gfx_text(s, 24, panel_y + 204, "Palette", GFX_DARK, GFX_FACE);
            for (int row = 0; row < 2; ++row) {
                gfx_text(s, 24, panel_y + 228 + row * 30, row ? "B" : "A", GUI_INK, GFX_FACE);
                for (int i = 0; i < 10; ++i) {
                    int px, py, pw, ph;
                    settings_swatch_rect(row, i, panel_y, &px, &py, &pw, &ph);
                    gfx_fill(s, px, py, pw, ph, settings_palette[i]);
                    gfx_bevel(
                        s,
                        px,
                        py,
                        pw,
                        ph,
                        palette_index((u32)(row ? wallpaper_color_b : wallpaper_color_a)) == i ? 0 : 1);
                }
            }
            gfx_text(s, 400, panel_y + 236, "Scale", GUI_INK, GFX_FACE);
            gfx_button(s, 400, panel_y + 258, 24, 20, "-", 0);
            gfx_button(s, 430, panel_y + 258, 24, 20, "+", 0);
            char scale_text[16];
            int sl = number_text(scale_text, sizeof(scale_text), (unsigned)wallpaper_scale);
            scale_text[sl] = 0;
            gfx_text(s, 462, panel_y + 262, scale_text, GUI_INK, GFX_FACE);
        } else if (w->settings_tab == 1) {
            gfx_groupbox(s, 14, panel_y, w->width - 28, 120, "Dock");
            gfx_text(s, 24, panel_y + 26, "Icon size", GFX_BLACK, 0);
            gfx_slider(s, 110, panel_y + 22, 260, 24, dock_size, 28, 1);
            gfx_button(s, 380, panel_y + 20, 24, 24, "-", 0);
            gfx_button(s, 408, panel_y + 20, 24, 24, "+", 0);
            gfx_button(
                s, 24, panel_y + 58, 150, 24, dock_autohide ? "Compact: on" : "Compact: off", dock_autohide);
            gfx_button(s, 182, panel_y + 58, 130, 24, "Reset layout", 0);
            gfx_text(s, 24, panel_y + 94, "Pinned: KGFM, KGED, KGTERM, KMonitor, KSystem", GFX_SHADOW, 0);
            gfx_groupbox(s, 14, panel_y + 130, w->width - 28, 60, "Behavior");
            gfx_text(
                s,
                24,
                panel_y + 156,
                "Pinned tiles launch apps; running apps stay in the Dock.",
                GFX_BLACK,
                0);
        } else if (w->settings_tab == 2) {
            gfx_groupbox(s, 14, panel_y, w->width - 28, 164, "Sound mixer");
            static const char* const channels[3] = { "Master", "Music (FM)", "Effects" };
            int* volumes[3] = { &volume_master, &volume_music, &volume_effects };
            for (int i = 0; i < 3; ++i) {
                int row_y = panel_y + 22 + i * 38;
                gfx_text(s, 24, row_y + 4, channels[i], GFX_BLACK, 0);
                gfx_slider(s, 130, row_y, 260, 22, *volumes[i], 100, 1);
                gfx_button(s, 398, row_y - 1, 24, 24, "-", 0);
                gfx_button(s, 426, row_y - 1, 24, 24, "+", 0);
                char value[8];
                int vn = number_text(value, sizeof(value), (unsigned)*volumes[i]);
                while (vn < 3 && vn + 1 < (int)sizeof(value)) {
                    value[vn++] = ' ';
                }
                value[vn] = 0;
                gfx_text(s, 456, row_y + 4, value, GFX_BLACK, 0);
            }
            gfx_button(s, 24, panel_y + 140, 110, 24, "Test sound", 0);
            u32 audio[3] = { 0 };
            syscall(KOS_SYS_SOUND_INFO, (u32)audio, 0, 0, 0);
            gfx_text(
                s,
                184,
                panel_y + 146,
                audio[2] ? "OPL2 FM synthesizer" : "PC speaker fallback",
                GFX_SHADOW,
                0);
        } else {
            gfx_groupbox(s, 260, panel_y, 226, 154, "Display and scale");
            kos_desktop_info dinfo = { 0 };
            syscall(KOS_SYS_DESKTOP, (u32)&dinfo, 0, 0, 0);
            char mode_text[32];
            int mn = 0;
            const char* mhead = "Current: ";
            while (*mhead && mn + 1 < (int)sizeof(mode_text)) {
                mode_text[mn++] = *mhead++;
            }
            mn += number_text(mode_text + mn, sizeof(mode_text) - mn, dinfo.width);
            if (mn + 1 < (int)sizeof(mode_text)) {
                mode_text[mn++] = 'x';
            }
            mn += number_text(mode_text + mn, sizeof(mode_text) - mn, dinfo.height);
            mode_text[mn] = 0;
            gfx_text(s, 272, panel_y + 24, mode_text, GFX_BLACK, 0);
            gfx_button(s, 272, panel_y + 50, 100, 22, "1024x768", 0);
            gfx_button(s, 378, panel_y + 50, 100, 22, "1280x800", 0);
            gfx_button(s, 272, panel_y + 78, 100, 22, "1600x900", 0);
            gfx_button(s, 378, panel_y + 78, 100, 22, "1920x1080", 0);
            gfx_text(s, 272, panel_y + 108, "Font size", GFX_DARK, 0);
            gfx_button(s, 356, panel_y + 104, 58, 22, "Small", 0);
            gfx_button(s, 420, panel_y + 104, 58, 22, "Large", 0);
            gfx_text(s, 24, panel_y + 8, "Language", GFX_DARK, 0);
            gfx_button(s, 24, panel_y + 30, 100, 24, "English", !gui_language);
            gfx_button(s, 132, panel_y + 30, 100, 24, "Russian", gui_language);
            gfx_text(s, 24, panel_y + 68, "Accounts", GFX_DARK, 0);
            int shown = settings_user_count < 4 ? settings_user_count : 4;
            for (int i = 0; i < shown; ++i) {
                gfx_text(s, 24, panel_y + 92 + i * 16, settings_users[i], GFX_BLACK, 0);
            }
            gfx_button(s, 24, panel_y + 158, 140, 26, "Save settings", 0);
            gfx_text(s, 176, panel_y + 164, "KintsugiOS GUI framework v0.8", GFX_SHADOW, 0);
        }
        gfx_text(s, 10, status_y + 2, "Settings - appearance, Dock, mixer, system", GUI_INK, GFX_FACE);
    } else if (w->kind == 6) {
        gfx_text(s, 14, content_y + 6, "Root password:", GFX_BLACK, 0);
        gfx_fill(s, 14, content_y + 24, w->width - 28, 22, GFX_WHITE);
        gfx_bevel(s, 14, content_y + 24, w->width - 28, 22, 0);
        int n = 0;
        while (w->admin_password[n]) {
            gfx_text(s, 20 + n * 12, content_y + 28, "*", GFX_BLACK, 0);
            ++n;
        }
        gfx_text(s, 14, content_y + 56, "Command:", GFX_BLACK, 0);
        gfx_fill(s, 14, content_y + 74, w->width - 28, 22, GFX_WHITE);
        gfx_bevel(s, 14, content_y + 74, w->width - 28, 22, 0);
        gfx_text(s, 20, content_y + 78, w->admin_command, GFX_BLACK, 0);
        gfx_button(s, 14, content_y + 104, 80, 24, "Run", 0);
        gfx_text(s, 104, content_y + 110, "Tab switches fields, Enter runs", GFX_BLACK, 0);
        gfx_fill(s, 14, content_y + 136, w->width - 28, status_y - content_y - 142, GFX_WHITE);
        gfx_bevel(s, 14, content_y + 136, w->width - 28, status_y - content_y - 142, 0);
        gfx_text(s, 20, content_y + 142, w->admin_output, GFX_BLACK, 0);
        gfx_text(s, 10, status_y + 2, "kadms - Kintsugi Administration Service", GUI_INK, GFX_FACE);
    } else if (w->kind == 7) {
        gfx_fill(s, 8, content_y, w->width - 16, status_y - content_y - 6, GFX_WHITE);
        gfx_bevel(s, 8, content_y, w->width - 16, status_y - content_y - 6, 0);
        gfx_text(s, 20, content_y + 24, "Recycle Bin is empty.", GFX_BLACK, 0);
        gfx_text(s, 20, content_y + 48, "Deleted files would appear here.", GFX_SHADOW, 0);
        gfx_text(s, 10, status_y + 2, "Recycle Bin", GUI_INK, GFX_FACE);
    } else if (w->kind == 8) {
        draw_monitor(w, s, content_y, status_y);
    } else if (w->kind == 9) {
        draw_system(w, s, content_y, status_y);
    } else {
        gfx_fill(s, 8, content_y, w->width - 16, status_y - content_y - 6, GFX_WHITE);
        gfx_bevel(s, 8, content_y, w->width - 16, status_y - content_y - 6, 0);
        note_draw(w, s, content_y, status_y);
        char info[80];
        int n = 0;
        const char* head = w->file_path[0] ? w->file_path : "[unsaved]";
        while (*head && n + 1 < (int)sizeof(info)) {
            info[n++] = *head++;
        }
        const char* suffix = " - ";
        while (*suffix && n + 1 < (int)sizeof(info)) {
            info[n++] = *suffix++;
        }
        n += number_text(info + n, sizeof(info) - n, (unsigned)w->text_length);
        if (n + 6 < (int)sizeof(info)) {
            const char* tail = " chars";
            while (*tail && n + 1 < (int)sizeof(info)) {
                info[n++] = *tail++;
            }
        }
        info[n] = 0;
        int info_width = gfx_text_width(info);
        gfx_text(s, w->width - info_width - 14, status_y + 2, info, GUI_INK, GFX_FACE);
        gfx_text(s, 10, status_y + 2, "Notepad", GUI_INK, GFX_FACE);
    }
    gfx_hline(s, 4, status_y, w->width - 8, GFX_WHITE);
    /* resize grip */
    gfx_fill(s, w->width - 14, w->height - 14, 12, 12, GFX_FACE);
    gfx_hline(s, w->width - 12, w->height - 4, 8, GFX_SHADOW);
    gfx_hline(s, w->width - 10, w->height - 8, 6, GFX_SHADOW);
    gfx_hline(s, w->width - 8, w->height - 12, 4, GFX_SHADOW);
}

/* NeXTSTEP-style application menu bar. Menus describe the *active* document,
 * not the window that happens to own the pixels (AppKit responder principle). */
static const char* const global_menu_titles[4] = { "File", "Edit", "View", "Help" };

static int global_menu_count(int column) {
    if (column == 0) {
        return 5;
    }
    if (column == 3) {
        return 2;
    }
    return 4;
}

static int global_menu_enabled(gui_window* w, int column) {
    if (!w || !w->used) {
        return column == 3;
    }
    if (w->kind == 0) {
        return column != 1;
    }
    if (w->kind == 2) {
        return column != 2;
    }
    return 1;
}

static void draw_menu_bar(gfx_surface* back) {
    gfx_fill(back, 0, 0, screen_width, MENUBAR_HEIGHT, GFX_FACE);
    gfx_hline(back, 0, MENUBAR_HEIGHT - 1, screen_width, GFX_SHADOW);
    /* NeXTSTEP menu bar: quiet text labels, inverted only while open. */
    gfx_fill(back, 4, 3, 28, 18, GFX_FACE);
    gfx_bevel(back, 4, 3, 28, 18, menu == 1 || menu == 2 || menu == 6 ? 0 : 1);
    gfx_text_clipped(back, 11, 5, 16, "K", GFX_DARK, GFX_FACE);
    gui_window* active = focused >= 0 && zvalid(focused) ? &windows[focused] : 0;
    for (int i = 0; i < 4; ++i) {
        int x = 42 + i * 64;
        int enabled = global_menu_enabled(active, i);
        int open = global_menu_column == i;
        if (open) {
            gfx_fill(back, x - 4, 2, 60, 20, gui_theme.selection);
            gfx_text_clipped(back, x, 5, 54, global_menu_titles[i], GFX_WHITE, gui_theme.selection);
        } else {
            gfx_text_clipped(
                back, x, 5, 58, global_menu_titles[i], enabled ? GFX_BLACK : GFX_SHADOW, GFX_FACE);
        }
    }
    if (active && active->title[0]) {
        gfx_text_clipped(back, 300, 5, 420, active->title, GFX_DARK, GFX_FACE);
    }
    kos_desktop_info info = { 0 };
    syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
    char status[48];
    text_copy(status, sizeof(status), gui_language ? "RU" : "EN");
    int n = text_length(status);
    const char* separator = "   RAM ";
    while (*separator && n + 1 < (int)sizeof(status)) {
        status[n++] = *separator++;
    }
    n += number_text(status + n, sizeof(status) - n, info.free_kib);
    const char* suffix = " KiB   ";
    while (*suffix && n + 1 < (int)sizeof(status)) {
        status[n++] = *suffix++;
    }
    unsigned seconds = info.uptime;
    n += number_text(status + n, sizeof(status) - n, seconds / 60);
    if (n + 1 < (int)sizeof(status)) {
        status[n++] = 's';
        status[n] = 0;
    }
    gfx_text(back, screen_width - 8 - gfx_text_width(status), 5, status, GUI_INK, GFX_FACE);

    if (global_menu_column >= 0) {
        static const char* const items[4][5] = {
            { "New",        "Open",          "Save",        "Save As...",  "Close" },
            { "Cut",        "Copy",          "Paste",       "Select All",  0       },
            { "Refresh",    "Status Bar",    "Large Icons", "Small Icons", 0       },
            { "About KGUI", "Keyboard help", 0,             0,             0       }
        };
        int count = global_menu_count(global_menu_column);
        int width = 152, height = count * 18 + 4;
        int x = 42 + global_menu_column * 64;
        int y = MENUBAR_HEIGHT;
        gfx_fill(back, x + 2, y + 2, width, height, GFX_SHADOW);
        gfx_fill(back, x, y, width, height, GFX_FACE);
        gfx_bevel(back, x, y, width, height, 1);
        for (int i = 0; i < count; ++i) {
            gfx_text_clipped(
                back, x + 8, y + 4 + i * 18, width - 16, items[global_menu_column][i], GFX_BLACK, GFX_FACE);
        }
    }
}

/* Bottom Dock: running applications as tiles, system tray on the right. */
/* Dock geometry is shared by the painter and the hit-test so icons never
 * drift away from their click targets. */
static void dock_layout(int* start_x, int* pinned_count, int* running_count) {
    int running = 0;
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (windows[i].used) {
            ++running;
        }
    }
    /* The tray owns the right edge. Compute capacity once from the same slot
     * constants the painter and hit-test use, so no tile can slide under it. */
    int tray_x = screen_width - TRAY_WIDTH;
    int capacity = (tray_x - 16 - 6) / DOCK_SLOT;
    if (capacity < 6) {
        capacity = 6;
    }
    if (running > capacity - 6) {
        running = capacity - 6;
    }
    int icons = 6 + running;
    int width = icons * DOCK_TILE + (icons - 1) * 6 + 10;
    int x = (tray_x - width) / 2;
    if (x < 8) {
        x = 8;
    }
    *start_x = x;
    *pinned_count = 6;
    *running_count = running;
}

/* NeXTSTEP dock behaviour: the tile under the pointer magnifies. Return a
 * stable key: 0..5 pinned, 100+index for a running window, -1 for none. */
static int dock_hovered_index(void) {
    int task_y = screen_height - TASKBAR_HEIGHT;
    int start_x, pinned_count, running_count;
    dock_layout(&start_x, &pinned_count, &running_count);
    if (gui_mouse.y < task_y || gui_mouse.x < start_x) {
        return -1;
    }
    if (gui_mouse.x < start_x + pinned_count * DOCK_SLOT) {
        int slot = (gui_mouse.x - start_x) / DOCK_SLOT;
        if (slot >= 0 && slot < pinned_count) {
            return slot;
        }
        return -1; /* gap between pinned and running tiles */
    }
    int button_x = start_x + pinned_count * DOCK_SLOT + 5;
    int drawn = 0;
    for (int i = 0; i < MAX_WINDOWS && drawn < running_count; ++i) {
        int index = zorder[i];
        if (!zvalid(index) || !windows[index].used) {
            continue;
        }
        if (gui_mouse.x >= button_x && gui_mouse.x < button_x + DOCK_TILE && gui_mouse.y >= task_y
            && gui_mouse.y < task_y + DOCK_HEIGHT) {
            return 100 + index;
        }
        button_x += DOCK_SLOT;
        ++drawn;
    }
    return -1;
}

/* One easing state for the magnified tile. Switching targets first retracts
 * the old one, which prevents two tiles from growing at once. Returns 1 while
 * the animation is still in progress. */
static int dock_tick(void) {
    int target = (menu || context_open) ? -1 : dock_hovered_index();
    int animating = 0;
    dock_hover_key = target;
    if (dock_mag_key != target && dock_mag > 0) {
        dock_mag -= (dock_mag + 7) / 8;
        animating = 1;
        if (dock_mag == 0) {
            dock_mag_key = target;
        }
    } else if (dock_mag_key == target && target >= 0 && dock_mag < 100) {
        dock_mag += (100 - dock_mag + 7) / 8;
        animating = 1;
    } else if (target < 0) {
        dock_mag_key = -1;
    }
    return animating;
}

/* Real system time from the CMOS RTC, with an uptime-based fallback when the
 * RTC is not available (so the tray clock is never blank). */
static void system_clock_text(char* out, u32 cap) {
    if (cap < 9) {
        return;
    }
    u32 packed = (u32)syscall(KOS_SYS_RTC, 0, 0, 0, 0);
    unsigned hour = packed >> 16, minute = (packed >> 8) & 0xff, second = packed & 0xff;
    if (!packed) {
        kos_desktop_info info = { 0 };
        syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
        hour = info.uptime / 3600;
        minute = (info.uptime / 60) % 60;
        second = info.uptime % 60;
    }
    out[0] = '0' + (hour / 10) % 10;
    out[1] = '0' + hour % 10;
    out[2] = ':';
    out[3] = '0' + minute / 10;
    out[4] = '0' + minute % 10;
    out[5] = ':';
    out[6] = '0' + second / 10;
    out[7] = '0' + second % 10;
    out[8] = 0;
}

static int window_pictogram_kind(const gui_window* w) {
    switch (w->kind) {
        case 0:
            return 1;
        case 1:
        case 4:
            return 2;
        case 2:
            return 3;
        case 8:
            return 4;
        case 9:
            return 5;
        default:
            return 1;
    }
}

static void draw_taskbar(gfx_surface* back) {
    int y = screen_height - DOCK_HEIGHT;
    int tray_x = screen_width - TRAY_WIDTH;
    gfx_fill(back, 0, y, screen_width, DOCK_HEIGHT, GFX_FACE);
    gfx_hline(back, 0, y, screen_width, GFX_WHITE);
    /* macOS-like Dock: the launcher and every app icon live in one centred
     * strip; the system status tray stays at the right edge. */
    static const int pinned_kind[6] = { 0, 1, 2, 3, 4, 5 };
    u32 pinned_color[6] = { gui_theme.title,       gui_theme.accent,      gui_theme.success,
                            GFX_RGB(50, 110, 180), GFX_RGB(120, 80, 170), gui_theme.warning };
    int start_x, pinned_count, running_count;
    dock_layout(&start_x, &pinned_count, &running_count);
    int x = start_x;
    int icon = dock_size < 14 ? 14 : dock_size > 26 ? 26 : dock_size;
    /* Magnification is shared state: dock_mag_key is the tile that owns the
     * current animation (possibly the one being retracted). */
    for (int i = 0; i < pinned_count; ++i) {
        int dsize = (i == dock_mag_key) ? icon + (icon / 2 * dock_mag) / 100 : icon;
        if (dsize > DOCK_HEIGHT - 2) {
            dsize = DOCK_HEIGHT - 2;
        }
        int iy = y + (DOCK_HEIGHT - dsize) / 2 - 2;
        int pressed = dock_hover_key == i;
        draw_pictogram(back, x, iy, dsize, pinned_kind[i], pressed ? gui_theme.selection : pinned_color[i]);
        x += DOCK_SLOT;
    }
    gfx_vline(back, x - 4, y + 4, DOCK_HEIGHT - 8, GFX_SHADOW);
    x += 1;
    int drawn = 0;
    for (int i = 0; i < MAX_WINDOWS && drawn < running_count; ++i) {
        int index = zorder[i];
        if (!zvalid(index) || !windows[index].used) {
            continue;
        }
        gui_window* w = &windows[index];
        int key = 100 + index;
        int dsize = (key == dock_mag_key) ? icon + (icon / 2 * dock_mag) / 100 : icon;
        if (dsize > DOCK_HEIGHT - 2) {
            dsize = DOCK_HEIGHT - 2;
        }
        int iy = y + (DOCK_HEIGHT - dsize) / 2 - 2;
        int active = index == focused && !w->minimized;
        u32 color = active ? gui_theme.accent : (w->minimized ? gui_theme.shadow : gui_theme.face);
        draw_pictogram(back, x, iy, dsize, window_pictogram_kind(w), color);
        if (!w->minimized) {
            gfx_fill(
                back, x + dsize / 2 - 2, y + DOCK_HEIGHT - 5, 4, 2, active ? GFX_WHITE : gui_theme.selection);
        }
        x += DOCK_SLOT;
        ++drawn;
    }

    gfx_bevel(back, tray_x + 2, y + 4, TRAY_WIDTH - 6, 32, 0);
    gfx_text(back, tray_x + 12, y + 12, gui_language ? "RU" : "EN", GFX_BLACK, 0);
    gfx_vline(back, tray_x + 46, y + 7, 24, GFX_SHADOW);
    gfx_text(back, tray_x + 56, y + 12, "RAM", GFX_BLACK, 0);
    kos_desktop_info info = { 0 };
    syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
    char memory[12];
    int m = number_text(memory, sizeof(memory), info.free_kib);
    while (m < 5 && m + 1 < (int)sizeof(memory)) {
        memory[m++] = ' ';
    }
    memory[m] = 0;
    gfx_text(back, tray_x + 100, y + 12, memory, GFX_BLACK, 0);
    gfx_vline(back, tray_x + 170, y + 7, 24, GFX_SHADOW);
    char clock[9];
    system_clock_text(clock, sizeof(clock));
    gfx_text(back, screen_width - 8 - gfx_text_width(clock), y + 12, clock, GFX_BLACK, 0);

    if (gui_toast_until > 0) {
        int toast_width = gfx_text_width(gui_toast) + 24;
        if (toast_width > screen_width - 16) {
            toast_width = screen_width - 16;
        }
        if (toast_width < 96) {
            toast_width = 96;
        }
        gfx_fill(back, 8, y - 24, toast_width, 20, GFX_RGB(255, 255, 220));
        gfx_bevel(back, 8, y - 24, toast_width, 20, 1);
        gfx_text(back, 14, y - 20, gui_toast, GFX_BLACK, 0);
        --gui_toast_until;
    }
}

static void draw_start_menu(gfx_surface* back) {
    int y = MENUBAR_HEIGHT;
    int height = start_menu_height();
    if (menu == 2) {
        gfx_fill(back, 62, y, 200, 220, GFX_FACE);
        gfx_bevel(back, 62, y, 200, 220, 1);
        gfx_fill(back, 66, y + 4, 192, 22, GFX_DARK);
        gfx_text(back, 72, y + 8, "Programs", GFX_WHITE, 0);
        const char* items[] = { "Accessories", "Graphics", "System", "Sound", "Games", "Back" };
        for (int i = 0; i < 6; ++i) {
            gfx_button(back, 68, y + 34 + i * 28, 188, 24, items[i], 0);
        }
        return;
    }
    if (menu == 6) {
        height = start_menu_height();
        gfx_fill(back, 62, y, 230, height, GFX_FACE);
        gfx_bevel(back, 62, y, 230, height, 1);
        gfx_fill(back, 66, y + 4, 222, 22, GFX_DARK);
        gfx_text(back, 72, y + 8, "Programs", GFX_WHITE, 0);
        int count = program_count(program_category);
        for (int i = 0; i < count; ++i) {
            gfx_button(back, 68, y + 34 + i * 28, 218, 24, program_names[program_category][i], 0);
        }
        gfx_button(back, 68, y + 34 + count * 28, 218, 24, "Back", 0);
        return;
    }
    /* Base Start menu: height comes from the same helper as the hit-test. */
    gfx_fill(back, 0, y, 220, height, GFX_FACE);
    gfx_bevel(back, 0, y, 220, height, 1);
    gfx_fill(back, 4, y + 4, 212, 24, GFX_DARK);
    gfx_text(back, 10, y + 9, "KintsugiOS OS", GFX_WHITE, 0);
    const char* items[] = { "Programs  >", "Search...", "Run...",       "Settings",     "Find...",
                            "Help",        "",          "Shut Down...", "Exit to Shell" };
    for (int i = 0; i < 9; ++i) {
        if (!items[i][0]) {
            gfx_hline(back, 8, y + 36 + i * 24, 204, GFX_SHADOW);
            continue;
        }
        gfx_button(back, 6, y + 34 + i * 24, 208, 22, items[i], 0);
    }
}

/* While the pointer holds inside a dialog button rectangle the control renders
 * in its pressed (sunken) state; release returns it to raised. */
static int dialog_button_pressed(int x, int y, int width, int height) {
    return (gui_mouse.buttons & 1) && gui_mouse.x >= x && gui_mouse.x < x + width && gui_mouse.y >= y
        && gui_mouse.y < y + height;
}

static void draw_dialog(gfx_surface* back) {
    int width = 480, height = menu == 3 ? 280 : 180;
    /* Keep the modal inside the work area on small screens. */
    if (width > screen_width) {
        width = screen_width;
    }
    if (height > screen_height - TASKBAR_HEIGHT - MENUBAR_HEIGHT - 8) {
        height = screen_height - TASKBAR_HEIGHT - MENUBAR_HEIGHT - 8;
    }
    int x = (screen_width - width) / 2, y = (screen_height - height) / 2;
    gfx_fill(back, x, y, width, height, GFX_FACE);
    gfx_bevel(back, x, y, width, height, 1);
    gfx_fill(back, x + 2, y + 2, width - 4, 22, GFX_DARK);
    const char* title = menu == 3 ? "Search for files or folders"
        : menu == 4               ? "Run"
        : menu == 7               ? (fm_action == 1       ? "New File"
                                         : fm_action == 2 ? "New Folder"
                                                          : "Rename")
        : menu == 8               ? "Save As"
                                  : "KintsugiOS Help";
    gfx_text(back, x + 8, y + 6, title, GFX_WHITE, 0);
    gfx_button(back, x + width - 26, y + 4, 18, 16, "x", 0);
    if (menu == 3) {
        gfx_text(back, x + 12, y + 34, "Named:", GFX_BLACK, 0);
        gfx_fill(back, x + 70, y + 32, width - 100, 22, GFX_WHITE);
        gfx_bevel(back, x + 70, y + 32, width - 100, 22, 0);
        gfx_text(back, x + 76, y + 36, dialog_input, GFX_BLACK, 0);
        gfx_fill(back, x + 12, y + 66, width - 24, height - 120, GFX_WHITE);
        gfx_bevel(back, x + 12, y + 66, width - 24, height - 120, 0);
        for (int i = 0; i < search_count && i < 8; ++i) {
            gfx_text(back, x + 18, y + 72 + i * 16, search_results[i], GFX_BLACK, 0);
        }
        gfx_button(
            back,
            x + 164,
            y + height - 42,
            72,
            26,
            "Cancel",
            dialog_button_pressed(x + 164, y + height - 42, 72, 26));
        gfx_button(
            back,
            x + 244,
            y + height - 42,
            72,
            26,
            "Search",
            dialog_button_pressed(x + 244, y + height - 42, 72, 26));
    } else if (menu == 4) {
        gfx_text(back, x + 12, y + 42, "Type the name of a program:", GFX_BLACK, 0);
        gfx_fill(back, x + 12, y + 66, width - 24, 24, GFX_WHITE);
        gfx_bevel(back, x + 12, y + 66, width - 24, 24, 0);
        gfx_text(back, x + 18, y + 70, dialog_input, GFX_BLACK, 0);
        gfx_button(
            back,
            x + 164,
            y + height - 42,
            72,
            26,
            "Cancel",
            dialog_button_pressed(x + 164, y + height - 42, 72, 26));
        gfx_button(
            back,
            x + 244,
            y + height - 42,
            72,
            26,
            "OK",
            dialog_button_pressed(x + 244, y + height - 42, 72, 26));
    } else if (menu == 7 || menu == 8) {
        gfx_text(back, x + 12, y + 42, menu == 8 ? "File name:" : "Name:", GFX_BLACK, 0);
        gfx_fill(back, x + 12, y + 66, width - 24, 24, GFX_WHITE);
        gfx_bevel(back, x + 12, y + 66, width - 24, 24, 0);
        if (menu == 8 && !dialog_input[0]) {
            gfx_text(back, x + 18, y + 70, "note.txt", GFX_SHADOW, 0);
        } else {
            gfx_text(back, x + 18, y + 70, dialog_input, GFX_BLACK, 0);
        }
        if (menu == 8) {
            gfx_text(back, x + 12, y + 104, "Saved in /home/user", GFX_SHADOW, 0);
        }
        gfx_button(
            back,
            x + 164,
            y + height - 42,
            72,
            26,
            "Cancel",
            dialog_button_pressed(x + 164, y + height - 42, 72, 26));
        gfx_button(
            back,
            x + 244,
            y + height - 42,
            72,
            26,
            "OK",
            dialog_button_pressed(x + 244, y + height - 42, 72, 26));
    } else {
        gfx_text(back, x + 16, y + 40, "KintsugiOS Pixel GUI", GFX_DARK, 0);
        gfx_text(back, x + 16, y + 66, "KGFM, KGED, KGTERM, KMonitor, KSystem, KServices", GFX_BLACK, 0);
        gfx_text(back, x + 16, y + 90, "Tab focus, arrows move, R resize, N minimize", GFX_BLACK, 0);
        gfx_text(back, x + 16, y + 114, "M maximize, Q exit, S Start, Ctrl+S save", GFX_BLACK, 0);
        gfx_button(
            back,
            x + (width - 72) / 2,
            y + height - 42,
            72,
            26,
            "OK",
            dialog_button_pressed(x + (width - 72) / 2, y + height - 42, 72, 26));
    }
}

static void fm_apply_name(void) {
    gui_window* w = context_window >= 0 ? &windows[context_window] : 0;
    if (!w || !w->used || w->kind != 0) {
        return;
    }
    char path[256];
    if (fm_action == 3) {
        fm_selected_path(w, path, sizeof(path));
        char target[256];
        fm_join(w, dialog_input, target, sizeof(target));
        syscall(12, (u32)path, (u32)target, 1, 0);
    } else {
        fm_join(w, dialog_input, path, sizeof(path));
        if (fm_action == 1) {
            int fd = open(path, KOS_O_WRITE | KOS_O_CREATE | KOS_O_EXCL);
            if (fd >= 0) {
                close(fd);
            }
        } else if (fm_action == 2) {
            syscall(11, (u32)path, 0, 0, 0);
        }
    }
    fm_refresh(w);
    toast("Done");
}

static void open_context_menu(int x, int y, int window, int kind) {
    static const char* const desktop[] = { "Open Terminal", "New Folder", "Display Settings", "About KGUI" };
    static const char* const files[] = { "New File", "New Folder", "Open", "Rename", "Delete", "Properties" };
    static const char* const system[] = { "Restore", "Minimize", "Maximize", "Close" };
    static const char* const header[] = { "Refresh", "New Folder", "Up", "Properties" };
    const char* const* source;
    int count;
    if (kind == 0) {
        source = desktop;
        count = 4;
    } else if (kind == 1) {
        source = files;
        count = 6;
    } else if (kind == 2) {
        source = system;
        count = 4;
    } else {
        source = header;
        count = 4;
    }
    context_kind = kind;
    context_window = window;
    context_count = count;
    for (int i = 0; i < count; ++i) {
        text_copy(context_items[i], sizeof(context_items[i]), source[i]);
    }
    context_x = x;
    context_y = y;
    context_hover = -1;
    context_open = 1;
}

static void open_save_dialog(gui_window* w) {
    if (!w || !w->used || w->kind != 4) {
        toast("No document to save");
        return;
    }
    dialog_window_index = (int)(w - windows);
    /* Empty field with a visible suggestion: typing replaces the suggestion
     * instead of appending to it, and Enter alone accepts "note.txt". */
    dialog_input[0] = 0;
    dialog_length = 0;
    menu = 8;
    context_open = 0;
    global_menu_column = -1;
}

static void system_action(gui_window* w, int action) {
    if (!w || !w->used || w->kind != 9) {
        return;
    }
    if (action == 3) {
        sys_refresh_frame = POLL_INTERVAL - 1;
        sys_refresh();
        toast("Refreshed");
        return;
    }
    if (w->sys_tab == 0) {
        if (action != 0 || w->sys_selected >= sys_process_count) {
            return;
        }
        unsigned pid = sys_processes[w->sys_selected].pid;
        if (pid <= 1 || (int)pid == getpid()) {
            toast("Cannot stop this process");
            return;
        }
        if (kill((int)pid) < 0) {
            toast("Cannot signal process");
        } else {
            wait((int)pid);
            toast("Process ended");
        }
        sys_refresh_frame = POLL_INTERVAL - 1;
        return;
    }
    if (w->sys_selected < 0 || w->sys_selected >= SYS_SERVICE_COUNT) {
        return;
    }
    const sys_service_t* svc = &sys_services[w->sys_selected];
    if (action == 2) {
        if (sys_service_pids[w->sys_selected] <= 0) {
            toast("Service is not running");
            return;
        }
        kill((int)sys_service_pids[w->sys_selected]);
        wait((int)sys_service_pids[w->sys_selected]);
        sys_service_pids[w->sys_selected] = 0;
        toast("Service stopped");
        return;
    }
    if (sys_service_pids[w->sys_selected] > 0) {
        toast("Service already running");
        return;
    }
    if (svc->system && syscall(KOS_SYS_GETUID, 0, 0, 0, 0) != 0) {
        toast("Root rights required");
        return;
    }
    int log = open("/var/log/kgui-service.log", KOS_O_WRITE | KOS_O_CREATE | KOS_O_APPEND);
    if (log >= 0) {
        dup2(log, 1);
        dup2(log, 2);
    }
    char* args[] = { (char*)svc->name, 0 };
    int pid = spawn(svc->path, 1, args);
    if (log >= 0) {
        int tty = open("/dev/tty", KOS_O_WRITE);
        if (tty >= 0) {
            dup2(tty, 1);
            dup2(tty, 2);
            close(tty);
        }
        close(log);
    }
    if (pid < 0) {
        toast(kos_error(pid));
    } else {
        sys_service_pids[w->sys_selected] = pid;
        toast("Service started");
    }
}

static void notepad_save(gui_window* w, const char* name) {
    char path[160];
    if (name[0] == '/') {
        text_copy(path, sizeof(path), name);
    } else {
        text_copy(path, sizeof(path), "/home/user/");
        int n = text_length(path), i = 0;
        while (name[i] && n + i + 1 < (int)sizeof(path)) {
            path[n + i] = name[i];
            ++i;
        }
        path[n + i] = 0;
    }
    int result = syscall(10, (u32)path, (u32)w->text, (u32)w->text_length, 0);
    if (result < 0) {
        toast(kos_error(result));
        return;
    }
    text_copy(w->file_path, sizeof(w->file_path), path);
    toast("Saved");
}

static void window_menu_action(gui_window* w, int column, int item) {
    if (column == 0) {
        if (item == 0) {
            create_window("Notepad", 680, 400, 520, 320, 4);
        } else if (item == 1) {
            create_window("KGFM - Files", 80, 50, 520, 320, 0);
        } else if (item == 2) {
            if (w && w->kind == 4) {
                if (w->file_path[0]) {
                    notepad_save(w, w->file_path);
                } else {
                    open_save_dialog(w);
                }
            } else {
                toast("No document to save");
            }
        } else if (item == 3) {
            open_save_dialog(w && w->kind == 4 ? w : 0);
        } else if (item == 4) {
            close_window(w);
        }
    } else if (column == 1) {
        static const char* const notes[] = { "Cut", "Copy", "Paste", "Select All" };
        char text[48];
        text_copy(text, sizeof(text), notes[item]);
        int n = text_length(text);
        text[n++] = ':';
        text[n++] = ' ';
        const char* suffix = " clipboard not shared";
        while (*suffix && n + 1 < (int)sizeof(text)) {
            text[n++] = *suffix++;
        }
        text[n] = 0;
        toast(text);
    } else if (column == 2) {
        if (item == 0) {
            if (w && w->kind == 0) {
                fm_refresh(w);
                toast("Refreshed");
            } else {
                toast("Nothing to refresh");
            }
        } else if (item == 1) {
            toast("Status bar is always visible");
        } else {
            toast("View mode changed");
        }
    } else {
        if (item == 0) {
            create_window("Welcome", 680, 400, 520, 320, 3);
        } else {
            open_help_document();
        }
    }
    global_menu_column = -1;
}

static void context_action(int item) {
    if (context_kind == 2) {
        gui_window* w = context_window >= 0 ? &windows[context_window] : 0;
        if (w && w->used) {
            if (item == 0 && w->maximized) {
                toggle_maximize(w);
            } else if (item == 1) {
                w->minimized = 1;
                if (focused == context_window) {
                    focused = -1;
                }
            } else if (item == 2 && !w->maximized) {
                toggle_maximize(w);
            } else if (item == 3) {
                close_window(w);
            }
        }
        context_open = 0;
        return;
    }
    if (context_kind == 3) {
        gui_window* w = context_window >= 0 ? &windows[context_window] : 0;
        if (w && w->used && w->kind == 0) {
            if (item == 0) {
                fm_refresh(w);
                toast("Refreshed");
            } else if (item == 1) {
                fm_action = 2;
                dialog_input[0] = 0;
                dialog_length = 0;
                menu = 7;
            } else if (item == 2) {
                fm_up(w);
            } else {
                toast(w->path);
            }
        }
        context_open = 0;
        return;
    }
    if (context_kind == 0) {
        if (item == 0) {
            create_window("KGTERM", 80, 400, 520, 320, 2);
        } else if (item == 1) {
            int result = syscall(11, (u32) "/home/user/New Folder", 0, 0, 0);
            toast(result < 0 ? kos_error(result) : "Folder created");
        } else if (item == 2) {
            if (!activate_window_kind(5, "Settings")) {
                create_window("Settings", 190, 40, 640, 500, 5);
            }
        } else {
            create_window("Welcome", 680, 400, 520, 320, 3);
        }
    } else {
        gui_window* w = context_window >= 0 ? &windows[context_window] : 0;
        if (!w || !w->used || w->kind != 0) {
            context_open = 0;
            return;
        }
        if (item == 0 || item == 1) {
            fm_action = item == 0 ? 1 : 2;
            dialog_input[0] = 0;
            dialog_length = 0;
            menu = 7;
        } else if (item == 2) {
            fm_open(w);
        } else if (item == 3) {
            if (w->selected < w->entry_count) {
                text_copy(dialog_input, sizeof(dialog_input), w->entries[w->selected].name);
                dialog_length = text_length(dialog_input);
                fm_action = 3;
                menu = 7;
            }
        } else if (item == 4) {
            if (w->selected < w->entry_count) {
                char path[256];
                fm_selected_path(w, path, sizeof(path));
                int result = syscall(11, (u32)path, w->entries[w->selected].stat.directory ? 2 : 1, 0, 0);
                if (result < 0) {
                    toast(kos_error(result));
                }
                fm_refresh(w);
            }
        } else if (item == 5) {
            char text[96];
            text_copy(text, sizeof(text), w->entries[w->selected].name);
            toast(text);
        }
    }
    context_open = 0;
}

/* The menu is clamped/flipped for drawing; hit testing must use the very same
 * rectangle, never the raw click position. */
static void context_menu_rect(int* out_x, int* out_y, int* out_width, int* out_height) {
    int width = 156, height = context_count * 22 + 4;
    int x = context_x, y = context_y;
    if (x + width > screen_width) {
        x = screen_width - width;
    }
    if (x < 0) {
        x = 0;
    }
    /* Flip above the pointer when the menu would cross the taskbar. */
    if (y + height > screen_height - TASKBAR_HEIGHT && y - height >= 0) {
        y -= height;
    } else if (y + height > screen_height - TASKBAR_HEIGHT) {
        y = screen_height - TASKBAR_HEIGHT - height;
    }
    if (y < 0) {
        y = 0;
    }
    *out_x = x;
    *out_y = y;
    *out_width = width;
    *out_height = height;
}

/* Bounding box of every currently visible overlay (Start, dialog, menu bar
 * dropdown, context menu), including its 2 px drop shadow. */
static void overlay_bounds(gfx_dirty* out) {
    out->valid = 0;
    if (menu == 1) {
        gfx_dirty_add(out, 0, MENUBAR_HEIGHT, 222, start_menu_height() + 4);
    } else if (menu == 2) {
        gfx_dirty_add(out, 62, MENUBAR_HEIGHT, 202, 224);
    } else if (menu == 6) {
        gfx_dirty_add(out, 62, MENUBAR_HEIGHT, 232, start_menu_height() + 4);
    }
    if (menu >= 3 && menu != 6) {
        int width = 480, height = menu == 3 ? 280 : 180;
        if (width > screen_width) {
            width = screen_width;
        }
        int limit = screen_height - TASKBAR_HEIGHT - MENUBAR_HEIGHT - 8;
        if (height > limit) {
            height = limit;
        }
        gfx_dirty_add(out, (screen_width - width) / 2, (screen_height - height) / 2, width + 4, height + 4);
    }
    if (global_menu_column >= 0) {
        int count = global_menu_count(global_menu_column);
        gfx_dirty_add(out, 42 + global_menu_column * 64, MENUBAR_HEIGHT, 160, count * 18 + 10);
    }
    if (context_open) {
        int x, y, width, height;
        context_menu_rect(&x, &y, &width, &height);
        gfx_dirty_add(out, x, y, width + 4, height + 4);
    }
}

static void draw_context_menu(gfx_surface* back) {
    if (!context_open) {
        return;
    }
    int x, y, width, height;
    context_menu_rect(&x, &y, &width, &height);
    gfx_fill(back, x + 2, y + 2, width, height, GFX_SHADOW);
    gfx_fill(back, x, y, width, height, GFX_FACE);
    gfx_bevel(back, x, y, width, height, 1);
    for (int i = 0; i < context_count; ++i) {
        int hovered = i == context_hover;
        u32 ink = hovered ? GFX_WHITE : GFX_BLACK;
        if (hovered) {
            gfx_fill(back, x + 3, y + 3 + i * 22, width - 6, 20, gui_theme.selection);
        } else {
            gfx_fill(back, x + 3, y + 3 + i * 22, width - 6, 20, GFX_FACE);
        }
        gfx_text_clipped(
            back,
            x + 10,
            y + 7 + i * 22,
            width - 20,
            context_items[i],
            ink,
            hovered ? gui_theme.selection : GFX_FACE);
    }
}

/* Pointer sprite: 'X' black outline, 'B' white body, space transparent.
 * Only the visible pixels are painted, so the background is never covered
 * by a solid block. */
#define CURSOR_WIDTH 12
#define CURSOR_HEIGHT 16
static const char* const cursor_art[CURSOR_HEIGHT] = {
    "X           ", "XX          ", "XBX         ", "XBBX        ", "XBBBX       ", "XBBBBX      ",
    "XBBBBBX     ", "XBBBBBBX    ", "XBBBBBBBX   ", "XBBBBBBBBX  ", "XBBXBBBBBBX ", "XBX XBBBBBX ",
    "X    XBBBX  ", "S    XBBX   ", " SS   XX    ", "  SSS       "
};
static u32 cursor_save[CURSOR_WIDTH * CURSOR_HEIGHT];
static int cursor_drawn, cursor_x, cursor_y;

static void cursor_bounds(int x, int y, int* left, int* top, int* width, int* height) {
    *left = x;
    *top = y;
    *width = CURSOR_WIDTH;
    *height = CURSOR_HEIGHT;
    if (*left < 0) {
        *width += *left;
        *left = 0;
    }
    if (*top < 0) {
        *height += *top;
        *top = 0;
    }
    if (*left + *width > screen_width) {
        *width = screen_width - *left;
    }
    if (*top + *height > screen_height) {
        *height = screen_height - *top;
    }
}

static void cursor_capture(int x, int y) {
    for (int row = 0; row < CURSOR_HEIGHT; ++row) {
        for (int col = 0; col < CURSOR_WIDTH; ++col) {
            int px = x + col, py = y + row;
            int index = row * CURSOR_WIDTH + col;
            cursor_save[index] = (px >= 0 && py >= 0 && px < screen_width && py < screen_height)
                ? desktop->pixels[py * screen_width + px]
                : desktop_color;
        }
    }
}

static void cursor_paint(int x, int y) {
    for (int row = 0; row < CURSOR_HEIGHT; ++row) {
        for (int col = 0; col < CURSOR_WIDTH; ++col) {
            char pixel = cursor_art[row][col];
            if (pixel != 'X' && pixel != 'B' && pixel != 'S') {
                continue;
            }
            int px = x + col, py = y + row;
            if (px < 0 || py < 0 || px >= screen_width || py >= screen_height) {
                continue;
            }
            u32 value = pixel == 'X' ? GFX_BLACK : pixel == 'S' ? GFX_SHADOW : GFX_WHITE;
            desktop->pixels[py * screen_width + px] = value;
        }
    }
}

static void cursor_restore(int x, int y) {
    for (int row = 0; row < CURSOR_HEIGHT; ++row) {
        for (int col = 0; col < CURSOR_WIDTH; ++col) {
            int px = x + col, py = y + row;
            if (px >= 0 && py >= 0 && px < screen_width && py < screen_height) {
                desktop->pixels[py * screen_width + px] = cursor_save[row * CURSOR_WIDTH + col];
            }
        }
    }
}

/* Remove the pointer before a full repaint so stale pixels can never be
 * restored over a freshly drawn window. */
static void cursor_erase(void) {
    if (!cursor_drawn) {
        return;
    }
    cursor_restore(cursor_x, cursor_y);
    cursor_drawn = 0;
}

static void cursor_move(int x, int y, gfx_dirty* extra) {
    if (!extra && cursor_drawn && x == cursor_x && y == cursor_y) {
        return; /* no motion: do not touch the framebuffer at all */
    }
    gfx_dirty dirty = { 0, 0, 0, 0, 0 };
    if (extra && extra->valid) {
        dirty.left = extra->left;
        dirty.top = extra->top;
        dirty.right = extra->right;
        dirty.bottom = extra->bottom;
        dirty.valid = 1;
    }
    if (cursor_drawn) {
        int left, top, width, height;
        cursor_bounds(cursor_x, cursor_y, &left, &top, &width, &height);
        if (width > 0 && height > 0) {
            gfx_dirty_add(&dirty, left, top, width, height);
        }
        cursor_restore(cursor_x, cursor_y);
    }
    cursor_x = x;
    cursor_y = y;
    cursor_capture(x, y);
    cursor_paint(x, y);
    cursor_drawn = 1;
    int left, top, width, height;
    cursor_bounds(x, y, &left, &top, &width, &height);
    if (width > 0 && height > 0) {
        gfx_dirty_add(&dirty, left, top, width, height);
    }
    gfx_present_dirty(desktop, 0, 0, &dirty);
}

static void compose(gfx_dirty* dirty) {
    if (!dirty || !dirty->valid) {
        if (dirty) {
            dirty_full(dirty);
        }
    }
    if (dirty && dirty->valid) {
        int left = dirty->left < 0 ? 0 : dirty->left;
        int top = dirty->top < 0 ? 0 : dirty->top;
        int right = dirty->right > screen_width ? screen_width : dirty->right;
        int bottom = dirty->bottom > screen_height ? screen_height : dirty->bottom;
        if (right > left && bottom > top) {
            /* Copy only the damaged rectangle from the retained wallpaper:
             * a keystroke must never push a full-screen blit through TCG. */
            if (wallpaper_surface) {
                gfx_blit(desktop, left, top, wallpaper_surface, left, top, right - left, bottom - top);
            } else {
                gfx_fill(desktop, left, top, right - left, bottom - top, desktop_color);
            }
        }
    } else {
        if (wallpaper_surface) {
            gfx_blit(desktop, 0, 0, wallpaper_surface, 0, 0, screen_width, screen_height);
        } else {
            gfx_fill(desktop, 0, 0, screen_width, screen_height, desktop_color);
        }
    }
    /* Desktop icons and windows outside the presented region are skipped:
     * a single keystroke must not push another megabyte through the VGA
     * aperture just to redraw pixels the user cannot see changing. */
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        const desktop_icon* icon = &desktop_icons[i];
        int ix, iy, iw, ih;
        desktop_icon_rect(icon, &ix, &iy, &iw, &ih);
        if (dirty_rect_hit(dirty, ix, iy, iw, ih)) {
            draw_desktop_icon(desktop, icon, selected_icon == i);
        }
    }
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        int index = zorder[i];
        if (!zvalid(index)) {
            continue;
        }
        gui_window* w = &windows[index];
        if (!w->used || w->minimized || !dirty_rect_hit(dirty, w->x, w->y, w->width, w->height)) {
            continue;
        }
        if (w->anim_open) {
            /* Ease-out scale: fast at first, settling gently at 1.0. */
            static const int num[11] = { 30, 32, 34, 36, 37, 38, 39, 39, 40, 40, 40 };
            gfx_blit_scale(
                desktop,
                w->x,
                w->y,
                w->surface,
                0,
                0,
                w->width,
                w->height,
                num[w->anim_progress < 10 ? w->anim_progress : 10],
                40);
            ++w->anim_progress;
            if (w->anim_progress > 10) {
                w->anim_open = 0;
            }
            continue;
        }
        if (w->anim_close) {
            /* Ease-in scale: the window accelerates into its disappearance. */
            static const int num[11] = { 40, 40, 40, 39, 39, 38, 37, 36, 34, 32, 30 };
            gfx_blit_scale(
                desktop,
                w->x,
                w->y,
                w->surface,
                0,
                0,
                w->width,
                w->height,
                num[w->anim_progress < 10 ? w->anim_progress : 10],
                40);
            ++w->anim_progress;
            if (w->anim_progress > 10) {
                /* Retire the slot now; the real teardown runs after the loop. */
                w->anim_close = 0;
                w->used = 0;
                if (pending_close_count < MAX_WINDOWS) {
                    pending_close[pending_close_count++] = (int)(w - windows);
                }
            }
            continue;
        }
        gfx_blit(desktop, w->x, w->y, w->surface, 0, 0, w->width, w->height);
    }
    /* External GUI clients own their own window buffers in the kernel. */
    kos_gui_window_info external[8];
    int external_count = gfx_window_list(external, 8);
    for (int i = 0; i < external_count; ++i) {
        if (external[i].minimized
            || !dirty_rect_hit(dirty, external[i].x, external[i].y, external[i].width, external[i].height)) {
            continue;
        }
        int id = external[i].id;
        gfx_surface* surface = external_surface[i];
        if (!surface || surface->width != (int)external[i].width
            || surface->height != (int)external[i].height) {
            if (surface) {
                gfx_surface_destroy(surface);
            }
            surface = gfx_surface_create((int)external[i].width, (int)external[i].height);
            external_surface[i] = surface;
        }
        if (!surface) {
            continue;
        }
        if (!gfx_window_read(id, surface->pixels, 0, 0, external[i].width, external[i].height)) {
            gfx_blit(
                desktop, external[i].x, external[i].y, surface, 0, 0, external[i].width, external[i].height);
        }
    }
    draw_menu_bar(desktop);
    draw_taskbar(desktop);
    if (menu == 1 || menu == 2 || menu == 6) {
        draw_start_menu(desktop);
    }
    if (menu >= 3 && menu != 6) {
        draw_dialog(desktop);
    }
    draw_context_menu(desktop);
    /* Remember what was painted on top so the next partial frame can clean it
     * up even after the overlay itself has closed. */
    overlay_bounds(&overlay_prev);
    gfx_present_dirty(desktop, 0, 0, dirty);
    for (int i = 0; i < pending_close_count; ++i) {
        window_finish(&windows[pending_close[i]]);
    }
    pending_close_count = 0;
}

static void drag_begin(gui_window* w);
static void drag_move(gui_window* w, int old_x, int old_y);
static void drag_end(void);

static gui_window* create_window(const char* title, int x, int y, int width, int height, int kind) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (windows[i].used) {
            continue;
        }
        gui_window* w = &windows[i];
        unsigned char* bytes = (unsigned char*)w;
        for (unsigned n = 0; n < sizeof(*w); ++n) {
            bytes[n] = 0;
        }
        /* Keep every window inside the work area: menu bar on top, task bar
         * on bottom. Nothing may be placed out of bounds. */
        if (x < 0) {
            x = 0;
        }
        if (y < MENUBAR_HEIGHT + 2) {
            y = MENUBAR_HEIGHT + 2;
        }
        if (width > screen_width - x) {
            x = screen_width - width;
        }
        if (y + height > screen_height - TASKBAR_HEIGHT) {
            y = screen_height - TASKBAR_HEIGHT - height;
        }
        w->used = 1;
        w->x = x;
        w->y = y;
        w->width = width;
        w->height = height;
        w->anim_open = 1;
        w->anim_progress = 0;
        w->saved_x = x;
        w->saved_y = y;
        w->saved_width = width;
        w->saved_height = height;
        w->kind = kind;
        w->pending_ink = TERM_DEFAULT_INK;
        text_copy(w->title, sizeof(w->title), title);
        w->surface = gfx_surface_create(width, height);
        if (!w->surface) {
            w->used = 0;
            return 0;
        }
        if (kind == 0) {
            text_copy(w->path, sizeof(w->path), "/home/user");
            fm_init_shelf(w);
            fm_refresh(w);
        } else if (kind == 1) {
            kged_sync_canvas(w);
            w->color = GFX_BLACK;
        } else if (kind == 2) {
            term_shell_banner(w);
        }
        if (kind == 9) {
            sys_refresh_frame = POLL_INTERVAL - 1;
            sys_refresh();
        }
        ++window_count;
        raise_window(i);
        draw_window(w);
        return w;
    }
    return 0;
}

/* Bring an already running application forward instead of stacking duplicate
 * windows — the launchers for TUI, terminal and GUI apps share this path. */
static gui_window* activate_window_kind(int kind, const char* title) {
    for (int i = MAX_WINDOWS - 1; i >= 0; --i) {
        int index = zorder[i];
        if (!zvalid(index) || !windows[index].used || windows[index].kind != kind) {
            continue;
        }
        if (title && !text_equal(windows[index].title, title)) {
            continue;
        }
        windows[index].minimized = 0;
        raise_window(index);
        return &windows[index];
    }
    return 0;
}

/* Documentation opens as a real document window (title bar, text canvas,
 * status bar, scroll), not as a flat modal panel. */
static void open_help_document(void) {
    gui_window* existing = activate_window_kind(4, "KintsugiOS Documentation");
    if (existing) {
        menu = 0;
        return;
    }
    gui_window* w = create_window("KintsugiOS Documentation", 220, 90, 600, 420, 4);
    if (!w) {
        toast("Cannot open documentation");
        menu = 0;
        return;
    }
    text_copy(w->text, sizeof(w->text),
        "KintsugiOS Documentation\n"
        "\n"
        "WORKSPACE\n"
        "Menu bar: File, Edit, View, Help. K opens Start.\n"
        "Drag a title bar to move a window; drag its lower-right corner to resize.\n"
        "Arrows / W A S D move the focused window; M maximizes, N minimizes.\n"
        "Ctrl+S saves a document, Esc closes a menu or dialog.\n"
        "\n"
        "APPLICATIONS\n"
        "KGFM  - file manager: Home, Bin, Etc, Logs, Root.\n"
        "KGED  - graphic editor: Pen, Erase, Clear.\n"
        "KGTERM - shell with history, ANSI colors and live output.\n"
        "KMonitor / KSystem - processes, services and resources.\n"
        "Settings - theme, wallpaper, Dock, mixer and system.\n"
        "\n"
        "TUI APPLICATIONS\n"
        "Launch from Programs or KGTERM. TUI programs share the 12x15\n"
        "console grid, CP866 box drawing and keyboard shortcuts.\n"
        "\n"
        "TIME AND SETTINGS\n"
        "The tray clock shows real system time from the CMOS RTC.\n"
        "Appearance and Dock layout persist in /home/user/KGUI.CFG.\n"
        "Reopen this document from Help or the Start menu.\n");
    w->text_length = text_length(w->text);
    w->text_top = 0;
    draw_window(w);
    menu = 0;
}

static void drag_begin(gui_window* w) {
    drag_index = (int)(w - windows);
    if (drag_backup) {
        gfx_surface_destroy(drag_backup);
    }
    drag_backup = gfx_surface_create(w->width, w->height);
    if (!drag_backup) {
        drag_index = -1;
        return;
    }
    int saved = w->minimized;
    w->minimized = 1;
    gfx_dirty dirty = { 0, 0, 0, 0, 0 };
    compose(&dirty);
    w->minimized = saved;
    gfx_blit(drag_backup, 0, 0, desktop, w->x, w->y, w->width, w->height);
}

static void drag_move(gui_window* w, int old_x, int old_y) {
    if (!drag_backup || drag_index != (int)(w - windows)) {
        return;
    }
    gfx_dirty dirty = { 0, 0, 0, 0, 0 };
    /* Never restore stale cursor pixels over freshly dragged window content. */
    cursor_erase();
    gfx_blit(desktop, old_x, old_y, drag_backup, 0, 0, w->width, w->height);
    gfx_dirty_add(&dirty, old_x, old_y, w->width, w->height);
    gfx_blit(drag_backup, 0, 0, desktop, w->x, w->y, w->width, w->height);
    gfx_blit(desktop, w->x, w->y, w->surface, 0, 0, w->width, w->height);
    gfx_dirty_add(&dirty, w->x, w->y, w->width, w->height);
    cursor_move(gui_mouse.x, gui_mouse.y, &dirty);
}

static void drag_end(void) {
    if (drag_backup) {
        gfx_surface_destroy(drag_backup);
    }
    drag_backup = 0;
    drag_index = -1;
}

static void gui_shutdown(void) {
    if (cli_child_pid > 0) {
        cli_finish(1);
    }
    if (tui_child_pid > 0) {
        kill(tui_child_pid);
        tui_finish();
    }
    if (gui_child_pid > 0) {
        kill(gui_child_pid);
        wait(gui_child_pid);
    }
    syscall(KOS_SYS_TUI_ATTACH, 0, 0, 0, 0);
    for (int i = 0; i < 8; ++i) {
        if (external_surface[i]) {
            gfx_surface_destroy(external_surface[i]);
            external_surface[i] = 0;
        }
    }
    gfx_exit_to_terminal();
}

static void activate_icon(int index) {
    gui_window* w = 0;
    if (index == 0) {
        w = create_window("My Computer", 60, 40, 520, 320, 0);
        if (w) {
            text_copy(w->path, sizeof(w->path), "/");
            fm_refresh(w);
        }
    } else if (index == 1) {
        create_window("KGTERM", 80, 400, 680, 320, 2);
    } else if (index == 2) {
        create_window("KGED - Editor", 680, 50, 520, 320, 1);
    } else if (index == 3) {
        create_window("Recycle Bin", 360, 200, 420, 220, 7);
    }
}

static void toggle_maximize(gui_window* w) {
    if (!w->maximized) {
        w->saved_x = w->x;
        w->saved_y = w->y;
        w->saved_width = w->width;
        w->saved_height = w->height;
        w->x = 2;
        w->y = 2;
        w->width = screen_width - 4;
        w->height = screen_height - MENUBAR_HEIGHT - TASKBAR_HEIGHT - 6;
        w->maximized = 1;
    } else {
        w->x = w->saved_x;
        w->y = w->saved_y;
        w->width = w->saved_width;
        w->height = w->saved_height;
        w->maximized = 0;
    }
    window_surface_rebuilt(w);
    draw_window(w);
}

static void window_finish(gui_window* w) {
    int index = (int)(w - windows);
    gfx_surface_destroy(w->surface);
    if (w->canvas) {
        gfx_surface_destroy(w->canvas);
    }
    unsigned char* bytes = (unsigned char*)w;
    for (unsigned i = 0; i < sizeof(*w); ++i) {
        bytes[i] = 0;
    }
    int position = 0;
    while (position < MAX_WINDOWS && zorder[position] != index) {
        ++position;
    }
    if (position < MAX_WINDOWS) {
        for (int i = position; i < MAX_WINDOWS - 1; ++i) {
            zorder[i] = zorder[i + 1];
        }
        zorder[MAX_WINDOWS - 1] = -1;
    }
    --window_count;
    focused = -1;
    for (int i = MAX_WINDOWS - 1; i >= 0; --i) {
        int index = zorder[i];
        if (zvalid(index) && windows[index].used && !windows[index].minimized) {
            focused = index;
            break;
        }
    }
}

static void close_window(gui_window* w) {
    if (!w) {
        return;
    }
    /* Closing animates out: the slot stays in the z-order until the fade
     * completes, then the real teardown runs through window_finish(). */
    w->anim_close = 1;
    w->anim_progress = 0;
    w->used = 1;
    int index = (int)(w - windows);
    int position = 0;
    while (position < MAX_WINDOWS && zorder[position] != index) {
        ++position;
    }
    if (position < MAX_WINDOWS) {
        for (int i = position; i < MAX_WINDOWS - 1; ++i) {
            zorder[i] = zorder[i + 1];
        }
        zorder[MAX_WINDOWS - 1] = index;
    }
    focused = -1;
    for (int i = MAX_WINDOWS - 1; i >= 0; --i) {
        int idx = zorder[i];
        if (zvalid(idx) && windows[idx].used && !windows[idx].minimized) {
            focused = idx;
            break;
        }
    }
}

static void run_program(const char* name);
static void gui_shutdown(void);

static void dock_launch_index(int index) {
    static const int map[5][2] = {
        { 2, 0 },
        { 1, 1 },
        { 2, 1 },
        { 2, 2 },
        { 2, 3 }
    };
    if (index >= 0 && index < 5) {
        launch_category_item(map[index][0], map[index][1]);
    }
}

static void launch_category_item(int category, int item) {
    if (category < 0 || category > 4 || item < 0 || item >= program_count(category)) {
        return;
    }
    const char* name = program_names[category][item];
    if (text_equal(name, "KGFM")) {
        if (!activate_window_kind(0, "KGFM - Files")) {
            create_window("KGFM - Files", 80, 50, 560, 340, 0);
        }
    } else if (text_equal(name, "KGED")) {
        if (!activate_window_kind(1, "KGED - Editor")) {
            create_window("KGED - Editor", 700, 50, 520, 320, 1);
        }
    } else if (text_equal(name, "KGTERM")) {
        if (!activate_window_kind(2, "KGTERM")) {
            create_window("KGTERM", 80, 400, 680, 320, 2);
        }
    } else if (text_equal(name, "Notepad")) {
        if (!activate_window_kind(4, "Notepad")) {
            create_window("Notepad", 700, 400, 520, 320, 4);
        }
    } else if (text_equal(name, "KMonitor")) {
        if (!activate_window_kind(8, "KMonitor")) {
            create_window("KMonitor", 380, 120, 560, 400, 8);
        }
    } else if (text_equal(name, "KSystem")) {
        if (!activate_window_kind(9, "KSystem")) {
            create_window("KSystem", 400, 180, 600, 400, 9);
        }
    } else {
        run_program(name);
    }
}

static void run_program(const char* name) {
    /* An external GUI client with the same name is already running: raise it
     * instead of spawning a second copy in the terminal. */
    kos_gui_window_info external[8];
    int external_count = gfx_window_list(external, 8);
    for (int i = 0; i < external_count; ++i) {
        if (text_equal_ci(external[i].title, name)) {
            gfx_window_raise(external[i].id);
            external_focus = external[i].id;
            focused = -1;
            toast("Activated");
            return;
        }
    }
    gui_window* term = 0;
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (windows[i].used && windows[i].kind == 2 && !windows[i].minimized) {
            term = &windows[i];
            break;
        }
    }
    if (!term) {
        term = create_window("KGTERM", 80, 400, 680, 320, 2);
    }
    if (!term) {
        toast("Cannot open terminal");
        return;
    }
    raise_window((int)(term - windows));
    char command[128];
    text_copy(command, sizeof(command), name);
    char* argv[8];
    int argc;
    split_command(command, argv, &argc);
    if (argc) {
        term_launch(term, argv[0], argv, argc);
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    syscall(KOS_SYS_GFX_MODE, 1, 0, 0, 0);
    kos_gfx_info info;
    if (gfx_screen_info(&info)) {
        return 1;
    }
    screen_width = (int)info.width;
    screen_height = (int)info.height;
    desktop = gfx_surface_create(screen_width, screen_height);
    if (!desktop) {
        return 1;
    }
    wallpaper_color_a = gui_theme.desktop;
    wallpaper_color_b = gui_theme.accent;
    settings_load();
    /* Ordinary 5x7 bitmap face: NeXTSTEP-style clarity without synthesized bold. */
    gfx_font_set_bold(0);
    settings_user_count = 0;
    /* Read /etc/passwd into a small table for the Settings window. */
    {
        char data[1024];
        int size = syscall(2, (u32) "/etc/passwd", 0, (u32)data, sizeof(data) - 1);
        if (size > 0) {
            data[size] = 0;
            char* line = data;
            while (*line && settings_user_count < 8) {
                char* end = line;
                while (*end && *end != '\n' && *end != '\r') {
                    ++end;
                }
                char saved = *end;
                *end = 0;
                char* colon = line;
                while (*colon && *colon != ':') {
                    ++colon;
                }
                if (*colon) {
                    *colon = 0;
                    text_copy(settings_users[settings_user_count++], 32, line);
                    *colon = ':';
                }
                if (!saved) {
                    break;
                }
                line = end + 1;
            }
        }
    }

    create_window("KGFM - Files", 80, 50, 560, 340, 0);
    create_window("KGED - Editor", 700, 50, 520, 320, 1);
    create_window("KGTERM", 80, 400, 680, 320, 2);
    create_window("Welcome", 780, 400, 420, 320, 3);

    gfx_dirty dirty = { 0, 0, 0, 0, 0 };
    syscall(KOS_SYS_MOUSE, (u32)&gui_mouse, 0, 0, 0);
    compose(&dirty);
    cursor_move(gui_mouse.x, gui_mouse.y, 0);

    for (;;) {
        ++gui_frame;
        kos_mouse mouse;
        int changed = 0;
        syscall(KOS_SYS_MOUSE, (u32)&mouse, 0, 0, 0);
        int old_x = gui_mouse.x, old_y = gui_mouse.y;
        int mouse_changed = mouse.x != gui_mouse.x || mouse.y != gui_mouse.y
            || mouse.buttons != gui_mouse.buttons || mouse.wheel != gui_mouse.wheel
            || mouse.sequence != gui_mouse.sequence;
        gui_mouse = mouse;
        /* The pointer hit area extends a couple of pixels past the visible
         * sprite so thin NeXTSTEP-style controls stay easy to grab. */
        mouse.x += GUI_CURSOR_HIT_MARGIN;
        mouse.y += GUI_CURSOR_HIT_MARGIN;
        if (mouse_changed) {
            int only_motion =
                (mouse.buttons == previous_buttons && !mouse.wheel && (mouse.x != old_x || mouse.y != old_y));
            if (only_motion) {
                cursor_move(mouse.x, mouse.y, 0);
                if (context_open) {
                    int mx, my, mw, mh;
                    context_menu_rect(&mx, &my, &mw, &mh);
                    int item = -1;
                    if (mouse.x >= mx && mouse.x < mx + mw && mouse.y >= my && mouse.y < my + mh) {
                        item = (mouse.y - my - 3) / 22;
                        if (item < 0 || item >= context_count) {
                            item = -1;
                        }
                    }
                    if (item != context_hover) {
                        context_hover = item;
                        changed = 1;
                    }
                }
            } else {
                changed = 1;
            }
        }
        unsigned pressed = mouse.buttons & ~previous_buttons;
        unsigned released = previous_buttons & ~mouse.buttons;
        previous_buttons = mouse.buttons;
        int key = syscall(8, 0, 0, 0, 0);
        if (cli_child_pid > 0) {
            ++cli_frames;
            if (cli_poll(cli_window)) {
                changed = 1;
            }
            int alive = 1;
            if (cli_frames % POLL_INTERVAL == 0) {
                kos_process_info list[16];
                int count = syscall(16, (u32)list, 16, 0, 0);
                alive = 0;
                for (int i = 0; i < count && i < 16; ++i) {
                    if (list[i].pid == (u32)cli_child_pid && list[i].state != 4) {
                        alive = 1;
                        break;
                    }
                }
            }
            if (!alive) {
                cli_finish(0);
                changed = 1;
            } else if (key == 3) { /* Ctrl+C cancels a running command. */
                cli_finish(1);
                key = 0;
                changed = 1;
            } else {
                key = 0;
            }
        }
        if (tui_child_pid > 0) {
            int tui_changed = syscall(KOS_SYS_TUI_READ, (u32)tui_cells, 0, 0, 0);
            int alive = 1;
            if (gui_frame % POLL_INTERVAL == 0) {
                kos_process_info list[16];
                int count = syscall(16, (u32)list, 16, 0, 0);
                alive = 0;
                for (int i = 0; i < count && i < 16; ++i) {
                    if (list[i].pid == (u32)tui_child_pid && list[i].state != 4) {
                        alive = 1;
                        break;
                    }
                }
            }
            if (!alive) {
                tui_finish();
                changed = 1;
            } else {
                /* The compositor owns the console: forward keys to the child,
                 * keeping Escape for itself so a TUI can always be closed. */
                if (key == 27) {
                    kill(tui_child_pid);
                    tui_finish();
                    changed = 1;
                } else if (key) {
                    syscall(KOS_SYS_KEY_INJECT, (u32)tui_child_pid, (u32)key, 0, 0);
                }
                if (tui_changed) {
                    changed = 1;
                }
            }
            key = 0;
        }
        if (gui_child_pid > 0 && gui_frame % 20 == 0) {
            kos_process_info list[16];
            int count = syscall(16, (u32)list, 16, 0, 0);
            int alive = 0;
            for (int i = 0; i < count && i < 16; ++i) {
                if (list[i].pid == (u32)gui_child_pid && list[i].state != 4) {
                    alive = 1;
                    break;
                }
            }
            if (!alive) {
                wait(gui_child_pid);
                gui_child_pid = -1;
                changed = 1;
            }
        }

        /* Forward pointer and key input to the active external client in its
         * own window coordinates. Without the release/move events a client
         * cannot tell a completed click from a held button. */
        if (external_focus > 0) {
            kos_gui_window_info info;
            if (external_locate(external_focus, &info)) {
                if (mouse.x != old_x || mouse.y != old_y) {
                    kos_gui_event event = {
                        KOS_GUI_EV_MOVE, mouse.x - info.x, mouse.y - info.y, mouse.buttons & 7, 0
                    };
                    gfx_window_post(external_focus, &event);
                    changed = 1;
                }
                if (released) {
                    kos_gui_event event = {
                        KOS_GUI_EV_RELEASE, mouse.x - info.x, mouse.y - info.y, released & 7, 0
                    };
                    gfx_window_post(external_focus, &event);
                    if (!mouse.buttons) {
                        external_focus = -1;
                    }
                    changed = 1;
                }
                if (key) {
                    kos_gui_event event = { KOS_GUI_EV_KEY, 0, 0, 0, key };
                    gfx_window_post(external_focus, &event);
                    key = 0;
                }
            } else {
                external_focus = -1;
            }
        }

        gui_window* focused_window = focused >= 0 ? &windows[focused] : 0;

        if (dragging && (mouse.buttons & 1) && focused_window && !focused_window->maximized) {
            int nx = mouse.x - drag_dx, ny = mouse.y - drag_dy;
            if (nx < 0) {
                nx = 0;
            }
            if (ny < MENUBAR_HEIGHT + 2) {
                ny = MENUBAR_HEIGHT + 2;
            }
            if (nx + focused_window->width > screen_width) {
                nx = screen_width - focused_window->width;
            }
            if (ny + focused_window->height > screen_height - TASKBAR_HEIGHT) {
                ny = screen_height - TASKBAR_HEIGHT - focused_window->height;
            }
            if (nx != focused_window->x || ny != focused_window->y) {
                int old_x = focused_window->x, old_y = focused_window->y;
                focused_window->x = nx;
                focused_window->y = ny;
                if (drag_backup && drag_index == (int)(focused_window - windows)) {
                    cursor_erase();
                    drag_move(focused_window, old_x, old_y);
                } else {
                    changed = 1;
                }
            }
        } else {
            if (dragging && drag_backup) {
                drag_end();
                changed = 1;
            }
            dragging = 0;
        }
        if (resizing && (mouse.buttons & 1) && focused_window && !focused_window->maximized) {
            int nw = mouse.x - focused_window->x + 8;
            int nh = mouse.y - focused_window->y + 8;
            if (nw < 200) {
                nw = 200;
            }
            if (nh < 160) {
                nh = 160;
            }
            if (nw != focused_window->width || nh != focused_window->height) {
                focused_window->width = nw;
                focused_window->height = nh;
                window_surface_rebuilt(focused_window);
                draw_window(focused_window);
                changed = 1;
            }
        } else {
            resizing = 0;
        }

        if (menu >= 3 && menu != 6) {
            int dx = (screen_width - 480) / 2, dy = (screen_height - (menu == 3 ? 280 : 180)) / 2;
            int button_y = dy + (menu == 3 ? 238 : 138);
            if (pressed & 1) {
                if (mouse.x >= dx + 454 && mouse.x < dx + 472 && mouse.y >= dy + 4 && mouse.y < dy + 20) {
                    menu = 0;
                    changed = 1;
                } else if (mouse.y >= button_y) {
                    /* Cancel occupies the left slot, the action (OK / Search /
                     * Run / Save) the right one: identical size and top edge, so
                     * the pair is symmetric and both ends are clickable. */
                    if (mouse.x >= dx + 164 && mouse.x < dx + 236) {
                        menu = 0; /* Cancel */
                        changed = 1;
                    } else if (mouse.x >= dx + 244 && mouse.x < dx + 316) {
                        if (menu == 3) {
                            search_count = 0;
                            search_directory("/bin", dialog_input, 0);
                            search_directory("/etc", dialog_input, 0);
                        } else if (menu == 4) {
                            run_program(dialog_input);
                        } else if (menu == 7) {
                            fm_apply_name();
                        } else if (menu == 8 && dialog_window_index >= 0) {
                            notepad_save(
                                &windows[dialog_window_index], dialog_input[0] ? dialog_input : "note.txt");
                        }
                        menu = 0;
                        changed = 1;
                    } else {
                        menu = 0;
                        changed = 1;
                    }
                }
            }
            if (key == 27) {
                menu = 0;
                changed = 1;
            } else if (key) {
                char encoded[4];
                unsigned added = text_key(key, encoded);
                if (added && dialog_length + (int)added < (int)sizeof(dialog_input) - 1) {
                    for (unsigned i = 0; i < added; ++i) {
                        dialog_input[dialog_length++] = encoded[i];
                    }
                    dialog_input[dialog_length] = 0;
                    changed = 1;
                } else if (key == 8 && dialog_length) {
                    dialog_input[--dialog_length] = 0;
                    changed = 1;
                } else if (key == '\n') {
                    if (menu == 3) {
                        search_count = 0;
                        search_directory("/bin", dialog_input, 0);
                        search_directory("/etc", dialog_input, 0);
                    } else if (menu == 4) {
                        run_program(dialog_input);
                    } else if (menu == 7) {
                        fm_apply_name();
                        menu = 0;
                    } else if (menu == 8 && dialog_window_index >= 0) {
                        notepad_save(
                            &windows[dialog_window_index], dialog_input[0] ? dialog_input : "note.txt");
                        menu = 0;
                    }
                    changed = 1;
                }
            }
            if (changed) {
                cursor_erase();
                compose(&dirty);
                cursor_move(mouse.x, mouse.y, 0);
            }
            syscall(KOS_SYS_SLEEP, 10, 0, 0, 0);
            continue;
        }

        int context_handled = 0;
        if (context_open && (pressed & 1)) {
            int mx, my, mw, mh;
            context_menu_rect(&mx, &my, &mw, &mh);
            int item = (mouse.y - my - 3) / 22;
            if (mouse.x >= mx && mouse.x < mx + mw && item >= 0 && item < context_count) {
                context_action(item);
            } else {
                context_open = 0;
            }
            changed = 1;
            context_handled = 1;
        } else if (pressed & 2) {
            if (!menu && mouse.y < screen_height - TASKBAR_HEIGHT) {
                int target = window_at(mouse.x, mouse.y);
                if (target >= 0) {
                    gui_window* w = &windows[target];
                    int ly = mouse.y - w->y;
                    if (ly < TITLE_HEIGHT) {
                        open_context_menu(mouse.x, mouse.y, target, 2); /* system menu */
                    } else if (w->kind == 0) {
                        int content_y = window_content_y();
                        int list_y = content_y + 29 + 28 + 20;
                        if (ly >= list_y) {
                            open_context_menu(mouse.x, mouse.y, target, 1);
                        } else {
                            open_context_menu(mouse.x, mouse.y, target, 3);
                        }
                    } else {
                        open_context_menu(mouse.x, mouse.y, target, 2);
                    }
                } else {
                    open_context_menu(mouse.x, mouse.y, -1, 0);
                }
            }
            changed = 1;
            context_handled = 1;
        }
        if ((pressed & 1) && !context_handled) {
            int icon_hit = -1;
            if (!menu && mouse.y < screen_height - TASKBAR_HEIGHT) {
                for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
                    const desktop_icon* icon = &desktop_icons[i];
                    int lx, ly, lw, lh;
                    desktop_icon_rect(icon, &lx, &ly, &lw, &lh);
                    if (mouse.x >= lx && mouse.x < lx + lw && mouse.y >= ly && mouse.y < ly + lh) {
                        icon_hit = i;
                        break;
                    }
                }
            }
            if (global_menu_column >= 0) {
                int count = global_menu_count(global_menu_column);
                int mx = 42 + global_menu_column * 64;
                if (mouse.y >= MENUBAR_HEIGHT && mouse.y < MENUBAR_HEIGHT + count * 18 + 4 && mouse.x >= mx
                    && mouse.x < mx + 152) {
                    int item = (mouse.y - MENUBAR_HEIGHT - 4) / 18;
                    gui_window* active = focused >= 0 && zvalid(focused) ? &windows[focused] : 0;
                    window_menu_action(active, global_menu_column, item);
                }
                global_menu_column = -1;
                changed = 1;
            } else if (mouse.y < MENUBAR_HEIGHT) {
                if (mouse.x >= 4 && mouse.x < 34) {
                    menu = menu == 1 ? 0 : 1;
                } else {
                    int column = (mouse.x - 42) / 64;
                    gui_window* active = focused >= 0 && zvalid(focused) ? &windows[focused] : 0;
                    if (column >= 0 && column < 4 && global_menu_enabled(active, column)) {
                        global_menu_column = column;
                        menu = 0;
                    }
                }
                changed = 1;
            } else if (icon_hit >= 0) {
                int double_click = icon_hit == last_icon_click && gui_frame - last_icon_tick < 40;
                selected_icon = icon_hit;
                last_icon_click = icon_hit;
                last_icon_tick = gui_frame;
                if (double_click) {
                    activate_icon(icon_hit);
                }
                changed = 1;
            } else {
                int task_y = screen_height - TASKBAR_HEIGHT;
                int start_y = MENUBAR_HEIGHT;
                int dock_x, dock_pinned, dock_running;
                dock_layout(&dock_x, &dock_pinned, &dock_running);
                if (mouse.y >= task_y && mouse.x >= dock_x && mouse.x < dock_x + DOCK_TILE) {
                    menu = menu == 1 ? 0 : 1; /* K launcher is the first Dock tile */
                    changed = 1;
                } else if (
                    menu == 1 && mouse.x >= 6 && mouse.x < 214 && mouse.y >= start_y + 34
                    && mouse.y < start_y + 250) {
                    int item = (mouse.y - start_y - 34) / 24;
                    if (item == 0) {
                        menu = 2;
                    } else if (item == 1 || item == 4) {
                        menu = 3;
                        dialog_input[0] = 0;
                        dialog_length = 0;
                        search_count = 0;
                    } else if (item == 2) {
                        menu = 4;
                        dialog_input[0] = 0;
                        dialog_length = 0;
                    } else if (item == 3) {
                        if (!activate_window_kind(5, "Settings")) {
                            create_window("Settings", 190, 40, 640, 500, 5);
                        }
                        menu = 0;
                    } else if (item == 5) {
                        open_help_document();
                    } else if (item == 7) {
                        syscall(KOS_SYS_SHUTDOWN, 0, 0, 0, 0);
                        return 0;
                    } else if (item == 8) {
                        gui_shutdown();
                        return 0;
                    } else if (item >= 0 && item <= 5) {
                        toast("Command started");
                    } else {
                        menu = 0;
                    }
                    changed = 1;
                } else if (
                    menu == 2 && mouse.x >= 68 && mouse.x < 256 && mouse.y >= start_y + 34
                    && mouse.y < start_y + 220) {
                    int item = (mouse.y - start_y - 34) / 28;
                    if (item >= 0 && item <= 4) {
                        program_category = item;
                        menu = 6;
                    } else {
                        menu = 1;
                    }
                    changed = 1;
                } else if (
                    menu == 6 && mouse.x >= 68 && mouse.x < 286 && mouse.y >= start_y + 34
                    && mouse.y < start_y + start_menu_height() - 4) {
                    int item = (mouse.y - start_y - 34) / 28;
                    if (item >= 0 && item < program_count(program_category)) {
                        launch_category_item(program_category, item);
                        menu = 0;
                    } else {
                        menu = 2; /* "Back" */
                    }
                    changed = 1;
                } else if (mouse.y >= task_y) {
                    int start_x, pinned_count, running_count;
                    dock_layout(&start_x, &pinned_count, &running_count);
                    if (mouse.x >= start_x + DOCK_SLOT && mouse.x < start_x + pinned_count * DOCK_SLOT) {
                        dock_launch_index((mouse.x - start_x) / DOCK_SLOT - 1);
                        changed = 1;
                    }
                    int button_x = start_x + pinned_count * DOCK_SLOT + 5;
                    int drawn = 0;
                    for (int i = 0; i < MAX_WINDOWS && drawn < running_count; ++i) {
                        int index = zorder[i];
                        if (!zvalid(index) || !windows[index].used) {
                            continue;
                        }
                        gui_window* w = &windows[index];
                        if (mouse.x >= button_x && mouse.x < button_x + DOCK_TILE) {
                            w->minimized = !w->minimized;
                            if (!w->minimized) {
                                raise_window(index);
                            } else if (focused == index) {
                                focused = -1;
                            }
                            break;
                        }
                        button_x += DOCK_SLOT;
                        ++drawn;
                    }
                    changed = 1;
                } else {
                    kos_gui_window_info external[8];
                    int external_count = gfx_window_list(external, 8);
                    int external_target = -1, external_index = -1;
                    for (int i = 0; i < external_count; ++i) {
                        if (external[i].minimized) {
                            continue;
                        }
                        if (mouse.x >= external[i].x && mouse.x < external[i].x + external[i].width
                            && mouse.y >= external[i].y && mouse.y < external[i].y + external[i].height) {
                            external_target = external[i].id;
                            external_index = i;
                            break;
                        }
                    }
                    if (external_target > 0) {
                        gfx_window_raise(external_target);
                        external_focus = external_target;
                        focused = -1;
                        kos_gui_event event = { KOS_GUI_EV_PRESS,
                                                mouse.x - external[external_index].x,
                                                mouse.y - external[external_index].y,
                                                mouse.buttons & 7,
                                                0 };
                        gfx_window_post(external_target, &event);
                        changed = 1;
                    } else {
                        int target = window_at(mouse.x, mouse.y);
                        if (target >= 0) {
                            gui_window* w = &windows[target];
                            int lx = mouse.x - w->x, ly = mouse.y - w->y;
                            if (ly >= TITLE_BUTTON_Y && ly < TITLE_BUTTON_Y + TITLE_BUTTON_SIZE
                                && lx >= TITLE_CLOSE_X && lx < TITLE_ZOOM_X + TITLE_BUTTON_SIZE) {
                                if (lx < TITLE_CLOSE_X + TITLE_BUTTON_SIZE) {
                                    close_window(w);
                                } else if (lx < TITLE_MIN_X + TITLE_BUTTON_SIZE) {
                                    w->minimized = 1;
                                    if (focused == target) {
                                        focused = -1;
                                    }
                                } else {
                                    toggle_maximize(w);
                                }
                                changed = 1;
                            } else if (lx >= w->width - 14 && ly >= w->height - 14) {
                                raise_window(target);
                                resizing = 1;
                            } else if (ly < TITLE_HEIGHT) {
                                int wi = (int)(w - windows);
                                int double_click = wi == last_title_click && gui_frame - last_title_tick < 40;
                                last_title_click = wi;
                                last_title_tick = gui_frame;
                                raise_window(target);
                                if (double_click) {
                                    toggle_maximize(w);
                                } else {
                                    cursor_erase();
                                    dragging = 1;
                                    drag_dx = lx;
                                    drag_dy = ly;
                                    drag_begin(w);
                                }
                                changed = 1;
                            } else {
                                raise_window(target);
                                int content_y = window_content_y();
                                int status_y = w->height - STATUS_HEIGHT;
                                if (mouse.wheel) {
                                    if (w->kind == 0) {
                                        w->top += mouse.wheel > 0 ? -1 : 1;
                                        if (w->top < 0) {
                                            w->top = 0;
                                        }
                                    } else if (w->kind == 2) {
                                        w->out_top += mouse.wheel > 0 ? -1 : 1;
                                        if (w->out_top < 0) {
                                            w->out_top = 0;
                                        }
                                    }
                                }
                                if (w->kind == 9) {
                                    int bar_y = content_y + 4;
                                    int list_y = bar_y + 30;
                                    if (ly >= bar_y && ly < bar_y + 22) {
                                        if (lx >= 14 && lx < 130) {
                                            w->sys_tab = 0;
                                            w->sys_selected = 0;
                                        } else if (lx >= 136 && lx < 240) {
                                            w->sys_tab = 1;
                                            w->sys_selected = 0;
                                        } else if (lx >= w->width - 96 && lx < w->width - 18) {
                                            system_action(w, 3);
                                        }
                                    } else if (ly >= status_y - 32 && ly < status_y - 8) {
                                        if (w->sys_tab == 0 && lx >= 14 && lx < 110) {
                                            system_action(w, 0);
                                        } else if (w->sys_tab == 1 && lx >= 14 && lx < 86) {
                                            system_action(w, 1);
                                        } else if (w->sys_tab == 1 && lx >= 92 && lx < 164) {
                                            system_action(w, 2);
                                        }
                                    } else if (ly >= list_y + 18) {
                                        int row = (ly - list_y - 18) / 18;
                                        int limit = w->sys_tab == 0 ? sys_process_count : SYS_SERVICE_COUNT;
                                        if (row >= 0 && row < limit) {
                                            w->sys_selected = row;
                                        }
                                    }
                                } else if (w->kind == 5) {
                                    settings_click(w, lx, ly);
                                    changed = 1;
                                } else if (w->kind == 0) {
                                    int address_y = content_y + 29;
                                    int list_y = address_y + 28;
                                    int list_h = status_y - list_y - 6;
                                    int rows_y = list_y + 20;
                                    int shelf_x = w->width - 124;
                                    int list_right = shelf_x - 8;
                                    if (ly >= content_y + 3 && ly < content_y + 23) {
                                        if (lx >= 10 && lx < 62) {
                                            fm_up(w); /* Back */
                                        } else if (lx >= 66 && lx < 110) {
                                            fm_up(w); /* Up */
                                        } else if (lx >= 114 && lx < 166) {
                                            fm_action = 1;
                                            dialog_input[0] = 0;
                                            dialog_length = 0;
                                            menu = 7;
                                        } else if (lx >= 170 && lx < 222) {
                                            toast("Explorer view");
                                        } else if (lx >= 226 && lx < 278) {
                                            text_copy(w->path, sizeof(w->path), "/home/user");
                                            w->selected = w->top = 0;
                                            fm_refresh(w);
                                        }
                                    } else if (lx >= shelf_x && ly >= list_y && ly < status_y - 6) {
                                        int item = (ly - list_y - 22) / 30;
                                        if (item >= 0 && item < w->shelf_count) {
                                            w->shelf_selected = item;
                                            text_copy(w->path, sizeof(w->path), w->shelf[item]);
                                            w->selected = w->top = 0;
                                            fm_refresh(w);
                                        }
                                    } else if (ly >= list_y && ly < status_y - 6) {
                                        int index = w->top + (ly - rows_y) / 20;
                                        if (lx < list_right - 8 && index >= 0 && index < w->entry_count) {
                                            int same = index == last_click_index
                                                && (int)(w - windows) == last_click_window
                                                && gui_frame - last_click_tick < 40;
                                            w->selected = index;
                                            last_click_index = index;
                                            last_click_window = (int)(w - windows);
                                            last_click_tick = gui_frame;
                                            if (same) {
                                                fm_open(w);
                                            }
                                        } else if (lx >= list_right - 8 && lx < list_right + 4) {
                                            int visible = (list_h - 18) / 20;
                                            int max_top =
                                                w->entry_count > visible ? w->entry_count - visible : 0;
                                            int rel = ly - rows_y;
                                            if (rel < 0) {
                                                w->top -= visible;
                                            } else if (rel >= list_h - 28) {
                                                w->top += visible;
                                            } else if (max_top && list_h > 28) {
                                                w->top = rel * max_top / (list_h - 28);
                                            }
                                            if (w->top < 0) {
                                                w->top = 0;
                                            }
                                            if (w->top > max_top) {
                                                w->top = max_top;
                                            }
                                        }
                                    }
                                } else if (w->kind == 1 && w->canvas) {
                                    int canvas_y = content_y + 28;
                                    if (ly >= content_y && ly < content_y + 20) {
                                        const u32 palette[] = { GFX_BLACK,
                                                                GFX_RED,
                                                                GFX_RGB(0, 128, 0),
                                                                gui_theme.title,
                                                                GFX_RGB(255, 255, 0),
                                                                GFX_WHITE };
                                        if (lx >= 10 && lx < 76) {
                                            w->tool = 0;
                                        } else if (lx >= 82 && lx < 148) {
                                            w->tool = 1;
                                        } else if (lx >= 154 && lx < 220) {
                                            gfx_clear(w->canvas, GFX_WHITE);
                                        } else {
                                            for (int i = 0; i < 6; ++i) {
                                                if (lx >= 228 + i * 22 && lx < 248 + i * 22) {
                                                    w->color = palette[i];
                                                }
                                            }
                                        }
                                        changed = 1;
                                    }
                                    int cx = lx - 10, cy = ly - canvas_y - 2;
                                    if (cx >= 0 && cy >= 0 && cx < w->canvas->width
                                        && cy < w->canvas->height) {
                                        int size = w->tool == 1 ? 6 : 3;
                                        gfx_fill(
                                            w->canvas,
                                            cx - size / 2,
                                            cy - size / 2,
                                            size,
                                            size,
                                            w->tool == 1 ? GFX_WHITE : w->color);
                                        w->last_paint_x = cx;
                                        w->last_paint_y = cy;
                                        w->drawing = 1;
                                    }
                                } else if (w->kind == 4) {
                                    if (mouse.wheel) {
                                        w->text_top -= mouse.wheel > 0 ? 2 : -2;
                                        if (w->text_top < 0) {
                                            w->text_top = 0;
                                        }
                                    }
                                }
                                changed = 1;
                            }
                        } else if (menu) {
                            menu = 0;
                            changed = 1;
                        }
                    }
                }
            }
        }

        /* Keyboard handling must run every frame, not only on mouse clicks. */
        if (focused_window && focused_window->drawing && (mouse.buttons & 1) && focused_window->kind == 1
            && focused_window->canvas) {
            int cx = mouse.x - focused_window->x - 10;
            int cy = mouse.y - focused_window->y - (window_content_y() + 28) - 2;
            if (cx >= 0 && cy >= 0 && cx < focused_window->canvas->width
                && cy < focused_window->canvas->height) {
                int size = focused_window->tool == 1 ? 6 : 3;
                u32 color = focused_window->tool == 1 ? GFX_WHITE : focused_window->color;
                /* Interpolate so fast pointer moves leave a continuous stroke. */
                gfx_line(
                    focused_window->canvas,
                    focused_window->last_paint_x,
                    focused_window->last_paint_y,
                    cx,
                    cy,
                    size,
                    color);
                focused_window->last_paint_x = cx;
                focused_window->last_paint_y = cy;
                paint_window = focused_window;
                paint_dirty = 1;
            }
        } else if (!(mouse.buttons & 1)) {
            if (focused_window) {
                focused_window->drawing = 0;
            }
        }

        if (key) {
            int menu_at_key = menu;
            changed = 1;
            focused_window = focused >= 0 ? &windows[focused] : 0;
            /* With no text window focused the terminal still owns typing, so
             * the Start shortcut never swallows ordinary characters. */
            if (!menu
                && !(
                    focused_window && focused_window->used
                    && (focused_window->kind == 2 || focused_window->kind == 4 || focused_window->kind == 6
                        || focused_window->kind == 8 || focused_window->kind == 9))) {
                for (int i = 0; i < MAX_WINDOWS; ++i) {
                    if (windows[i].used && windows[i].kind == 2 && !windows[i].minimized) {
                        raise_window(i);
                        focused_window = &windows[i];
                        break;
                    }
                }
            }
            int typing = focused_window && focused_window->used
                && (focused_window->kind == 2 || focused_window->kind == 4 || focused_window->kind == 6
                    || focused_window->kind == 8 || focused_window->kind == 9);
            if (key == 27) {
                menu = 0;
                global_menu_column = -1;
                context_open = 0;
            } else if (key == 9 && !(focused_window && focused_window->kind == 6)) {
                for (int step = 1; step <= MAX_WINDOWS; ++step) {
                    int candidate = (focused + step + MAX_WINDOWS) % MAX_WINDOWS;
                    if (windows[candidate].used && !windows[candidate].minimized) {
                        raise_window(candidate);
                        break;
                    }
                }
                changed = 1;
            } else if ((key == 's' || key == 'S') && !typing) {
                menu = menu == 1 ? 0 : 1;
                changed = 1;
            } else if (menu == 1 && key >= '1' && key <= '9') {
                int item = key - '1';
                if (item == 0) {
                    menu = 2;
                } else if (item == 1 || item == 4) {
                    menu = 3;
                    dialog_input[0] = 0;
                    dialog_length = 0;
                    search_count = 0;
                } else if (item == 2) {
                    menu = 4;
                    dialog_input[0] = 0;
                    dialog_length = 0;
                } else if (item == 3) {
                    if (!activate_window_kind(5, "Settings")) {
                        create_window("Settings", 190, 40, 640, 500, 5);
                    }
                    menu = 0;
                } else if (item == 5) {
                    open_help_document();
                } else if (item == 7) {
                    syscall(KOS_SYS_SHUTDOWN, 0, 0, 0, 0);
                    return 0;
                } else if (item == 8) {
                    gui_shutdown();
                    return 0;
                } else {
                    menu = 0;
                }
                changed = 1;
            } else if (menu == 2 && key >= '1' && key <= '6') {
                int category = key - '1';
                if (category < 5) {
                    program_category = category;
                    menu = 6;
                } else {
                    menu = 1;
                }
                changed = 1;
            } else if (menu == 6 && key >= '1' && key <= '9') {
                int item = key - '1';
                if (item < program_count(program_category)) {
                    launch_category_item(program_category, item);
                    menu = 0;
                } else if (item == program_count(program_category)) {
                    menu = 2; /* Back */
                }
                changed = 1;
            } else if ((menu == 2 || menu == 6) && (key == '0' || key == 27)) {
                menu = menu == 6 ? 2 : 1;
                changed = 1;
            } else if ((key == 'q' || key == 'Q') && !typing && menu == 0) {
                gui_shutdown();
                return 0;
            }
            /* A menu shortcut must not also be typed into the window it opens. */
            if (menu_at_key) {
                key = 0;
            }
            /* Documented window shortcuts (only when no text field owns input). */
            if (key && !typing && menu == 0 && focused_window && focused_window->used) {
                gui_window* fw = focused_window;
                if (keyboard_resize && !fw->maximized) {
                    if (key == 256) {
                        fw->height -= 16;
                        key = 0;
                    } else if (key == 257) {
                        fw->height += 16;
                        key = 0;
                    } else if (key == 258) {
                        fw->width -= 16;
                        key = 0;
                    } else if (key == 259) {
                        fw->width += 16;
                        key = 0;
                    } else if (key == 'r') {
                        keyboard_resize = 0;
                        key = 0;
                    }
                    if (fw->width < 200) {
                        fw->width = 200;
                    }
                    if (fw->height < 160) {
                        fw->height = 160;
                    }
                    if (fw->x + fw->width > screen_width) {
                        fw->width = screen_width - fw->x;
                    }
                    if (fw->y + fw->height > screen_height - TASKBAR_HEIGHT) {
                        fw->height = screen_height - TASKBAR_HEIGHT - fw->y;
                    }
                    window_surface_rebuilt(fw);
                    changed = 1;
                } else if (!fw->maximized) {
                    if (key == 'w') {
                        fw->y -= 8;
                        gui_keyboard_move = 1;
                        changed = 1;
                        key = 0;
                    } else if (key == 'x') {
                        fw->y += 8;
                        gui_keyboard_move = 1;
                        changed = 1;
                        key = 0;
                    } else if (key == 'a') {
                        fw->x -= 8;
                        gui_keyboard_move = 1;
                        changed = 1;
                        key = 0;
                    } else if (key == 'd') {
                        fw->x += 8;
                        gui_keyboard_move = 1;
                        changed = 1;
                        key = 0;
                    } else if (key == 'n') {
                        fw->minimized = 1;
                        focused = -1;
                        changed = 1;
                        key = 0;
                    } else if (key == 'm') {
                        toggle_maximize(fw);
                        changed = 1;
                        key = 0;
                    } else if (key == 'r') {
                        keyboard_resize = 1;
                        toast("Resize: arrows, R to finish");
                        key = 0;
                    }
                    if (fw->x < 0) {
                        fw->x = 0;
                    }
                    if (fw->y < MENUBAR_HEIGHT + 2) {
                        fw->y = MENUBAR_HEIGHT + 2;
                    }
                    if (fw->x + fw->width > screen_width) {
                        fw->x = screen_width - fw->width;
                    }
                    if (fw->y + fw->height > screen_height - TASKBAR_HEIGHT) {
                        fw->y = screen_height - TASKBAR_HEIGHT - fw->height;
                    }
                } else if (key == 'm') {
                    toggle_maximize(fw);
                    changed = 1;
                    key = 0;
                }
            }
            focused_window = focused >= 0 ? &windows[focused] : 0;
            if (focused_window && focused_window->used && focused_window->kind == 9) {
                int limit = focused_window->sys_tab == 0 ? sys_process_count : SYS_SERVICE_COUNT;
                if (key == 256) {
                    if (focused_window->sys_selected > 0) {
                        --focused_window->sys_selected;
                    }
                    changed = 1;
                } else if (key == 257) {
                    if (focused_window->sys_selected + 1 < limit) {
                        ++focused_window->sys_selected;
                    }
                    changed = 1;
                } else if (key == '\n' || key == 'k') {
                    system_action(focused_window, focused_window->sys_tab == 0 ? 0 : 1);
                    changed = 1;
                } else if (key == 'f' || key == 'r') {
                    system_action(focused_window, 3);
                    changed = 1;
                } else if (key == '\t' || key == 'p' || key == 's') {
                    focused_window->sys_tab = focused_window->sys_tab ? 0 : 1;
                    focused_window->sys_selected = 0;
                    changed = 1;
                }
            } else if (focused_window && focused_window->used && focused_window->kind == 2) {
                char encoded[4];
                unsigned added = text_key(key, encoded);
                if (added && focused_window->input_length + (int)added < 120) {
                    for (unsigned i = 0; i < added; ++i) {
                        focused_window->input[focused_window->input_length++] = encoded[i];
                    }
                    focused_window->input[focused_window->input_length] = 0;
                    changed = 1;
                } else if (key == 8 && focused_window->input_length) {
                    focused_window->input[--focused_window->input_length] = 0;
                    changed = 1;
                } else if (key == 256 && focused_window->history_pos > 0) {
                    --focused_window->history_pos;
                    text_copy(
                        focused_window->input,
                        sizeof(focused_window->input),
                        focused_window->history[focused_window->history_pos]);
                    focused_window->input_length = text_length(focused_window->input);
                    changed = 1;
                } else if (key == 257 && focused_window->history_pos < focused_window->history_count) {
                    ++focused_window->history_pos;
                    if (focused_window->history_pos == focused_window->history_count) {
                        focused_window->input[0] = 0;
                        focused_window->input_length = 0;
                    } else {
                        text_copy(
                            focused_window->input,
                            sizeof(focused_window->input),
                            focused_window->history[focused_window->history_pos]);
                        focused_window->input_length = text_length(focused_window->input);
                    }
                    changed = 1;
                } else if (key == '\n') {
                    term_execute(focused_window);
                    focused_window->input[0] = 0;
                    focused_window->input_length = 0;
                    changed = 1;
                }
            } else if (focused_window && focused_window->used && focused_window->kind == 4) {
                if (key == 19 || key == 1) { /* Ctrl+S / Ctrl+A: save and save as */
                    if (key == 1 || !focused_window->file_path[0]) {
                        open_save_dialog(focused_window);
                    } else {
                        notepad_save(focused_window, focused_window->file_path);
                    }
                    key = 0;
                    changed = 1;
                } else if (key == 256 || key == 265) { /* Up / Page Up */
                    focused_window->text_top -= key == 265 ? 4 : 1;
                    if (focused_window->text_top < 0) {
                        focused_window->text_top = 0;
                    }
                    changed = 1;
                } else if (key == 257 || key == 266) { /* Down / Page Down */
                    focused_window->text_top += key == 266 ? 4 : 1;
                    changed = 1;
                } else {
                    char encoded[4];
                    unsigned added = text_key(key, encoded);
                    if (added && focused_window->text_length + (int)added < NOTE_CAPACITY - 1) {
                        for (unsigned i = 0; i < added; ++i) {
                            focused_window->text[focused_window->text_length++] = encoded[i];
                        }
                        focused_window->text[focused_window->text_length] = 0;
                        changed = 1;
                    } else if (key == 8 && focused_window->text_length) {
                        focused_window->text[--focused_window->text_length] = 0;
                        changed = 1;
                    } else if (key == '\n' && focused_window->text_length < NOTE_CAPACITY - 2) {
                        focused_window->text[focused_window->text_length++] = '\n';
                        focused_window->text[focused_window->text_length] = 0;
                        changed = 1;
                    }
                    if (changed) {
                        focused_window->text_top = 1000000; /* clamped to the last line by note_draw */
                    }
                }
            } else if (focused_window && focused_window->used && focused_window->kind == 0) {
                if (key == 256 || key == 'w') {
                    focused_window->selected -= focused_window->selected > 0;
                    changed = 1;
                } else if (key == 257 || key == 'x') {
                    if (focused_window->selected + 1 < focused_window->entry_count) {
                        ++focused_window->selected;
                    }
                    changed = 1;
                } else if (key == '\n' || key == ' ') {
                    fm_open(focused_window);
                    changed = 1;
                } else if (key == 258 || key == 'a') {
                    fm_up(focused_window);
                    changed = 1;
                }
            } else if (focused_window && focused_window->used && focused_window->kind == 6) {
                char encoded[4];
                unsigned added = text_key(key, encoded);
                char* field = focused_window->admin_field ? focused_window->admin_command
                                                          : focused_window->admin_password;
                int length = focused_window->admin_field ? text_length(focused_window->admin_command)
                                                         : text_length(focused_window->admin_password);
                if (added && length + (int)added < 120) {
                    for (unsigned i = 0; i < added; ++i) {
                        field[length++] = encoded[i];
                    }
                    field[length] = 0;
                    changed = 1;
                } else if (key == 8 && length) {
                    field[--length] = 0;
                    changed = 1;
                } else if (key == '\t') {
                    focused_window->admin_field = !focused_window->admin_field;
                    changed = 1;
                } else if (key == '\n') {
                    if (!syscall(KOS_SYS_AUTH, (u32) "root", (u32)focused_window->admin_password, 0, 0)) {
                        if (syscall(KOS_SYS_ELEVATE, (u32) "root", (u32)focused_window->admin_password, 0, 0)
                            == 0) {
                            /* Launch the requested program with one-shot root. */
                            char path[160];
                            if (focused_window->admin_command[0] == '/') {
                                text_copy(path, sizeof(path), focused_window->admin_command);
                            } else {
                                text_copy(path, sizeof(path), "/bin/");
                                int n = text_length(path), i = 0;
                                while (focused_window->admin_command[i] && n + i + 4 < (int)sizeof(path)) {
                                    path[n + i] = focused_window->admin_command[i];
                                    ++i;
                                }
                                path[n + i] = '.';
                                path[n + i + 1] = 'E';
                                path[n + i + 2] = 'L';
                                path[n + i + 3] = 'F';
                                path[n + i + 4] = 0;
                            }
                            char* args[] = { focused_window->admin_command, 0 };
                            int pid = spawn(path, 1, args);
                            if (pid >= 0) {
                                wait(pid);
                            }
                            syscall(KOS_SYS_CANCEL_ELEVATE, 0, 0, 0, 0);
                            text_copy(
                                focused_window->admin_output,
                                sizeof(focused_window->admin_output),
                                "launched");
                        } else {
                            text_copy(
                                focused_window->admin_output,
                                sizeof(focused_window->admin_output),
                                "elevation failed");
                        }
                    } else {
                        text_copy(
                            focused_window->admin_output,
                            sizeof(focused_window->admin_output),
                            "wrong password");
                    }
                    changed = 1;
                }
            } else if (focused_window && focused_window->used && focused_window->kind == 1) {
                if (key == '1') {
                    focused_window->tool = 0;
                    changed = 1;
                } else if (key == '2') {
                    focused_window->tool = 1;
                    changed = 1;
                } else if (key == 'c' && focused_window->canvas) {
                    gfx_clear(focused_window->canvas, GFX_WHITE);
                    changed = 1;
                }
            }
        }

        /* Dock magnification is an animation: keep the pump alive while it
         * grows or retracts, and cleanly retract it under modal overlays. */
        if (dock_tick()) {
            changed = 1;
        }

        int external_only = 0;
        int animating = 0;
        for (int i = 0; i < MAX_WINDOWS; ++i) {
            if (windows[i].used && (windows[i].anim_open || windows[i].anim_close)) {
                animating = 1;
                break;
            }
        }
        if (animating) {
            /* Keep open/close animations alive frame by frame. */
            changed = 1;
        }
        gui_window* live_window = 0;
        {
            /* External clients change their windows through the kernel WM;
             * only recompose when their generation actually changes. */
            static u32 last_external_gen;
            u32 gen = (u32)syscall(KOS_SYS_GFX_WINDOW_GEN, 0, 0, 0, 0);
            if (gen != last_external_gen) {
                last_external_gen = gen;
                if (!changed) {
                    external_only = 1;
                }
                changed = 1;
            }
            /* Keep live monitors fresh (five updates per second). */
            if (gui_frame % POLL_INTERVAL == 0) {
                for (int i = 0; i < MAX_WINDOWS; ++i) {
                    if (windows[i].used && !windows[i].minimized
                        && (windows[i].kind == 8 || windows[i].kind == 9)) {
                        if (!changed) {
                            live_window = &windows[i];
                        }
                        changed = 1;
                        break;
                    }
                }
            }
        }
        if (paint_window && paint_dirty && !changed && !overlay_prev.valid && menu == 0 && !context_open
            && global_menu_column < 0) {
            /* Painting only touches one window surface: repaint that
             * rectangle instead of the whole 1280x800 frame. */
            gui_mouse = mouse;
            gfx_dirty partial = { 0, 0, 0, 0, 0 };
            gfx_dirty_add(
                &partial, paint_window->x, paint_window->y, paint_window->width, paint_window->height);
            cursor_erase();
            draw_window(paint_window);
            gfx_blit(
                desktop,
                paint_window->x,
                paint_window->y,
                paint_window->surface,
                0,
                0,
                paint_window->width,
                paint_window->height);
            cursor_move(gui_mouse.x, gui_mouse.y, &partial);
        } else if (changed) {
            gui_mouse = mouse;
            cursor_erase();
            gfx_dirty frame = { 0, 0, 0, 0, 0 };
            if (external_only) {
                kos_gui_window_info external[8];
                int count = gfx_window_list(external, 8);
                for (int i = 0; i < count; ++i) {
                    if (!external[i].minimized) {
                        gfx_dirty_add(
                            &frame, external[i].x, external[i].y, external[i].width, external[i].height);
                    }
                }
                if (!frame.valid) {
                    dirty_full(&frame);
                }
            } else if (live_window) {
                dirty_window(&frame, live_window);
                gfx_dirty_add(&frame, 0, screen_height - TASKBAR_HEIGHT, screen_width, TASKBAR_HEIGHT);
            } else if (
                key && !menu && !context_open && focused_window && focused_window->used && !gui_keyboard_move
                && zorder[MAX_WINDOWS - 1] == (int)(focused_window - windows)
                && (focused_window->kind == 2 || focused_window->kind == 4 || focused_window->kind == 6
                    || focused_window->kind == 8 || focused_window->kind == 9)) {
                dirty_window(&frame, focused_window);
                gfx_dirty_add(&frame, 0, screen_height - TASKBAR_HEIGHT, screen_width, TASKBAR_HEIGHT);
            } else if (
                focused_window && focused_window->used && !focused_window->minimized && !dragging && !resizing
                && !keyboard_resize && !gui_keyboard_move && !menu && !context_open
                && zorder[MAX_WINDOWS - 1] == (int)(focused_window - windows)
                && (focused_window->kind == 2 || focused_window->kind == 4 || focused_window->kind == 6
                    || focused_window->kind == 8 || focused_window->kind == 9)) {
                /* Mouse-driven content changes (scroll, list moves) repaint the
                 * focused document window instead of the whole frame. */
                dirty_window(&frame, focused_window);
                gfx_dirty_add(&frame, 0, screen_height - TASKBAR_HEIGHT, screen_width, TASKBAR_HEIGHT);
            } else {
                dirty_full(&frame);
            }
            /* An overlay (Start, dialog, menu dropdown, context menu) paints
             * above every window. Both its last rectangle and its new one must
             * be repainted: otherwise closing or moving it leaves fragments. */
            if (overlay_prev.valid) {
                gfx_dirty_add(
                    &frame,
                    overlay_prev.left,
                    overlay_prev.top,
                    overlay_prev.right - overlay_prev.left,
                    overlay_prev.bottom - overlay_prev.top);
            }
            gfx_dirty overlay_now = { 0, 0, 0, 0, 0 };
            overlay_bounds(&overlay_now);
            if (overlay_now.valid) {
                gfx_dirty_add(
                    &frame,
                    overlay_now.left,
                    overlay_now.top,
                    overlay_now.right - overlay_now.left,
                    overlay_now.bottom - overlay_now.top);
            }
            if (dock_mag > 0) {
                /* A magnified tile grows a few pixels above the dock strip. */
                gfx_dirty_add(
                    &frame, 0, screen_height - TASKBAR_HEIGHT - 8, screen_width, TASKBAR_HEIGHT + 8);
            }
            gui_keyboard_move = 0;
            for (int i = 0; i < MAX_WINDOWS; ++i) {
                if (windows[i].used && !windows[i].minimized
                    && dirty_rect_hit(
                        &frame, windows[i].x, windows[i].y, windows[i].width, windows[i].height)) {
                    draw_window(&windows[i]);
                }
            }
            compose(&frame);
            cursor_move(gui_mouse.x, gui_mouse.y, 0);
        }
        paint_window = 0;
        paint_dirty = 0;
        syscall(KOS_SYS_SLEEP, 10, 0, 0, 0);
    }
}
