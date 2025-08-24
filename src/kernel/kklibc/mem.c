/*------------------------------------------------------------------------------
 *  Kintsugi OS KKLIBC source code
 *  File: kklibc/mem.c
 *  Title: Функции работы с памятью
 *	Description: null
 * ----------------------------------------------------------------------------*/

#include "mem.h"
#include "../drivers/screen.h"
#include "ctypes.h"
#include "stdio.h"
#include "stdlib.h"
#include "paging.h"

u32 free_mem_addr_guard1 = 0xDEADBEEF;
u32 free_mem_addr = HEAP_START;
u32 free_mem_addr_guard2 = 0xCAFEBABE;
static mem_block_t *free_blocks = NULL;
u32 heap_current_end = HEAP_START + HEAP_SIZE;

// Инициализация памяти heap
void heap_init() {
    // Инициализируем первый свободный блок на всей области кучи
    free_blocks = (mem_block_t *)HEAP_START;
    free_blocks->size = HEAP_SIZE - sizeof(mem_block_t);
    free_blocks->next = NULL;
    free_blocks->is_free = 1;

    // Обновим текущий конец кучи
    heap_current_end = HEAP_START + HEAP_SIZE;

    kprint("Heap initialized at ");
    char buf[32] = "";
    hex_to_ascii(HEAP_START, buf);
    kprint(buf);
    kprint("\n");
}

int expand_heap(u32 size) {
    // Выравниваем размер вверх до границы страницы
    u32 num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    u32 expand_size = num_pages * PAGE_SIZE;

    printf("Expanding heap by %d bytes (%d pages)\n", expand_size, num_pages);

    // Мы будем расширять кучу начиная с адреса heap_current_end
    u32 virtual_address = heap_current_end;

    for (u32 i = 0; i < num_pages; i++) {
        // Запрашиваем физический фрейм
        page_t *page = get_page(virtual_address, 1, kernel_directory);
        if (!page) {
            kprint("Failed to get page for heap expansion!\n");
            return 0;
        }

        // Выделяем фрейм для страницы (с флагами ядра и записи)
        alloc_frame(page, 0, 1);

        // Обновляем виртуальный адрес для следующей страницы
        virtual_address += PAGE_SIZE;
    }

    // Теперь нам нужно добавить новую область памяти в список свободных блоков
    mem_block_t *new_block = (mem_block_t *)heap_current_end;
    new_block->size = expand_size - sizeof(mem_block_t); // Учитываем заголовок
    new_block->is_free = 1;
    new_block->next = free_blocks; // Добавляем в начало списка

    // Обновляем глобальный список свободных блоков
    free_blocks = new_block;

    // Обновляем текущий конец кучи
    heap_current_end += expand_size;

    printf("Heap expanded successfully. New end: %x\n", heap_current_end);
    return 1;
}

void *kmalloc(u32 size) {
    // Выравниваем размер до границы BLOCK_SIZE
    if (size % BLOCK_SIZE != 0) {
        size += BLOCK_SIZE - (size % BLOCK_SIZE);
    }

    mem_block_t *current = free_blocks;
    mem_block_t *prev = NULL;

    while (current) {
        if (current->is_free && current->size >= size) {
            // Нашли подходящий свободный блок: размер текущего больше чем размер
            // выделяемой памяти плюс размер структуры блока памяти и плюс размер
            // самого блока
            if (current->size > size + sizeof(mem_block_t) + BLOCK_SIZE) {
                // Можем разделить блок
                mem_block_t *new_block = (mem_block_t *)((u32)current + sizeof(mem_block_t) +
                                                        size); // поинтер на новый блок
                new_block->size = current->size - size - sizeof(mem_block_t); // размер текущего - выделяемый - размер структуры
                new_block->is_free = 1; // свободен
                new_block->next = current->next;

                current->size = size;
                current->next = new_block;

                free_mem_addr += current->size;
            }

            current->is_free = 0;
            return (void *)((u32)current + sizeof(mem_block_t));
        }
        prev = current;
        current = current->next;
    }

    // Если дошли сюда, значит, подходящего блока не найдено -> пробуем расширить кучу
    printf("No free block found for size %d. Trying to expand heap...\n", size);
    if (expand_heap(size)) {
        // После расширения кучи пробуем аллоцировать снова (рекурсивно)
        return kmalloc(size);
    } else {
        kprint("Heap expansion failed!\n");
        return NULL;
    }
}

void *krealloc(void *ptr, u32 size) {
    if (!ptr)
        return kmalloc(size);

    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    // Получаем заголовок блока
    mem_block_t *block = (mem_block_t *)((u32)ptr - sizeof(mem_block_t));

    // Если текущий блок достаточно большой
    if (block->size >= size) {
        // Можно ли разделить блок?
        if (block->size - size >= sizeof(mem_block_t) + BLOCK_SIZE) {
            mem_block_t *new_block = (mem_block_t *)((u32)block + sizeof(mem_block_t) + size);
            new_block->size = block->size - size - sizeof(mem_block_t);
            new_block->is_free = 1;
            new_block->next = block->next;

            block->size = size;
            block->next = new_block;
        }
        return ptr;
    }

    // Пытаемся объединить с последующим свободным блоком
    if (block->next && block->next->is_free) {
        u32 total_size = block->size + sizeof(mem_block_t) + block->next->size;
        if (total_size >= size) {
            block->size = total_size;
            block->next = block->next->next;

            // Разделяем блок, если осталось место
            if (total_size - size >= sizeof(mem_block_t) + BLOCK_SIZE) {
                mem_block_t *new_block = (mem_block_t *)((u32)block + sizeof(mem_block_t) + size);
                new_block->size = total_size - size - sizeof(mem_block_t);
                new_block->is_free = 1;
                new_block->next = block->next;
                block->next = new_block;
                block->size = size;
            }
            return ptr;
        }
    }

    // Если не можем расширить, выделяем новый блок и копируем данные
    void *new_ptr = kmalloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        kfree(ptr);
    }
    return new_ptr;
}

void kfree(void *ptr) {
    if (!ptr)
        return;

    // Получаем указатель на заголовок блока
    mem_block_t *block = (mem_block_t *)((u32)ptr - sizeof(mem_block_t));

    if (block->is_free) {
        kprint("Double free detected!\n");
        return;
    }

    block->is_free = 1;

    // Попробуем объединить с соседними свободными блоками
    mem_block_t *current = free_blocks;
    mem_block_t *prev = NULL;

    while (current) {
        if (current->is_free && current->next && current->next->is_free) {
            // Объединяем текущий блок со следующим
            current->size += sizeof(mem_block_t) + current->next->size;
            current->next = current->next->next;
        }

        // Проверяем, можно ли освободить страницы
        if (current->is_free) {
            u32 block_start = (u32)current;
            u32 block_end = block_start + sizeof(mem_block_t) + current->size;

            // Выравниваем границы блока по границам страниц
            u32 start_page = block_start & ~(PAGE_SIZE - 1);
            u32 end_page = (block_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

            // Проверяем, полностью ли свободны страницы в этом блоке
            for (u32 page = start_page; page < end_page; page += PAGE_SIZE) {
                page_t *page_entry = get_page(page, 0, kernel_directory);
                if (page_entry && page_entry->present) {
                    // Проверяем, нет ли занятых блоков в этой странице
                    int page_is_free = 1;
                    mem_block_t *check = free_blocks;
                    while (check) {
                        u32 check_start = (u32)check;
                        u32 check_end = check_start + sizeof(mem_block_t) + check->size;

                        if (!check->is_free && check_start < page + PAGE_SIZE && check_end > page) {
                            page_is_free = 0;
                            break;
                        }
                        check = check->next;
                    }

                    if (page_is_free) {
                        // Освобождаем фрейм и страницу
                        free_frame(page_entry);
                        page_entry->present = 0;
                    }
                }
            }
        }

        prev = current;
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
    meminfo.heap_size = heap_current_end - HEAP_START;
    meminfo.heap_current_end = heap_current_end;
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

    printf("Heap: %x - %x (%d bytes)\n", HEAP_START,
                    info.heap_current_end, info.heap_current_end - info.heap_start);
    printf("Block size: %d bytes\n", info.block_size);
    printf("Total: USED=%d bytes, FREE=%d bytes, in %d blocks\n",
                    info.total_used, info.total_free, info.block_count);

    // Вывод информации о страницах хипа

    int present_pages_count = 0;
    int not_present_pages_count = 0;
    for (u32 addr = info.heap_start; addr < info.heap_current_end; addr += PAGE_SIZE) {
        page_t *page = get_page(addr, 0, kernel_directory);
        if (page && page->present) {
            present_pages_count += 1;
        } else {
            not_present_pages_count += 1;
        }
    }

    printf("Heap pages: PRESENT=%d  |  NOT PRESENT=%d", present_pages_count, not_present_pages_count);

    printf("\nMemory blocks:\n");
    while (current) {
        printf("Block %d: %x, Size=%d, %s\n", counter++, (u32)current,
                        current->size, current->is_free ? "FREE" : "USED");

        // Дополнительная информация о страницах этого блока
        u32 block_start = (u32)current;
        u32 block_end = block_start + sizeof(mem_block_t) + current->size;
        u32 start_page = block_start & ~(PAGE_SIZE - 1);
        u32 end_page = (block_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        for (u32 page_addr = start_page; page_addr < end_page; page_addr += PAGE_SIZE) {
            page_t *page = get_page(page_addr, 0, kernel_directory);
            if (page && page->present) {
                printf("    Page %x -> Frame %x\n", page_addr, page->frame * PAGE_SIZE);
            }
        }

        current = current->next;
    }
}

void get_freememaddr() {
    char free_mem_addr_str[32] = "";
    hex_to_ascii(free_mem_addr, free_mem_addr_str);

    printf("%s\n", free_mem_addr_str);
}
