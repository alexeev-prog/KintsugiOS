#include "ktuilib.h"
#define TABS 4
#define ENTRIES 256

typedef struct {
    char directory[256];
    char previous[256];
    u32 selected, top;
} tab;

typedef struct {
    tab tabs[TABS];
    u32 active, count, entries_count;
    tui_entry entries[ENTRIES];
} panel;

static panel panels[2];
static unsigned char settings[2] = { 1, 1 }; /* sizes, directories first */

static int compare(const tui_entry* a, const tui_entry* b) {
    if (settings[1] && a->stat.directory != b->stat.directory) {
        return b->stat.directory - a->stat.directory;
    }
    for (u32 i = 0; i < 13; ++i) {
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
    u32 cursor = 0;
    int result = 0;
    p->entries_count = 0;
    if (tui_len(p->tabs[p->active].directory) > 1) {
        tui_entry* parent = &p->entries[p->entries_count++];
        tui_copy(parent->name, "..", sizeof(parent->name));
        parent->stat = (tui_stat){ 0, 1, 1 };
    }
    while (p->entries_count < ENTRIES
           && (result = syscall(
                   3, (u32)p->tabs[p->active].directory, (u32)&cursor, (u32)&p->entries[p->entries_count], 0))
               > 0) {
        ++p->entries_count;
    }
    u32 first = tui_len(p->tabs[p->active].directory) > 1 ? 1 : 0;
    for (u32 i = first + 1; i < p->entries_count; ++i) {
        tui_entry value = p->entries[i];
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
        unsigned char color = active && i == t->selected ? 0x70 : 0x17;
        tui_fill(x + 1, y, 38, 1, color);
        tui_text(x + 2, y, p->entries[i].stat.directory ? "/" : " ", color);
        tui_textn(x + 3, y, p->entries[i].name, 13, color);
        if (settings[0]) {
            tui_number(x + 26, y, p->entries[i].stat.size, color);
        }
    }
    tui_scrollbar(x + 38, 4, 18, t->top, 18, p->entries_count);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    tui_load_settings("/ETC/KC.CFG", settings, sizeof(settings));
    settings[0] = !!settings[0];
    settings[1] = !!settings[1];
    for (int i = 0; i < 2; ++i) {
        panels[i].count = 1;
        syscall(4, (u32)panels[i].tabs[0].directory, 256, 0, 0);
    }
    int active = 0;
    const char* status = "Tab panel | T new tab | [ ] switch tab | X close tab | O settings";
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
        tui_bar(24, "Enter edit | V view | / path | R rename | C copy M move D delete N mkdir Q quit");
        tui_present(79, 24);
        int key = tui_key(1);
        panel* p = &panels[active];
        tab* t = &p->tabs[p->active];
        if (key == 'q' || key == 27) {
            break;
        }
        if (key == '\t') {
            active ^= 1;
            continue;
        }
        if (key == 'h') {
            tui_message("Commander help", "Copy/move targets other panel. Enter opens files in KUNED.");
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
            tui_entry* e = &p->entries[t->selected];
            tui_message(
                e->name,
                e->stat.directory      ? "Directory; deletion requires it to be empty"
                    : e->stat.readonly ? "Read-only file"
                                       : "Writable file; V preview / Enter edit");
        } else if (key == 'o') {
            const char* items[] = { "Toggle sizes", "Toggle directories first", "Save settings" };
            int c = tui_menu("Commander settings", items, 3, 0);
            if (c == 0) {
                settings[0] ^= 1;
            }
            if (c == 1) {
                settings[1] ^= 1;
            }
            if (c == 2) {
                status = tui_save_settings("/ETC/KC.CFG", settings, sizeof(settings))
                    ? "Settings saved"
                    : "Settings save failed";
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
            tui_entry* e = p->entries_count ? &p->entries[t->selected] : 0;
            if (e) {
                tui_path(source, t->directory, e->name);
            }
            int parent = e && e->name[0] == '.' && e->name[1] == '.' && !e->name[2];
            if (parent && key != '\n') {
                continue;
            }
            if (key == 'v' && e && !e->stat.directory) {
                char* args[] = { "KUNED", source, "--readonly", 0 };
                int pid = spawn("/BIN/KUNED.ELF", 3, args);
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
                    char* args[] = { "KUNED", source, 0 };
                    int pid = spawn("/BIN/KUNED.ELF", 2, args);
                    status = pid > 0 && wait(pid) == 0 ? "Editor closed" : "Editor failed";
                }
            } else if (key == 'n' && tui_prompt("New directory:", input, sizeof(input))) {
                tui_path(dest, t->directory, input);
                status = syscall(11, (u32)dest, 0, 0, 0) ? "mkdir failed" : "Created";
            } else if (key == 'd' && e && tui_confirm("Delete selected entry?")) {
                status = syscall(11, (u32)source, e->stat.directory ? 2 : 1, 0, 0)
                    ? "Delete failed (directory must be empty)"
                    : "Deleted";
            } else if ((key == 'c' || key == 'm') && e) {
                panel* other = &panels[active ^ 1];
                tui_path(dest, other->tabs[other->active].directory, e->name);
                if (tui_confirm(key == 'c' ? "Copy to other panel?" : "Move to other panel?")) {
                    status = syscall(12, (u32)source, (u32)dest, key == 'm', 0)
                        ? "Failed: no overwrite or recursive directory copy"
                        : "Done";
                }
            }
        }
    }
    return 0;
}
