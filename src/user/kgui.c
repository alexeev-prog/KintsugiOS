#include "../shared/text.h"
#include "authabi.h"
#include "errors.h"
#include "gfx.h"
#include "sysabi.h"
#include "tuiabi.h"

/* KGUI - pixel window manager, Win95-like desktop and three graphic apps:
 * KGFM (files), KGED (editor), KGTERM (terminal). */

#define MAX_WINDOWS 12
#define TASKBAR_HEIGHT 36
#define TITLE_HEIGHT 20
#define MENU_HEIGHT 18
#define STATUS_HEIGHT 20
#define MAX_RESULTS 24
#define TRAY_WIDTH 240
/* Title-bar buttons, window-relative. Keep these in sync with gfx_titlebar(),
 * which draws at (2,2) with width (w->width - 4). */
#define TITLE_BUTTON_W 14
#define TITLE_BUTTON_Y 5
#define TITLE_CLOSE_X(w) ((w)->width - 22)
#define TITLE_MAX_X(w) ((w)->width - 40)
#define TITLE_MIN_X(w) ((w)->width - 58)

static int zvalid(int index) {
    return index >= 0 && index < MAX_WINDOWS;
}

typedef struct {
    int used, x, y, width, height;
    int saved_x, saved_y, saved_width, saved_height;
    int minimized, maximized;
    char title[48];
    gfx_surface* surface;
    int kind; /* 0 KGFM, 1 KGED, 2 KGTERM, 3 About, 4 Notepad */
    /* KGFM */
    char path[160];
    kos_long_dirent entries[80];
    int entry_count, selected, top;
    /* KGED */
    gfx_surface* canvas;
    int tool;
    u32 color;
    int drawing, last_paint_x, last_paint_y;
    /* KGTERM - in-window shell */
    char output[48][120];
    int out_count, out_top;
    char input[128];
    int input_length;
    char history[16][120];
    int history_count, history_pos;
    /* Notepad */
    char text[512];
    int text_length;
    char file_path[160];
    /* Administration window */
    int admin_field;
    char admin_password[64];
    char admin_command[128];
    char admin_output[256];
} gui_window;

static gui_window windows[MAX_WINDOWS];
static int zorder[MAX_WINDOWS] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static int window_count;
static u32 desktop_color = GFX_TEAL;
static int gui_language; /* 0 English, 1 Russian */
static char gui_toast[64];
static int gui_toast_until;
static char settings_users[8][32];
static int settings_user_count;
static int context_open, context_x, context_y, context_window, fm_action, context_kind;
static char context_items[10][32];
static int context_count;
static int active_menu_column;
static gfx_surface* desktop;
static int screen_width, screen_height;
static int focused = -1;
static int menu; /* 0 none, 1 Start, 2 Programs, 3 Search, 4 Run, 5 Help */
static char dialog_input[128];
static int dialog_length;
static char search_results[MAX_RESULTS][160];
static int search_count;
static int dragging, resizing, drag_dx, drag_dy;
static int active_menu_window = -1;
static int dialog_window_index = -1;
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
static gfx_surface* drag_backup;
static int drag_index = -1;

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
static gui_window* create_window(const char* title, int x, int y, int width, int height, int kind);
static void term_prompt_string(gui_window* w, char* out, u32 cap);
static void term_shell_banner(gui_window* w);
static void draw_window(gui_window* w);
static void toggle_maximize(gui_window* w);

static int start_menu_height(void) {
    return menu == 2 ? 220 : menu == 6 ? 260 : 240;
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

static void term_print_row(gui_window* w, const char* line) {
    if (w->out_count < 48) {
        text_copy(w->output[w->out_count], sizeof(w->output[0]), line);
        ++w->out_count;
    } else {
        for (int i = 1; i < 48; ++i) {
            text_copy(w->output[i - 1], sizeof(w->output[0]), w->output[i]);
        }
        text_copy(w->output[47], sizeof(w->output[0]), line);
    }
}

static void term_print(gui_window* w, const char* line) {
    /* Wrap to the visible width: gfx_text would otherwise draw past the
     * window border and silently clip the rest of the line. */
    int limit = (w->width - 28) / 12;
    if (limit < 8) {
        limit = 8;
    }
    if (limit > 100) {
        limit = 100;
    }
    if (!*line) {
        term_print_row(w, "");
        return;
    }
    while (*line) {
        char part[120];
        int n = 0;
        while (line[n] && n < limit && n + 1 < (int)sizeof(part)) {
            ++n;
        }
        for (int i = 0; i < n; ++i) {
            part[i] = line[i];
        }
        part[n] = 0;
        term_print_row(w, part);
        line += n;
    }
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

static void term_run_program(gui_window* w, const char* name, char** argv, int argc) {
    char path[160];
    build_bin_path(name, path, sizeof(path));
    int capture = open("/tmp/kgui-term.out", KOS_O_WRITE | KOS_O_CREATE | KOS_O_TRUNC);
    if (capture < 0) {
        term_print(w, "terminal: cannot capture output");
        return;
    }
    dup2(capture, 1);
    int pid = spawn(path, argc, argv);
    if (pid >= 0) {
        int status = wait(pid);
        if (status) {
            char text[96];
            text_copy(text, sizeof(text), "exit status ");
            int n = text_length(text);
            unsigned v = (unsigned)status;
            char digits[11];
            int d = 10;
            digits[d] = 0;
            do {
                digits[--d] = '0' + v % 10;
                v /= 10;
            } while (v);
            while (d < 11 && digits[d] && n + 1 < (int)sizeof(text)) {
                text[n++] = digits[d++];
            }
            text[n] = 0;
            term_print(w, text);
        }
    } else {
        term_print(w, kos_error(pid));
    }
    int tty = open("/dev/tty", KOS_O_WRITE);
    if (tty >= 0) {
        dup2(tty, 1);
        close(tty);
    }
    close(capture);
    char output[2048];
    int size = syscall(2, (u32) "/tmp/kgui-term.out", 0, (u32)output, sizeof(output) - 1);
    if (size > 0) {
        output[size] = 0;
        int start = 0;
        for (int i = 0; i <= size; ++i) {
            if (i == size || output[i] == '\n') {
                output[i] = 0;
                term_print(w, output + start);
                start = i + 1;
            }
        }
    }
}

static int is_tui_name(const char* name) {
    static const char* const apps[] = { "kiwi",    "kpaint", "kuned",   "vik",      "kc",      "kterm",
                                        "kset",    "ktte",   "k2048",   "kmines",   "klife",   "kcsv",
                                        "krrawl",  "kmusa",  "klisten", "soundctl", "ktmux",   "kservices",
                                        "ksersys", "kinfo",  "kman",    "pager",    "journal", "hiero",
                                        "kbsm",    "kfetch" };
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
    syscall(KOS_SYS_TUI_ATTACH, (u32)pid, 0, 0, 0);
    tui_child_pid = pid;
    tui_window = w;
    tui_saved_x = w->x;
    tui_saved_y = w->y;
    tui_saved_w = w->width;
    tui_saved_h = w->height;
    w->x = 2;
    w->y = 2;
    w->width = screen_width - 4;
    w->height = screen_height - TASKBAR_HEIGHT - 4;
    gfx_surface_destroy(w->surface);
    w->surface = gfx_surface_create(w->width, w->height);
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
        gfx_surface_destroy(w->surface);
        w->surface = gfx_surface_create(w->width, w->height);
        term_print(w, "[TUI application finished]");
        tui_window = 0;
    }
}

static void term_shell_banner(gui_window* w) {
    w->out_count = w->out_top = 0;
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
        term_print(w, "Any other command is launched from /bin (output shown here).");
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
    if (is_tui_name(argv[0])) {
        tui_launch(w, argv[0]);
    } else {
        term_run_program(w, argv[0], argv, argc);
    }
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

static void draw_icon(gfx_surface* s, int x, int y, char letter, u32 color) {
    gfx_fill(s, x, y, 18, 18, color);
    gfx_bevel(s, x, y, 18, 18, 1);
    char label[2] = { letter, 0 };
    gfx_text(s, x + 4, y + 2, label, GFX_WHITE, 0);
}

typedef struct {
    int x, y, kind;
    char letter;
    u32 color;
    const char* label;
} desktop_icon;

static const desktop_icon desktop_icons[] = {
    { 22, 20,  0, 'F', GFX_RGB(180, 150, 40),  "My Computer" },
    { 22, 110, 1, 'T', GFX_RGB(60,  120, 180), "Terminal"    },
    { 22, 200, 2, 'E', GFX_RGB(60,  160, 90),  "Editor"      },
    { 22, 290, 3, 'R', GFX_RGB(128, 128, 128), "Trash"       }
};
#define DESKTOP_ICON_COUNT ((int)(sizeof(desktop_icons) / sizeof(desktop_icons[0])))
static int selected_icon = -1, last_icon_click = -1;
static u32 last_icon_tick;

static void draw_desktop_icon(gfx_surface* back, const desktop_icon* icon, int selected) {
    draw_icon(back, icon->x, icon->y, icon->letter, icon->color);
    if (selected) {
        gfx_bevel(back, icon->x - 2, icon->y - 2, 22, 42, 1);
    }
    int width = gfx_text_width(icon->label);
    int label_x = icon->x + 9 - width / 2;
    if (label_x < 2) {
        label_x = 2;
    }
    gfx_text(back, label_x, icon->y + 22, icon->label, GFX_WHITE, selected ? GFX_DARK : 0);
}

static void draw_window(gui_window* w) {
    gfx_surface* s = w->surface;
    int active = (w - windows) == focused;
    int menu_y = TITLE_HEIGHT + 6;
    int content_y = menu_y + MENU_HEIGHT + 4;
    int status_y = w->height - STATUS_HEIGHT;

    gfx_clear(s, GFX_FACE);
    gfx_bevel(s, 0, 0, w->width, w->height, 1);
    gfx_titlebar(s, 2, 2, w->width - 4, w->title, active);

    /* menu bar */
    gfx_fill(s, 4, menu_y, w->width - 8, MENU_HEIGHT, GFX_FACE);
    const char* menus = w->kind == 1 ? "File  Edit  View  Help"
        : w->kind == 2               ? "File  Edit  Help"
                                     : "File  View  Help";
    gfx_text(s, 10, menu_y + 2, menus, GFX_BLACK, 0);
    gfx_hline(s, 4, menu_y + MENU_HEIGHT, w->width - 8, GFX_SHADOW);

    if (w->kind == 0) {
        /* KGFM: Explorer-style toolbar, address bar, column list, status bar. */
        gfx_hline(s, 4, content_y + 25, w->width - 8, GFX_SHADOW);
        gfx_button(s, 10, content_y + 3, 52, 20, "Back", 0);
        gfx_button(s, 66, content_y + 3, 44, 20, "Up", 0);
        gfx_button(s, 114, content_y + 3, 52, 20, "New", 0);
        gfx_button(s, 170, content_y + 3, 52, 20, "View", 0);
        int address_y = content_y + 29;
        gfx_text(s, 10, address_y + 5, "Address", GFX_BLACK, 0);
        int addr_x = 82;
        gfx_fill(s, addr_x, address_y, w->width - addr_x - 26, 20, GFX_WHITE);
        gfx_bevel(s, addr_x, address_y, w->width - addr_x - 26, 20, 0);
        gfx_text(s, addr_x + 5, address_y + 3, w->path, GFX_BLACK, 0);
        gfx_button(s, w->width - 30, address_y, 18, 20, "^", 0);
        int list_y = address_y + 28;
        int list_h = status_y - list_y - 6;
        /* Column header */
        gfx_fill(s, 8, list_y, w->width - 26, 18, GFX_FACE);
        gfx_bevel(s, 8, list_y, w->width - 26, 18, 1);
        gfx_text(s, 38, list_y + 3, "Name", GFX_BLACK, 0);
        gfx_text(s, w->width - 120, list_y + 3, "Size", GFX_BLACK, 0);
        gfx_hline(s, 8, list_y + 18, w->width - 26, GFX_SHADOW);
        int rows_y = list_y + 20;
        rows_y += 0;
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
            if (index == w->selected) {
                gfx_fill(s, 10, y, w->width - 30, 19, GFX_DARK);
                gfx_text(s, 38, y + 2, entry->name, GFX_WHITE, 0);
                char size[16];
                number_text(size, sizeof(size), entry->stat.size);
                gfx_text(s, w->width - 120, y + 2, size, GFX_WHITE, 0);
            } else {
                gfx_text(s, 38, y + 2, entry->name, GFX_BLACK, 0);
                char size[16];
                number_text(size, sizeof(size), entry->stat.size);
                gfx_text(s, w->width - 120, y + 2, size, GFX_BLACK, 0);
            }
            draw_icon(
                s,
                14,
                y,
                entry->stat.directory ? 'D' : 'F',
                entry->stat.directory ? GFX_RGB(180, 150, 40) : GFX_RGB(60, 120, 180));
        }
        gfx_scrollbar(s, w->width - 18, list_y, list_h, w->top, w->entry_count ? w->entry_count : 1);
        char status[120];
        if (!w->entry_count) {
            text_copy(status, sizeof(status), "Empty folder");
        } else {
            int n = 0;
            char count[16];
            number_text(count, sizeof(count), (unsigned)w->entry_count);
            for (int i = 0; count[i]; ++i) {
                status[n++] = count[i];
            }
            const char* tail = " object(s)";
            for (int i = 0; tail[i]; ++i) {
                status[n++] = tail[i];
            }
            status[n] = 0;
        }
        gfx_text(s, 10, status_y + 2, status, GFX_BLACK, GFX_FACE);
    } else if (w->kind == 1) {
        /* KGED: toolbar + canvas */
        int y = content_y;
        gfx_button(s, 10, y, 66, 20, "Pen", w->tool == 0);
        gfx_button(s, 82, y, 66, 20, "Erase", w->tool == 1);
        gfx_button(s, 154, y, 66, 20, "Clear", 0);
        static const u32 palette[] = {
            GFX_BLACK, GFX_RED, GFX_RGB(0, 128, 0), GFX_DARK, GFX_RGB(255, 255, 0), GFX_RGB(255, 255, 255)
        };
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
        gfx_text(s, 10, status_y + 2, "KGED - Kintsugi Graphic Editor", GFX_BLACK, GFX_FACE);
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
            gfx_text(s, 10, status_y + 2, "TUI session - q/Esc to exit", GFX_BLACK, GFX_FACE);
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
                gfx_text(
                    s,
                    14,
                    content_y + 4 + i * line_height,
                    w->output[i + w->out_top],
                    GFX_RGB(120, 255, 120),
                    0);
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
                gfx_text(s, 14, py, line, GFX_RGB(200, 255, 200), 0);
                gfx_fill(s, 14 + gfx_text_width(line), py, 2, 14, GFX_RGB(200, 255, 200));
            }
            gfx_text(s, 10, status_y + 2, "KGTERM - shell", GFX_BLACK, GFX_FACE);
        }
    } else if (w->kind == 3) {
        gfx_text(s, 20, content_y + 12, "KintsugiOS", GFX_DARK, 0);
        gfx_text(s, 20, content_y + 36, "Pixel GUI - Win95 style", GFX_BLACK, 0);
        gfx_text(s, 20, content_y + 60, "Surfaces + compositor", GFX_BLACK, 0);
        gfx_text(s, 20, content_y + 84, "KGFM / KGED / KGTERM", GFX_BLACK, 0);
        gfx_text(s, 20, content_y + 108, "Drag title, resize corner", GFX_BLACK, 0);
        gfx_text(s, 10, status_y + 2, "About KGUI", GFX_BLACK, GFX_FACE);
    } else if (w->kind == 5) {
        gfx_text(s, 14, content_y + 6, "Wallpaper", GFX_DARK, 0);
        static const u32 colors[] = { GFX_TEAL,
                                      GFX_DARK,
                                      GFX_RGB(0, 96, 96),
                                      GFX_RGB(64, 64, 96),
                                      GFX_RGB(96, 64, 64),
                                      GFX_RGB(48, 96, 48),
                                      GFX_RGB(96, 96, 96) };
        for (int i = 0; i < 7; ++i) {
            gfx_fill(s, 16 + (i % 4) * 46, content_y + 26 + (i / 4) * 46, 40, 40, colors[i]);
            gfx_bevel(
                s, 16 + (i % 4) * 46, content_y + 26 + (i / 4) * 46, 40, 40, desktop_color == colors[i]);
        }
        gfx_text(s, 14, content_y + 124, "Language", GFX_DARK, 0);
        gfx_button(s, 16, content_y + 148, 88, 24, "English", !gui_language);
        gfx_button(s, 108, content_y + 148, 88, 24, "Russian", gui_language);
        gfx_text(s, 14, content_y + 184, "Accounts", GFX_DARK, 0);
        int shown_users = settings_user_count < 4 ? settings_user_count : 4;
        for (int i = 0; i < shown_users; ++i) {
            gfx_text(s, 14, content_y + 208 + i * 16, settings_users[i], GFX_BLACK, 0);
        }
        gfx_text(s, 10, status_y + 2, "Settings - wallpaper, language, accounts", GFX_BLACK, GFX_FACE);
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
        gfx_text(s, 10, status_y + 2, "kadms - Kintsugi Administration Service", GFX_BLACK, GFX_FACE);
    } else if (w->kind == 7) {
        gfx_fill(s, 8, content_y, w->width - 16, status_y - content_y - 6, GFX_WHITE);
        gfx_bevel(s, 8, content_y, w->width - 16, status_y - content_y - 6, 0);
        gfx_text(s, 20, content_y + 24, "Recycle Bin is empty.", GFX_BLACK, 0);
        gfx_text(s, 20, content_y + 48, "Deleted files would appear here.", GFX_SHADOW, 0);
        gfx_text(s, 10, status_y + 2, "Recycle Bin", GFX_BLACK, GFX_FACE);
    } else {
        gfx_fill(s, 8, content_y, w->width - 16, status_y - content_y - 6, GFX_WHITE);
        gfx_bevel(s, 8, content_y, w->width - 16, status_y - content_y - 6, 0);
        gfx_text_wrapped(s, 14, content_y + 6, w->width - 32, 15, w->text, GFX_BLACK, 0);
        if (active) {
            char info[40];
            int n = 0;
            const char* head = "chars: ";
            while (*head) {
                info[n++] = *head++;
            }
            n += number_text(info + n, sizeof(info) - n, (unsigned)w->text_length);
            info[n] = 0;
            gfx_text(s, w->width - 120, status_y + 2, info, GFX_BLACK, GFX_FACE);
        }
        gfx_text(s, 10, status_y + 2, "Notepad", GFX_BLACK, GFX_FACE);
    }
    /* Dropdown painted last so the window content cannot overwrite it. */
    if (active && active_menu_window == (w - windows)) {
        static const char* const items[4][5] = {
            { "New",        "Open",          "Save",        "Save As...",  "Close"       },
            { "Cut",        "Copy",          "Paste",       "Select All",  0             },
            { "Refresh",    "Status Bar",    "Large Icons", "Small Icons", 0             },
            { "About KGUI", "Keyboard help", 0,             0,             0             }
        };
        int count = active_menu_column == 3 ? 2 : active_menu_column == 0 ? 5 : 4;
        int width = 136, height = count * 18 + 4;
        gfx_fill(s, 8, menu_y + MENU_HEIGHT, width, height, GFX_FACE);
        gfx_bevel(s, 8, menu_y + MENU_HEIGHT, width, height, 1);
        for (int i = 0; i < count; ++i) {
            gfx_text(s, 14, menu_y + MENU_HEIGHT + 4 + i * 18, items[active_menu_column][i], GFX_BLACK, 0);
        }
    }
    gfx_hline(s, 4, status_y, w->width - 8, GFX_WHITE);
    /* resize grip */
    gfx_fill(s, w->width - 14, w->height - 14, 12, 12, GFX_FACE);
    gfx_hline(s, w->width - 12, w->height - 4, 8, GFX_SHADOW);
    gfx_hline(s, w->width - 10, w->height - 8, 6, GFX_SHADOW);
    gfx_hline(s, w->width - 8, w->height - 12, 4, GFX_SHADOW);
}

static void draw_taskbar(gfx_surface* back) {
    int y = screen_height - TASKBAR_HEIGHT;
    int tray_x = screen_width - TRAY_WIDTH;
    gfx_fill(back, 0, y, screen_width, TASKBAR_HEIGHT, GFX_FACE);
    gfx_hline(back, 0, y, screen_width, GFX_WHITE);
    gfx_button(back, 2, y + 4, 62, 26, "Start", menu == 1 || menu == 2 || menu == 6);
    gfx_fill(back, 68, y + 2, 2, TASKBAR_HEIGHT - 4, GFX_SHADOW);
    gfx_fill(back, 70, y + 2, 2, TASKBAR_HEIGHT - 4, GFX_WHITE);

    /* Task buttons stop before the tray; each group gets its own fixed slot. */
    int x = 78;
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        int index = zorder[i];
        if (!zvalid(index)) {
            continue;
        }
        gui_window* w = &windows[index];
        if (!w->used) {
            continue;
        }
        if (x + 108 > tray_x - 4) {
            break;
        }
        char label[10];
        int n = 0;
        while (w->title[n] && n + 1 < (int)sizeof(label)) {
            label[n] = w->title[n];
            ++n;
        }
        label[n] = 0;
        gfx_button(back, x, y + 4, 108, 26, label, index == focused && !w->minimized);
        x += 112;
    }

    /* System tray with fixed slots: icons | language | clock. Every field is
     * positioned relative to the same tray origin, so text can never spill
     * into its neighbour when the uptime grows. */
    gfx_bevel(back, tray_x + 2, y + 4, TRAY_WIDTH - 6, 26, 0);
    draw_icon(back, tray_x + 10, y + 8, 'V', GFX_RGB(0, 100, 180));
    draw_icon(back, tray_x + 34, y + 8, 'S', GFX_RGB(40, 140, 40));
    gfx_vline(back, tray_x + 60, y + 7, 20, GFX_SHADOW);
    gfx_fill(back, tray_x + 68, y + 8, 48, 18, GFX_FACE);
    gfx_bevel(back, tray_x + 68, y + 8, 48, 18, 0);
    gfx_text(back, tray_x + 80, y + 10, gui_language ? "RU" : "EN", GFX_BLACK, 0);
    gfx_vline(back, tray_x + 124, y + 7, 20, GFX_SHADOW);

    kos_desktop_info info = { 0 };
    syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
    unsigned seconds = info.uptime;
    unsigned hours = seconds / 3600;
    unsigned minutes = (seconds / 60) % 60;
    unsigned secs = seconds % 60;
    char clock[9];
    int cn = 0;
    clock[cn++] = '0' + (hours / 10) % 10;
    clock[cn++] = '0' + hours % 10;
    clock[cn++] = ':';
    clock[cn++] = '0' + (minutes / 10) % 10;
    clock[cn++] = '0' + minutes % 10;
    clock[cn++] = ':';
    clock[cn++] = '0' + (secs / 10) % 10;
    clock[cn++] = '0' + secs % 10;
    clock[cn] = 0;
    gfx_text(back, screen_width - 8 - gfx_text_width(clock), y + 10, clock, GFX_BLACK, 0);

    if (gui_toast_until > 0) {
        gfx_fill(back, 8, y - 24, 320, 20, GFX_RGB(255, 255, 220));
        gfx_bevel(back, 8, y - 24, 320, 20, 1);
        gfx_text(back, 14, y - 20, gui_toast, GFX_BLACK, 0);
        --gui_toast_until;
    }
}

static void draw_start_menu(gfx_surface* back) {
    int y = screen_height - TASKBAR_HEIGHT - start_menu_height();
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
        gfx_fill(back, 62, y, 230, 260, GFX_FACE);
        gfx_bevel(back, 62, y, 230, 260, 1);
        gfx_fill(back, 66, y + 4, 222, 22, GFX_DARK);
        gfx_text(back, 72, y + 8, "Programs", GFX_WHITE, 0);
        const char* items[6];
        int count = 0;
        if (program_category == 0) {
            items[count++] = "Notepad";
            items[count++] = "kfetch";
        }
        if (program_category == 1) {
            items[count++] = "KPaint";
            items[count++] = "KGED";
        }
        if (program_category == 2) {
            items[count++] = "KGFM";
            items[count++] = "KGTERM";
            items[count++] = "KSettings";
            items[count++] = "KServices";
            items[count++] = "KSersys";
            items[count++] = "KInfo";
            items[count++] = "KSet";
        }
        if (program_category == 3) {
            items[count++] = "KMusa";
            items[count++] = "KListen";
            items[count++] = "SoundCtl";
        }
        if (program_category == 4) {
            items[count++] = "KRrawl";
            items[count++] = "KMines";
            items[count++] = "K2048";
            items[count++] = "KLife";
            items[count++] = "KTmux";
        }
        items[count++] = "Back";
        for (int i = 0; i < count; ++i) {
            gfx_button(back, 68, y + 34 + i * 28, 218, 24, items[i], 0);
        }
        return;
    }
    gfx_fill(back, 0, y, 220, 240, GFX_FACE);
    gfx_bevel(back, 0, y, 220, 240, 1);
    gfx_fill(back, 4, y + 4, 212, 24, GFX_DARK);
    gfx_text(back, 10, y + 9, "KintsugiOS 95", GFX_WHITE, 0);
    const char* items[] = { "Programs  >", "Search...", "Run...", "Settings",
                            "Find...",     "Help",      "",       "Shut Down..." };
    for (int i = 0; i < 8; ++i) {
        if (!items[i][0]) {
            gfx_hline(back, 8, y + 36 + i * 24, 204, GFX_SHADOW);
            continue;
        }
        gfx_button(back, 6, y + 34 + i * 24, 208, 22, items[i], 0);
    }
}

static void draw_dialog(gfx_surface* back) {
    int width = 480, height = menu == 3 ? 280 : 180;
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
        gfx_button(back, x + width - 170, y + height - 42, 72, 26, "Search", 0);
        gfx_button(back, x + width - 88, y + height - 42, 72, 26, "Cancel", 0);
    } else if (menu == 4) {
        gfx_text(back, x + 12, y + 42, "Type the name of a program:", GFX_BLACK, 0);
        gfx_fill(back, x + 12, y + 66, width - 24, 24, GFX_WHITE);
        gfx_bevel(back, x + 12, y + 66, width - 24, 24, 0);
        gfx_text(back, x + 18, y + 70, dialog_input, GFX_BLACK, 0);
        gfx_button(back, x + width - 170, y + height - 44, 72, 26, "OK", 0);
        gfx_button(back, x + width - 88, y + height - 44, 72, 26, "Cancel", 0);
    } else if (menu == 7 || menu == 8) {
        gfx_text(back, x + 12, y + 42, menu == 8 ? "File name:" : "Name:", GFX_BLACK, 0);
        gfx_fill(back, x + 12, y + 66, width - 24, 24, GFX_WHITE);
        gfx_bevel(back, x + 12, y + 66, width - 24, 24, 0);
        gfx_text(back, x + 18, y + 70, dialog_input, GFX_BLACK, 0);
        if (menu == 8) gfx_text(back, x + 12, y + 104, "Saved in /home/user", GFX_SHADOW, 0);
        gfx_button(back, x + width - 170, y + height - 44, 72, 26, "OK", 0);
        gfx_button(back, x + width - 88, y + height - 44, 72, 26, "Cancel", 0);
    } else {
        gfx_text(back, x + 16, y + 40, "KintsugiOS Pixel GUI", GFX_DARK, 0);
        gfx_text(back, x + 16, y + 66, "Start menu, taskbar, windows, KGFM, KGED, KGTERM", GFX_BLACK, 0);
        gfx_text(back, x + 16, y + 90, "Keyboard: Tab focus, WASD move, R resize, N minimize", GFX_BLACK, 0);
        gfx_text(back, x + 16, y + 114, "M maximize, Q exit, S Start menu", GFX_BLACK, 0);
        gfx_button(back, x + width / 2 - 36, y + height - 44, 72, 26, "OK", 0);
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
    if (kind == 0) { source = desktop; count = 4; }
    else if (kind == 1) { source = files; count = 6; }
    else if (kind == 2) { source = system; count = 4; }
    else { source = header; count = 4; }
    context_kind = kind;
    context_window = window;
    context_count = count;
    for (int i = 0; i < count; ++i)
        text_copy(context_items[i], sizeof(context_items[i]), source[i]);
    context_x = x;
    context_y = y;
    context_open = 1;
}

static void notepad_save(gui_window* w, const char* name) {
    char path[160];
    if (name[0] == '/') text_copy(path, sizeof(path), name);
    else {
        text_copy(path, sizeof(path), "/home/user/");
        int n = text_length(path), i = 0;
        while (name[i] && n + i + 1 < (int)sizeof(path)) { path[n + i] = name[i]; ++i; }
        path[n + i] = 0;
    }
    int result = syscall(10, (u32)path, (u32)w->text, (u32)w->text_length, 0);
    if (result < 0) { toast(kos_error(result)); return; }
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
                if (w->file_path[0]) notepad_save(w, w->file_path);
                else {
                    dialog_window_index = (int)(w - windows);
                    dialog_input[0] = 0; dialog_length = 0;
                    menu = 8;
                }
            } else toast("No document to save");
        } else if (item == 3) {
            if (w && w->kind == 4) {
                dialog_window_index = (int)(w - windows);
                dialog_input[0] = 0; dialog_length = 0;
                menu = 8;
            } else toast("No document to save as");
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
            menu = 5;
        }
    }
    active_menu_window = -1;
}

static void context_action(int item) {
    if (context_kind == 2) {
        gui_window* w = context_window >= 0 ? &windows[context_window] : 0;
        if (w && w->used) {
            if (item == 0 && w->maximized) toggle_maximize(w);
            else if (item == 1) { w->minimized = 1; if (focused == context_window) focused = -1; }
            else if (item == 2 && !w->maximized) toggle_maximize(w);
            else if (item == 3) close_window(w);
        }
        context_open = 0;
        return;
    }
    if (context_kind == 3) {
        gui_window* w = context_window >= 0 ? &windows[context_window] : 0;
        if (w && w->used && w->kind == 0) {
            if (item == 0) { fm_refresh(w); toast("Refreshed"); }
            else if (item == 1) {
                fm_action = 2;
                dialog_input[0] = 0; dialog_length = 0;
                menu = 7;
            } else if (item == 2) fm_up(w);
            else toast(w->path);
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
            create_window("Settings", 380, 180, 520, 320, 5);
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

static void draw_context_menu(gfx_surface* back) {
    if (!context_open) {
        return;
    }
    int width = 156, height = context_count * 22 + 4;
    int x = context_x, y = context_y;
    if (x + width > screen_width) {
        x = screen_width - width;
    }
    if (y + height > screen_height - TASKBAR_HEIGHT) {
        y = screen_height - TASKBAR_HEIGHT - height;
    }
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    context_x = x;
    context_y = y;
    gfx_fill(back, x, y, width, height, GFX_FACE);
    gfx_bevel(back, x, y, width, height, 1);
    for (int i = 0; i < context_count; ++i) {
        gfx_button(back, x + 3, y + 3 + i * 22, width - 6, 20, context_items[i], 0);
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
    "X    XBBBX  ", "     XBBX   ", "     XBBX   ", "      XX    "
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
            cursor_save[row * CURSOR_WIDTH + col] =
                (px >= 0 && py >= 0 && px < screen_width && py < screen_height)
                ? desktop->pixels[py * screen_width + px]
                : desktop_color;
        }
    }
}

static void cursor_paint(int x, int y) {
    for (int row = 0; row < CURSOR_HEIGHT; ++row) {
        for (int col = 0; col < CURSOR_WIDTH; ++col) {
            char pixel = cursor_art[row][col];
            if (pixel != 'X' && pixel != 'B') {
                continue;
            }
            int px = x + col, py = y + row;
            if (px < 0 || py < 0 || px >= screen_width || py >= screen_height) {
                continue;
            }
            desktop->pixels[py * screen_width + px] = pixel == 'X' ? GFX_BLACK : GFX_WHITE;
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
    gfx_dirty dirty = { 0, 0, 0, 0, 0 };
    if (extra && extra->valid) {
        dirty.left = extra->left; dirty.top = extra->top;
        dirty.right = extra->right; dirty.bottom = extra->bottom;
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
    gfx_fill(desktop, 0, 0, screen_width, screen_height, desktop_color);
    /* desktop icons - fixed column, centered captions, no overlap */
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        draw_desktop_icon(desktop, &desktop_icons[i], selected_icon == i);
    }
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        int index = zorder[i];
        if (!zvalid(index)) {
            continue;
        }
        gui_window* w = &windows[index];
        if (!w->used || w->minimized) {
            continue;
        }
        gfx_blit(desktop, w->x, w->y, w->surface, 0, 0, w->width, w->height);
    }
    /* External GUI clients own their own window buffers in the kernel. */
    kos_gui_window_info external[8];
    int external_count = gfx_window_list(external, 8);
    for (int i = 0; i < external_count; ++i) {
        if (external[i].minimized) {
            continue;
        }
        int id = external[i].id;
        gfx_surface* surface = gfx_surface_create(external[i].width, external[i].height);
        if (!surface) {
            continue;
        }
        if (!gfx_window_read(id, surface->pixels, 0, 0, external[i].width, external[i].height)) {
            gfx_blit(
                desktop, external[i].x, external[i].y, surface, 0, 0, external[i].width, external[i].height);
        }
        gfx_surface_destroy(surface);
    }
    draw_taskbar(desktop);
    if (menu == 1 || menu == 2 || menu == 6) {
        draw_start_menu(desktop);
    }
    if (menu >= 3 && menu != 6) {
        draw_dialog(desktop);
    }
    draw_context_menu(desktop);
    gfx_dirty_add(dirty, 0, 0, screen_width, screen_height);
    gfx_present_dirty(desktop, 0, 0, dirty);
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
        w->used = 1;
        w->x = x;
        w->y = y;
        w->width = width;
        w->height = height;
        w->saved_x = x;
        w->saved_y = y;
        w->saved_width = width;
        w->saved_height = height;
        w->kind = kind;
        text_copy(w->title, sizeof(w->title), title);
        w->surface = gfx_surface_create(width, height);
        if (!w->surface) {
            w->used = 0;
            return 0;
        }
        if (kind == 0) {
            text_copy(w->path, sizeof(w->path), "/bin");
            fm_refresh(w);
        } else if (kind == 1) {
            w->canvas = gfx_surface_create(240, 160);
            if (w->canvas) {
                gfx_clear(w->canvas, GFX_WHITE);
            }
            w->color = GFX_BLACK;
        } else if (kind == 2) {
            term_shell_banner(w);
        }
        ++window_count;
        raise_window(i);
        draw_window(w);
        return w;
    }
    return 0;
}

static void drag_begin(gui_window* w) {
    drag_index = (int)(w - windows);
    if (drag_backup) gfx_surface_destroy(drag_backup);
    drag_backup = gfx_surface_create(w->width, w->height);
    if (!drag_backup) { drag_index = -1; return; }
    int saved = w->minimized;
    w->minimized = 1;
    gfx_dirty dirty = { 0, 0, 0, 0, 0 };
    compose(&dirty);
    w->minimized = saved;
    gfx_blit(drag_backup, 0, 0, desktop, w->x, w->y, w->width, w->height);
}

static void drag_move(gui_window* w, int old_x, int old_y) {
    if (!drag_backup || drag_index != (int)(w - windows)) return;
    gfx_dirty dirty = { 0, 0, 0, 0, 0 };
    gfx_blit(desktop, old_x, old_y, drag_backup, 0, 0, w->width, w->height);
    gfx_dirty_add(&dirty, old_x, old_y, w->width, w->height);
    gfx_blit(drag_backup, 0, 0, desktop, w->x, w->y, w->width, w->height);
    gfx_blit(desktop, w->x, w->y, w->surface, 0, 0, w->width, w->height);
    gfx_dirty_add(&dirty, w->x, w->y, w->width, w->height);
    cursor_move(gui_mouse.x, gui_mouse.y, &dirty);
}

static void drag_end(void) {
    if (drag_backup) gfx_surface_destroy(drag_backup);
    drag_backup = 0;
    drag_index = -1;
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
        create_window("KGTERM", 80, 400, 520, 320, 2);
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
        w->height = screen_height - TASKBAR_HEIGHT - 4;
        w->maximized = 1;
    } else {
        w->x = w->saved_x;
        w->y = w->saved_y;
        w->width = w->saved_width;
        w->height = w->saved_height;
        w->maximized = 0;
    }
    gfx_surface_destroy(w->surface);
    w->surface = gfx_surface_create(w->width, w->height);
    draw_window(w);
}

static void close_window(gui_window* w) {
    if (!w) {
        return;
    }
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

static void run_program(const char* name);

static void launch_category_item(int category, int item) {
    static const char* const names[5][8] = {
        { "Notepad", "kfetch",  0,           0,           0,         0,       0,      0 },
        { "KPaint",  "KGED",    0,           0,           0,         0,       0,      0 },
        { "KGFM",    "KGTERM",  "KSettings", "KServices", "KSersys", "KInfo", "KSet", 0 },
        { "KMusa",   "KListen", "SoundCtl",  0,           0,         0,       0,      0 },
        { "KRrawl",  "KMines",  "K2048",     "KLife",     "KTmux",   0,       0,      0 }
    };
    static const int counts[5] = { 2, 2, 7, 3, 5 };
    if (category < 0 || category > 4 || item < 0 || item >= counts[category]) {
        return;
    }
    const char* name = names[category][item];
    if (text_equal(name, "KGFM")) {
        create_window("KGFM - Files", 80, 50, 520, 320, 0);
    } else if (text_equal(name, "KGED")) {
        create_window("KGED - Editor", 680, 50, 520, 320, 1);
    } else if (text_equal(name, "KGTERM")) {
        create_window("KGTERM", 80, 400, 520, 320, 2);
    } else if (text_equal(name, "Notepad")) {
        create_window("Notepad", 680, 400, 520, 320, 4);
    } else if (text_equal(name, "KSettings") || text_equal(name, "KSet")) {
        create_window("Settings", 380, 180, 520, 320, 5);
    } else {
        run_program(name);
    }
}

static void run_program(const char* name) {
    gui_window* term = 0;
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (windows[i].used && windows[i].kind == 2 && !windows[i].minimized) {
            term = &windows[i];
            break;
        }
    }
    if (!term) {
        term = create_window("KGTERM", 80, 400, 520, 320, 2);
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
        term_run_program(term, argv[0], argv, argc);
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

    create_window("KGFM - Files", 80, 50, 520, 320, 0);
    create_window("KGED - Editor", 680, 50, 520, 320, 1);
    create_window("KGTERM", 80, 400, 520, 320, 2);
    create_window("Welcome", 680, 400, 520, 320, 3);

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
        if (mouse_changed) {
            int only_motion =
                (mouse.buttons == previous_buttons && !mouse.wheel && (mouse.x != old_x || mouse.y != old_y));
            if (only_motion) {
                cursor_move(mouse.x, mouse.y, 0);
            } else {
                changed = 1;
            }
        }
        unsigned pressed = mouse.buttons & ~previous_buttons;
        unsigned released = previous_buttons & ~mouse.buttons;
        previous_buttons = mouse.buttons;
        int key = syscall(8, 0, 0, 0, 0);
        if (tui_child_pid > 0) {
            int tui_changed = syscall(KOS_SYS_TUI_READ, (u32)tui_cells, 0, 0, 0);
            kos_process_info list[16];
            int count = syscall(16, (u32)list, 16, 0, 0);
            int alive = 0;
            for (int i = 0; i < count && i < 16; ++i) {
                if (list[i].pid == (u32)tui_child_pid && list[i].state != 4) {
                    alive = 1;
                    break;
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
            if (ny < 0) {
                ny = 0;
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
                } else changed = 1;
            }
        } else {
            if (dragging && drag_backup) { drag_end(); changed = 1; }
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
                gfx_surface_destroy(focused_window->surface);
                focused_window->surface = gfx_surface_create(nw, nh);
                draw_window(focused_window);
                changed = 1;
            }
        } else {
            resizing = 0;
        }

        if (menu >= 3 && menu != 6) {
            int dx = (screen_width - 480) / 2, dy = (screen_height - (menu == 3 ? 280 : 180)) / 2;
            if (pressed & 1) {
                if (mouse.x >= dx + 454 && mouse.x < dx + 472 && mouse.y >= dy + 4 && mouse.y < dy + 20) {
                    menu = 0;
                    changed = 1;
                } else if (mouse.y >= dy + (menu == 3 ? 238 : 136)) {
                    if (menu == 3 && mouse.x >= dx + 310 && mouse.x < dx + 382) {
                        search_count = 0;
                        search_directory("/bin", dialog_input, 0);
                        search_directory("/etc", dialog_input, 0);
                    } else if (menu == 4 && mouse.x >= dx + 310 && mouse.x < dx + 382) {
                        run_program(dialog_input);
                        menu = 0;
                    } else if (menu == 7 && mouse.x >= dx + 310 && mouse.x < dx + 382) {
                        fm_apply_name();
                        menu = 0;
                    } else if (menu == 8 && mouse.x >= dx + 310 && mouse.x < dx + 382
                               && dialog_window_index >= 0 && dialog_input[0]) {
                        notepad_save(&windows[dialog_window_index], dialog_input);
                        menu = 0;
                    } else {
                        menu = 0;
                    }
                    changed = 1;
                }
            }
            if (key == 27) {
                menu = 0;
                changed = 1;
            } else if (key) {
                char encoded[4];
                unsigned added = text_key(key, encoded);
                if (added && dialog_length + (int)added < 120) {
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
                    } else if (menu == 8 && dialog_window_index >= 0 && dialog_input[0]) {
                        notepad_save(&windows[dialog_window_index], dialog_input);
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
            int item = (mouse.y - context_y - 3) / 22;
            if (mouse.x >= context_x && mouse.x < context_x + 156 && item >= 0 && item < context_count) {
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
                        int content_y = TITLE_HEIGHT + 6 + MENU_HEIGHT + 4;
                        int list_y = content_y + 29 + 28 + 20;
                        if (ly >= list_y) open_context_menu(mouse.x, mouse.y, target, 1);
                        else open_context_menu(mouse.x, mouse.y, target, 3);
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
                    int lw = gfx_text_width(icon->label);
                    int lx = icon->x + 9 - lw / 2;
                    if (lx < 2) {
                        lx = 2;
                    }
                    if ((mouse.x >= icon->x - 2 && mouse.x < icon->x + 20 && mouse.y >= icon->y - 2
                         && mouse.y < icon->y + 20)
                        || (mouse.x >= lx && mouse.x < lx + lw && mouse.y >= icon->y + 22
                            && mouse.y < icon->y + 38)) {
                        icon_hit = i;
                        break;
                    }
                }
            }
            if (icon_hit >= 0) {
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
                int start_y = task_y - start_menu_height();
                if (mouse.y >= task_y && mouse.x < 64) {
                    menu = menu == 1 ? 0 : 1;
                    changed = 1;
                } else if (
                    menu == 1 && mouse.x >= 6 && mouse.x < 214 && mouse.y >= start_y + 34
                    && mouse.y < start_y + 226) {
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
                        create_window("Settings", 380, 180, 520, 320, 5);
                        menu = 0;
                    } else if (item == 5) {
                        menu = 5;
                    } else if (item == 7) {
                        gfx_exit_to_terminal();
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
                    && mouse.y < start_y + 260) {
                    int item = (mouse.y - start_y - 34) / 28;
                    if (item >= 0 && item <= 7) {
                        launch_category_item(program_category, item);
                        menu = 0;
                    } else {
                        menu = 2;
                    }
                    changed = 1;
                } else if (mouse.y >= task_y) {
                    int button_x = 78;
                    for (int i = 0; i < MAX_WINDOWS; ++i) {
                        int index = zorder[i];
                        if (!zvalid(index)) {
                            continue;
                        }
                        gui_window* w = &windows[index];
                        if (!w->used) {
                            continue;
                        }
                        if (button_x + 108 > screen_width - TRAY_WIDTH - 4) {
                            break;
                        }
                        if (mouse.x >= button_x && mouse.x < button_x + 108) {
                            w->minimized = !w->minimized;
                            if (!w->minimized) {
                                raise_window(index);
                            } else if (focused == index) {
                                focused = -1;
                            }
                            break;
                        }
                        button_x += 112;
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
                            int menu_y = TITLE_HEIGHT + 6;
                            if (ly >= TITLE_BUTTON_Y && ly < TITLE_BUTTON_Y + TITLE_BUTTON_W
                                && lx >= TITLE_MIN_X(w) && lx < TITLE_CLOSE_X(w) + TITLE_BUTTON_W) {
                                if (lx >= TITLE_CLOSE_X(w)) {
                                    close_window(w);
                                } else if (lx >= TITLE_MAX_X(w)) {
                                    toggle_maximize(w);
                                } else {
                                    w->minimized = 1;
                                    if (focused == target) {
                                        focused = -1;
                                    }
                                }
                                changed = 1;
                            } else if (ly >= menu_y && ly < menu_y + MENU_HEIGHT && lx >= 8 && lx < 320) {
                                active_menu_column = lx < 70 ? 0 : lx < 150 ? 1 : lx < 230 ? 2 : 3;
                                active_menu_window = (active_menu_window == target) ? -1 : target;
                                raise_window(target);
                                changed = 1;
                            } else if (
                                active_menu_window == target && lx >= 8 && lx < 144
                                && ly >= menu_y + MENU_HEIGHT
                                && ly < menu_y + MENU_HEIGHT
                                    + (active_menu_column == 3 ? 40 : active_menu_column == 0 ? 94 : 76)) {
                                int item = (ly - menu_y - MENU_HEIGHT - 4) / 18;
                                window_menu_action(w, active_menu_column, item);
                                changed = 1;
                            } else if (lx >= w->width - 14 && ly >= w->height - 14) {
                                raise_window(target);
                                resizing = 1;
                            } else if (ly < TITLE_HEIGHT) {
                                cursor_erase();
                                raise_window(target);
                                dragging = 1;
                                drag_dx = lx;
                                drag_dy = ly;
                                drag_begin(w);
                            } else {
                                raise_window(target);
                                int content_y = menu_y + MENU_HEIGHT + 4;
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
                                if (w->kind == 5) {
                                    static const u32 colors[] = { GFX_TEAL,
                                                                  GFX_DARK,
                                                                  GFX_RGB(0, 96, 96),
                                                                  GFX_RGB(64, 64, 96),
                                                                  GFX_RGB(96, 64, 96),
                                                                  GFX_RGB(48, 96, 48),
                                                                  GFX_RGB(96, 96, 96) };
                                    for (int i = 0; i < 7; ++i) {
                                        int bx = w->x + 16 + (i % 4) * 46;
                                        int by = w->y + content_y + 26 + (i / 4) * 46;
                                        if (lx >= bx - w->x && lx < bx - w->x + 40 && ly >= by - w->y
                                            && ly < by - w->y + 40) {
                                            desktop_color = colors[i];
                                            toast("Wallpaper changed");
                                            changed = 1;
                                        }
                                    }
                                    if (lx >= 16 && lx < 104 && ly >= content_y + 148
                                        && ly < content_y + 172) {
                                        gui_language = 0;
                                        syscall(KOS_SYS_LAYOUT, 0, 0, 0, 0);
                                        toast("Language: English");
                                        changed = 1;
                                    }
                                    if (lx >= 108 && lx < 196 && ly >= content_y + 148
                                        && ly < content_y + 172) {
                                        gui_language = 1;
                                        syscall(KOS_SYS_LAYOUT, 1, 0, 0, 0);
                                        toast("Language: Russian");
                                        changed = 1;
                                    }
                                } else if (w->kind == 0) {
                                    int address_y = content_y + 29;
                                    int list_y = address_y + 28;
                                    int list_h = status_y - list_y - 6;
                                    int rows_y = list_y + 20;
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
                                        }
                                    } else if (
                                        ly >= address_y && ly < address_y + 20 && lx >= w->width - 32) {
                                        /* Address dropdown: cycle common locations. */
                                        if (text_equal(w->path, "/")) {
                                            text_copy(w->path, sizeof(w->path), "/bin");
                                        } else if (text_equal(w->path, "/bin")) {
                                            text_copy(w->path, sizeof(w->path), "/home");
                                        } else {
                                            text_copy(w->path, sizeof(w->path), "/");
                                        }
                                        w->selected = w->top = 0;
                                        fm_refresh(w);
                                    } else if (ly >= list_y && ly < status_y - 6) {
                                        int index = w->top + (ly - rows_y) / 20;
                                        if (lx < w->width - 20 && index >= 0 && index < w->entry_count) {
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
                                        } else if (lx >= w->width - 20) {
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
                                        static const u32 palette[] = { GFX_BLACK,
                                                                       GFX_RED,
                                                                       GFX_RGB(0, 128, 0),
                                                                       GFX_DARK,
                                                                       GFX_RGB(255, 255, 0),
                                                                       GFX_RGB(255, 255, 255) };
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
            int cy = mouse.y - focused_window->y - (TITLE_HEIGHT + 6 + MENU_HEIGHT + 4 + 28) - 2;
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
                    && (focused_window->kind == 2 || focused_window->kind == 4
                        || focused_window->kind == 6))) {
                for (int i = 0; i < MAX_WINDOWS; ++i) {
                    if (windows[i].used && windows[i].kind == 2 && !windows[i].minimized) {
                        raise_window(i);
                        focused_window = &windows[i];
                        break;
                    }
                }
            }
            int typing = focused_window && focused_window->used
                && (focused_window->kind == 2 || focused_window->kind == 4 || focused_window->kind == 6);
            if (key == 27) {
                menu = 0;
                active_menu_window = -1;
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
            } else if (menu == 1 && key >= '1' && key <= '7') {
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
                    create_window("Settings", 380, 180, 520, 320, 5);
                    menu = 0;
                } else if (item == 5) {
                    menu = 5;
                } else if (item == 6) {
                    gfx_exit_to_terminal();
                    return 0;
                } else {
                    menu = 0;
                }
                changed = 1;
            } else if (menu == 2 && key >= '1' && key <= '6') {
                program_category = key - '1';
                menu = 6;
                changed = 1;
            } else if (menu == 6 && key >= '1' && key <= '8') {
                launch_category_item(program_category, key - '1');
                menu = 0;
                changed = 1;
            } else if ((menu == 2 || menu == 6) && (key == '0' || key == 27)) {
                menu = menu == 6 ? 2 : 1;
                changed = 1;
            } else if ((key == 'q' || key == 'Q') && !typing && menu == 0) {
                gfx_exit_to_terminal();
                return 0;
            }
            /* A menu shortcut must not also be typed into the window it opens. */
            if (menu_at_key) {
                key = 0;
            }
            focused_window = focused >= 0 ? &windows[focused] : 0;
            if (focused_window && focused_window->used && focused_window->kind == 2) {
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
                char encoded[4];
                unsigned added = text_key(key, encoded);
                if (added && focused_window->text_length + (int)added < 500) {
                    for (unsigned i = 0; i < added; ++i) {
                        focused_window->text[focused_window->text_length++] = encoded[i];
                    }
                    focused_window->text[focused_window->text_length] = 0;
                    changed = 1;
                } else if (key == 8 && focused_window->text_length) {
                    focused_window->text[--focused_window->text_length] = 0;
                    changed = 1;
                } else if (key == '\n' && focused_window->text_length < 499) {
                    focused_window->text[focused_window->text_length++] = '\n';
                    focused_window->text[focused_window->text_length] = 0;
                    changed = 1;
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

        {
            kos_gui_window_info external[8];
            if (gfx_window_list(external, 8) > 0) {
                changed = 1;
            }
        }
        if (paint_window && paint_dirty && !changed) {
            /* Painting only touches one window surface: repaint that
             * rectangle instead of the whole 1280x800 frame. */
            gui_mouse = mouse;
            gfx_dirty partial = { 0, 0, 0, 0, 0 };
            gfx_dirty_add(&partial, paint_window->x, paint_window->y,
                          paint_window->width, paint_window->height);
            cursor_erase();
            draw_window(paint_window);
            gfx_blit(desktop, paint_window->x, paint_window->y, paint_window->surface,
                     0, 0, paint_window->width, paint_window->height);
            cursor_move(gui_mouse.x, gui_mouse.y, &partial);
        } else if (changed) {
            gui_mouse = mouse;
            cursor_erase();
            for (int i = 0; i < MAX_WINDOWS; ++i) {
                if (windows[i].used) {
                    draw_window(&windows[i]);
                }
            }
            compose(&dirty);
            cursor_move(gui_mouse.x, gui_mouse.y, 0);
        }
        paint_window = 0;
        paint_dirty = 0;
        syscall(KOS_SYS_SLEEP, 10, 0, 0, 0);
    }
}
