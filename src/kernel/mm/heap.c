#include "heap.h"

#include "../kernel/log.h"
#include "../kklibc/kklibc.h"
#include "memory.h"
#define MAGIC 0x48454150u
u32 heap_current_end = HEAP_START;
static mem_block_t* head;
static u32 allocations, frees, peak;
_Static_assert(sizeof(mem_block_t) == 16, "heap alignment");

static int valid(mem_block_t* b) {
    u32 p = (u32)b;
    int ok = p >= HEAP_START && p <= heap_current_end - sizeof(*b) && !(p & 15) && b->magic == MAGIC
        && b->size <= heap_current_end - p - sizeof(*b) && !(b->size & 15) && b->is_free <= 1
        && (u32)b->next == (p + sizeof(*b) + b->size == heap_current_end ? 0 : p + sizeof(*b) + b->size);
    if (!ok) {
        if (p < HEAP_START || p > heap_current_end - sizeof(*b) || (p & 15)) {
            panic("Heap list pointer outside heap", NULL);
        }
        char text[200];
        snprintf(
            text,
            sizeof(text),
            "Bad heap header %x size=%x next=%x magic=%x free=%u end=%x",
            p,
            b->size,
            (u32)b->next,
            b->magic,
            b->is_free,
            heap_current_end);
        panic(text, NULL);
    }
    return ok;
}

static mem_block_t* find(void* ptr) {
    for (mem_block_t* b = head; b; b = b->next) {
        if (!valid(b)) {
            panic("Heap metadata corruption", NULL);
        }
        if ((void*)(b + 1) == ptr) {
            return b;
        }
    }
    return NULL;
}

static void merge(void) {
    for (mem_block_t* b = head; b; b = b->next) {
        if (!valid(b)) {
            panic("Heap metadata corruption", NULL);
        }
        while (b->is_free && b->next) {
            if (!valid(b->next)) {
                panic("Heap metadata corruption", NULL);
            }
            if (!b->next->is_free) {
                break;
            }
            b->size += sizeof(*b) + b->next->size;
            b->next = b->next->next;
        }
    }
}

int expand_heap(u32 size) {
    if (!size || size > HEAP_SIZE || size > HEAP_SIZE - (heap_current_end - HEAP_START)) {
        return 0;
    }
    u32 bytes = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (bytes > HEAP_SIZE - (heap_current_end - HEAP_START)) {
        return 0;
    }
    u32 offset = 0;
    for (; offset < bytes; offset += PAGE_SIZE) {
        u32 page = pmm_alloc_page();
        if (!page) {
            break;
        }
        if (!vmm_map_page(heap_current_end + offset, page, VMM_WRITE)) {
            pmm_free_page(page);
            break;
        }
    }
    if (offset != bytes) {
        while (offset) {
            offset -= PAGE_SIZE;
            u32 va = heap_current_end + offset, pa = vmm_translate(va);
            vmm_unmap_page(va);
            pmm_free_page(pa);
        }
        return 0;
    }
    mem_block_t* b = (mem_block_t*)heap_current_end;
    *b = (mem_block_t){ .size = bytes - sizeof(*b), .magic = MAGIC, .is_free = 1 };
    if (!head) {
        head = b;
    } else {
        mem_block_t* last = head;
        while (last->next) {
            last = last->next;
        }
        last->next = b;
    }
    heap_current_end += bytes;
    merge();
    return 1;
}

void heap_init(void) {
    if (!expand_heap(64 * 1024)) {
        panic("Cannot initialize heap", NULL);
    }
}

static void split(mem_block_t* b, u32 size) {
    if (b->size < size + sizeof(*b) + 16) {
        return;
    }
    mem_block_t* rest = (mem_block_t*)((u8*)(b + 1) + size);
    *rest =
        (mem_block_t){ .size = b->size - size - sizeof(*b), .next = b->next, .magic = MAGIC, .is_free = 1 };
    b->size = size;
    b->next = rest;
}

void* kmalloc(u32 size) {
    if (!size || size > HEAP_SIZE - 32) {
        return NULL;
    }
    size = (size + 15) & ~15u;
    for (;;) {
        mem_block_t* best = NULL;
        for (mem_block_t* b = head; b; b = b->next) {
            if (!valid(b)) {
                panic("Heap metadata corruption", NULL);
            }
            if (b->is_free && b->size >= size && (!best || b->size < best->size)) {
                best = b;
            }
        }
        if (best) {
            split(best, size);
            best->is_free = 0;
            ++allocations;
            meminfo_t info = get_meminfo();
            if (info.total_used > peak) {
                peak = info.total_used;
            }
            return best + 1;
        }
        if (!expand_heap(size + sizeof(mem_block_t))) {
            return NULL;
        }
    }
}

void kfree(void* ptr) {
    if (!ptr) {
        return;
    }
    mem_block_t* b = find(ptr);
    if (!b || b->is_free) {
        printf("Invalid/double free: %x\n", (u32)ptr);
        return;
    }
    memset(ptr, 0xdd, b->size);
    b->is_free = 1;
    ++frees;
    merge();
}

void* krealloc(void* ptr, u32 size) {
    if (!ptr) {
        return kmalloc(size);
    }
    mem_block_t* b = find(ptr);
    if (!b || b->is_free) {
        return NULL;
    }
    if (!size) {
        kfree(ptr);
        return NULL;
    }
    if (size > HEAP_SIZE - 32) {
        return NULL;
    }
    u32 aligned = (size + 15) & ~15u;
    if (b->size >= aligned) {
        split(b, aligned);
        merge();
        return ptr;
    }
    if (b->next) {
        if (!valid(b->next)) {
            panic("Heap metadata corruption", NULL);
        }
        if (b->next->is_free && b->size + sizeof(*b) + b->next->size >= aligned) {
            b->size += sizeof(*b) + b->next->size;
            b->next = b->next->next;
            split(b, aligned);
            meminfo_t info = get_meminfo();
            if (info.total_used > peak) {
                peak = info.total_used;
            }
            return ptr;
        }
    }
    void* fresh = kmalloc(size);
    if (fresh) {
        memcpy(fresh, ptr, b->size);
        kfree(ptr);
    }
    return fresh;
}

void* kmalloc_aligned(u32 size, u32 alignment) {
    if (!size || alignment < 16 || (alignment & (alignment - 1)) || alignment > HEAP_SIZE / 2
        || size > HEAP_SIZE - alignment - sizeof(void*)) {
        return NULL;
    }
    void* raw = kmalloc(size + alignment + sizeof(void*));
    if (!raw) {
        return NULL;
    }
    void* result = (void*)(((u32)raw + sizeof(void*) + alignment - 1) & ~(alignment - 1));
    ((void**)result)[-1] = raw;
    return result;
}

void kfree_aligned(void* ptr) {
    if (!ptr) {
        return;
    }
    u32 p = (u32)ptr;
    if (p < HEAP_START + sizeof(void*) || p >= heap_current_end) {
        return;
    }
    void* raw = ((void**)ptr)[-1];
    mem_block_t* b = find(raw);
    if (b && !b->is_free && p >= (u32)raw + sizeof(void*) && p < (u32)raw + b->size) {
        kfree(raw);
    }
}

meminfo_t get_meminfo(void) {
    meminfo_t info = { .heap_start = HEAP_START,
                       .heap_size = heap_current_end - HEAP_START,
                       .heap_current_end = heap_current_end,
                       .block_size = 16,
                       .free_blocks = head,
                       .alloc_count = allocations,
                       .free_count = frees,
                       .max_used = peak };
    for (mem_block_t* b = head; b; b = b->next) {
        if (!valid(b)) {
            panic("Heap metadata corruption", NULL);
        }
        ++info.block_count;
        if (b->is_free) {
            info.total_free += b->size;
            if (b->size > info.largest_free) {
                info.largest_free = b->size;
            }
        } else {
            info.total_used += b->size;
            ++info.leak_count;
        }
    }
    return info;
}

void kmemdump(void) {
    meminfo_t i = get_meminfo();
    printf("Heap: %x - %x committed=%u limit=%u\n", HEAP_START, heap_current_end, i.heap_size, HEAP_SIZE);
    printf(
        "Heap used=%u free=%u largest=%u metadata=%u\n",
        i.total_used,
        i.total_free,
        i.largest_free,
        i.block_count * 16);
    printf(
        "Allocations=%u frees=%u live=%u peak=%u blocks=%u\n",
        allocations,
        frees,
        i.leak_count,
        peak,
        i.block_count);
    for (mem_block_t* b = head; b; b = b->next) {
        printf("%x size=%u %s\n", (u32)(b + 1), b->size, b->is_free ? "FREE" : "USED");
    }
}

void kmemcheck(void* ptr) {
    mem_block_t* b = find(ptr);
    printf("Heap pointer %x: %s\n", (u32)ptr, b && !b->is_free ? "allocated" : "invalid/free");
}

void get_freememaddr(void) {
    printf("Heap end: %x (virtual)\n", heap_current_end);
}
