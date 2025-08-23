/*------------------------------------------------------------------------------
 *  Kintsugi OS KKLIBC source code
 *  File: kklibc/mem.c
 *  Title: Функции работы с памятью
 *	Description: null
 * ----------------------------------------------------------------------------*/

#include "mem.h"
#include "../drivers/screen.h"
#include "ctypes.h"
#include "paging/paging.h"
#include "stdio.h"
#include "stdlib.h"
#include "paging/frame_alloc.h"

static mem_block_t *free_blocks = NULL;

static page_header_t* page_list = NULL;
static u32 next_virtual_addr = 0xC0000000; // Начинаем с 3GB для ядра

// Инициализация аллокатора страниц
void init_page_allocator() {
    page_list = NULL;
    kprint("Page allocator initialized\n");
}

// Выделение новой страницы
page_header_t* alloc_page_struct(u32 size) {
    page_header_t* new_page = (page_header_t*)kmalloc(sizeof(page_header_t));
    if (!new_page) {
        kprint("Failed to allocate page header\n");
        return NULL;
    }

    // Выделяем физический кадр
    u32 frame_addr = alloc_frame();
    if (!frame_addr) {
        kprint("Failed to allocate physical frame\n");
        kfree(new_page);
        return NULL;
    }

    // Назначаем виртуальный адрес
    u32 virt_addr = next_virtual_addr;
    next_virtual_addr += PAGE_SIZE;

    // Настраиваем отображение виртуального адреса на физический
    if (!map_page(virt_addr, frame_addr, 0x03)) { // Present + R/W
        kprint("Failed to map page\n");
        free_frame(frame_addr);
        kfree(new_page);
        return NULL;
    }

    // Инициализируем структуру страницы
    new_page->physical_addr = frame_addr;
    new_page->virtual_addr = virt_addr;
    new_page->next = page_list;
    new_page->ref_count = 1;

    page_list = new_page;
    return new_page;
}

// Освобождение страницы
void free_page_struct(page_header_t* page) {
    if (!page) return;

    if (--page->ref_count == 0) {
        // Удаляем отображение страницы
        unmap_page(page->virtual_addr);

        // Освобождаем физический кадр
        free_frame(page->physical_addr);

        // Удаляем из списка
        if (page_list == page) {
            page_list = page->next;
        } else {
            page_header_t* current = page_list;
            while (current && current->next != page) {
                current = current->next;
            }
            if (current) {
                current->next = page->next;
            }
        }

        // Освобождаем саму структуру
        kfree(page);
    }
}

// Инициализация памяти heap
void heap_init() {
    // Инициализируем аллокатор страниц
    init_page_allocator();

    // Остальная инициализация кучи
    free_blocks = (mem_block_t*)HEAP_START;
    free_blocks->size = HEAP_SIZE - sizeof(mem_block_t);
    free_blocks->next = NULL;
    free_blocks->is_free = 1;
    free_blocks->is_page = 0;
    free_blocks->page = NULL;

    kprint("Heap initialized at ");
    char buf[32] = "";
    hex_to_ascii(HEAP_START, buf);
    kprint(buf);
    kprint("\n");
}

// TODO: Paging is not implemented
void *get_physaddr(void *virtualaddr) {
    unsigned long pdindex = (unsigned long)virtualaddr >> 22;
    unsigned long ptindex = (unsigned long)virtualaddr >> 12 & 0x03FF;

    unsigned long *pd = (unsigned long *)0xFFFFF000;
    // Here you need to check whether the PD entry is present.

    unsigned long *pt = ((unsigned long *)0xFFC00000) + (0x400 * pdindex);
    // Here you need to check whether the PT entry is present.

    return (void *)((pt[ptindex] & ~0xFFF) +
                                    ((unsigned long)virtualaddr & 0xFFF));
}

void *kmalloc(u32 size) {
        // Выравниваем размер до границы BLOCK_SIZE
    if (size % BLOCK_SIZE != 0) {
        size += BLOCK_SIZE - (size % BLOCK_SIZE);
    }

    // Для больших блоков используем отдельные страницы
    if (size >= PAGE_SIZE / 2) { // Полстраницы или больше
        page_header_t* new_page = alloc_page_struct(size);
        if (!new_page) {
            kprint("Failed to allocate page for large block\n");
            return NULL;
        }

        // Создаем блок памяти в начале страницы
        mem_block_t* block = (mem_block_t*)new_page->virtual_addr;
        block->size = size;
        block->is_free = 0;
        block->is_page = 1;
        block->page = new_page;
        block->next = NULL;

        return (void*)((u32)block + sizeof(mem_block_t));
    }

    // Стандартная логика для маленьких блоков
    mem_block_t* current = free_blocks;
    mem_block_t* prev = NULL;

    while (current) {
        if (current->is_free && current->size >= size) {
            // Нашли подходящий блок
            if (current->size > size + sizeof(mem_block_t) + BLOCK_SIZE) {
                // Можем разделить блок
                mem_block_t* new_block = (mem_block_t*)((u32)current + sizeof(mem_block_t) + size);
                new_block->size = current->size - size - sizeof(mem_block_t);
                new_block->is_free = 1;
                new_block->next = current->next;
                new_block->is_page = 0;
                new_block->page = NULL;

                current->size = size;
                current->next = new_block;
            }

            current->is_free = 0;
            return (void*)((u32)current + sizeof(mem_block_t));
        }
        prev = current;
        current = current->next;
    }

    kprint("No free blocks found\n");
    return NULL;
}

void *krealloc(void *ptr, u32 size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    // Выравниваем размер как в kmalloc_new
    if (size % BLOCK_SIZE != 0) {
        size += BLOCK_SIZE - (size % BLOCK_SIZE);
    }

    mem_block_t *block = (mem_block_t*)((u32)ptr - sizeof(mem_block_t));

    // Если блок является страницей, обрабатываем особо
    if (block->is_page) {
        // Для страниц проверяем, можно ли расширить
        if (size <= block->size) {
            return ptr; // Уже достаточно места
        }
        // Для страниц просто выделяем новую и копируем
        void *new_ptr = kmalloc(size);
        if (!new_ptr) return NULL;
        memory_copy(ptr, new_ptr, block->size);
        kfree(ptr);
        return new_ptr;
    }

    // Попытка расширения на месте
    if (block->next && block->next->is_free &&
        (block->size + sizeof(mem_block_t) + block->next->size) >= size) {
        // Можно расширить на месте
        u32 needed = size - block->size;
        if (block->next->size >= (needed + sizeof(mem_block_t))) {
            // Разделяем следующий блок
            mem_block_t *new_block = (mem_block_t*)((u32)block + sizeof(mem_block_t) + block->size);
            new_block->size = block->next->size - needed - sizeof(mem_block_t);
            new_block->is_free = 1;
            new_block->next = block->next->next;
            new_block->is_page = 0;

            block->size = size;
            block->next = new_block;

            return ptr;
        } else {
            // Просто объединяем с следующим блоком
            block->size += sizeof(mem_block_t) + block->next->size;
            block->next = block->next->next;
            return ptr;
        }
    }

    // Стандартный случай: выделяем новый блок и копируем
    void *new_ptr = kmalloc(size);
    if (!new_ptr) return NULL;
    memory_copy(ptr, new_ptr, block->size);
    kfree(ptr);
    return new_ptr;
}

void kfree(void* ptr) {
    if (!ptr) return;

    // Получаем заголовок блока
    mem_block_t* block = (mem_block_t*)((u32)ptr - sizeof(mem_block_t));

    if (block->is_page) {
        // Освобождаем всю страницу
        free_page_struct(block->page);
        return;
    }

    // Стандартная логика освобождения блока
    block->is_free = 1;

    // Попробуем объединить с соседними свободными блоками
    mem_block_t* current = free_blocks;
    while (current) {
        if (current->is_free && current->next && current->next->is_free) {
            // Объединяем текущий блок со следующим
            current->size += sizeof(mem_block_t) + current->next->size;
            current->next = current->next->next;
        }
        current = current->next;
    }
}

meminfo_t get_meminfo() {
    meminfo_t meminfo;

    mem_block_t *current = free_blocks;
    u32 total_used = 0;
    u32 total_free = 0;
    u32 block_count = 0;

    while (current) {
        block_count++;

        if (current->is_free) {
            total_free += current->size;
        } else {
            total_used += current->size;
        }

        current = current->next;
    }

    meminfo.heap_start = HEAP_START;
    meminfo.heap_size = HEAP_SIZE;
    meminfo.block_size = BLOCK_SIZE;
    meminfo.free_blocks = free_blocks;
    meminfo.total_used = total_used;
    meminfo.total_free = total_free;
    meminfo.block_count = block_count;

    return meminfo;
}

void kmemdump() {
    meminfo_t info = get_meminfo();
    mem_block_t *current = info.free_blocks;
    u32 counter = 0;

    kprintf("Heap: %x - %x (%d bytes)\n", info.heap_start,
                    info.heap_start + info.heap_size, info.heap_size);
    kprintf("Block size: %d bytes\n", info.block_size);
    kprintf("Total: USED=%d bytes, FREE=%d bytes, in %d blocks\n\n",
                    info.total_used, info.total_free, info.block_count);

    while (current) {
        kprintf("Block %d: %x, Size=%d, %s\n", counter++, (u32)current,
                        current->size, current->is_free ? "FREE" : "USED");
        current = current->next;
    }

    kprint("Paging:\n");
    dump_page_tables();
}
