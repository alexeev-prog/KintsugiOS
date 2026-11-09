/* Host-side tests: no QEMU, no kernel or hardware dependencies. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/user/tuiabi.h"

typedef unsigned int u32;
static ktui_cell frame[2000];
static int keys[32], position;

int syscall(u32 number, u32 a, u32 b, u32 c, u32 d) {
    (void)b;
    (void)c;
    (void)d;
    if (number == 7) {
        memcpy(frame, (void*)(unsigned long)a, sizeof(frame));
    }
    if (number == 8) {
        return keys[position++];
    }
    return 0;
}

/* runtime.h declares freestanding malloc with u32 size. Avoid libc conflicts. */
#define USER_RUNTIME_H
#include "../src/user/ktuilib.c"
#define CAPACITY 1024u

static void remember(const char* t, u32 n, u32 p) {
    (void)t;
    (void)n;
    (void)p;
}

#include "../src/user/syntax.h"
#include "../src/user/vikmode.h"

int main(void) {
    vik_state vim = { 0 };
    char text[CAPACITY] = "one two\nthree\nfour\n";
    u32 length = strlen(text), cursor = 0;
    int insert = 0, dirty = 0;
    assert(vik_key(&vim, text, &length, &cursor, 'd', 0, &insert, &dirty));
    assert(vik_key(&vim, text, &length, &cursor, 'w', 0, &insert, &dirty));
    assert(length == 15 && !memcmp(text, "two\nthree\nfour\n", length));
    vik_key(&vim, text, &length, &cursor, 'p', 0, &insert, &dirty);
    assert(length == 19);
    cursor = 0;
    vik_key(&vim, text, &length, &cursor, '2', 0, &insert, &dirty);
    vik_key(&vim, text, &length, &cursor, 'y', 0, &insert, &dirty);
    vik_key(&vim, text, &length, &cursor, 'y', 0, &insert, &dirty);
    assert(vim.lines[0] && vim.sizes[0] == 14);
    u32 before = length;
    vik_key(&vim, text, &length, &cursor, 'd', 1, &insert, &dirty);
    vik_key(&vim, text, &length, &cursor, 'd', 1, &insert, &dirty);
    assert(length == before);
    vik_key(&vim, text, &length, &cursor, 'v', 0, &insert, &dirty);
    vik_key(&vim, text, &length, &cursor, 'l', 0, &insert, &dirty);
    vik_key(&vim, text, &length, &cursor, 'c', 0, &insert, &dirty);
    assert(insert && !vim.visual && length == before - 2);
    for (int i = 0; i < 27; ++i) {
        free(vim.registers[i]);
    }
    assert(syntax_language("/HOME/TEST.C") == SYN_C);
    assert(syntax_language("x.asm") == SYN_ASM);
    assert(syntax_language("x.txt") == SYN_TEXT);
    syntax_state syntax = { .language = SYN_C };
    const char* code = "int x; // comment\n\"hi\"";
    assert(syntax_color(&syntax, code, strlen(code), 0, 7) == TUI_KEYWORD);
    for (u32 i = 1; i < 7; ++i) {
        syntax_color(&syntax, code, strlen(code), i, 7);
    }
    assert(syntax_color(&syntax, code, strlen(code), 7, 7) == TUI_COMMENT);
    char value[5] = "ab";
    tui_input input = { value, sizeof(value), 1 };
    assert(tui_input_key(&input, 'X') && !strcmp(value, "aXb"));
    assert(tui_input_key(&input, TUI_DELETE) && !strcmp(value, "aX"));
    tui_input_key(&input, TUI_HOME);
    assert(tui_input_key(&input, '1'));
    assert(tui_input_key(&input, '2'));
    assert(!tui_input_key(&input, '3') && !strcmp(value, "12aX"));
    int checked = 0;
    assert(tui_toggle_key(' ', &checked) && checked);
    u32 focus = 2;
    assert(tui_focus_key('\t', &focus, 3) && focus == 0);
    assert(!tui_focus_key('\t', &focus, 0));
    assert(tui_theme_parse("07=17\n73=30\n", 12));
    assert(palette[TUI_INPUT] == 0x30 && palette[TUI_STRING] == 0x0a);
    tui_input_draw(&input, 2, 2, 2, 1);
    tui_checkbox(1, 3, "Enabled", checked, 1);
    tui_radio(1, 4, "Choice", 1, 0);
    tui_button(1, 5, 12, "Apply", 1, 1);
    char path[256];
    assert(tui_theme_path(path, "custom"));
    assert(!strcmp(path, "/ETC/THEMES/CUSTOM.THM"));
    assert(tui_theme_path(path, "/HOME/test.thm"));
    assert(!strcmp(path, "/ETC/THEMES/TEST.THM"));
    assert(!tui_theme_path(path, "too-long-name"));
    assert(!tui_theme_path(path, "test.txt"));
    tui_clear(0x07);
    assert(tui_theme_parse("70=70\n71=70\n", 12));
    tui_choice_row(2, 2, 30, "First", 0, TUI_POPUP);
    tui_choice_row(2, 3, 30, "Second", 1, TUI_POPUP);
    tui_present(0, 0);
    assert(frame[3 * 80 + 2].character == '>');
    assert(frame[2 * 80 + 2].character == ' ');
    assert(frame[3 * 80 + 2].color != frame[2 * 80 + 2].color);
    assert(tui_theme_parse("70=00\n71=00\n", 12));
    tui_choice_row(2, 3, 30, "Readable", 1, TUI_POPUP);
    tui_present(0, 0);
    assert((frame[3 * 80 + 2].color & 15) != (frame[3 * 80 + 2].color >> 4));
    tui_theme_parse("", 0);
    const char* choices[] = { "First", "Second", "Third" };
    keys[0] = TUI_DOWN;
    keys[1] = TUI_DOWN;
    keys[2] = TUI_UP;
    keys[3] = '\n';
    position = 0;
    assert(tui_menu("Navigation", choices, 3, 0) == 1);
    keys[0] = TUI_DOWN;
    keys[1] = 27;
    position = 0;
    assert(tui_menu("Cancel", choices, 3, 0) == -1);
    tui_clear(0x07);
    assert(tui_theme_parse("07=70", 5));
    tui_sample(0, 0, "raw", 0x07);
    assert(tui_modal_begin("sample", 30, 7));
    tui_modal_end();
    tui_present(0, 0);
    assert(frame[0].color == 0x07);
    tui_text(0, 0, "themed", 0x07);
    tui_present(0, 0);
    assert(frame[0].color == 0x70);
    tui_theme_parse("", 0);
    assert(palette[TUI_POPUP] == 0x70 && palette[TUI_DESKTOP] == 0x17);
    const char* many[20];
    for (int i = 0; i < 20; ++i) {
        many[i] = "Theme";
    }
    keys[0] = TUI_END;
    keys[1] = '\n';
    position = 0;
    assert(tui_menu("Many themes", many, 20, 0) == 19);
    tui_clear(0x07);
    tui_window w = { 2, 2, 10, 5, 1, "Test" };
    tui_window_draw(&w);
    tui_window_text(&w, -2, 0, "abcdefghijklm", 0x1e);
    assert(screen[3][3].character == 'c');
    assert(screen[3][10].character == 'j');
    assert(screen[3][11].character == '|');
    ktui_cell original[25][80];
    memcpy(original, screen, sizeof(screen));
    for (int i = 0; i < MODALS; ++i) {
        assert(tui_modal_begin("modal", 30, 7));
    }
    assert(!tui_modal_begin("overflow", 30, 7));
    for (int i = 0; i < MODALS; ++i) {
        tui_modal_end();
    }
    assert(!memcmp(original, screen, sizeof(screen)));
    u32 selected = 0, top = 0;
    assert(tui_list_key(TUI_END, &selected, &top, 100, 10));
    assert(selected == 99 && top == 90);
    tui_list_key(TUI_HOME, &selected, &top, 100, 10);
    assert(selected == 0 && top == 0);
    tui_list_key(TUI_PAGE_DOWN, &selected, &top, 100, 10);
    assert(selected == 10 && top == 1);
    tui_list_key(TUI_PAGE_UP, &selected, &top, 100, 10);
    assert(selected == 0 && top == 0);
    tui_list_key(TUI_DOWN, &selected, &top, 0, 10);
    assert(selected == 0 && top == 0);
    tui_scrollbar(79, 0, 25, 90, 10, 100);
    assert(screen[24][79].character == '#');
    const char* items[] = { "one", "two", "three" };
    memcpy(original, screen, sizeof(screen));
    keys[0] = TUI_DOWN;
    keys[1] = '\n';
    position = 0;
    assert(tui_menu("Choose", items, 3, 0) == 1);
    assert(!memcmp(original, screen, sizeof(screen)));
    assert(tui_theme_parse("# test\n17=70\n1F=07\n", 19));
    assert(palette[0x17] == 0x70 && palette[0x1f] == 0x07);
    assert(!tui_theme_parse("17=ZZ\n", 6));
    assert(palette[0x17] == 0x70);
    assert(!tui_theme_parse("17=70\nBAD", 9));
    assert(palette[0x17] == 0x70);
    assert(tui_theme_parse("17=07\r\n17=70", 12));
    assert(palette[0x17] == 0x70);
    assert(tui_theme_parse("", 0));
    keys[0] = 27;
    position = 0;
    assert(tui_menu("Cancel", items, 3, 0) == -1);
    assert(!memcmp(original, screen, sizeof(screen)));
    puts("PASS: TUI clipping, nested modal restoration, list navigation, scrollbar");
    return 0;
}
