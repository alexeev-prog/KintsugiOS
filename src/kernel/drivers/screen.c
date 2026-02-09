/*------------------------------------------------------------------------------
 *  Kintsugi OS Drivers source code
 *  File: drivers/screen.c
 *  Title: Функции работы с экраном
 *  Author: alexeev-prog
 *  License: MIT License
 * ------------------------------------------------------------------------------
 *	Description: null
 * ----------------------------------------------------------------------------*/

#include "screen.h"

#include "../kklibc/ctypes.h"
#include "../kklibc/stdlib.h"
#include "lowlevel_io.h"
#include "screen_output_switch.h"
#ifdef TERMINAL_DRIVER_ENABLED
#    include "terminal.h"
#endif

#define INPUT_BUFFER_SIZE 256
static char input_buffer[INPUT_BUFFER_SIZE];
static int input_buffer_pos = 0;
static int input_ready = 0;

/**
 * Вывод сообщения в специфической локации
 * Если col, row отрицательные, то используем текущий оффсет
 */
void kprint_at(char* message, int col, int row, int color) {
    /* Установка курсора и оффсета если если col, row отрицательные */
    int offset;
    if (col >= 0 && row >= 0) {
        offset = get_offset(col, row);
    } else {
        offset = get_cursor_offset();
        row = get_offset_row(offset);
        col = get_offset_col(offset);
    }
    /* "Прокрутка" сообщения и его вывод */
    int i = 0;
    while (message[i] != 0) {
        offset = print_char(message[i++], col, row, color);

        /* Вычисление row/col для следующей итерации */
        row = get_offset_row(offset);
        col = get_offset_col(offset);
    }
}

void kprint_at_pos(int col, int row, char c, char attr) {
    if (col < 0 || col >= MAX_COLS || row < 0 || row >= MAX_ROWS) {
        return;
    }

    if (col >= 0 && col < MAX_COLS && row >= 0 && row < MAX_ROWS) {
        u8* vidmem = (u8*)VIDEO_ADDRESS;
        int offset = get_offset(col, row);
        vidmem[offset] = c;
        vidmem[offset + 1] = attr;
    }
}

void kprint(char* message) {
    if (IS_TERMINAL_MODE()) {
        terminal_print(message);
    } else {
        _screen_kprint(message);
    }
}

void kprintln(char* message) {
    kprint(message);
    kprint("\n");
}

void kprintln_colored(char* message, int color) {
    kprint_colored(message, color);
    kprint("\n");
}

void kprint_colored(char* message, int color) {
    if (IS_TERMINAL_MODE()) {
        terminal_print_colored(message, color);
    } else {
        _screen_kprint_colored(message, color);
    }
}

void kprint_backspace(void) {
    if (IS_TERMINAL_MODE()) {
        terminal_handle_backspace();
    } else {
        _screen_kprint_backspace();
    }
}

void _screen_kprint(char* message) {
    // Вывод текста. Цвет по умолчанию - белый на черном
    kprint_at(message, -1, -1, WHITE_ON_BLACK);
}

void _screen_kprintln(char* message) {
    // Вывод текста. Цвет по умолчанию - белый на черном
    kprint_at(message, -1, -1, WHITE_ON_BLACK);
    _screen_kprint("\n");
}

void _screen_kprintln_colored(char* message, int color) {
    /* Цветной вывод текста. Принимает также, в отличии от _screen_kprint, код цвета.*/
    kprint_at(message, -1, -1, color);
    _screen_kprint("\n");
}

void _screen_kprint_colored(char* message, int color) {
    /* Цветной вывод текста. Принимает также, в отличии от _screen_kprint, код цвета.*/
    kprint_at(message, -1, -1, color);
}

void _screen_kprint_backspace() {
    int offset = get_cursor_offset() - 2;
    int row = get_offset_row(offset);
    int col = get_offset_col(offset);

    // print_char(0x08, col, row, WHITE_ON_BLACK);

    print_char(' ', col, row, WHITE_ON_BLACK);

    set_cursor_offset(offset);
}

void halted_cpu_screen_clear() {
    int screen_size = MAX_COLS * MAX_ROWS;
    int i;
    u8* screen = (u8*)VIDEO_ADDRESS;

    for (i = 0; i < screen_size; i++) {
        screen[i * 2] = ' ';
        screen[i * 2 + 1] = WHITE_ON_BLUE;
    }
    set_cursor_offset(get_offset(0, 0));
}

void panic_red_screen(char* title, char* description) {
    int screen_size = MAX_COLS * MAX_ROWS;
    u8* screen = (u8*)VIDEO_ADDRESS;

    for (int i = 0; i < screen_size; ++i) {
        screen[i * 2] = ' ';
        screen[i * 2 + 1] = WHITE_ON_RED;
    }

    set_cursor_offset(get_offset(0, 0));

    _screen_kprint_colored("KINTSUGIOS KERNEL PANIC RED SCREEN\n\n", WHITE_ON_RED);
    _screen_kprint_colored(title, WHITE_ON_RED);
    _screen_kprint_colored("\n\n", WHITE_ON_RED);
    _screen_kprint_colored(description, WHITE_ON_RED);

    __asm__ volatile("hlt");
}

void printf_panic_screen(char* title, const char* reason_fmt, ...) {
    char description[256];
    va_list args;

    va_start(args, reason_fmt);
    vsnprintf(description, sizeof(description), reason_fmt, args);
    va_end(args);

    panic_red_screen(title, description);
}

/**
 * Функции вывода строки для ядра, использующие видео-память
 *
 * Если 'col' и 'row' отрицательные, мы пишем на текущей позиции курсора
 * Если 'attr' равен 0 он использует 'белое на чернм' по умолчанию
 * Возвращает оффсет следующего символа
 * Устанавливает курсор на оффсете
 */
int print_char(char c, int col, int row, char attr) {
    u8* vidmem = (u8*)VIDEO_ADDRESS;
    if (!attr) {
        attr = WHITE_ON_BLUE;
    }

    /* Контроль ошибок: вывод крассной 'E' если координаты неверные */
    if (col >= MAX_COLS || row >= MAX_ROWS) {
        vidmem[2 * (MAX_COLS) * (MAX_ROWS)-2] = 'E';
        vidmem[2 * (MAX_COLS) * (MAX_ROWS)-1] = RED_ON_WHITE;
        return get_offset(col, row);
    }

    int offset;
    if (col >= 0 && row >= 0) {
        offset = get_offset(col, row);
    } else {
        offset = get_cursor_offset();
    }

    if (c == '\n') {
        row = get_offset_row(offset);
        offset = get_offset(0, row + 1);
    } else if (c == 0x08) { /* Backspace */
        vidmem[offset] = ' ';
        vidmem[offset + 1] = attr;
    } else {
        vidmem[offset] = c;
        vidmem[offset + 1] = attr;
        offset += 2;
    }

    /* Проверяем, больше ли оффсет экрана, и скроллим */
    if (offset >= MAX_ROWS * MAX_COLS * 2) {
        int i;
        for (i = 1; i < MAX_ROWS; i++) {
            memory_copy(
                (u8*)(get_offset(0, i) + VIDEO_ADDRESS),
                (u8*)(get_offset(0, i - 1) + VIDEO_ADDRESS),
                MAX_COLS * 2);
        }

        /* Пустая последняя строка */
        char* last_line = (char*)(get_offset(0, MAX_ROWS - 1) + (u8*)VIDEO_ADDRESS);
        for (i = 0; i < MAX_COLS * 2; i++) {
            last_line[i] = 0;
        }

        offset -= 2 * MAX_COLS;
    }

    set_cursor_offset(offset);
    return offset;
}

int get_cursor_offset() {
    /* Используем VGA порты для получения текущей позиции курсора
     * 1. (data 14) Получаем высший байт оффсета курсора
     * 2. (data 15) Получаем низший байт
     */
    port_byte_out(REG_SCREEN_CTRL, 14);
    int offset = port_byte_in(REG_SCREEN_DATA) << 8; /* High byte: << 8 */
    port_byte_out(REG_SCREEN_CTRL, 15);
    offset += port_byte_in(REG_SCREEN_DATA);
    return offset * 2; /* Позиция * размер клетки */
}

void set_cursor_offset(int offset) {
    offset /= 2;
    port_byte_out(REG_SCREEN_CTRL, 14);
    port_byte_out(REG_SCREEN_DATA, (u8)(offset >> 8));
    port_byte_out(REG_SCREEN_CTRL, 15);
    port_byte_out(REG_SCREEN_DATA, (u8)(offset & 0xff));
}

void clear_screen() {
    if (IS_TERMINAL_MODE()) {
        terminal_clear();
        terminal_refresh();
        return;
    }

    int screen_size = MAX_COLS * MAX_ROWS;
    int i;
    u8* screen = (u8*)VIDEO_ADDRESS;

    for (i = 0; i < screen_size; i++) {
        screen[i * 2] = ' ';
        screen[i * 2 + 1] = WHITE_ON_BLACK;
    }
    set_cursor_offset(get_offset(0, 0));
}

int get_offset(int col, int row) {
    return 2 * (row * MAX_COLS + col);
}

int get_offset_row(int offset) {
    return offset / (2 * MAX_COLS);
}

int get_offset_col(int offset) {
    return (offset - (get_offset_row(offset) * 2 * MAX_COLS)) / 2;
}

void handle_input_char(char c) {
    if (input_ready) {
        return;
    }

    if (c == '\n' || c == '\r') {
        kprint("\n");
        input_buffer[input_buffer_pos] = '\0';
        input_ready = 1;
    } else if (c == 0x08 || c == 0x7F) {
        if (input_buffer_pos > 0) {
            input_buffer_pos--;
            kprint_backspace();
        }
    } else if (c >= 32 && c < 127) {    // Печатные символы
        if (input_buffer_pos < INPUT_BUFFER_SIZE - 1) {
            input_buffer[input_buffer_pos++] = c;
            char str[2] = { c, '\0' };
            kprint(str);
        }
    }
}

int read_line(char* buffer, int max_len) {
    input_buffer_pos = 0;
    input_ready = 0;

    while (!input_ready) {
        __asm__ volatile("hlt");
    }

    int len = strlen(input_buffer);
    if (len >= max_len) {
        len = max_len - 1;
    }

    strncpy(buffer, input_buffer, len);
    buffer[len] = '\0';

    return len;
}

int read_line_with_prompt(const char* prompt, char* buffer, int max_len) {
    kprint(prompt);
    return read_line(buffer, max_len);
}

char* get_input_buffer(void) {
    return input_buffer;
}

void clear_input_buffer(void) {
    input_buffer[0] = '\0';
    input_buffer_pos = 0;
    input_ready = 0;
}

int has_input_ready(void) {
    return input_ready;
}

char* take_input(void) {
    if (!input_ready) {
        return NULL;
    }

    input_ready = 0;
    return input_buffer;
}
