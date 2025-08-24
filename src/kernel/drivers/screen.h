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

// #include "../common.h"
//
// #define VIDEO_ADDRESS 0xb8000	// Адрес начала VGA для печати символов
// #define MAX_ROWS 25				// макс. строк
// #define MAX_COLS 80				// макс. столбцов
//
// #define WHITE_ON_BLACK 0x0f		// 0x0 == white fg, 0xf == black bg
// #define WHITE_ON_BLUE 0x1f
//
// // Адреса I/O портов для взаимодействия с экраном.
// #define REG_SCREEN_CTRL 0x3d4	// этот порт для описания данных
// #define REG_SCREEN_DATA 0x3d5	// а этот порт для самих данных
//
// void	kprint(u8 *str);
// void	putchar(u8 character, u8 attribute_byte);
// void	clear_screen();
// void	write(u8 character, u8 attribute_byte, u16 offset);
// void	scroll_line();
// u16		get_cursor();
// void	set_cursor(u16 pos);
// ^^^ Старый код

#ifndef SCREEN_H
#define SCREEN_H

// Подключаем типы из CPU
#include "../kklibc/ctypes.h"

#define VIDEO_ADDRESS 0xb8000					// Видео-адрес
#define MAX_ROWS 25								// Максимальное кол-во линий
#define MAX_COLS 80								// Максимальное кол-во колонок
#define WHITE_ON_BLACK 0x0f						// HEX-код белого на черном
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

// Цветовые коды
#define WHITE_ON_BLACK_CLR_CODE 0
#define BLUE_ON_BLACK_CLR_CODE 1
#define GREEN_ON_BLACK_CLR_CODE 2
#define CYAN_ON_BLACK_CLR_CODE 3
#define RED_ON_BLACK_CLR_CODE 4
#define MAGENTA_ON_BLACK_CLR_CODE 5
#define BROWN_ON_BLACK_CLR_CODE 6
#define LGREY_ON_BLACK_CLR_CODE 7
#define DGREY_ON_BLACK_CLR_CODE 8
#define LBLUE_ON_BLACK_CLR_CODE 9
#define LGREEN_ON_BLACK_CLR_CODE 10
#define LCYAN_ON_BLACK_CLR_CODE 11
#define LRED_ON_BLACK_CLR_CODE 12
#define LMAGENTA_ON_BLACK_CLR_CODE 13
#define YELLOW_ON_BLACK_CLR_CODE 14
#define WHITE_ON_BLUE_CLR_CODE 15
#define WHITE_ON_RED_CLR_CODE 16
#define RED_ON_WHITE_CLR_CODE 17
#define BLUE_ON_WHITE_CLR_CODE 18

/* Порты ввода/вывода экрана */
#define REG_SCREEN_CTRL 0x3d4
#define REG_SCREEN_DATA 0x3d5

/* Публичное API ядра */
int get_cursor_offset();
void clear_screen();
void halted_cpu_screen_clear();
void kprint_at(char *message, int col, int row, int color);
void kprint(char *message);
void kprintln(char *message);
void kprintln_colored(char *message, int color);
void kprint_colored(char *message, int color);
void kprint_backspace();
void panic_red_screen(char* title, char* description);

#endif
