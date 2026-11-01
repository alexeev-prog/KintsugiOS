#include "../drivers/keyboard.h"
#include "../drivers/terminal.h"
#include "../kklibc/kklibc.h"
#include "shell.h"

#define HISTORY_SIZE 16
static char line[SHELL_LINE_SIZE];
static char history[HISTORY_SIZE][SHELL_LINE_SIZE];
static char draft[SHELL_LINE_SIZE];
static u32 length, cursor, rendered, origin_x, origin_y;
static u32 history_count, history_position;

static void prompt(void) {
    terminal_print("!#> ");
    terminal_get_cursor(&origin_x, &origin_y);
    length = cursor = rendered = 0;
    line[0] = 0;
    history_position = history_count;
}

static void redraw(void) {
    terminal_set_cursor(origin_x, origin_y);
    u32 cells = length > rendered ? length : rendered;
    for (u32 i = 0; i < cells; ++i) {
        terminal_putchar(i < length ? line[i] : ' ');
    }
    terminal_state_t* state = terminal_get_state();
    u32 end_y = origin_y + (origin_x + cells) / TERMINAL_WIDTH;
    if (end_y >= TERMINAL_HEIGHT) {
        origin_y -= end_y - (TERMINAL_HEIGHT - 1);
    }
    rendered = length;
    u32 position = origin_x + cursor;
    terminal_set_cursor(position % TERMINAL_WIDTH, origin_y + position / TERMINAL_WIDTH);
    state->auto_scroll = 1;
    terminal_scroll_to_bottom();
    terminal_refresh();
}

void line_editor_init(void) {
    prompt();
}

static void recall(int direction) {
    if (history_position == history_count) {
        strcpy(draft, line);
    }
    if (direction < 0 && history_position) {
        --history_position;
    }
    if (direction > 0 && history_position < history_count) {
        ++history_position;
    }
    strcpy(line, history_position == history_count ? draft : history[history_position]);
    cursor = length = strlen(line);
}

void line_editor_key(int key) {
    if (key == KEY_SCROLL_UP) {
        terminal_scroll_up(1);
        terminal_refresh();
        return;
    }
    if (key == KEY_SCROLL_DOWN) {
        terminal_scroll_down(1);
        terminal_refresh();
        return;
    }
    if (key == '\n') {
        cursor = length;
        redraw();
        terminal_print("\n");
        if (length) {
            if (history_count == HISTORY_SIZE) {
                for (u32 i = 1; i < HISTORY_SIZE; ++i) {
                    strcpy(history[i - 1], history[i]);
                }
                --history_count;
            }
            strcpy(history[history_count++], line);
        }
        shell_execute(line);
        terminal_print("\n");
        prompt();
        return;
    }
    if (key == 3) {
        terminal_print("^C\n");
        prompt();
        return;
    }
    if (key == KEY_LEFT && cursor) {
        --cursor;
    } else if (key == KEY_RIGHT && cursor < length) {
        ++cursor;
    } else if (key == KEY_HOME) {
        cursor = 0;
    } else if (key == KEY_END) {
        cursor = length;
    } else if (key == KEY_UP) {
        recall(-1);
    } else if (key == KEY_DOWN) {
        recall(1);
    } else if (key == '\t') {
        shell_complete(line, &length, &cursor);
    } else if (key == '\b' && cursor) {
        --cursor;
        for (u32 i = cursor; i < length; ++i) {
            line[i] = line[i + 1];
        }
        --length;
    } else if (key == KEY_DELETE && cursor < length) {
        for (u32 i = cursor; i < length; ++i) {
            line[i] = line[i + 1];
        }
        --length;
    } else if (key >= 32 && key < 127 && length < sizeof(line) - 1) {
        for (u32 i = length + 1; i > cursor; --i) {
            line[i] = line[i - 1];
        }
        line[cursor++] = key;
        ++length;
    }
    redraw();
}
