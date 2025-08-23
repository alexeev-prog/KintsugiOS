#include "paging.h"
#include "../../drivers/screen.h"
#include "frame_alloc.h"
#include "../stdio.h"
#include "frame_alloc.h"
#include "../stdlib.h"
#include "../mem.h"
#include "../../cpu/isr.h"

// внешнии функции из paging.asm
extern void load_page_dir(u32 *);
extern void enable_paging();
extern u32 frame_bitmap[FRAME_BITMAP_SIZE];

// каталог страниц, выровненный по границе 4 КБ
u32 page_dir[1024] __attribute__((aligned(4096)));
// первая таблица страниц, тоже aligned
u32 first_page_tab[1024] __attribute__((aligned(4096)));

void debug_mapping(u32 vaddr) {
    u32 pd_index = vaddr >> 22;
    u32 pt_index = (vaddr >> 12) & 0x3FF;

    kprintf("Debugging address 0x%x\n", vaddr);
    kprintf("PD index: %d, PT index: %d\n", pd_index, pt_index);

    // Используем рекурсивное отображение для доступа к PD и PT
    u32* recursive_pd = (u32*)0xFFFFF000;

    if (pd_index < 1024) {
        kprintf("PDE[%d] = 0x%x\n", pd_index, recursive_pd[pd_index]);

        if (recursive_pd[pd_index] & 1) {
            u32* recursive_pt = (u32*)(0xFFC00000 + (pd_index << 12));
            kprintf("PTE[%d][%d] = 0x%x\n", pd_index, pt_index, recursive_pt[pt_index]);
        } else {
            kprint("PDE is not present\n");
        }
    }
}

void mark_used_frames() {
    // Пометить фреймы, занятые ядром
    u32 kernel_start = 0x100000; // Начало ядра
    u32 kernel_end = 0x200000;/* твой расчет конца ядра */

    for (u32 addr = kernel_start; addr < kernel_end; addr += PAGE_SIZE) {
        set_frame_used(addr);
    }

    // Пометить фреймы, занятые таблицами страниц
    set_frame_used((u32)page_dir);
    set_frame_used((u32)first_page_tab);

    // Пометить другие занятые области (например, видеопамять)
    set_frame_used(0xB8000); // Видеопамять
}


void init_frame_allocator() {
    // 1. Убедись, что знаешь реальный объем ОЗУ
    u32 total_memory = 16 * 1024 * 1024; // Например, 16MB
    u32 total_frames = total_memory / PAGE_SIZE;

    // 2. Помеь все фреймы как свободные
    // u32memory_set(frame_bitmap, 0, sizeof(frame_bitmap));

    // 3. Пометиь как занятые только те фреймы, которые действительно заняты
    // mark_used_frames();
}

void debug_page_fault(u32 fault_addr) {
    kprintf("Page fault at address: %x\n", fault_addr);
    debug_mapping(fault_addr);
}

void check_recursive_mapping() {
    // Проверяем, доступен ли PD через рекурсивное отображение
    u32* recursive_pd = (u32*)0xFFFFF000; // Виртуальный адрес PD
    kprintf("Recursive PD address: %x\n", (u32)recursive_pd);

    for (int i = 0; i < 2; i++) {
        kprintf("PD[%d] via recursive mapping: %x\n", i, recursive_pd[i]);
    }

    // Проверяем доступ к первой page table через рекурсивное отображение
    u32* recursive_pt = (u32*)0xFFC00000; // Виртуальный адрес первой PT
    kprintf("First PT entry via recursive mapping: %x\n", recursive_pt[0]);
}

void init_paging() {
    // 1  инициалайзим все PDE как нон-present
    for(int i = 0; i < 1024; i++) {
        page_dir[i] = 0x00000002; // R/W=1, Present=0
    }

    // 2. Инициализируем первую таблицу страниц (identity mapping)
    for(int i = 0; i < 1024; i++) {
        first_page_tab[i] = (i * 0x1000) | 3; // Present=1, R/W=1
    }

    // 3. Настраиваем первую запись в каталоге страниц
    page_dir[0] = ((u32)first_page_tab) | 3; // Present=1, R/W=1

    // 4. рекурсионный маппинг
    // ластовая запись PD должна указывать на сам PD
    page_dir[1023] = ((u32)page_dir) | 0x003; // Present=1, R/W=1

    // 5. Загружаем адрес нашего PD в  CR3
    load_page_dir(page_dir);

    // 6. энейблим пажинг
    enable_paging();

    init_frame_allocator();

    kprint("Paging enabled!\n");
    check_recursive_mapping();
    kprintf("Recursive mapping setup at 0xFFC00000, PD physical: %x\n", (u32)page_dir);

    // если вы это читаете значит я смог, врагу не пожелаешь. пожалейте грешную душу
}

void dump_page_tables() {
    kprint("Dumping page tables...\n");

    // Используем рекурсивное отображение для доступа к PD
    u32* recursive_pd = (u32*)0xFFFFF000;

    for (int i = 0; i < 1024; i++) {
        if (recursive_pd[i] & 1) { // Если PDE присутствует
            kprintf("PD[%d] = %x: ", i, recursive_pd[i]);

            if (recursive_pd[i] & 0x80) {
                kprint("4MB page\n");
            } else {
                kprint("4KB page table\n");

                // Используем рекурсивное отображение для доступа к PT
                u32* recursive_pt = (u32*)(0xFFC00000 + (i << 12));

                for (int j = 0; j < 5; j++) {
                    if (recursive_pt[j] & 1) {
                        kprintf("  PT[%d][%d] = 0x%x\n", i, j, recursive_pt[j]);
                    }
                }
            }
        }
    }

    kprint("Page table dump completed\n");
}

page_tab_entry_t* get_pte(u32 vaddr, int create) {
    u32 pd_index = PAGE_DIR_INDEX(vaddr);
    u32 pt_index = PAGE_TAB_INDEX(vaddr);

    // если PDE не present и create == 1, то создаем новую таблицу страниц
    if (!(page_dir[pd_index] & 1)) {
        if (!create) return 0;

        u32 new_pt_phys = alloc_frame(); // Выделяем фреймик под новую таблицу
        if (!new_pt_phys) return 0; // не удалось

        page_dir[pd_index] = new_pt_phys | 0x07; // Present, R/W, User

        // Зануляем новую таблицу страниц
        u32* new_pt = (u32*) (new_pt_phys + 0xFFC00000);
        for (int i = 0; i < 1024; i++) {
            new_pt[i] = 0x00000002; // R/W=1, Present=0
        }
    }

    // получаем физический адрес таблицы страниц из PDE
    u32 pt_phys = page_dir[pd_index] & 0xFFFFF000;
    // преобразуем его в виртуальный адрес (через Higher Half Kernel)
    u32* pt_virt = (u32*) (pt_phys + 0xFFC00000);
    return (page_tab_entry_t*)&pt_virt[pt_index];
}

void* alloc_page(u32 vaddr, u32 flags) {
    page_tab_entry_t* pte = get_pte(vaddr, 1);
    if (!pte) return 0;

    if (pte->present) return (void*)vaddr;

    u32 frame = alloc_frame();
    if (!frame) return 0;

    pte->frame = frame >> 12;
    pte->present = 1;
    pte->rw = (flags & 0x02) ? 1 : 0;
    pte->user = (flags & 0x04) ? 1 : 0;
    pte->accessed = (flags & 0x06) ? 1 : 0;
    pte->dirty = (flags & 0x08) ? 1 : 0;
    pte->unused = (flags & 0x0A) ? 1 : 0;

    asm volatile("invlpg (%0)" : : "r" (vaddr));

    return (void*)vaddr;
}

void free_page(u32 vaddr) {
    page_tab_entry_t* pte = get_pte(vaddr, 0);
    if (!pte || !pte->present) return;

    u32 frame = pte->frame << 12;
    free_frame(frame);

    pte->present = 0;
    pte->frame = 0;

    asm volatile("invlpg (%0)" : : "r" (vaddr));
}

int map_page(u32 virtual_addr, u32 physical_addr, u32 flags) {
    u32 pd_index = virtual_addr >> 22;
    u32 pt_index = (virtual_addr >> 12) & 0x3FF;

    // Проверяем, существует ли page table
    if (!(page_dir[pd_index] & 1)) {
        // создаем новую таблицу паджей
        u32 new_pt = alloc_frame();
        if (!new_pt) return 0;

        // инициализируем новую page table
        u32* pt = (u32*)(new_pt + 0xFFC00000); // юзаем higher half mapping
        for (int i = 0; i < 1024; i++) {
            pt[i] = 0x00000002; // R/W=1, Present=0
        }

        // запись в page directory
        page_dir[pd_index] = new_pt | flags;
    }

    // указатель на page table
    u32* pt = (u32*)((page_dir[pd_index] & 0xFFFFF000) + 0xFFC00000);

    // апись в page table
    pt[pt_index] = physical_addr | flags;

    // Инвалидируем TLB
    asm volatile("invlpg (%0)" : : "r" (virtual_addr));

    return 1;
}

void unmap_page(u32 virtual_addr) {
    u32 pd_index = virtual_addr >> 22;
    u32 pt_index = (virtual_addr >> 12) & 0x3FF;

    if (page_dir[pd_index] & 1) {
        u32* pt = (u32*)((page_dir[pd_index] & 0xFFFFF000) + 0xFFC00000);
        pt[pt_index] = 0x00000002; // R/W=1, Present=0

        asm volatile("invlpg (%0)" : : "r" (virtual_addr));
    }
}
