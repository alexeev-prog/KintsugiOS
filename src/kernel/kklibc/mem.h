/*------------------------------------------------------------------------------
*  Kintsugi OS C Libraries source code
*  File: libc/mem.h
*  Title: Функции работы с памятью (заголовочный файл mem.c)
*	Description: null
* ----------------------------------------------------------------------------*/

#ifndef MEM_H
#define MEM_H

#include "ctypes.h"
#include "../cpu/paging.h"

#define HEAP_VIRTUAL_START 0xC0000000  // Виртуальный адрес начала кучи (3GB)
#define HEAP_SIZE          0x100000    // Размер кучи: 1 МБ
#define BLOCK_SIZE         16          // Минимальный размер блока

typedef struct page_header {
    u32 physical_addr;
    u32 virtual_addr;
    struct page_header* next;
    u32 ref_count;
} page_header_t;

// Обновляем структуру блока памяти
typedef struct mem_block {
    u32 size;
    struct mem_block* next;
    u8 is_free;
    u8 is_page;
    page_header_t* page;
} mem_block_t;

// Добавляем информацию о страницах в структуру meminfo
typedef struct meminfo {
    u32 heap_virtual_start;
    u32 heap_physical_start;
    u32 heap_size;
    u32 block_size;
    mem_block_t* free_blocks;
    u32 total_used;
    u32 total_free;
    u32 block_count;
    u32 page_directory_phys;  // Физический адрес каталога страниц
    u32 total_pages;          // Общее количество страниц
    u32 used_pages;           // Используемые страницы
    u32 free_pages;           // Свободные страницы
} meminfo_t;

// Обновляем прототипы функций
void *get_physaddr(void *virtualaddr);
meminfo_t get_meminfo();
<<<<<<< HEAD
<<<<<<< HEAD

=======
void get_freememaddr();
>>>>>>> 3fe2ead (realize paging memory, add kmalloc aligned; todo: realize kmemdump fully)
=======
void get_freememaddr();
>>>>>>> 3fe2ead (realize paging memory, add kmalloc aligned; todo: realize kmemdump fully)
void heap_init();
void* kmalloc(u32 size);
void kfree(void* ptr);
void* krealloc(void* ptr, u32 size);
void *kmalloc_a(u32 size);
void kmemdump();

#endif
