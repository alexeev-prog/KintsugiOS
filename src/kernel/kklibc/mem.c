/*------------------------------------------------------------------------------
 *  Kintsugi OS KKLIBC source code
 *  File: kklibc/mem.c
 *  Title: Функции работы с памятью
 *	Description: null
 * ----------------------------------------------------------------------------*/

#include "mem.h"

#include "../drivers/screen.h"
#include "../kernel/sysinfo.h"
#include "ctypes.h"
#include "stdio.h"
#include "stdlib.h"

u32 free_mem_addr = HEAP_START;
static mem_block_t* free_blocks = NULL;
u32 heap_current_end = HEAP_START + HEAP_SIZE;

// инициализация кучи
void heap_init() {
    free_blocks = (mem_block_t*)HEAP_START;
    free_blocks->size = HEAP_SIZE - sizeof(mem_block_t);
    free_blocks->next = NULL;
    free_blocks->is_free = 1;
    heap_current_end = HEAP_START + HEAP_SIZE;
}

int expand_heap(u32 size) {
    if (heap_current_end + size > HEAP_START + HEAP_SIZE) {
        return 0;
    }

    u32 expand_size = size;

    mem_block_t* new_block = (mem_block_t*)heap_current_end;
    new_block->size = expand_size - sizeof(mem_block_t);
    new_block->is_free = 1;
    new_block->next = free_blocks;
    free_blocks = new_block;

    heap_current_end += expand_size;
    return 1;
}

void* kmalloc(u32 size) {
    if (HEAP_START + size > HEAP_START + HEAP_SIZE) {
        panic_red_screen("Memory Error", "Heap overflow");
        return NULL;
    }

    // выравниваем
    size = (size + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);

    mem_block_t* current = free_blocks;
    mem_block_t* prev = NULL;
    mem_block_t* best_fit = NULL;
    mem_block_t* best_fit_prev = NULL;

    while (current) {
        if (current->is_free && current->size >= size) {
            if (!best_fit || current->size < best_fit->size) {
                best_fit = current;
                best_fit_prev = prev;
            }
        }
        prev = current;
        current = current->next;
    }

    if (best_fit) {
        // сплиттим блог если достаточно места
        if (best_fit->size > size + sizeof(mem_block_t) + BLOCK_SIZE) {
            mem_block_t* new_block = (mem_block_t*)((u32)best_fit + sizeof(mem_block_t) + size);
            new_block->size = best_fit->size - size - sizeof(mem_block_t);
            new_block->is_free = 1;
            new_block->next = best_fit->next;

            best_fit->size = size;
            best_fit->next = new_block;
        }

        best_fit->is_free = 0;
        return (void*)((u32)best_fit + sizeof(mem_block_t));
    }

    // расширяем кучу если нужно
    if (expand_heap(size)) {
        return kmalloc(size);
    }

    panic_red_screen("Memory Error", "Out of memory");
    return NULL;
}

void* krealloc(void* ptr, u32 size) {
    if (!ptr) {
        return kmalloc(size);
    }
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    mem_block_t* block = (mem_block_t*)((u32)ptr - sizeof(mem_block_t));
    if (block->size >= size) {
        // уменьшение блока при необходимости
        if (block->size - size >= sizeof(mem_block_t) + BLOCK_SIZE) {
            mem_block_t* new_block = (mem_block_t*)((u32)block + sizeof(mem_block_t) + size);
            new_block->size = block->size - size - sizeof(mem_block_t);
            new_block->is_free = 1;
            new_block->next = block->next;
            block->next = new_block;
        }
        return ptr;
    }

    // выделяем нового блока и копипастим данных
    void* new_ptr = kmalloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        kfree(ptr);
    }
    return new_ptr;
}

void kfree(void* ptr) {
    if (!ptr) {
        return;
    }

    mem_block_t* block = (mem_block_t*)((u32)ptr - sizeof(mem_block_t));
    if (block->is_free) {
        return;
    }

    block->is_free = 1;

    // Объединение с последующим свободным блоком
    if (block->next && block->next->is_free) {
        block->size += sizeof(mem_block_t) + block->next->size;
        block->next = block->next->next;
    }

    // объединение с предыдущим свободным блоком
    mem_block_t* current = free_blocks;
    mem_block_t* prev = NULL;

    while (current) {
        if (current->is_free && (u32)current + sizeof(mem_block_t) + current->size == (u32)block) {
            current->size += sizeof(mem_block_t) + block->size;
            if (block->next && block->next->is_free) {
                current->size += sizeof(mem_block_t) + block->next->size;
                current->next = block->next->next;
            }
            return;
        }
        prev = current;
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
    mem_block_t* current = info.free_blocks;
    u32 counter = 0;

    printf(
        "Heap: %x - %x (%d bytes)\n", HEAP_START, info.heap_current_end, info.heap_current_end - HEAP_START);
    printf("Block size: %d bytes\n", info.block_size);
    printf("Total: USED=%d bytes, FREE=%d bytes, in %d blocks\n",
           info.total_used,
           info.total_free,
           info.block_count);

    while (current) {
        printf("Block %d: %x, Size=%d, %s\n",
               counter++,
               (u32)current,
               current->size,
               current->is_free ? "FREE" : "USED");
        current = current->next;
    }
}

void get_freememaddr() {
    char free_mem_addr_str[32] = "";
    hex_to_ascii(free_mem_addr, free_mem_addr_str);
    printf("%s\n", free_mem_addr_str);
}
