// [file name]: mem.h (обновленная версия)
/*------------------------------------------------------------------------------
*  Kintsugi OS C Libraries source code
*  File: libc/mem.h
*  Title: Функции работы с памятью (заголовочный файл mem.c)
*	Description: null
* ----------------------------------------------------------------------------*/

#ifndef MEM_H
#define MEM_H

#include "ctypes.h"

#define HEAP_START 0x100000  // Начинаем кучу с 1 МБ (выше ядра)
#define HEAP_SIZE  0x100000  // Размер кучи: 1 МБ
#define BLOCK_SIZE 16        // Минимальный размер блока

typedef struct mem_block {
    u32 size;
    struct mem_block* next;
    u8 is_free;
} mem_block_t;

typedef struct meminfo {
    u32 heap_start;
    u32 heap_size;
    u32 block_size;
    mem_block_t* free_blocks;
    u32 total_used;
    u32 total_free;
    u32 block_count;
    u32 used_pages;      // Добавлено: использовано страниц
    u32 free_pages;      // Добавлено: свободно страниц
    u32 total_pages;     // Добавлено: всего страниц
    u32 page_size;       // Добавлено: размер страницы
} meminfo_t;

void *get_physaddr(void *virtualaddr);
meminfo_t get_meminfo();
void get_freememaddr();

// Новые функции
void heap_init();
void* kmalloc_a(u32 size);
void* kmalloc(u32 size);
void kfree(void* ptr);
void kmemdump();

#endif
