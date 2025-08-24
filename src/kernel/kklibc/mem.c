// [file name]: mem.c (обновленная версия)
/*------------------------------------------------------------------------------
 *  Kintsugi OS KKLIBC source code
 *  File: kklibc/mem.c
 *  Title: Функции работы с памятью
 *	Description: Реализация менеджера памяти с поддержкой paging
 * ----------------------------------------------------------------------------*/

#include "mem.h"
#include "../cpu/paging.h"
#include "../drivers/screen.h"
#include "ctypes.h"
#include "stdio.h"
#include "stdlib.h"

u32 free_mem_addr_guard1 = 0xDEADBEEF;
static u32 free_mem_addr = HEAP_START;
u32 free_mem_addr_guard2 = 0xCAFEBABE;
static mem_block_t *free_blocks = NULL;

// Инициализация памяти heap с поддержкой paging
void heap_init() {
    // Инициализируем paging
    paging_init();

    // Инициализируем первый свободный блок на всей области кучи
    free_blocks = (mem_block_t *)HEAP_START;
    free_blocks->size = HEAP_SIZE - sizeof(mem_block_t);
    free_blocks->next = NULL;
    free_blocks->is_free = 1;

    // Отображаем всю кучу в виртуальную память
    u32 virtual_addr = HEAP_START;
    u32 physical_addr = HEAP_START;
    u32 end_addr = HEAP_START + HEAP_SIZE;

    while (physical_addr < end_addr) {
        map_page((void *)virtual_addr, (void *)physical_addr,
                 PAGE_PRESENT | PAGE_WRITE);
        virtual_addr += PAGE_SIZE;
        physical_addr += PAGE_SIZE;
    }

    kprint("Heap initialized at ");
    char buf[32] = "";
    hex_to_ascii(HEAP_START, buf);
    kprint(buf);
    kprint("\n");
}

void *kmalloc(u32 size) {
    return kmalloc_a(size);
    // Выравниваем размер до границы BLOCK_SIZE
    if (size % BLOCK_SIZE != 0) {
        size += BLOCK_SIZE - (size % BLOCK_SIZE);
    }

    // Добавляем размер для заголовка страницы, если нужно
    u32 total_size = size + sizeof(mem_block_t);
    if (total_size % PAGE_SIZE != 0) {
        total_size += PAGE_SIZE - (total_size % PAGE_SIZE);
    }

    mem_block_t *current = free_blocks;
    mem_block_t *prev = NULL;

    while (current) {
        if (current->is_free && current->size >= size) {
            // Нашли подходящий свободный блок
            if (current->size > size + sizeof(mem_block_t) + BLOCK_SIZE) {
                // Можем разделить блок
                mem_block_t *new_block =
                        (mem_block_t *)((u32)current + sizeof(mem_block_t) + size);
                new_block->size =
                        current->size - size - sizeof(mem_block_t);
                new_block->is_free = 1;
                new_block->next = current->next;

                current->size = size;
                current->next = new_block;

                free_mem_addr += current->size;
            }

            current->is_free = 0;

            // Отображаем физическую память в виртуальную, если нужно
            void *virtual_addr = (void *)((u32)current + sizeof(mem_block_t));
            void *physical_addr = get_physical_address(virtual_addr);

            if (!physical_addr) {
                // Нужно выделить новую физическую страницу
                physical_addr = (void *)free_mem_addr;
                free_mem_addr += PAGE_SIZE;

                map_page(virtual_addr, physical_addr,
                         PAGE_PRESENT | PAGE_WRITE);
            }

            return virtual_addr;
        }
        prev = current;
        current = current->next;
    }

    kprint("No free blocks found\n");
    return NULL;
}

void *krealloc(void *ptr, u32 size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) { kfree(ptr); return NULL; }

    mem_block_t *block = (mem_block_t*)((u32)ptr - sizeof(mem_block_t));
    if (block->size >= size) return ptr;

    void *new_ptr = kmalloc(size);
    if (new_ptr) {
        memory_copy(ptr, new_ptr, block->size);
        kfree(ptr);
    }
    return new_ptr;
}

void kfree(void *ptr) {
    if (!ptr)
        return;

    // получаем указатель на заголовок блока
    mem_block_t *block = (mem_block_t *)((u32)ptr - sizeof(mem_block_t));

    if (block->is_free) {
        kprint("Double free detected!\n");
        return;
    }

    block->is_free = 1;

    // Освобождаем связанные страницы, если нужно
    u32 start_addr = (u32)block;
    u32 end_addr = start_addr + sizeof(mem_block_t) + block->size;

    for (u32 addr = start_addr; addr < end_addr; addr += PAGE_SIZE) {
        if (addr % PAGE_SIZE == 0) {
            // Проверяем, используются ли еще другие блоки в этой странице
            int page_in_use = 0;
            mem_block_t *check = free_blocks;
            while (check) {
                u32 check_start = (u32)check;
                u32 check_end = check_start + sizeof(mem_block_t) + check->size;

                if (check != block && !check->is_free &&
                    addr >= check_start && addr < check_end) {
                    page_in_use = 1;
                    break;
                }
                check = check->next;
            }

            if (!page_in_use) {
                unmap_page((void *)addr);
            }
        }
    }

    /* попробуем объединить с соседними свободными блоками*/
    mem_block_t *current = free_blocks;
    while (current) {
        if (current->is_free && current->next && current->next->is_free) {
            // соединим текущий блок со следующим
            current->size += sizeof(mem_block_t) + current->next->size;
            current->next = current->next->next;
            free_mem_addr -= current->size;
        }
        current = current->next;
    }
}

void *kmalloc_a(u32 size) {
    // Выравниваем размер до границы PAGE_SIZE
    if (size % PAGE_SIZE != 0) {
        size += PAGE_SIZE - (size % PAGE_SIZE);
    }

    // Для простоты используем обычный kmalloc и выравниваем вручную
    // Это не оптимально, но работает на ранних этапах инициализации
    void *ptr = kmalloc(size + PAGE_SIZE);
    if (!ptr) {
        kprint("kmalloc_a: No memory available\n");
        return NULL;
    }

    // Выравниваем указатель
    u32 addr = (u32)ptr;
    u32 aligned_addr = (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // Если нужно, создаем смещение для последующего освобождения
    if (aligned_addr != addr) {
        // Сохраняем оригинальный указатель перед выровненным адресом
        *((u32 *)(aligned_addr - sizeof(u32))) = addr;
    } else {
        // Если адрес уже выровнен, сохраняем 0 как маркер
        *((u32 *)(aligned_addr - sizeof(u32))) = 0;
    }

    return (void *)aligned_addr;
}

// Функция для освобождения выровненной памяти
void kfree_a(void *ptr) {
    if (!ptr) return;

    u32 aligned_addr = (u32)ptr;
    u32 original_addr = *((u32 *)(aligned_addr - sizeof(u32)));

    if (original_addr == 0) {
        // Адрес был уже выровнен, освобождаем как есть
        kfree(ptr);
    } else {
        // Освобождаем оригинальный указатель
        kfree((void *)original_addr);
    }
}

// Обновленная функция get_meminfo с информацией о paging
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

    // Добавляем информацию о paging
    meminfo.used_pages = get_memory_used_pages();
    meminfo.free_pages = get_memory_free_pages();
    meminfo.total_pages = meminfo.used_pages + meminfo.free_pages;
    meminfo.page_size = PAGE_SIZE;

    return meminfo;
}

// Обновленная функция kmemdump с информацией о paging
void kmemdump() {
    meminfo_t info = get_meminfo();
    mem_block_t *current = info.free_blocks;
    u32 counter = 0;

    kprintf("Heap: %x - %x (%d bytes)\n", info.heap_start,
                    info.heap_start + info.heap_size, info.heap_size);
    kprintf("Block size: %d bytes\n", info.block_size);
    kprintf("Total: USED=%d bytes, FREE=%d bytes, in %d blocks\n\n",
                    info.total_used, info.total_free, info.block_count);

    kprintf("Paging: USED=%d pages, FREE=%d pages, TOTAL=%d pages (%d KB)\n",
                    info.used_pages, info.free_pages, info.total_pages,
                    info.total_pages * info.page_size / 1024);

    while (current) {
        kprintf("Block %d: %x, Size=%d, %s\n", counter++, (u32)current,
                        current->size, current->is_free ? "FREE" : "USED");
        current = current->next;
    }
}

void get_freememaddr() {
    char free_mem_addr_str[32] = "";
    hex_to_ascii(free_mem_addr, free_mem_addr_str);

    kprintf("%s\n", free_mem_addr_str);
}
