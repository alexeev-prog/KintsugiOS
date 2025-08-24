// [file name]: paging.c
#include "paging.h"
#include "../kklibc/kklibc.h"
#include "../drivers/screen.h"

// Текущий каталог страниц
static page_directory_t *current_directory = NULL;
// Каталог страниц ядра
static page_directory_t *kernel_directory = NULL;

// Статистика использования памяти
static u32 used_pages = 0;
static u32 total_pages = 0;

// Временная таблица страниц для идентичного отображения
static page_table_t *identity_table = NULL;

#define PAGING_STRUCTS_START 0x200000
#define KERNEL_PAGE_DIRECTORY_ADDR (PAGING_STRUCTS_START)
#define IDENTITY_PAGE_TABLE_ADDR (PAGING_STRUCTS_START + 0x1000)

void paging_init() {
    kprint("Initializing paging...\n");

    // Используем фиксированные адреса для структур paging
    kernel_directory = (page_directory_t *)KERNEL_PAGE_DIRECTORY_ADDR;
    memory_set((u8 *)kernel_directory, 0, sizeof(page_directory_t));

    // Создаем таблицу для идентичного отображения первых 4MB
    page_table_t *identity_table = (page_table_t *)IDENTITY_PAGE_TABLE_ADDR;
    memory_set((u8 *)identity_table, 0, sizeof(page_table_t));

    // Заполняем таблицу идентичного отображения
    u32 i;
    for (i = 0; i < 1024; i++) {
        identity_table->entries[i].present = 1;
        identity_table->entries[i].rw = 1;
        identity_table->entries[i].frame = i;
    }

    // Добавляем таблицу в каталог
    kernel_directory->entries[0].present = 1;
    kernel_directory->entries[0].rw = 1;
    kernel_directory->entries[0].frame = (u32)identity_table >> 12;

    // Устанавливаем каталог страниц
    current_directory = kernel_directory;
    switch_page_directory(kernel_directory);

    // Включаем paging
    u32 cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // Устанавливаем бит PG
    asm volatile("mov %0, %%cr0" :: "r"(cr0));

    kprint("Paging enabled\n");

    // Инициализируем статистику
    total_pages = (HEAP_SIZE + HEAP_START) / PAGE_SIZE;
    used_pages = 1024; // Уже использовано для идентичного отображения
}

void switch_page_directory(page_directory_t *new_dir) {
    current_directory = new_dir;
    asm volatile("mov %0, %%cr3" :: "r"(new_dir));
}

page_directory_t *get_kernel_page_directory() {
    return kernel_directory;
}

void map_page(void *virtual_addr, void *physical_addr, u32 flags) {
    u32 pd_index = PAGE_DIRECTORY_INDEX((u32)virtual_addr);
    u32 pt_index = PAGE_TABLE_INDEX((u32)virtual_addr);

    // Проверяем, существует ли таблица страниц
    if (!current_directory->entries[pd_index].present) {
        // Создаем новую таблицу страниц
        page_table_t *new_table = (page_table_t *)kmalloc_a(sizeof(page_table_t));
        memory_set((u8 *)new_table, 0, sizeof(page_table_t));

        // Добавляем таблицу в каталог
        current_directory->entries[pd_index].present = 1;
        current_directory->entries[pd_index].rw = 1;
        current_directory->entries[pd_index].frame = (u32)new_table >> 12;
    }

    // Получаем указатель на таблицу страниц
    page_table_t *table = (page_table_t *)(current_directory->entries[pd_index].frame << 12);

    // Заполняем запись в таблице страниц
    table->entries[pt_index].present = (flags & PAGE_PRESENT) ? 1 : 0;
    table->entries[pt_index].rw = (flags & PAGE_WRITE) ? 1 : 0;
    table->entries[pt_index].user = (flags & PAGE_USER) ? 1 : 0;
    table->entries[pt_index].write_through = (flags & PAGE_WRITE_THROUGH) ? 1 : 0;
    table->entries[pt_index].cache_disable = (flags & PAGE_CACHE_DISABLE) ? 1 : 0;
    table->entries[pt_index].global = (flags & PAGE_GLOBAL) ? 1 : 0;
    table->entries[pt_index].frame = (u32)physical_addr >> 12;

    // Обновляем статистику
    if (table->entries[pt_index].present) {
        used_pages++;
    }

    // Принудительно обновляем TLB
    asm volatile("invlpg (%0)" :: "r"(virtual_addr));
}

void unmap_page(void *virtual_addr) {
    u32 pd_index = PAGE_DIRECTORY_INDEX((u32)virtual_addr);
    u32 pt_index = PAGE_TABLE_INDEX((u32)virtual_addr);

    if (current_directory->entries[pd_index].present) {
        page_table_t *table = (page_table_t *)(current_directory->entries[pd_index].frame << 12);
        if (table->entries[pt_index].present) {
            table->entries[pt_index].present = 0;
            used_pages--;

            // Принудительно обновляем TLB
            asm volatile("invlpg (%0)" :: "r"(virtual_addr));
        }
    }
}

void *get_physical_address(void *virtual_addr) {
    u32 pd_index = PAGE_DIRECTORY_INDEX((u32)virtual_addr);
    u32 pt_index = PAGE_TABLE_INDEX((u32)virtual_addr);

    if (!current_directory->entries[pd_index].present) {
        return NULL;
    }

    page_table_t *table = (page_table_t *)(current_directory->entries[pd_index].frame << 12);
    if (!table->entries[pt_index].present) {
        return NULL;
    }

    return (void *)(table->entries[pt_index].frame << 12);
}

u32 get_memory_used_pages() {
    return used_pages;
}

u32 get_memory_free_pages() {
    return total_pages - used_pages;
}
