/*------------------------------------------------------------------------------
 *  Kintsugi OS Drivers source code
 *  File: drivers/screen.h
 *  Title: Заголовочный файл screen.c
 *  Last Change Date: 30 October 2023, 12:28 (UTC)
 *  Author: alexeev-prog
 *  License: GNU GPL v3
 * ------------------------------------------------------------------------------
 *	Description: null
 * ----------------------------------------------------------------------------*/

#ifndef SCREEN_H
#define SCREEN_H

// Подключаем типы из CPU
#include "../kklibc/ctypes.h"

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
void kprint(char* message);

/**
 * @brief Вывод текста с новой строкой
 *
 * @param message сообщение
 **/
void kprintln(char* message);

/**
 * @brief Цветной вывод с новой строкой
 *
 * @param message сообщение
 * @param color цвет
 **/
void kprintln_colored(char* message, int color);

/**
 * @brief Цветной вывод
 *
 * @param message сообщение
 * @param color цвет
 **/
void kprint_colored(char* message, int color);

/**
 * @brief Вывод символа backspace
 *
 **/
void kprint_backspace();

/**
 * @brief Красный экран для паники ядра
 *
 * @param title заголовок
 * @param description описание
 **/
void panic_red_screen(char* title, char* description);

void printf_panic_screen(char* title, const char* reason_fmt, ...);

#endif
