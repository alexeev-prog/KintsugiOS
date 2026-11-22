/*------------------------------------------------------------------------------
 *  Kintsugi OS Drivers source code
 *  File: drivers/terminal.c
 *  Title: Функции работы с терминалом
 *  Author: alexeev-prog
 *  License: MIT License
 * ------------------------------------------------------------------------------
 *	Description: Терминал - логический слой абстракции над экраном
 * ----------------------------------------------------------------------------*/

#include "terminal.h"

#include "../kklibc/mem.h"
#include "../kklibc/stdio.h"
#include "../kklibc/stdlib.h"
#include "screen.h"
#include "framebuffer.h"
#include "keyboard.h"
#include "../../shared/text.h"

/* Глобальное состояние терминала */
static terminal_state_t term_state;
/* Hardware-independent presentation snapshot for diagnostics/headless tests. */
u16 console_cells[MAX_ROWS * MAX_COLS];
static u8 ansi_state;
static u8 ansi_value;
static u32 utf_code, utf_min;
static u8 utf_left;

static int terminal_ansi(char c) {
    if (!ansi_state && c == 27) { ansi_state = 1; return 1; }
    if (ansi_state == 1) {
        if (c == '[') { ansi_state = 2; ansi_value = 0; return 1; }
        ansi_state = 0;
        return 0;
    }
    if (ansi_state == 2) {
        if (c >= '0' && c <= '9') {
            ansi_value = ansi_value * 10 + c - '0';
            return 1;
        }
        if (c == 'm') {
            if (ansi_value == 0) terminal_reset_color();
            else if (ansi_value >= 30 && ansi_value <= 37) {
                static const u8 colors[] = { 0x04, 0x02, 0x06, 0x01, 0x04, 0x05, 0x03, 0x0f };
                terminal_set_color(colors[ansi_value - 30]);
            }
            ansi_state = 0;
            return 1;
        }
        ansi_state = 0;
    }
    return 0;
}

/* Инициализация терминала */
void terminal_init(void) {
    for (u32 y = 0; y < TERMINAL_HEIGHT; y++) {
        for (u32 x = 0; x < TERMINAL_WIDTH; x++) {
            term_state.buffer[y][x].character = ' ';
            term_state.buffer[y][x].attribute = WHITE_ON_BLACK;
        }
    }

    term_state.cursor_x = 0;
    term_state.cursor_y = 0;
    term_state.scroll_offset = 0;
    term_state.current_attribute = WHITE_ON_BLACK;
    term_state.insert_mode = 0;
    term_state.dirty = 1;
    term_state.auto_scroll = 1;

    clear_screen();

    printf(
        "[TERMINAL] Initializing terminal %dx%d (screen %dx%d)\n",
        TERMINAL_WIDTH,
        TERMINAL_HEIGHT,
        SCREEN_WIDTH,
        SCREEN_HEIGHT);
}

/* Внутренняя функция: перенос строки в буфере */
static void terminal_newline(void) {
    term_state.cursor_x = 0;
    term_state.cursor_y++;

    if (term_state.cursor_y >= TERMINAL_HEIGHT) {
        for (u32 y = 1; y < TERMINAL_HEIGHT; y++) {
            for (u32 x = 0; x < TERMINAL_WIDTH; x++) {
                term_state.buffer[y - 1][x] = term_state.buffer[y][x];
            }
        }

        for (u32 x = 0; x < TERMINAL_WIDTH; x++) {
            term_state.buffer[TERMINAL_HEIGHT - 1][x].character = ' ';
            term_state.buffer[TERMINAL_HEIGHT - 1][x].attribute = term_state.current_attribute;
        }

        term_state.cursor_y = TERMINAL_HEIGHT - 1;
    }

    term_state.dirty = 1;
    if (term_state.auto_scroll) {
        terminal_scroll_to_bottom();
    }
}

void terminal_putchar(char c) {
    u8 byte = (u8)c;
    if (utf_left) {
        if ((byte & 0xc0) == 0x80) {
            utf_code = (utf_code << 6) | (byte & 63);
            if (--utf_left) return;
            c = utf_code >= utf_min && utf_code <= 0x10ffff
                && !(utf_code >= 0xd800 && utf_code <= 0xdfff) ? text_glyph(utf_code) : '?';
        } else {
            utf_left = 0;
            terminal_putchar('?');
            terminal_putchar(c);
            return;
        }
    } else if (byte >= 0xc2 && byte <= 0xf4) {
        utf_left = byte < 0xe0 ? 1 : byte < 0xf0 ? 2 : 3;
        utf_min = byte < 0xe0 ? 0x80 : byte < 0xf0 ? 0x800 : 0x10000;
        utf_code = byte & (byte < 0xe0 ? 31 : byte < 0xf0 ? 15 : 7);
        return;
    }
    if (terminal_ansi(c)) return;
    /* Move only: line editors erase explicitly by writing spaces. */
    if (c == '\b') {
        if (term_state.cursor_x) {
            --term_state.cursor_x;
        } else if (term_state.cursor_y) {
            --term_state.cursor_y;
            term_state.cursor_x = TERMINAL_WIDTH - 1;
        }
        term_state.dirty = 1;
        return;
    }
    if (c == '\n') {
        terminal_newline();
        return;
    }

    if (c == '\r') {
        term_state.cursor_x = 0;
        term_state.dirty = 1;
        return;
    }

    if (c == '\t') {
        term_state.cursor_x = (term_state.cursor_x + 4) & ~3;
        if (term_state.cursor_x >= TERMINAL_WIDTH) {
            terminal_newline();
        }
        term_state.dirty = 1;
        return;
    }

    if (term_state.cursor_x >= TERMINAL_WIDTH) {
        terminal_newline();
    }

    term_state.buffer[term_state.cursor_y][term_state.cursor_x].character = c;
    term_state.buffer[term_state.cursor_y][term_state.cursor_x].attribute = term_state.current_attribute;

    term_state.cursor_x++;
    term_state.dirty = 1;

    if (term_state.auto_scroll) {
        terminal_scroll_to_bottom();
    }
}

/* Вывод строки */
void terminal_print(const char* str) {
    while (*str) {
        terminal_putchar(*str);
        str++;
    }
    terminal_refresh();    // обновляем экран
}

void terminal_print_at(char* str, int col, int row, int color) {
    u32 old_x = 0, old_y = 0;
    terminal_get_cursor(&old_x, &old_y);
    terminal_set_cursor(col, row);
    terminal_print_colored(str, color);
    terminal_set_cursor(old_x, old_y);
}

/* Цветной вывод */
void terminal_print_colored(const char* str, u8 color) {
    u8 old_color = term_state.current_attribute;
    term_state.current_attribute = color;
    terminal_print(str);
    term_state.current_attribute = old_color;
}

/* Установка позиции курсора в логическом буфере */
void terminal_set_cursor(u32 x, u32 y) {
    if (x >= TERMINAL_WIDTH) {
        x = TERMINAL_WIDTH - 1;
    }
    if (y >= TERMINAL_HEIGHT) {
        y = TERMINAL_HEIGHT - 1;
    }

    term_state.cursor_x = x;
    term_state.cursor_y = y;
    term_state.dirty = 1;
}

/* Получение позиции курсора */
void terminal_get_cursor(u32* x, u32* y) {
    if (x) {
        *x = term_state.cursor_x;
    }
    if (y) {
        *y = term_state.cursor_y;
    }
}

void terminal_scroll_up(u32 lines) {
    if (lines == 0) {
        return;
    }

    if (term_state.scroll_offset >= lines) {
        term_state.scroll_offset -= lines;
    } else {
        term_state.scroll_offset = 0;
    }

    term_state.dirty = 1;
}

void terminal_scroll_down(u32 lines) {
    if (lines == 0) {
        return;
    }

    u32 max_scroll = 0;
    if (TERMINAL_HEIGHT > SCREEN_HEIGHT) {
        max_scroll = TERMINAL_HEIGHT - SCREEN_HEIGHT;
    }

    if (max_scroll == 0) {
        term_state.scroll_offset = 0;
        term_state.dirty = 1;
        return;
    }

    if (term_state.scroll_offset + lines <= max_scroll) {
        term_state.scroll_offset += lines;
    } else {
        term_state.scroll_offset = max_scroll;
    }

    term_state.dirty = 1;
}

/* Прокрутка к определенной строке */
void terminal_scroll_to(u32 line) {
    u32 max_scroll = 0;
    if (TERMINAL_HEIGHT > SCREEN_HEIGHT) {
        max_scroll = TERMINAL_HEIGHT - SCREEN_HEIGHT;
    }

    if (line <= max_scroll) {
        term_state.scroll_offset = line;
    } else {
        term_state.scroll_offset = max_scroll;
    }
    term_state.dirty = 1;
}

/* Прокрутка к нижней части */
void terminal_scroll_to_bottom(void) {
    term_state.scroll_offset =
        term_state.cursor_y >= SCREEN_HEIGHT ? term_state.cursor_y - SCREEN_HEIGHT + 1 : 0;
    term_state.dirty = 1;
}

/* Установка цвета */
void terminal_set_color(u8 color) {
    term_state.current_attribute = color;
}

/* Получение текущего цвета */
u8 terminal_get_color(void) {
    return term_state.current_attribute;
}

/* Сброс цвета к умолчанию */
void terminal_reset_color(void) {
    term_state.current_attribute = WHITE_ON_BLACK;
}

/* Полная очистка буфера */
void terminal_clear(void) {
    utf_left = ansi_state = 0;
    for (u32 y = 0; y < TERMINAL_HEIGHT; y++) {
        for (u32 x = 0; x < TERMINAL_WIDTH; x++) {
            term_state.buffer[y][x].character = ' ';
            term_state.buffer[y][x].attribute = WHITE_ON_BLACK;
        }
    }

    term_state.cursor_x = 0;
    term_state.cursor_y = 0;
    term_state.scroll_offset = 0;
    term_state.dirty = 1;
}

/* Очистка строки */
void terminal_clear_line(u32 line) {
    if (line >= TERMINAL_HEIGHT) {
        return;
    }

    for (u32 x = 0; x < TERMINAL_WIDTH; x++) {
        term_state.buffer[line][x].character = ' ';
        term_state.buffer[line][x].attribute = WHITE_ON_BLACK;
    }
    term_state.dirty = 1;
}

/* Низкоуровневая отрисовка */
static void terminal_direct_render(void) {
    volatile u16* vidmem = (volatile u16*)VIDEO_ADDRESS;
    static u16 previous[MAX_ROWS][MAX_COLS];
    static int initialized, old_cursor = -1;
    static u32 last_generation;
    int graphics = framebuffer_get_info()->available;
    if (graphics) {
        framebuffer_mouse_hide();
        if (last_generation != framebuffer_generation()) {
            initialized = 0;
            last_generation = framebuffer_generation();
        }
    }
    int cursor = -1;
    if (term_state.cursor_y >= term_state.scroll_offset
        && term_state.cursor_y < term_state.scroll_offset + MAX_ROWS
        && term_state.cursor_x < MAX_COLS)
        cursor = (term_state.cursor_y - term_state.scroll_offset) * MAX_COLS + term_state.cursor_x;
    if (graphics && !initialized) framebuffer_clear(0x141820);
    if (graphics) framebuffer_chrome(keyboard_russian(), term_state.scroll_offset
        < (term_state.cursor_y >= MAX_ROWS ? term_state.cursor_y - MAX_ROWS + 1 : 0));

    u32 max_scroll = 0;
    if (TERMINAL_HEIGHT > SCREEN_HEIGHT) {
        max_scroll = TERMINAL_HEIGHT - SCREEN_HEIGHT;
    }

    if (term_state.scroll_offset > max_scroll) {
        term_state.scroll_offset = max_scroll;
    }

    u32 start_line = term_state.scroll_offset;

    for (u32 screen_row = 0; screen_row < MAX_ROWS; screen_row++) {
        u32 buffer_row = start_line + screen_row;

        for (u32 screen_col = 0; screen_col < MAX_COLS; screen_col++) {
            u32 offset = (screen_row * MAX_COLS + screen_col) * 2;

            char character = ' ';
            char attribute = WHITE_ON_BLACK;

            if (buffer_row < TERMINAL_HEIGHT && screen_col < TERMINAL_WIDTH) {
                character = term_state.buffer[buffer_row][screen_col].character;
                attribute = term_state.buffer[buffer_row][screen_col].attribute;
            }

            // Записываем напрямую в видеопамять
            u16 cell = (u8)character | ((u16)(u8)attribute << 8);
            console_cells[offset / 2] = cell;
            if (graphics && (!initialized || previous[screen_row][screen_col] != cell
                || (int)(offset / 2) == cursor || (int)(offset / 2) == old_cursor))
                framebuffer_cell(screen_col, screen_row, character, attribute, (int)(offset / 2) == cursor);
            previous[screen_row][screen_col] = cell;
            if (!graphics && vidmem[offset / 2] != cell) {
                vidmem[offset / 2] = cell;
            }
        }
    }

    initialized = 1;
    old_cursor = cursor;
    if (graphics) framebuffer_mouse_show();
    u32 cursor_screen_row = 0;
    u8 cursor_visible = 0;

    if (term_state.cursor_y >= start_line && term_state.cursor_y < start_line + MAX_ROWS) {
        cursor_screen_row = term_state.cursor_y - start_line;
        cursor_visible = 1;
    }

    if (cursor_visible && term_state.cursor_x < MAX_COLS) {
        set_cursor_offset(get_offset(term_state.cursor_x, cursor_screen_row));
    } else {
        set_cursor_offset(get_offset(0, MAX_ROWS));
    }
}

void terminal_refresh(void) {
    if (!term_state.dirty) {
        return;
    }
    extern u32 tick;
    static u32 refresh_tick = ~0u;
    if (framebuffer_get_info()->available && tick && refresh_tick == tick) return;
    refresh_tick = tick;

    terminal_direct_render();

    term_state.dirty = 0;
}

void terminal_handle_input(char c) {
    terminal_putchar(c);
    terminal_refresh();
}

void terminal_handle_backspace(void) {
    if (term_state.cursor_x > 0) {
        term_state.cursor_x--;
        term_state.buffer[term_state.cursor_y][term_state.cursor_x].character = ' ';
        term_state.dirty = 1;
        terminal_refresh();
    }
}

void terminal_handle_enter(void) {
    terminal_newline();

    u32 cursor_line = term_state.cursor_y;
    u32 visible_bottom = term_state.scroll_offset + SCREEN_HEIGHT;

    if (cursor_line >= visible_bottom) {
        term_state.scroll_offset = cursor_line - SCREEN_HEIGHT + 1;
        if (term_state.scroll_offset > TERMINAL_HEIGHT - SCREEN_HEIGHT) {
            term_state.scroll_offset = TERMINAL_HEIGHT - SCREEN_HEIGHT;
        }
    }

    terminal_refresh();
}

void terminal_handle_arrow_up(void) {
    terminal_scroll_up(1);
    terminal_refresh();
}

void terminal_handle_arrow_down(void) {
    terminal_scroll_down(1);
    terminal_refresh();
}

/* Получение состояния терминала */
terminal_state_t* terminal_get_state(void) {
    return &term_state;
}

u32 terminal_get_scroll_offset(void) {
    return term_state.scroll_offset;
}

u32 terminal_get_buffer_height(void) {
    return TERMINAL_HEIGHT;
}
