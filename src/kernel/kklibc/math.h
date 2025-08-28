/*------------------------------------------------------------------------------
 *  Kintsugi OS KKLIBC source code
 *  File: kklibc/mem.h
 *  Title: Набор математических функций и алгоритмов (заголовчный файл)
 *	Description: null
 * ----------------------------------------------------------------------------*/
#ifndef MATH_H
#define MATH_H

#include "ctypes.h"

/**
 * @brief Фибоначчи
 *
 * @param num число
 * @return u32
 **/
u32 fibonacci(int num);

/**
 * @brief Возведение в степень
 *
 * @param b основа
 * @param e экспонента
 * @return int
 **/
int binary_pow(int b, u32 e);

#endif
