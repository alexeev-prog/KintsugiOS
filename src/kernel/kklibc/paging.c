/*------------------------------------------------------------------------------
 *  Kintsugi OS C Libraries source code
 *  File: libc/paging.c
 *  Title: Функции работы со страничной обработкой памяти
 *	Description: Страничная организация памяти (paging) — это схема
 *управления памятью, которая вводит концепции **_логических адресов_**
 *  (виртуальных адресов) и **_виртуальной памяти_**. На архитектурах x86_* это
 * реализуется на аппаратном уровне. Paging обеспечивает уровень трансляции
 * между виртуальными и физическими адресами, а также между виртуальным и
 * физическим адресными пространствами, а также добавляет несколько
 * дополнительных функций (таких как защита доступа, защита уровня привилегий).
 * ----------------------------------------------------------------------------*/
#include "paging.h"

#include "stdio.h"
#include "stdlib.h"

/* Глобальные переменные для управления фреймами */
u32* frames;
u32 nframes;

/* Глобальные переменные для page directory */
page_directory_t* kernel_directory = 0;
page_directory_t* current_directory = 0;

/* Внешняя переменная текущего адреса свободной памяти */
extern u32 free_mem_addr;

/* Внутренняя функция выделения памяти с учетом выравнивания и физического
 * адреса */
u32 pkmalloc_internal(u32 sz, int align, u32* phys) {
    if (align == 1 && (free_mem_addr & 0xFFFFF000)) {
        free_mem_addr &= 0xFFFFF000;
        free_mem_addr += PAGE_SIZE;
    }

    if (phys) {
        *phys = free_mem_addr;
    }

    u32 tmp = free_mem_addr;
    free_mem_addr += sz;
    return tmp;
}

/* Выделение памяти с выравниванием по странице */
u32 pkmalloc_a(u32 sz) {
    return pkmalloc_internal(sz, 1, 0);
}

/* Выделение памяти с возвратом физического адреса */
u32 pkmalloc_p(u32 sz, u32* phys) {
    return pkmalloc_internal(sz, 0, phys);
}

/* Выделение памяти с выравниванием и возвратом физического адреса */
u32 pkmalloc_ap(u32 sz, u32* phys) {
    return pkmalloc_internal(sz, 1, phys);
}

/* Обычное выделение памяти */
u32 pkmalloc(u32 sz) {
    return pkmalloc_internal(sz, 0, 0);
}

/* Установка бита фрейма как занятого */
static void set_frame(u32 frame_addr) {
    u32 frame = frame_addr / PAGE_SIZE;
    u32 idx = INDEX_FROM_BIT(frame);
    u32 off = OFFSET_FROM_BIT(frame);
    frames[idx] |= (0x1 << off);
}

/* Очистка бита фрейма как свободного */
static void clear_frame(u32 frame_addr) {
    u32 frame = frame_addr / PAGE_SIZE;
    u32 idx = INDEX_FROM_BIT(frame);
    u32 off = OFFSET_FROM_BIT(frame);
    frames[idx] &= ~(0x1 << off);
}

/* Проверка занятости фрейма */
u32 test_frame(u32 frame_addr) {
    u32 frame = frame_addr / PAGE_SIZE;
    u32 idx = INDEX_FROM_BIT(frame);
    u32 off = OFFSET_FROM_BIT(frame);
    return (frames[idx] & (0x1 << off));
}

/* Поиск первого свободного фрейма */
static u32 first_frame() {
    for (u32 i = 0; i < INDEX_FROM_BIT(nframes); i++) {
        if (frames[i] != 0xFFFFFFFF) {
            for (u32 j = 0; j < 32; j++) {
                u32 toTest = 0x1 << j;
                if (!(frames[i] & toTest)) {
                    return i * 4 * 8 + j;
                }
            }
        }
    }
    return (u32)-1;
}

/* Выделение фрейма для страницы */
void alloc_frame(page_t* page, int is_kernel, int is_writeable) {
    if (page->frame != 0) {
        return;
    }

    u32 idx = first_frame();
    if (idx == (u32)-1) {
        panic_red_screen("Paging Error", "No free frames available");
        for (;;)
            ;    // Зависание системы при отсутствии фреймов
        return;
    }

    set_frame(idx * 0x1000);
    page->present = 1;
    page->rw = (is_writeable) ? 1 : 0;
    page->user = (is_kernel) ? 0 : 1;
    page->frame = idx;
}

/* Освобождение фрейма страницы */
void free_frame(page_t* page) {
    u32 frame;
    if (!(frame = page->frame)) {
        return;
    }

    clear_frame(frame);
    page->frame = 0x0;
}

/* Обработчик page fault */
void page_fault(registers_t regs) {
    u32 faulting_address;
    __asm__ volatile("mov %%cr2, %0" : "=r"(faulting_address));

    int present = !(regs.err_code & 0x1);
    int rw = regs.err_code & 0x2;
    int us = regs.err_code & 0x4;
    int reserved = regs.err_code & 0x8;
    int id = regs.err_code & 0x10;

    char buffer[1024];
    int offset = snprintf(buffer, sizeof(buffer), "Page Fault at %x (", faulting_address);

    if (present) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "present ");
    }
    if (rw) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "read-only ");
    }
    if (us) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "user-mode ");
    }
    if (reserved) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "reserved ");
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, ")");

    panic_red_screen("Page Fault", buffer);
}

/* Инициализация подсистемы paging */
void initialise_paging() {
    u32 mem_end_page = 0x1000000;
    nframes = mem_end_page / PAGE_SIZE;
    frames = (u32*)pkmalloc(INDEX_FROM_BIT(nframes) * 4);
    memset(frames, 0, INDEX_FROM_BIT(nframes) * 4);

    kernel_directory = (page_directory_t*)pkmalloc_a(sizeof(page_directory_t));
    memset(kernel_directory, 0, sizeof(page_directory_t));
    current_directory = kernel_directory;

    int i = 0;
    while (i < free_mem_addr) {
        alloc_frame(get_page(i, 1, kernel_directory), 0, 0);
        i += PAGE_SIZE;
    }

    register_interrupt_handler(14, page_fault);
    switch_page_directory(kernel_directory);
}

/* Переключение page directory */
void switch_page_directory(page_directory_t* dir) {
    current_directory = dir;
    __asm__ volatile("mov %0, %%cr3" ::"r"(&dir->tablesPhysical));
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" ::"r"(cr0));
}

/* Получение страницы по адресу */
page_t* get_page(u32 address, int make, page_directory_t* dir) {
    address /= PAGE_SIZE;
    u32 table_idx = address / 1024;

    if (dir->tables[table_idx]) {
        return &dir->tables[table_idx]->pages[address % 1024];
    } else if (make) {
        u32 tmp;
        dir->tables[table_idx] = (page_table_t*)pkmalloc_ap(sizeof(page_table_t), &tmp);
        if (!dir->tables[table_idx]) {
            panic_red_screen("Paging Error", "Failed to allocate page table");
            for (;;)
                ;    // Зависание системы при ошибке
            return 0;
        }
        memset(dir->tables[table_idx], 0, 0x1000);
        dir->tablesPhysical[table_idx] = tmp | 0x7;
        return &dir->tables[table_idx]->pages[address % 1024];
    }
    return 0;
}
