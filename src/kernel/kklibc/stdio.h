/*------------------------------------------------------------------------------
 *  Kintsugi OS KKLIBC source code
 *  File: kklibc/stdio.h
 *  Title: Набор средств ввода/вывода (заголовчный файл)
 *	Description: null
 * ----------------------------------------------------------------------------*/

#ifndef LIBC_STDIO_H
#define LIBC_STDIO_H

#include "../drivers/screen.h"

typedef char* va_list;

#define va_start(ap, last) (ap = (va_list) & last + sizeof(last))
#define va_arg(ap, type) (*(type*)((ap += sizeof(type)) - sizeof(type)))
#define va_end(ap) (ap = (va_list)0)

/**
 * @brief Стандартный форматированный вывод
 *
 * @param fmt строка
 * @param ... аргументы для форматирования
 **/
void printf(char* fmt, ...);

/**
 * @brief Цветной форматированный вывод
 *
 * @param fmt строка
 * @param color цвет
 * @param ... аргументы для форматирования
 **/
void printf_colored(char* fmt, int color, ...);

/**
 * @brief Функция форматированного вывода в определенном месте
 *
 * @param fmt строка
 * @param col колонка
 * @param row ряд
 * @param color код цвета
 * @param ... аргументы для оорматирования
 **/
void printf_at(char* fmt, int col, int row, int color, ...);

#endif    // LIBC_STDIO_H
