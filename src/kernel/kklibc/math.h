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

/**
 * @brief Вычисление факториала
 *
 * @param a переменная A
 * @param b переменная B
 * @param c переменная C
 * @return float
 **/
float calculate_discriminant(float a, float b, float c);

/**
 * @brief Факториал
 *
 * @param n число
 * @return u32
 **/
u32 factorial(long n);

/**
 * @brief Факторил из строки
 *
 * @param num_chars строка чисел
 * @return u32
 **/
u32 cfactorial_sum(char num_chars[]);

/**
 * @brief Факториал из массива чисел
 *
 * @param nums массив чисел
 * @param size размер массива
 * @return u32
 **/
u32 ifactorial_sum(long nums[], int size);

#endif
