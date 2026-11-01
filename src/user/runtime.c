#include "runtime.h"

int syscall(u32 number, u32 a, u32 b, u32 c, u32 d) {
    int result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "b"(a), "c"(b), "d"(c), "S"(d)
                     : "memory", "cc");
    return result;
}

typedef struct block {
    u32 size;
    struct block* next;
    u32 available, padding;
} block_t;

static block_t* blocks;

void* malloc(u32 size) {
    if (!size || size > 16 * 1024 * 1024 - 32) {
        return (void*)0;
    }
    size = (size + 15) & ~15u;
    for (block_t* b = blocks; b; b = b->next) {
        if (!b->available || b->size < size) {
            continue;
        }
        if (b->size >= size + sizeof(*b) + 16) {
            block_t* rest = (block_t*)((char*)(b + 1) + size);
            *rest = (block_t){ b->size - size - sizeof(*b), b->next, 1, 0 };
            b->next = rest;
            b->size = size;
        }
        b->available = 0;
        return b + 1;
    }
    u32 bytes = (size + sizeof(block_t) + 4095) & ~4095u;
    block_t* b = (block_t*)syscall(5, bytes, 0, 0, 0);
    if (!b) {
        return (void*)0;
    }
    *b = (block_t){ bytes - sizeof(*b), (void*)0, 1, 0 };
    if (!blocks) {
        blocks = b;
    } else {
        block_t* last = blocks;
        while (last->next) {
            last = last->next;
        }
        last->next = b;
    }
    return malloc(size);
}

void free(void* pointer) {
    if (!pointer) {
        return;
    }
    for (block_t* b = blocks; b; b = b->next) {
        if ((void*)(b + 1) == pointer) {
            b->available = 1;
            break;
        }
    }
    for (block_t* b = blocks; b && b->next;) {
        block_t* n = b->next;
        if (b->available && n->available && (char*)(b + 1) + b->size == (char*)n) {
            b->size += sizeof(*n) + n->size;
            b->next = n->next;
        } else {
            b = n;
        }
    }
}

int read_stdin(void* buffer, u32 capacity) {
    return syscall(6, (u32)buffer, capacity, 0, 0);
}
