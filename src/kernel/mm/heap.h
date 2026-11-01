#ifndef MM_HEAP_H
#define MM_HEAP_H
#include "../kklibc/ctypes.h"
#define HEAP_START 0xc0000000u
#define HEAP_SIZE 0x04000000u
#define BLOCK_SIZE 16u
typedef struct mem_block {
    u32 size;
    struct mem_block* next;
    u32 magic;
    u8 is_free;
    u8 padding[3];
} mem_block_t;
typedef struct {
    u32 heap_start, heap_size, heap_current_end, block_size;
    mem_block_t* free_blocks;
    u32 total_used, total_free, block_count, alloc_count, free_count, max_used, leak_count;
    u32 largest_free;
} meminfo_t;
extern u32 heap_current_end;
void heap_init(void);
int expand_heap(u32 size);
void* kmalloc(u32 size);
void* krealloc(void* ptr, u32 size);
void kfree(void* ptr);
void* kmalloc_aligned(u32 size, u32 alignment);
void kfree_aligned(void* ptr);
meminfo_t get_meminfo(void);
void kmemdump(void);
void kmemcheck(void* ptr);
void get_freememaddr(void);
#endif
