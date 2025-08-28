/*------------------------------------------------------------------------------
 *  Kintsugi OS C Libraries source code
 *  File: libc/mem.h
 *  Title: Функции работы с памятью (заголовочный файл mem.c)
 *	Description: null
 * ----------------------------------------------------------------------------*/

#ifndef MEM_H
#define MEM_H

#include "ctypes.h"

#define HEAP_START 0x100000     // Начинаем кучу с 1 МБ (выше ядра)
#define HEAP_SIZE 0x1000000     // Размер кучи: 1 МБ
#define BLOCK_SIZE 16           // Минимальный размер блока

/**
 * @brief Блок памяти
 *
 **/
typedef struct mem_block {
    u32 size;
    struct mem_block* next;
    u8 is_free;
} mem_block_t;

/**
 * @brief Структура информации о памяти
 *
 **/
typedef struct meminfo {
    u32 heap_start;
    u32 heap_size;
    u32 heap_current_end;
    u32 block_size;
    mem_block_t* free_blocks;
    u32 total_used;
    u32 total_free;
    u32 block_count;
} meminfo_t;

/**
 * @brief Получение информации о памяти
 *
 * @return meminfo_t
 **/
meminfo_t get_meminfo();

/**
 * @brief Получение адреса свободной памяти
 *
 **/
void get_freememaddr();

/**
 * @brief Увеличение хипа
 *
 * @param size размер
 * @return int
 **/
int expand_heap(u32 size);

/**
 * @brief Инициализация хипа
 *
 **/
void heap_init();

/**
 * @brief Аллокация памяти
 *
 * @param size размер
 * @return void*
 **/
void* kmalloc(u32 size);

/**
 * @brief Реаллокация памяти
 *
 * @param ptr указатель
 * @param size размер
 * @return void*
 **/
void* krealloc(void* ptr, u32 size);

/**
 * @brief Освобождение памяти
 *
 * @param ptr указатель
 **/
void kfree(void* ptr);

/**
 * @brief Информация о памяти
 **/
void kmemdump();

#endif
