#ifndef PAGING_H
#define PAGING_H

#include "../cpu/isr.h"
#include "ctypes.h"

/* Макросы для работы с битовыми массивами */
#define INDEX_FROM_BIT(a) (a / (8 * 4))
#define OFFSET_FROM_BIT(a) (a % (8 * 4))

#define PAGE_SIZE 0x1000

extern u32 nframes;

/**
 * @brief Внутренняя функция выделения памяти
 *
 * @param sz размер
 * @param align выравнивание
 * @param phys физический адрес
 * @return u32
 **/
u32 pkmalloc_internal(u32 sz, int align, u32* phys);

/**
 * @brief Выделение с выравниванием по странице
 *
 * @param sz размер
 * @return u32
 **/
u32 pkmalloc_a(u32 sz);

/**
 * @brief Выделение с возвратом физического адреса
 *
 * @param sz размер
 * @param phys физический размер
 * @return u32
 **/
u32 pkmalloc_p(u32 sz, u32* phys);

/**
 * @brief Выделение с выравниванием и возвратом физического адреса
 *
 * @param sz размер
 * @param phys физический адрес
 * @return u32
 **/
u32 pkmalloc_ap(u32 sz, u32* phys);

/**
 * @brief Обычное выделение памяти
 *
 * @param sz размер
 * @return u32
 **/
u32 pkmalloc(u32 sz);

/* Внешняя переменная текущего адреса свободной памяти */
extern u32 free_mem_addr;

/* Структура страницы */
typedef struct page {
    u32 present : 1;    // Страница присутствует в памяти
    u32 rw : 1;    // Только для чтения если 0, чтение/запись если 1
    u32 user : 1;    // Только уровень супервизора если 0
    u32 accessed : 1;    // Страница была доступна с последнего обновления?
    u32 dirty : 1;    // В страницу производилась запись с последнего обновления?
    u32 unused : 7;    // Объединение неиспользуемых и зарезервированных битов
    u32 frame : 20;    // Адрес фрейма (сдвинутый вправо на 12 бит)
} page_t;

/* Структура таблицы страниц */
typedef struct page_table {
    page_t pages[1024];    // Массив из 1024 страниц
} page_table_t;

/* Структура директории страниц */
typedef struct page_directory {
    /**
     Массив указателей на таблицы страниц
    **/
    page_table_t* tables[1024];

    /**
     Массив указателей на таблицы страниц выше, но с их *физическим*
     расположением, для загрузки в регистр CR3
    **/
    u32 tablesPhysical[1024];

    /**
     Физический адрес tablesPhysical
    **/
    u32 physicalAddr;
} page_directory_t;

/* Внешние переменные директорий страниц */
extern page_directory_t* kernel_directory;
extern page_directory_t* current_directory;

/**
 * @brief Настройка окружения, директорий страниц и включение paging
 **/
void initialise_paging();

/**
 * @brief Загрузка указанной директории страниц в регистр CR3
 * @param dir директория страницы в памяти
 **/
void switch_page_directory(page_directory_t* dir);

/**
 * @brief Get the page object
 *
 * Получение указателя на требуемую страницу.
 * Если make == 1, создает таблицу страниц если она не создана
 *
 * @param address
 * @param make
 * @param dir
 * @return page_t*
 **/
page_t* get_page(u32 address, int make, page_directory_t* dir);

/**
 * @brief Обработчик Page Fault
 *
 * @param regs регистры
 **/
void page_fault(registers_t regs);

/**
 * @brief Аллокация фрейма
 *
 * @param page страница в памяти
 * @param is_kernel это ядро
 * @param is_writeable этот участок возможен для записи
 **/
void alloc_frame(page_t* page, int is_kernel, int is_writeable);

/**
 * @brief Освобождение фрейма
 *
 * @param page страница в памяти
 **/
void free_frame(page_t* page);

/**
 * @brief Тест фрейма
 *
 * @param frame_addr адрес фрейма
 * @return u32
 **/
u32 test_frame(u32 frame_addr);

#endif
