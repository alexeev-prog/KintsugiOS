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
page_directory_t* kernel_directory;
page_directory_t* current_directory;

/* Внутренняя функция выделения памяти с учетом выравнивания и физического
 * адреса */
u32 pkmalloc_internal(u32 sz, int align, u32* phys) {
    if (align == 1 && (free_mem_addr & 0xFFFFF000)) {
        // Выравниваем адрес по границе страницы
        free_mem_addr &= 0xFFFFF000;
        free_mem_addr += 0x1000;
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
    pkmalloc_internal(sz, 0, phys);
    return *phys;
}

/* Выделение памяти с выравниванием и возвратом физического адреса */
u32 pkmalloc_ap(u32 sz, u32* phys) {
    pkmalloc_internal(sz, 1, phys);
    return *phys;
}

/* Обычное выделение памяти */
u32 pkmalloc(u32 sz) {
    return pkmalloc_internal(sz, 0, 0);
}

/* Установка бита фрейма как занятого */
static void set_frame(u32 frame_addr) {
    u32 frame = frame_addr / 0x1000;
    u32 idx = INDEX_FROM_BIT(frame);
    u32 off = OFFSET_FROM_BIT(frame);
    frames[idx] |= (0x1 << off);
}

/* Очистка бита фрейма как свободного */
static void clear_frame(u32 frame_addr) {
    u32 frame = frame_addr / 0x1000;
    u32 idx = INDEX_FROM_BIT(frame);
    u32 off = OFFSET_FROM_BIT(frame);
    frames[idx] &= ~(0x1 << off);
}

/* Проверка занятости фрейма */
u32 test_frame(u32 frame_addr) {
    u32 frame = frame_addr / 0x1000;
    u32 idx = INDEX_FROM_BIT(frame);
    u32 off = OFFSET_FROM_BIT(frame);
    return (frames[idx] & (0x1 << off));
}

/* Поиск первого свободного фрейма */
static u32 first_frame() {
    u32 i, j;
    for (i = 0; i < INDEX_FROM_BIT(nframes); i++) {
        if (frames[i] != 0xFFFFFFFF) {    // Если есть свободные фреймы
            // Ищем конкретный свободный бит
            for (j = 0; j < 32; j++) {
                u32 toTest = 0x1 << j;
                if (!(frames[i] & toTest)) {
                    return i * 4 * 8 + j;
                }
            }
        }
    }
}

/* Выделение фрейма для страницы */
void alloc_frame(page_t* page, int is_kernel, int is_writeable) {
    if (page->frame != 0) {
        return;    // Фрейм уже выделен
    } else {
        u32 idx = first_frame();    // Индекс первого свободного фрейма
        if (idx == (u32)-1) {
            char buffer[1024];
            snprintf(
                buffer,
                1024,
                "No free frames found to allocate. IDX: %d\nAlloc Frame: " "IS_KERNEL=%d, IS_WRITEABLE=%d\n",
                idx,
                is_kernel,
                is_writeable);
            panic_red_screen("Paging alloc_frame error", buffer);
        }
        set_frame(idx * 0x1000);    // Помечаем фрейм как занятый
        page->present = 1;    // Отмечаем страницу как присутствующую
        page->rw = (is_writeable) ? 1 : 0;    // Страница доступна для записи?
        page->user = (is_kernel) ? 0 : 1;    // Страница пользовательского режима?
        page->frame = idx;
    }
}

/* Освобождение фрейма страницы */
void free_frame(page_t* page) {
    u32 frame;
    if (!(frame = page->frame)) {
        return;    // У страницы не было выделенного фрейма
    } else {
        clear_frame(frame);    // Освобождаем фрейм
        page->frame = 0x0;    // Сбрасываем фрейм страницы
    }
}

/* Обработчик page fault */
void page_fault(registers_t regs) {
    // Адрес вызвавший ошибку хранится в регистре CR2
    u32 faulting_address;
    asm volatile("mov %%cr2, %0" : "=r"(faulting_address));

    // Анализируем код ошибки
    int present = !(regs.err_code & 0x1);    // Страница отсутствует
    int rw = regs.err_code & 0x2;    // Операция записи?
    int us = regs.err_code & 0x4;    // Процессор в пользовательском режиме?
    int reserved = regs.err_code & 0x8;    // Перезапись зарезервированных битов?
    int id = regs.err_code & 0x10;    // Вызвано выполнением инструкции?

    // Выводим сообщение об ошибке
    kprint("Page Fault has occured ( ");
    if (present) {
        kprint("present ");
    }
    if (rw) {
        kprint("read-only ");
    }
    if (us) {
        kprint("user-mode ");
    }
    if (reserved) {
        kprint("reserved ");
    }
    printf(") at %x\n\n", faulting_address);
    printf("PRESENT: %d | RW: %d | US: %d | RESERVED: %d | ID: %d", present, rw, us, reserved, id);
    kprint("\n");
}

/* Инициализация подсистемы paging */
void initialise_paging() {
    // Размер физической памяти (предполагаем 16MB)
    u32 mem_end_page = 0x1000000;

    nframes = mem_end_page / 0x1000;
    frames = (u32*)pkmalloc(INDEX_FROM_BIT(nframes));
    memset(frames, 0, INDEX_FROM_BIT(nframes));

    // Создаем page directory
    kernel_directory = (page_directory_t*)pkmalloc_a(sizeof(page_directory_t));
    memset(kernel_directory, 0, sizeof(page_directory_t));
    current_directory = kernel_directory;

    // Identity mapping (phys addr = virt addr) от 0x0 до конца используемой
    // памяти Используем while для вычисления free_mem_addr на лету в теле цикла
    int i = 0;
    while (i < free_mem_addr) {
        // Код ядра доступен для чтения, но не для записи из пользовательского
        // пространства
        alloc_frame(get_page(i, 1, kernel_directory), 0, 0);
        i += 0x1000;
    }

    // Регистрируем обработчик page fault перед включением paging
    register_interrupt_handler(14, page_fault);

    // Включаем paging!
    switch_page_directory(kernel_directory);

    printf("Paging enabled: nframes=%d, frames=%d, free mem addr: %x\n", nframes, frames, free_mem_addr);
}

/* Переключение page directory */
void switch_page_directory(page_directory_t* dir) {
    current_directory = dir;
    asm volatile("mov %0, %%cr3" ::"r"(&dir->tablesPhysical));
    u32 cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;    // Включаем paging!
    asm volatile("mov %0, %%cr0" ::"r"(cr0));
}

/* Получение страницы по адресу */
page_t* get_page(u32 address, int make, page_directory_t* dir) {
    // Преобразуем адрес в индекс
    address /= 0x1000;
    // Ищем page table содержащую этот адрес
    u32 table_idx = address / 1024;

    if (dir->tables[table_idx]) {    // Если таблица уже назначена
        return &dir->tables[table_idx]->pages[address % 1024];
    } else if (make) {
        u32 tmp;
        dir->tables[table_idx] = (page_table_t*)pkmalloc_ap(sizeof(page_table_t), &tmp);
        memset(dir->tables[table_idx], 0, 0x1000);
        dir->tablesPhysical[table_idx] = tmp | 0x7;    // PRESENT, RW, US
        return &dir->tables[table_idx]->pages[address % 1024];
    } else {
        return 0;
    }
}
