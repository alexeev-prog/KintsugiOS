/*------------------------------------------------------------------------------
 *  Kintsugi OS Drivers source code
 *  File: drivers/screen.h
 *  Title: Заголовочный файл screen.c
 *  Author: alexeev-prog
 *  License: MIT License
 * ------------------------------------------------------------------------------
 *	Description: null
 * ----------------------------------------------------------------------------*/

#ifndef SCREEN_H
#define SCREEN_H

// Подключаем типы из CPU
#include "../kklibc/ctypes.h"
#include "screen_output_switch.h"
#include "terminal.h"

#define VIDEO_ADDRESS 0xb8000    // Видео-адрес
#define MAX_ROWS 25    // Максимальное кол-во линий
#define MAX_COLS 80    // Максимальное кол-во колонок
#define WHITE_ON_BLACK 0x0f    // HEX-код белого на черном
#define WHITE_ON_BLUE 0x1f
#define WHITE_ON_RED 0x4f
#define BLUE_ON_WHITE 0x1f
#define WHITE_ON_DGREY 0x8

// HEX-коды разных цветов на черном
#define BLUE_ON_BLACK 0x01
#define GREEN_ON_BLACK 0x02
#define CYAN_ON_BLACK 0x03
#define RED_ON_BLACK 0x04
#define MAGENTA_ON_BLACK 0x05
#define BROWN_ON_BLACK 0x06
#define LGREY_ON_BLACK 0x07
#define DGREY_ON_BLACK 0x08
#define LBLUE_ON_BLACK 0x09
#define LGREEN_ON_BLACK 0x0A
#define LCYAN_ON_BLACK 0x0B
#define LRED_ON_BLACK 0x0C
#define LMAGENTA_ON_BLACK 0x0D
#define YELLOW_ON_BLACK 0x0E
#define RED_ON_WHITE 0xf4

/* Порты ввода/вывода экрана */
#define REG_SCREEN_CTRL 0x3d4
#define REG_SCREEN_DATA 0x3d5

/* Публичное API ядра */

/**
 * @brief Получение оффсета курсора
 *
 * @return int
 **/
int get_cursor_offset();

/**
 * @brief Стандартная очистка экрана
 *
 **/
void clear_screen();

/**
 * @brief Очистка и заполнение экраном halted cpu
 *
 **/
void halted_cpu_screen_clear();

void kprint(char* message);
void kprintln(char* message);
void kprintln_colored(char* message, int color);
void kprint_colored(char* message, int color);
void kprint_backspace(void);

void handle_input_char(char c);

/**
 * @brief Чтение строки с клавиатуры с экрана
 * @param buffer Буфер для строки
 * @param max_len Максимальная длина (включая нулевой символ)
 * @return Длина введенной строки (без нулевого символа)
 */
int read_line(char* buffer, int max_len);

/**
 * @brief Вывести приглашение и прочитать строку
 * @param prompt Приглашение (например, "Enter name: ")
 * @param buffer Буфер для строки
 * @param max_len Максимальная длина
 * @return Длина введенной строки
 */
int read_line_with_prompt(const char* prompt, char* buffer, int max_len);

/**
 * @brief Получить текущий буфер ввода (для шелла)
 * @return Указатель на буфер ввода
 */
char* get_input_buffer(void);

/**
 * @brief Очистить буфер ввода
 */
void clear_input_buffer(void);

/**
 * @brief Вывод текста в определенном месте
 *
 * @param message сообщение
 * @param col колонка
 * @param row ряд
 * @param color цвет
 **/
void kprint_at(char* message, int col, int row, int color);

/**
 * @brief Вывод текста
 *
 * @param message сообщение
 **/
void _screen_kprint(char* message);

/**
 * @brief Вывод текста с новой строкой
 *
 * @param message сообщение
 **/
void _screen_kprintln(char* message);

/**
 * @brief Цветной вывод с новой строкой
 *
 * @param message сообщение
 * @param color цвет
 **/
void _screen_kprintln_colored(char* message, int color);

/**
 * @brief Цветной вывод
 *
 * @param message сообщение
 * @param color цвет
 **/
void _screen_kprint_colored(char* message, int color);

/**
 * @brief Вывод символа backspace
 *
 **/
void _screen_kprint_backspace();

/**
 * @brief Красный экран для паники ядра
 *
 * @param title заголовок
 * @param description описание
 **/
void panic_red_screen(char* title, char* description);

void printf_panic_screen(char* title, const char* reason_fmt, ...);

void set_cursor_offset(int offset);
int print_char(char c, int col, int row, char attr);
int get_offset(int col, int row);
int get_offset_row(int offset);
int get_offset_col(int offset);

/**
 * @brief Вывод символа в определенной позиции
 * @param col колонка
 * @param row строка
 * @param c символ
 * @param attr атрибуты (цвет)
 */
void kprint_at_pos(int col, int row, char c, char attr);

#endif
