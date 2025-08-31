/*------------------------------------------------------------------------------
 *  Kintsugi OS KKLIBC source code
 *  File: kklibc/datatypes/vector.h
 *  Title: Библиотека для работы с векторами (заголовочный файл)
 *	Description: null
 * ----------------------------------------------------------------------------*/
#ifndef DATATYPE_VECTOR_H
#define DATATYPE_VECTOR_H

#include "../ctypes.h"

typedef struct vector {
    void** data;
    u32 size;
    u32 capacity;
} vector_t;

/**
 * @brief Создать вектор
 *
 * @param initial_capacity начальная вместимость
 * @return vector_t* объект вектора
 **/
vector_t* vector_create(u32 initial_capacity);

/**
 * @brief Добавить элемент в конец вектора
 *
 * @param vec вектор
 * @param item элемент
 **/
void vector_push_back(vector_t* vec, void* item);

#endif    // DATATYPE_VECTOR_H
