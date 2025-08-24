// [file name]: paging.h
#ifndef PAGING_H
#define PAGING_H

#include "../kklibc/ctypes.h"

#define PAGE_SIZE 4096
#define PAGE_DIRECTORY_INDEX(x) (((x) >> 22) & 0x3FF)
#define PAGE_TABLE_INDEX(x) (((x) >> 12) & 0x3FF)
#define PAGE_GET_PHYS_ADDRESS(x) ((x) & 0xFFFFF000)

// Флаги для записей страниц
#define PAGE_PRESENT        0x1
#define PAGE_WRITE          0x2
#define PAGE_USER           0x4
#define PAGE_WRITE_THROUGH  0x8
#define PAGE_CACHE_DISABLE  0x10
#define PAGE_ACCESSED       0x20
#define PAGE_DIRTY          0x40
#define PAGE_GLOBAL         0x100

// Структура записи в каталоге страниц
typedef struct {
    u32 present    : 1;
    u32 rw         : 1;
    u32 user       : 1;
    u32 write_through : 1;
    u32 cache_disable : 1;
    u32 accessed   : 1;
    u32 reserved   : 1;
    u32 page_size  : 1;
    u32 global     : 1;
    u32 available  : 3;
    u32 frame      : 20;
} page_directory_entry_t;

// Структура записи в таблице страниц
typedef struct {
    u32 present    : 1;
    u32 rw         : 1;
    u32 user       : 1;
    u32 write_through : 1;
    u32 cache_disable : 1;
    u32 accessed   : 1;
    u32 dirty      : 1;
    u32 reserved   : 1;
    u32 global     : 1;
    u32 available  : 3;
    u32 frame      : 20;
} page_table_entry_t;

// Структура каталога страниц
typedef struct {
    page_directory_entry_t entries[1024];
} page_directory_t;

// Структура таблицы страниц
typedef struct {
    page_table_entry_t entries[1024];
} page_table_t;

// Функции для работы с paging
void paging_init();
void switch_page_directory(page_directory_t *new_dir);
page_directory_t *get_kernel_page_directory();
void map_page(void *virtual_addr, void *physical_addr, u32 flags);
void unmap_page(void *virtual_addr);
void *get_physical_address(void *virtual_addr);
u32 get_memory_used_pages();
u32 get_memory_free_pages();

// Функции для выровненного выделения памяти
void *kmalloc_a(u32 size);
void kfree_a(void *ptr);

#endif
