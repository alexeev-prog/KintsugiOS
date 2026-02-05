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

static meminfo_t stats;

// инициализация кучи
void heap_init() {
    free_blocks = (mem_block_t*)HEAP_START;
    free_blocks->size = HEAP_SIZE - sizeof(mem_block_t);
    free_blocks->next = NULL;
    free_blocks->is_free = 1;
    heap_current_end = HEAP_START + HEAP_SIZE;

    stats.alloc_count = 0;
    stats.free_count = 0;
    stats.max_used = 0;
    stats.leak_count = 0;
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

    u32 total_size = size + 2 * GUARD_SIZE;
    u32 aligned_size = (total_size + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);

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

        u32* guard_start = (u32*)((u32)best_fit + sizeof(mem_block_t));

        for (int i = 0; i < GUARD_SIZE / sizeof(u32); i++) {
            guard_start[i] = MAGIC_NUMBER;
        }

        void* user_ptr = (void*)((u32)guard_start + GUARD_SIZE);

        u32 user_data_size = aligned_size - 2 * GUARD_SIZE;

        u32 block_end = (u32)best_fit + sizeof(mem_block_t) + best_fit->size;
        u32* guard_end = (u32*)(block_end - GUARD_SIZE);
        for (int i = 0; i < GUARD_SIZE / sizeof(u32); i++) {
            guard_end[i] = MAGIC_NUMBER;
        }

        stats.alloc_count++;

        return user_ptr;
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

    u32* guard_start = (u32*)((u32)ptr - GUARD_SIZE);

    mem_block_t* block = (mem_block_t*)((u32)guard_start - sizeof(mem_block_t));

    for (int i = 0; i < GUARD_SIZE / sizeof(u32); i++) {
        if (guard_start[i] != MAGIC_NUMBER) {
            printf_panic_screen("Memory Corruption",
                                "Start guard corrupted at offset %d\n" "Block: 0x%x, User ptr: 0x%x",
                                i * sizeof(u32),
                                (u32)block,
                                (u32)ptr);
        }
    }

    u32 user_data_size = block->size - 2 * GUARD_SIZE;

    u32 block_end = (u32)block + sizeof(mem_block_t) + block->size;
    u32* guard_end = (u32*)(block_end - GUARD_SIZE);
    for (int i = 0; i < GUARD_SIZE / sizeof(u32); i++) {
        if (guard_end[i] != MAGIC_NUMBER) {
            printf_panic_screen("Memory Corruption",
                                "End guard corrupted at offset %d\n" "Block: 0x%x, User ptr: 0x%x",
                                i * sizeof(u32),
                                (u32)block,
                                (u32)ptr);
        }
    }

    if (block->is_free) {
        return;
    }

    for (int i = 0; i < GUARD_SIZE / sizeof(u32); i++) {
        guard_start[i] = 0;
        guard_end[i] = 0;
    }

    block->is_free = 1;

    stats.free_count++;

    if (block->next && block->next->is_free) {
        block->size += sizeof(mem_block_t) + block->next->size;
        block->next = block->next->next;
    }

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
    mem_block_t* current = free_blocks;
    u32 total_used = 0;
    u32 total_free = 0;
    u32 block_count = 0;
    u32 used_blocks = 0;

    while (current) {
        block_count++;
        if (current->is_free) {
            total_free += current->size;
        } else {
            total_used += current->size;
            used_blocks++;
        }
        current = current->next;
    }

    stats.total_used = total_used;
    stats.total_free = total_free;
    stats.block_count = block_count;
    stats.leak_count = used_blocks;

    stats.heap_start = HEAP_START;
    stats.heap_size = heap_current_end - HEAP_START;
    stats.heap_current_end = heap_current_end;
    stats.block_size = BLOCK_SIZE;
    stats.free_blocks = free_blocks;

    return stats;
}

void kmemdump() {
    meminfo_t info = get_meminfo();
    mem_block_t* current = info.free_blocks;
    u32 counter = 0;

    printf("\nHeap: 0x%x - 0x%x (%d bytes)\n",
           HEAP_START,
           info.heap_current_end,
           info.heap_current_end - HEAP_START);
    printf("Block size: %d bytes, Guard: %d bytes\n", info.block_size, GUARD_SIZE);
    printf(
        "Allocations: %d, Frees: %d, Leaks: %d blocks\n", info.alloc_count, info.free_count, info.leak_count);
    printf(
        "Max used: %d bytes, Current: USED=%d, FREE=%d\n", info.max_used, info.total_used, info.total_free);
    printf("Total blocks: %d\n", info.block_count);

    while (current) {
        printf("Block %d: 0x%x, Size=%d, %s",
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
