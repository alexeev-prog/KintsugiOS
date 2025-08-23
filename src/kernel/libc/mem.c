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

// TODO: Paging is not implemented
void *get_physaddr(void *virtualaddr) {
    unsigned long pdindex = (unsigned long)virtualaddr >> 22;
    unsigned long ptindex = (unsigned long)virtualaddr >> 12 & 0x03FF;

    unsigned long *pd = (unsigned long *)0xFFFFF000;
    // Here you need to check whether the PD entry is present.

    unsigned long *pt = ((unsigned long *)0xFFC00000) + (0x400 * pdindex);
    // Here you need to check whether the PT entry is present.

    return (void *)((pt[ptindex] & ~0xFFF) + ((unsigned long)virtualaddr & 0xFFF));
}

void* kmalloc(u32 size) {
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
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;

    // получаем указатель на заголовок блока
    mem_block_t* block = (mem_block_t*)((u32)ptr - sizeof(mem_block_t));

    if (block->is_free) { // блок уже освобожден
        kprint("Double free detected!\n");
        return;
    }

    block->is_free = 1;

    /* попробуем объединить с соседними свободными блоками*/
    mem_block_t* current = free_blocks;
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

meminfo_t get_meminfo() {
    meminfo_t meminfo;

    mem_block_t* current = free_blocks;
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
    mem_block_t* current = info.free_blocks;
    u32 counter = 0;

    kprintf("Heap: %x - %x (%d bytes)\n", info.heap_start, info.heap_start + info.heap_size, info.heap_size);
    kprintf("Block size: %d bytes\n", info.block_size);
    kprintf("Total: USED=%d bytes, FREE=%d bytes, in %d blocks\n\n", info.total_used, info.total_free, info.block_count);

    while (current) {
        kprintf("Block %d: %x, Size=%d, %s\n", counter++, (u32)current, current->size, current->is_free ? "FREE" : "USED");
        current = current->next;
    }
}

void get_freememaddr() {
    char free_mem_addr_str[32] = "";
	hex_to_ascii(free_mem_addr, free_mem_addr_str);

	kprintf("%s\n", free_mem_addr_str);
}
