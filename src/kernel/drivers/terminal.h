/*------------------------------------------------------------------------------
 *  Kintsugi OS Drivers source code
 *  File: drivers/terminal.h
 *  Title: Заголовочный файл terminal.c
 *  Author: alexeev-prog
 *  License: MIT License
 * ------------------------------------------------------------------------------
 *	Description: null
 * ----------------------------------------------------------------------------*/

#ifndef TERMINAL_H
#define TERMINAL_H

#include "../kklibc/ctypes.h"

/* Конфигурация терминала */
#define TERMINAL_WIDTH 80    // логическая ширина терминала
#define TERMINAL_HEIGHT 200    // логическая высота терминала
#define SCREEN_WIDTH 80    // Физическая ширина экрана
#define SCREEN_HEIGHT 25    // Физическая высота экрана

/* для хранения символа с атрибутами */
typedef struct {
    u8 character;
    u8 attribute;    // цвет и атрибуты
} terminal_char_t;

typedef struct {
    // буффер
    terminal_char_t buffer[TERMINAL_HEIGHT][TERMINAL_WIDTH];

    u32 cursor_x;
    u32 cursor_y;

    u32 scroll_offset;

    u8 current_attribute;

    u8 insert_mode;

    u8 dirty;
    u8 auto_scroll;
} terminal_state_t;

/* Инициализация терминала */
void terminal_init(void);

/* Основной API для вывода (заменяет старый kprint) */
void terminal_putchar(char c);
void terminal_print(const char* str);
void terminal_print_colored(const char* str, u8 color);
void terminal_print_at(char* str, int col, int row, int color);

/* Управление курсором */
void terminal_set_cursor(u32 x, u32 y);
void terminal_get_cursor(u32* x, u32* y);

/* Управление скроллом */
void terminal_scroll_up(u32 lines);
void terminal_scroll_down(u32 lines);
void terminal_scroll_to(u32 line);
void terminal_scroll_to_bottom(void);

/* Управление атрибутами */
void terminal_set_color(u8 color);
u8 terminal_get_color(void);
void terminal_reset_color(void);

/* Очистка */
void terminal_clear(void);
void terminal_clear_line(u32 line);

/* Обновление экрана (рендеринг логического буфера на VGA) */
void terminal_refresh(void);

/* Обработка ввода (вызывается из keyboard.c) */
void terminal_handle_input(char c);
void terminal_handle_backspace(void);
void terminal_handle_enter(void);
void terminal_handle_arrow_up(void);
void terminal_handle_arrow_down(void);

/* Получение состояния */
terminal_state_t* terminal_get_state(void);
u32 terminal_get_scroll_offset(void);
u32 terminal_get_buffer_height(void);

#endif
