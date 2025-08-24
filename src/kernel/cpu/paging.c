// paging.c
#include "paging.h"
#include "../kklibc/kklibc.h"
#include "isr.h"
#include "../drivers/screen.h"

// Текущий каталог страниц
page_directory_t *current_directory = NULL;

// Внешние функции из assembly
extern void load_page_directory(u32);
extern void enable_paging();

// Инициализация paging
void paging_init() {
    kprint("Initializing paging...\n");

    // Создаем каталог страниц
    current_directory = (page_directory_t*)kmalloc_a(sizeof(page_directory_t));
    memory_set((u8*)current_directory, 0, sizeof(page_directory_t));

    // Создаем таблицы страниц для идентичного отображения первых 4MB
    for (u32 i = 0; i < 1024; i++) {
        // Каждая запись каталога указывает на таблицу страниц
        page_table_t *table = (page_table_t*)kmalloc_a(sizeof(page_table_t));
        memory_set((u8*)table, 0, sizeof(page_table_t));

        // Заполняем таблицу страниц
        for (u32 j = 0; j < 1024; j++) {
            u32 frame = (i * 1024 + j) * PAGE_SIZE;
            table->entries[j].present = 1;
            table->entries[j].rw = 1;
            table->entries[j].frame = frame >> 12;
        }

        // Устанавливаем запись в каталоге страниц
        current_directory->entries[i].present = 1;
        current_directory->entries[i].rw = 1;
        current_directory->entries[i].frame = ((u32)table) >> 12;
    }

    // Переключаемся на новый каталог страниц
    switch_page_directory(current_directory);

    // Включаем paging
    enable_paging();

    kprint("Paging enabled\n");
}

// Переключение каталога страниц
void switch_page_directory(page_directory_t *dir) {
    current_directory = dir;
    load_page_directory((u32)dir);
}

// Получение текущего каталога страниц
page_directory_t *get_current_page_directory() {
    return current_directory;
}

// Отображение виртуальной страницы на физическую
void map_page(void *virtual_addr, void *physical_addr, u32 flags) {
    u32 virtual = (u32)virtual_addr;
    u32 physical = (u32)physical_addr;

    u32 pd_index = PAGE_DIRECTORY_INDEX(virtual);
    u32 pt_index = PAGE_TABLE_INDEX(virtual);

    // Получаем таблицу страниц
    page_table_t *table = (page_table_t*)(current_directory->entries[pd_index].frame << 12);

    // Устанавливаем запись в таблице страниц
    table->entries[pt_index].present = (flags & PAGE_PRESENT) ? 1 : 0;
    table->entries[pt_index].rw = (flags & PAGE_WRITABLE) ? 1 : 0;
    table->entries[pt_index].user = (flags & PAGE_USER) ? 1 : 0;
    table->entries[pt_index].frame = physical >> 12;

    // Инвалидируем TLB
    asm volatile("invlpg (%0)" : : "r" (virtual_addr));
}

// Удаление отображения страницы
void unmap_page(void *virtual_addr) {
    u32 virtual = (u32)virtual_addr;
    u32 pd_index = PAGE_DIRECTORY_INDEX(virtual);
    u32 pt_index = PAGE_TABLE_INDEX(virtual);

    // Получаем таблицу страниц
    page_table_t *table = (page_table_t*)(current_directory->entries[pd_index].frame << 12);

    // Очищаем запись
    table->entries[pt_index].present = 0;

    // Инвалидируем TLB
    asm volatile("invlpg (%0)" : : "r" (virtual_addr));
}

// Получение физического адреса по виртуальному
void *get_physical_address(void *virtual_addr) {
    u32 virtual = (u32)virtual_addr;
    u32 pd_index = PAGE_DIRECTORY_INDEX(virtual);
    u32 pt_index = PAGE_TABLE_INDEX(virtual);

    // Проверяем наличие страницы
    if (!current_directory->entries[pd_index].present) {
        return NULL;
    }

    // Получаем таблицу страниц
    page_table_t *table = (page_table_t*)(current_directory->entries[pd_index].frame << 12);

    // Проверяем наличие записи в таблице
    if (!table->entries[pt_index].present) {
        return NULL;
    }

    // Вычисляем физический адрес
    u32 physical = (table->entries[pt_index].frame << 12) + (virtual & 0xFFF);
    return (void*)physical;
}

// Обработчик page fault
void page_fault_handler(registers_t regs) {
    u32 faulting_address;
    asm volatile("mov %%cr2, %0" : "=r" (faulting_address));

    // Проверяем причину page fault
    int present = regs.err_code & 0x1;
    int rw = regs.err_code & 0x2;
    int user = regs.err_code & 0x4;
    int reserved = regs.err_code & 0x8;
    int id = regs.err_code & 0x10;

    kprintf("Page fault at 0x%x, caused by ", faulting_address);

    if (present) {
        kprint("page protection violation");
    } else {
        kprint("non-present page");
    }

    if (rw) {
        kprint(" write attempt");
    } else {
        kprint(" read attempt");
    }

    if (user) {
        kprint(" in user mode");
    } else {
        kprint(" in kernel mode");
    }

    kprint("\n");

    // Зависаем систему при page fault в ядре
    asm volatile("hlt");
}
