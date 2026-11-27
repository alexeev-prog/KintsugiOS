#include "ktuilib.h"
#define TABS 4
#define ENTRIES 256

typedef struct {
    char directory[256];
    char previous[256];
    u32 selected, top;
    char marked[ENTRIES][256];
    u32 marks;
} tab;

typedef struct {
    tab tabs[TABS];
    u32 active, count, entries_count;
    tui_long_entry entries[ENTRIES];
    char loaded_directory[256];
} panel;

static panel panels[2];
static unsigned char settings[2] = { 1, 1 }; /* sizes, directories first */

static int same(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int marked(tab* t, const char* name) {
    for (u32 i = 0; i < t->marks; ++i) {
        if (same(t->marked[i], name)) {
            return i;
        }
    }
    return -1;
}

static int is_parent(const tui_long_entry* e) {
    return same(e->name, "..");
}

static void toggle(tab* t, const tui_long_entry* e) {
    if (is_parent(e)) {
        return;
    }
    int at = marked(t, e->name);
    if (at >= 0) {
        --t->marks;
        tui_copy(t->marked[at], t->marked[t->marks], sizeof(t->marked[at]));
    } else if (t->marks < ENTRIES) {
        tui_copy(t->marked[t->marks++], e->name, sizeof(t->marked[0]));
    }
}

static const char* batch(panel* p, panel* other, int key) {
    tab* t = &p->tabs[p->active];
    if (!p->entries_count) {
        return "Empty directory";
    }
    if (!tui_confirm(
            key == 'd'       ? "Delete marked entries (or current entry)?"
                : key == 'm' ? "Move marked entries to other panel?"
                             : "Copy marked entries to other panel?")) {
        return "Cancelled";
    }
    u32 failed = 0, done = 0;
    for (u32 i = 0; i < p->entries_count; ++i) {
        tui_long_entry* e = &p->entries[i];
        if (is_parent(e) || (t->marks ? marked(t, e->name) < 0 : i != t->selected)) {
            continue;
        }
        char source[256], dest[256];
        tui_path(source, t->directory, e->name);
        tui_path(dest, other->tabs[other->active].directory, e->name);
        int result = key == 'd' ? syscall(11, (u32)source, e->stat.directory ? 2 : 1, 0, 0)
                                : syscall(12, (u32)source, (u32)dest, key == 'm', 0);
        if (result) {
            ++failed;
        } else {
            ++done;
        }
    }
    t->marks = 0;
    if (failed) {
        return done ? "Partially completed; failures: existing target/nonempty directory/I/O"
                    : "Failed: existing target, nonempty directory, permissions or I/O";
    }
    return done ? "Group operation completed" : "No actionable entries";
}

static int compare(const tui_long_entry* a, const tui_long_entry* b) {
    if (settings[1] && a->stat.directory != b->stat.directory) {
        return b->stat.directory - a->stat.directory;
    }
    for (u32 i = 0; i < 256; ++i) {
        if (a->name[i] != b->name[i]) {
            return (unsigned char)a->name[i] - (unsigned char)b->name[i];
        }
        if (!a->name[i]) {
            break;
        }
    }
    return 0;
}

static int refresh(panel* p) {
    tab* current = &p->tabs[p->active];
    if (!same(p->loaded_directory, current->directory)) {
        current->marks = 0;
        tui_copy(p->loaded_directory, current->directory, 256);
    }
    u32 cursor = 0;
    int result = 0;
    p->entries_count = 0;
    if (tui_len(p->tabs[p->active].directory) > 1) {
        tui_long_entry* parent = &p->entries[p->entries_count++];
        tui_copy(parent->name, "..", sizeof(parent->name));
        parent->stat = (tui_stat){ .size = 0, .directory = 1, .readonly = 1, .mtime = 0 };
    }
    while (p->entries_count < ENTRIES
           && (result = tui_readdir_long(
                   p->tabs[p->active].directory, &cursor, (tui_long_entry*)&p->entries[p->entries_count]))
               > 0) {
        ++p->entries_count;
    }
    u32 first = tui_len(p->tabs[p->active].directory) > 1 ? 1 : 0;
    for (u32 i = first + 1; i < p->entries_count; ++i) {
        tui_long_entry value = p->entries[i];
        u32 j = i;
        while (j > first && compare(&value, &p->entries[j - 1]) < 0) {
            p->entries[j] = p->entries[j - 1];
            --j;
        }
        p->entries[j] = value;
    }
    tab* t = &p->tabs[p->active];
    if (t->selected >= p->entries_count) {
        t->selected = p->entries_count ? p->entries_count - 1 : 0;
    }
    if (t->selected < t->top) {
        t->top = t->selected;
    }
    if (t->selected >= t->top + 18) {
        t->top = t->selected - 17;
    }
    return result;
}

static void draw(panel* p, int x, int active) {
    tab* t = &p->tabs[p->active];
    tui_window window = { x, 1, 40, 22, active, active ? "Active" : "Panel" };
    tui_window_draw(&window);
    for (u32 i = 0; i < p->count; ++i) {
        tui_text(x + 2 + i * 8, 2, "Tab", i == p->active ? 0x70 : 0x1f);
        tui_number(x + 5 + i * 8, 2, i + 1, i == p->active ? 0x70 : 0x1f);
    }
    tui_textn(x + 1, 3, t->directory, 38, 0x1e);
    for (u32 i = t->top; i < p->entries_count && i < t->top + 18; ++i) {
        int y = 4 + i - t->top;
        int mark = marked(t, p->entries[i].name) >= 0;
        unsigned char color = mark ? TUI_MARKED : p->entries[i].stat.directory ? TUI_DIRECTORY : 0x17;
        tui_choice_row(x + 1, y, 37, "", active && i == t->selected, color);
        tui_text(
            x + 3,
            y,
            mark                               ? "*"
                : p->entries[i].stat.directory ? "/"
                                               : " ",
            active && i == t->selected ? 0x70 : color);
        tui_textn(x + 4, y, p->entries[i].name, 21, active && i == t->selected ? 0x70 : color);
        if (settings[0]) {
            tui_number(x + 26, y, p->entries[i].stat.size, active && i == t->selected ? 0x70 : color);
        }
    }
    tui_scrollbar(x + 38, 4, 18, t->top, 18, p->entries_count);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    tui_load_settings("/etc/KC.CFG", settings, sizeof(settings));
    settings[0] = !!settings[0];
    settings[1] = !!settings[1];
    for (int i = 0; i < 2; ++i) {
        panels[i].count = 1;
        syscall(4, (u32)panels[i].tabs[0].directory, 256, 0, 0);
    }
    int active = 0;
    const char* status = "Tab panel | Ins/Space mark | +/- all/none | T new tab | [ ] tabs";
    for (;;) {
        for (int i = 0; i < 2; ++i) {
            if (refresh(&panels[i]) < 0) {
                status = "Directory read failed";
            }
        }
        tui_clear(0x17);
        tui_bar(0, "KC - Kintsugi Commander");
        draw(&panels[0], 0, active == 0);
        draw(&panels[1], 40, active == 1);
        tui_bar(23, status);
        tui_bar(24, "1 Help 2 Info 3 View 4 Edit 5 Copy 6 Move 7 Mkdir 8 Delete 9 Menu 10 Quit");
        tui_present(79, 24);
        tui_event event;
        tui_event_read(&event, 1);
        int key = event.key;
        if ((event.pressed & 1) && event.mouse_y >= 4 && event.mouse_y < 22 && event.mouse_x >= 0
            && event.mouse_x < 80) {
            active = event.mouse_x >= 40;
            tab* clicked = &panels[active].tabs[panels[active].active];
            unsigned at = clicked->top + event.mouse_y - 4;
            if (at < panels[active].entries_count) {
                clicked->selected = at;
            }
        }
        if ((event.pressed & 2) && event.mouse_y >= 4 && event.mouse_y < 22) {
            key = '\n';
        }
        static const int functions[] = { 'h', 'i', 'v', '\n', 'c', 'm', 'n', 'd', 'o', 'q' };
        if (key >= TUI_F1 && key <= TUI_F10) {
            key = functions[key - TUI_F1];
        }
        panel* p = &panels[active];
        tab* t = &p->tabs[p->active];
        if (key == 'q' || key == 27) {
            break;
        }
        if (key == '\t') {
            active ^= 1;
            continue;
        }
        if (key == TUI_INSERT || key == ' ') {
            if (p->entries_count) {
                toggle(t, &p->entries[t->selected]);
                tui_list_key(TUI_DOWN, &t->selected, &t->top, p->entries_count, 18);
            }
        } else if (key == '+' || key == '-') {
            t->marks = 0;
            if (key == '+') {
                for (u32 i = 0; i < p->entries_count; ++i) {
                    toggle(t, &p->entries[i]);
                }
            }
        } else if (key == 'c' || key == 'm' || key == 'd') {
            status = batch(p, &panels[active ^ 1], key);
        } else if (key == 'h') {
            tui_message("Commander help", "F1-F10 actions; Ins marks; +/- all/none; C/M/D act on marks.");
        } else if (key == '/') {
            char destination[256];
            tui_stat st;
            if (tui_prompt("Open directory (absolute path):", destination, sizeof(destination))) {
                if (destination[0] != '/' || tui_stat_path(destination, &st) || !st.directory) {
                    status = "Not an accessible absolute directory";
                } else {
                    tui_copy(t->previous, t->directory, 256);
                    tui_copy(t->directory, destination, 256);
                    t->selected = t->top = 0;
                }
            }
        } else if (key == 'i' && p->entries_count) {
            tui_long_entry* e = &p->entries[t->selected];
            tui_message(
                e->name,
                e->stat.directory      ? "Directory; deletion requires it to be empty"
                    : e->stat.readonly ? "Read-only file"
                                       : "Writable file; V preview / Enter edit");
        } else if (key == 'o') {
            const char* items[] = {
                "Toggle sizes", "Toggle directories first", "Save settings", "Sort by name", "Sort by size"
            };
            int c = tui_menu("Commander settings", items, 5, 0);
            if (c == 0) {
                settings[0] ^= 1;
            }
            if (c == 1) {
                settings[1] ^= 1;
            }
            if (c == 2) {
                status = tui_save_settings("/etc/KC.CFG", settings, sizeof(settings))
                    ? "Settings saved"
                    : "Settings save failed";
            }
            if (c == 3 || c == 4) {
                settings[1] = c == 3;
                status = c == 3 ? "Name sort selected" : "Size view selected";
            }
        } else if (key == 't' && p->count < TABS) {
            p->tabs[p->count] = *t;
            p->active = p->count++;
        } else if (key == '[' || key == ']') {
            p->active = (p->active + (key == ']' ? 1 : p->count - 1)) % p->count;
        } else if (key == 'x' && p->count > 1) {
            for (u32 i = p->active; i + 1 < p->count; ++i) {
                p->tabs[i] = p->tabs[i + 1];
            }
            --p->count;
            if (p->active >= p->count) {
                --p->active;
            }
        } else if (
            (key >= TUI_UP && key <= TUI_END) || key == TUI_PAGE_UP || key == TUI_PAGE_DOWN
            || key == TUI_SCROLL_UP || key == TUI_SCROLL_DOWN) {
            tui_list_key(key, &t->selected, &t->top, p->entries_count, 18);
        } else if (key == 8) {
            tui_copy(t->previous, t->directory, sizeof(t->previous));
            tui_parent(t->directory);
            t->selected = t->top = 0;
        } else if (key == 'b' && *t->previous) {
            char previous[256];
            tui_copy(previous, t->previous, sizeof(previous));
            tui_copy(t->previous, t->directory, sizeof(t->previous));
            tui_copy(t->directory, previous, sizeof(t->directory));
            t->selected = t->top = 0;
        } else {
            char source[256], dest[256], input[128];
            tui_long_entry* e = p->entries_count ? &p->entries[t->selected] : 0;
            if (e) {
                tui_path(source, t->directory, e->name);
            }
            int parent = e && e->name[0] == '.' && e->name[1] == '.' && !e->name[2];
            if (parent && key != '\n') {
                continue;
            }
            if (key == 'v' && e && !e->stat.directory) {
                char* args[] = { "KUNED", source, "--readonly", 0 };
                int pid = spawn("/bin/KUNED.ELF", 3, args);
                status = pid > 0 && wait(pid) == 0 ? "Viewer closed" : "Viewer failed";
            } else if (key == 'r' && e && tui_prompt("New name (same directory):", input, sizeof(input))) {
                tui_path(dest, t->directory, input);
                status = syscall(12, (u32)source, (u32)dest, 1, 0) ? "Rename failed" : "Renamed";
            } else if (key == '\n' && e) {
                if (e->stat.directory) {
                    tui_copy(t->previous, t->directory, sizeof(t->previous));
                    if (parent) {
                        tui_parent(t->directory);
                    } else {
                        tui_copy(t->directory, source, 256);
                    }
                    t->selected = t->top = 0;
                } else {
                    u32 n = tui_len(source);
                    int executable = n > 4 && same(source + n - 4, ".ELF");
                    int painting = n > 4 && same(source + n - 4, ".KPT");
                    int result = tui_launch(
                        executable     ? source
                            : painting ? "/bin/KPAINT.ELF"
                                       : "/bin/KUNED.ELF",
                        executable ? 0 : source);
                    status = result < 0 ? kos_error(result)
                        : result        ? "Application returned error"
                                        : "Application closed";
                }
            } else if (key == 'n' && tui_prompt("New directory:", input, sizeof(input))) {
                tui_path(dest, t->directory, input);
                status = syscall(11, (u32)dest, 0, 0, 0) ? "mkdir failed" : "Created";
            }
        }
    }
    return 0;
}
