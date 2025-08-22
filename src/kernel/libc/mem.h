/*------------------------------------------------------------------------------
*  Kintsugi OS C Libraries source code
*  File: libc/mem.h
*  Title: Функции работы с памятью (заголовочный файл mem.c)
*	Description: null
* ----------------------------------------------------------------------------*/

#ifndef MEM_H
#define MEM_H

#include "../cpu/type.h"

#define HEAP_START 0x100000  // Начинаем кучу с 1 МБ (выше ядра)
#define HEAP_SIZE  0x100000  // Размер кучи: 1 МБ
#define BLOCK_SIZE 16        // Минимальный размер блока

typedef struct mem_block {
    u32 size;
    struct mem_block* next;
    u8 is_free;
} mem_block_t;

void memory_copy(u8 *source, u8 *dest, int nbytes);
void memory_set(u8 *dest, u8 val, u32 len);
void get_freememaddr();
u32 kmalloc(u32 size, int align, u32 *phys_addr);

// Новые функции
void heap_init();
void* kmalloc_new(u32 size);
void kfree(void* ptr);
void kmemdump();

#endif
