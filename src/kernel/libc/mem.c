/*------------------------------------------------------------------------------
*  Kintsugi OS C Libraries source code
*  File: libc/mem.c
*  Title: Функции работы с памятью
*	Description: null
* ----------------------------------------------------------------------------*/


#include "mem.h"
#include "../drivers/screen.h"
#include "../libc/string.h"
#include "../libc/stdio.h"

void memory_copy(u8 *source, u8 *dest, int nbytes) {
    int i;
    for (i = 0; i < nbytes; i++) {
        *(dest + i) = *(source + i);
    }
}

void memory_set(u8 *dest, u8 val, u32 len) {
    u8 *temp = (u8 *)dest;
    for ( ; len != 0; len--) *temp++ = val;
}

u32 free_mem_addr_guard1 = 0xDEADBEEF;
static u32 free_mem_addr = HEAP_START;
u32 free_mem_addr_guard2 = 0xCAFEBABE;
static mem_block_t* free_blocks = NULL;

// Инициализация памяти heap
void heap_init() {
    // Инициализируем первый свободный блок на всей области кучи
    free_blocks = (mem_block_t*)HEAP_START;
    free_blocks->size = HEAP_SIZE - sizeof(mem_block_t);
    free_blocks->next = NULL;
    free_blocks->is_free = 1;

    kprint("Heap initialized at ");
    char buf[32] = "";
    hex_to_ascii(HEAP_START, buf);
    kprint(buf);
    kprint("\n");
}

void* kmalloc_new(u32 size) {
    // Выравниваем размер до границы BLOCK_SIZE
    if (size % BLOCK_SIZE != 0) {
        size += BLOCK_SIZE - (size % BLOCK_SIZE);
    }

    mem_block_t* current = free_blocks;
    mem_block_t* prev = NULL;

    while (current) {
        if (current->is_free && current->size >= size) {
            // Нашли подходящий свободный блок: размер текущего больше чем размер выделяемой памяти плюс размер структуры блока памяти и плюс размер самого блока
            if (current->size > size + sizeof(mem_block_t) + BLOCK_SIZE) {
                // Можем разделить блок
                mem_block_t* new_block = (mem_block_t*)((u32)current + sizeof(mem_block_t) + size); // поинтер на новый блок
                new_block->size = current->size - size - sizeof(mem_block_t); // размер текущего - выделяемый - размер структуры
                new_block->is_free = 1; // свободен
                new_block->next = current->next;

                current->size = size;
                current->next = new_block;

                free_mem_addr += current->size;
            }

            current->is_free = 0;
            return (void*)((u32)current + sizeof(mem_block_t));
        }
        prev = current;
        current = current->next;
    }

    kprint("No free blocks found\n");
    return NULL; // Не нашли свободного блока
}

void kfree(void* ptr) {
    if (!ptr) return;

    // Получаем указатель на заголовок блока
    mem_block_t* block = (mem_block_t*)((u32)ptr - sizeof(mem_block_t));

    if (block->is_free) { // блок уже освобожден
        kprint("Double free detected!\n");
        return;
    }

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

    mem_block_t* current = free_blocks;
    u32 total_used = 0;
    u32 total_free = 0;
    u32 block_count = 0;

    kprint("Memory dump:\n");

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

void kmemdump() { // дамп памяти
    mem_block_t* current = free_blocks;
    u32 total_used = 0;
    u32 total_free = 0;
    u32 block_count = 0;

    kprint("Memory dump:\n");

    while (current) {
        kprint("Block ");
        char buf[32];
        int_to_ascii(block_count++, buf);
        kprint(buf);
        kprint(": Addr=");
        hex_to_ascii((u32)current, buf);
        kprint(buf);
        kprint(", Size=");
        int_to_ascii(current->size, buf);
        kprint(buf);
        kprint(", ");
        kprint(current->is_free ? "FREE" : "USED");
        kprint("\n");

        if (current->is_free) {
            total_free += current->size;
        } else {
            total_used += current->size;
        }

        current = current->next;
    }

    kprint("Total: USED=");
    char buf2[32];
    int_to_ascii(total_used, buf2);
    kprint(buf2);
    kprint(", FREE=");
    int_to_ascii(total_free, buf2);
    kprint(buf2);
    kprint("\n");
}

// LEGACY Arena
/* Реализация - это просто указатель на некоторую свободную память, которая продолжает расти */
u32 kmalloc(u32 size, int align, u32 *phys_addr) {

    if(free_mem_addr_guard1 != 0xDEADBEEF || free_mem_addr_guard2 != 0xCAFEBABE) {
        kprint("PANIC: Memory corruption detected around free_mem_addr!\n");
        // Зависаем или перезагружаемся
        // asm volatile("hlt");
        return -1;
    }

    /* Страницы выровнены по 4K, или 0x1000 */
    if (align == 1 && (free_mem_addr & 0xFFFFF000)) {
        free_mem_addr &= 0xFFFFF000;
        free_mem_addr += 0x1000;
    }
    /* Сохранить также физический адрес */
    if (phys_addr) *phys_addr = free_mem_addr;

    kprint("kmalloc legacy: allocating ");
    char size_str[16] = "";
    hex_to_ascii(size, size_str);
    kprint(size_str);
    kprint(" bytes at ");
    get_freememaddr();
    kprint("\n");

    u32 ret = free_mem_addr;
    free_mem_addr += size;
    return ret;
}

void get_freememaddr() {
    char free_mem_addr_str[32] = "";
	hex_to_ascii(free_mem_addr, free_mem_addr_str);

	kprintf("%s\n", free_mem_addr_str);
}
