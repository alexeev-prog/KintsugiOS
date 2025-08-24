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

u32 free_mem_addr_guard1 = 0xDEADBEEF;
static u32 free_mem_addr = HEAP_VIRTUAL_START;  // Теперь виртуальный адрес
u32 free_mem_addr_guard2 = 0xCAFEBABE;
static mem_block_t *free_blocks = NULL;

static u32 heap_physical_start = 0;

// Инициализация памяти heap
void heap_init() {
    // Выделяем физическую память для кучи
    heap_physical_start = 0x200000;  // Начинаем кучу с 2MB

    // Отображаем виртуальные адреса кучи на физические
    for (u32 i = 0; i < HEAP_SIZE; i += PAGE_SIZE) {
        void *virtual_addr = (void*)(HEAP_VIRTUAL_START + i);
        void *physical_addr = (void*)(heap_physical_start + i);
        map_page(virtual_addr, physical_addr, PAGE_PRESENT | PAGE_WRITABLE);
    }

    // Инициализируем первый свободный блок
    free_blocks = (mem_block_t *)HEAP_VIRTUAL_START;
    free_blocks->size = HEAP_SIZE - sizeof(mem_block_t);
    free_blocks->next = NULL;
    free_blocks->is_free = 1;
    free_blocks->is_page = 0;
    free_blocks->page = NULL;

    kprint("Heap initialized at virtual: ");
<<<<<<< HEAD
    kprint("Heap initialized at virtual: ");
=======
>>>>>>> 3fe2ead (realize paging memory, add kmalloc aligned; todo: realize kmemdump fully)
    char buf[32] = "";
    hex_to_ascii(HEAP_VIRTUAL_START, buf);
    kprint(buf);
    kprint(", physical: ");
    hex_to_ascii(heap_physical_start, buf);
<<<<<<< HEAD
    hex_to_ascii(HEAP_VIRTUAL_START, buf);
    kprint(buf);
    kprint(", physical: ");
    hex_to_ascii(heap_physical_start, buf);
=======
>>>>>>> 3fe2ead (realize paging memory, add kmalloc aligned; todo: realize kmemdump fully)
    kprint(buf);
    kprint("\n");
}


<<<<<<< HEAD

=======
>>>>>>> 3fe2ead (realize paging memory, add kmalloc aligned; todo: realize kmemdump fully)
// TODO: Paging is not implemented
void *get_physaddr(void *virtualaddr) {
    return get_physical_address(virtualaddr);
}

void *kmalloc_a(u32 size) {
    // Выравниваем размер до границы страницы
    if (size % PAGE_SIZE != 0) {
        size += PAGE_SIZE - (size % PAGE_SIZE);
    }

    // Ищем свободную физическую страницу
    // (здесь должна быть реализация аллокатора физических страниц)
    static u32 next_physical_page = 0x300000;  // Начинаем с 3MB

    void *physical_addr = (void*)next_physical_page;
    next_physical_page += size;

    // Выделяем виртуальный адрес
    static u32 next_virtual_addr = HEAP_VIRTUAL_START + HEAP_SIZE;
    void *virtual_addr = (void*)next_virtual_addr;
    next_virtual_addr += size;

    // Отображаем виртуальный адрес на физический
    for (u32 i = 0; i < size; i += PAGE_SIZE) {
        map_page((void*)((u32)virtual_addr + i),
                 (void*)((u32)physical_addr + i),
                 PAGE_PRESENT | PAGE_WRITABLE);
    }

    return virtual_addr;
<<<<<<< HEAD
    return get_physical_address(virtualaddr);
}

void *kmalloc_a(u32 size) {
    // Выравниваем размер до границы страницы
    if (size % PAGE_SIZE != 0) {
        size += PAGE_SIZE - (size % PAGE_SIZE);
    }

    // Ищем свободную физическую страницу
    // (здесь должна быть реализация аллокатора физических страниц)
    static u32 next_physical_page = 0x300000;  // Начинаем с 3MB

    void *physical_addr = (void*)next_physical_page;
    next_physical_page += size;

    // Выделяем виртуальный адрес
    static u32 next_virtual_addr = HEAP_VIRTUAL_START + HEAP_SIZE;
    void *virtual_addr = (void*)next_virtual_addr;
    next_virtual_addr += size;

    // Отображаем виртуальный адрес на физический
    for (u32 i = 0; i < size; i += PAGE_SIZE) {
        map_page((void*)((u32)virtual_addr + i),
                 (void*)((u32)physical_addr + i),
                 PAGE_PRESENT | PAGE_WRITABLE);
    }

    return virtual_addr;
=======
>>>>>>> 3fe2ead (realize paging memory, add kmalloc aligned; todo: realize kmemdump fully)
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
            // Нашли подходящий свободный блок
            if (current->size > size + sizeof(mem_block_t) + BLOCK_SIZE) {
                // Можем разделить блок
                mem_block_t *new_block = (mem_block_t*)((u32)current + sizeof(mem_block_t) + size);
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
<<<<<<< HEAD
            return (void*)((u32)current + sizeof(mem_block_t));
=======
>>>>>>> 3fe2ead (realize paging memory, add kmalloc aligned; todo: realize kmemdump fully)
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

    meminfo.heap_virtual_start = HEAP_VIRTUAL_START;
    meminfo.heap_physical_start = heap_physical_start;
<<<<<<< HEAD
    meminfo.heap_virtual_start = HEAP_VIRTUAL_START;
    meminfo.heap_physical_start = heap_physical_start;
=======
>>>>>>> 3fe2ead (realize paging memory, add kmalloc aligned; todo: realize kmemdump fully)
    meminfo.heap_size = HEAP_SIZE;
    meminfo.block_size = BLOCK_SIZE;
    meminfo.free_blocks = free_blocks;
    meminfo.total_used = total_used;
    meminfo.total_free = total_free;
    meminfo.block_count = block_count;

    // Информация о страницах (заглушка)
    meminfo.page_directory_phys = (u32)get_physical_address(get_current_page_directory());
    meminfo.total_pages = 1024;  // Примерное значение
    meminfo.used_pages = 256;    // Примерное значение
    meminfo.free_pages = 768;    // Примерное значение

<<<<<<< HEAD
    // Информация о страницах (заглушка)
    meminfo.page_directory_phys = (u32)get_physical_address(get_current_page_directory());
    meminfo.total_pages = 1024;  // Примерное значение
    meminfo.used_pages = 256;    // Примерное значение
    meminfo.free_pages = 768;    // Примерное значение

=======
>>>>>>> 3fe2ead (realize paging memory, add kmalloc aligned; todo: realize kmemdump fully)
    return meminfo;
}

// Дамп информации о памяти
<<<<<<< HEAD
// Дамп информации о памяти
=======
>>>>>>> 3fe2ead (realize paging memory, add kmalloc aligned; todo: realize kmemdump fully)
void kmemdump() {
    meminfo_t info = get_meminfo();
    mem_block_t *current = info.free_blocks;
    u32 counter = 0;

    kprintf("Heap: virtual %x - %x (%d bytes)\n", info.heap_virtual_start,
            info.heap_virtual_start + info.heap_size, info.heap_size);
    kprintf("Heap: physical %x - %x\n", info.heap_physical_start,
            info.heap_physical_start + info.heap_size);
<<<<<<< HEAD
    kprintf("Heap: virtual %x - %x (%d bytes)\n", info.heap_virtual_start,
            info.heap_virtual_start + info.heap_size, info.heap_size);
    kprintf("Heap: physical %x - %x\n", info.heap_physical_start,
            info.heap_physical_start + info.heap_size);
=======
>>>>>>> 3fe2ead (realize paging memory, add kmalloc aligned; todo: realize kmemdump fully)
    kprintf("Block size: %d bytes\n", info.block_size);
    kprintf("Total: USED=%d bytes, FREE=%d bytes, in %d blocks\n",
            info.total_used, info.total_free, info.block_count);
    kprintf("Pages: TOTAL=%d, USED=%d, FREE=%d\n\n",
            info.total_pages, info.used_pages, info.free_pages);
<<<<<<< HEAD
    kprintf("Total: USED=%d bytes, FREE=%d bytes, in %d blocks\n",
            info.total_used, info.total_free, info.block_count);
    kprintf("Pages: TOTAL=%d, USED=%d, FREE=%d\n\n",
            info.total_pages, info.used_pages, info.free_pages);
=======
>>>>>>> 3fe2ead (realize paging memory, add kmalloc aligned; todo: realize kmemdump fully)

    while (current) {
        kprintf("Block %d: virt=%x, phys=%x, Size=%d, %s\n", counter++,
                (u32)current, (u32)get_physical_address(current),
                current->size, current->is_free ? "FREE" : "USED");
<<<<<<< HEAD
        kprintf("Block %d: virt=%x, phys=%x, Size=%d, %s\n", counter++,
                (u32)current, (u32)get_physical_address(current),
                current->size, current->is_free ? "FREE" : "USED");
=======
>>>>>>> 3fe2ead (realize paging memory, add kmalloc aligned; todo: realize kmemdump fully)
        current = current->next;
    }

    kprint("Paging:\n");
    dump_page_tables();
}
