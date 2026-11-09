#include "runtime.h"

#include "errors.h"

const char* kos_error(int error) {
    return kos_error_text(error);
}

int ftruncate(int fd, u32 size) {
    return syscall(KOS_SYS_FTRUNCATE, fd, size, 0, 0);
}

int fstat(int fd, kos_stat* stat) {
    return syscall(KOS_SYS_FSTAT, fd, (u32)stat, 0, 0);
}

int sync(void) {
    return syscall(KOS_SYS_SYNC, 0, 0, 0, 0);
}

int kill(int pid) {
    return syscall(KOS_SYS_KILL, pid, 0, 0, 0);
}

static kos_env_entry env_cache[KOS_ENV_MAX];

const char* getenv(const char* name) {
    if (syscall(KOS_SYS_ENV_GET, (u32)env_cache, 0, 0, 0)) {
        return (void*)0;
    }
    for (int i = 0; i < KOS_ENV_MAX; ++i) {
        u32 n = 0;
        while (name[n] && name[n] == env_cache[i].name[n]) {
            ++n;
        }
        if (!name[n] && !env_cache[i].name[n]) {
            return env_cache[i].value;
        }
    }
    return (void*)0;
}

static int environment_set(const char* name, const char* value, int remove) {
    kos_env_entry entry = { { 0 }, { 0 } };
    u32 n = 0;
    while (name[n] && n + 1 < KOS_ENV_NAME) {
        entry.name[n] = name[n];
        ++n;
    }
    if (name[n]) {
        return -3;
    }
    n = 0;
    while (value[n] && n + 1 < KOS_ENV_VALUE) {
        entry.value[n] = value[n];
        ++n;
    }
    if (value[n]) {
        return -3;
    }
    return syscall(KOS_SYS_ENV_SET, (u32)&entry, remove, 0, 0);
}

int setenv(const char* name, const char* value) {
    return environment_set(name, value, 0);
}

int unsetenv(const char* name) {
    return environment_set(name, "", 1);
}

int open(const char* path, u32 flags) {
    return syscall(KOS_SYS_OPEN, (u32)path, flags, 0, 0);
}

int close(int fd) {
    return syscall(KOS_SYS_CLOSE, fd, 0, 0, 0);
}

int read(int fd, void* buffer, u32 size) {
    return syscall(KOS_SYS_READ, fd, (u32)buffer, size, 0);
}

int write(int fd, const void* buffer, u32 size) {
    return syscall(KOS_SYS_WRITE, fd, (u32)buffer, size, 0);
}

int dup2(int source, int destination) {
    return syscall(KOS_SYS_DUP2, source, destination, 0, 0);
}

int pipe(int pair[2]) {
    return syscall(KOS_SYS_PIPE, (u32)pair, 0, 0, 0);
}

int seek(int fd, u32 offset) {
    return syscall(KOS_SYS_SEEK, fd, offset, 0, 0);
}

int write_all(int fd, const void* buffer, u32 size) {
    u32 offset = 0;
    while (offset < size) {
        u32 chunk = size - offset;
        if (chunk > 4096) {
            chunk = 4096;
        }
        int result = write(fd, (const char*)buffer + offset, chunk);
        if (result <= 0) {
            return result < 0 ? result : -2;
        }
        offset += result;
    }
    return offset;
}

/* Freestanding compiler-generated aggregate copies may call these symbols. */
void* memcpy(void* destination, const void* source, u32 size) {
    unsigned char* out = destination;
    const unsigned char* in = source;
    for (u32 i = 0; i < size; ++i) {
        out[i] = in[i];
    }
    return destination;
}

int spawn(const char* path, int argc, char** argv) {
    return syscall(13, (u32)path, argc, (u32)argv, 0);
}

int wait(int pid) {
    return syscall(14, pid, 0, 0, 0);
}

int getpid(void) {
    return syscall(15, 0, 0, 0, 0);
}

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
