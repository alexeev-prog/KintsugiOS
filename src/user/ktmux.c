#include "ktuilib.h"

/* KTmux - terminal multiplexer in the spirit of tmux: named windows, each
 * running an ELF program, with a status line and mouse/keyboard switching. */

#define MAX_WIN 8
#define NAME_LEN 24

typedef struct {
    char name[NAME_LEN];
    char path[128];
    int pid, active, used;
} session;

static session wins[MAX_WIN];
static int current;
static int split_view;

static const char* presets[] = { "Shell",    "Commander", "Editor", "Processes", "Disks",
                                 "Services", "CSV",       "Racing", "Dungeon",   "Music" };
static const char* preset_paths[] = { "/bin/SHELL.ELF",   "/bin/KC.ELF",       "/bin/KUNED.ELF",
                                      "/bin/KPROCM.ELF",  "/bin/KDISKMAN.ELF", "/bin/KSERVICES.ELF",
                                      "/bin/KCSV.ELF",    "/bin/KRRAWL.ELF",   "/bin/KMUSA.ELF",
                                      "/bin/SOUNDCTL.ELF" };
#define PRESET_COUNT ((int)(sizeof(presets) / sizeof(presets[0])))

static void status_line(void) {
    tui_bar(24, "KTmux");
    tui_text(7, 24, "|", 0x70);
    int x = 9;
    for (int i = 0; i < MAX_WIN; ++i) {
        if (!wins[i].used) {
            continue;
        }
        char label[NAME_LEN + 8];
        u32 n = 0;
        label[n++] = i == current ? '[' : ' ';
        for (u32 j = 0; wins[i].name[j] && n < NAME_LEN + 4; ++j) {
            label[n++] = wins[i].name[j];
        }
        label[n++] = i == current ? ']' : ' ';
        label[n] = 0;
        if (x + (int)n >= 70) {
            break;
        }
        tui_text(x, 24, label, i == current ? 0x7e : 0x70);
        x += n + 1;
    }
    tui_text(71, 24, split_view ? "SPLIT" : "FULL", 0x7e);
}

static int open_window(const char* name, const char* path) {
    for (int i = 0; i < MAX_WIN; ++i) {
        if (wins[i].used) {
            continue;
        }
        tui_copy(wins[i].name, name, NAME_LEN);
        tui_copy(wins[i].path, path, sizeof(wins[i].path));
        wins[i].used = 1;
        wins[i].pid = -1;
        current = i;
        return i;
    }
    return -1;
}

static void launch_current(void) {
    if (!wins[current].used) {
        return;
    }
    char* argv[] = { wins[current].name, 0 };
    wins[current].pid = spawn(wins[current].path, 1, argv);
}

static void close_current(void) {
    if (!wins[current].used) {
        return;
    }
    wins[current].used = 0;
    wins[current].pid = -1;
    for (int i = current + 1; i < MAX_WIN; ++i) {
        if (wins[i].used) {
            current = i;
            return;
        }
    }
    for (int i = current - 1; i >= 0; --i) {
        if (wins[i].used) {
            current = i;
            return;
        }
    }
    current = 0;
}

static void draw_panes(void) {
    tui_clear(0x07);
    tui_bar(0, "KTmux - Kintsugi Terminal Multiplexer | F1 new | F2 switch | Del close | S split | Q quit");
    if (split_view) {
        tui_box(1, 2, 38, 20, "Pane A", TUI_BORDER);
        tui_box(41, 2, 38, 20, "Pane B", TUI_BORDER);
        int other = current + 1 < MAX_WIN && wins[current + 1].used ? current + 1 : current;
        tui_text(3, 4, "A:", 0x1e);
        tui_text(5, 4, wins[current].name, 0x1f);
        tui_text(43, 4, "B:", 0x1e);
        tui_text(45, 4, wins[other].name, 0x1f);
        tui_text(3, 7, "Each pane hosts one session. Press F2 to rotate focus.", 0x1f);
        tui_text(3, 9, "Applications launch fullscreen, then return to KTmux.", 0x1f);
    } else {
        tui_box(4, 3, 72, 18, wins[current].used ? wins[current].name : "No session", TUI_BORDER);
        if (wins[current].used) {
            tui_text(7, 6, "Command:", 0x1e);
            tui_text(16, 6, wins[current].path, 0x1f);
            tui_text(7, 9, wins[current].pid > 0 ? "Session running / returned" : "Session ready", 0x1f);
        } else {
            tui_text(7, 6, "Press F1 to create a session window.", 0x1f);
        }
        tui_text(7, 13, "F1 New    F2 Next    Del Close    S Split    Enter Launch    Q Quit", 0x1e);
    }
    status_line();
    tui_present(79, 24);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    open_window("shell", "/bin/SHELL.ELF");
    for (;;) {
        draw_panes();
        int key = tui_key(1);
        if (key == 'q' || key == 27 || key == TUI_F10) {
            return 0;
        }
        if (key == TUI_F1) {
            int choice = tui_menu("New session", presets, PRESET_COUNT, 0);
            if (choice >= 0) {
                open_window(presets[choice], preset_paths[choice]);
            }
        } else if (key == TUI_F2) {
            for (int n = 1; n <= MAX_WIN; ++n) {
                int i = (current + n) % MAX_WIN;
                if (wins[i].used) {
                    current = i;
                    break;
                }
            }
        } else if (key == TUI_DELETE) {
            close_current();
        } else if (key == 's') {
            split_view = !split_view;
        } else if (key == '\n') {
            launch_current();
        }
    }
}
