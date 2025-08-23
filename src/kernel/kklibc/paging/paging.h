#ifndef PAGING_H
#define PAGING_H

#include "../ctypes.h"

#define PAGE_SIZE 4096 // 4KM

#define ALIGN(addr) (((u32)(addr)) & 0xFFFFF000) //выраванивие
#define PAGE_DIR_INDEX(vaddr) (((u32)vaddr) >> 22) // Получить индекс в PD (31-22)
#define PAGE_TAB_INDEX(vaddr) ((((u32)vaddr) >> 12) & 0x3FF) // Получить индекс в PT (21-12)

// Структура записи в каталоге страниц (PDE)
typedef struct page_dir_entry {
    u32 present    : 1;   // Страница есть
    u32 rw         : 1;   // 0 = Read-only; 1 = Read-write
    u32 user       : 1;   // 0 = Supervisor; 1 = User
    u32 accessed   : 1;   // Был ли доступ к странице
    u32 dirty      : 1;   // (Для PDE: не используется)
    u32 unused     : 7;   // анюзед биты
    u32 frame      : 20;  // алигнед адрес фрейма (сдвинуть на 12 вправо)
} page_dir_entry_t;

// Структура записи в таблице страниц (PTE)
typedef struct page_tab_entry {
    u32 present    : 1;
    u32 rw         : 1;
    u32 user       : 1;
    u32 accessed   : 1;
    u32 dirty      : 1;   // грязная или нет
    u32 unused     : 7;
    u32 frame      : 20;
} page_tab_entry_t;

// Функции для работы с паджингом
void init_paging();
void load_page_dir(u32 *page_dir);
void enable_paging();
void debug_page_fault(u32 fault_addr);
int map_page(u32 virtual_addr, u32 physical_addr, u32 flags);
void dump_page_tables();

void unmap_page(u32 virtual_addr);

#endif

