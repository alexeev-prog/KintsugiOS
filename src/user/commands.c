/* Freestanding user programs: no kernel headers or direct device access. */
#include "runtime.h"

typedef struct {
    u32 size;
    unsigned char directory, readonly;
} stat_t;

typedef struct {
    char name[13];
    stat_t stat;
} dirent_t;

static int call(u32 number, u32 a, u32 b, u32 c, u32 d) {
    return syscall(number, a, b, c, d);
}

static u32 length(const char* s) {
    u32 n = 0;
    while (s[n]) {
        ++n;
    }
    return n;
}

static void print(const char* s) {
    call(1, (u32)s, length(s), 0, 0);
}

static void number(u32 n) {
    char text[11];
    u32 i = 10;
    text[i] = 0;
    do {
        text[--i] = '0' + n % 10;
        n /= 10;
    } while (n);
    print(text + i);
}

int main(int argc, char** argv) {
#if COMMAND == 1
    for (int i = 1; i < argc; ++i) {
        if (i > 1) {
            print(" ");
        }
        print(argv[i]);
    }
    print("\n");
    return 0;
#elif COMMAND == 2
    if (argc != 2) {
        print("Usage: cat <file>\n");
        return 2;
    }
    char buffer[512];
    u32 offset = 0;
    int n;
    while ((n = call(2, (u32)argv[1], offset, (u32)buffer, sizeof(buffer))) > 0) {
        call(1, (u32)buffer, n, 0, 0);
        offset += n;
    }
    if (n < 0) {
        print("cat: read failed\n");
        return 1;
    }
    return 0;
#elif COMMAND == 3
    if (argc > 2) {
        print("Usage: ls [path]\n");
        return 2;
    }
    u32 cursor = 0;
    dirent_t entry;
    int n;
    while ((n = call(3, (u32)(argc == 2 ? argv[1] : "."), (u32)&cursor, (u32)&entry, 0)) > 0) {
        print(entry.name);
        print(entry.stat.directory ? " <DIR> " : " ");
        number(entry.stat.size);
        print(" bytes\n");
    }
    if (n < 0) {
        print("ls: filesystem unavailable or invalid directory\n");
        return 1;
    }
    return 0;
#elif COMMAND == 4
    (void)argc;
    (void)argv;
    char path[256];
    if (call(4, (u32)path, sizeof(path), 0, 0)) {
        return 1;
    }
    print(path);
    print("\n");
    return 0;
#elif COMMAND == 5
    (void)argc;
    (void)argv;
    *(volatile u32*)0xb8000 = 0; /* Protection test: must terminate only this program. */
    return 1;
#elif COMMAND == 6
    (void)argc;
    (void)argv;
    int ok = call(1, 0xb8000, 4, 0, 0) < 0 && call(2, (u32) "/README.TXT", 0, 0xb8000, 4) < 0
        && call(999, 0, 0, 0, 0) < 0;
    print(ok ? "User syscall checks: PASS\n" : "User syscall checks: FAIL\n");
    char* memory = malloc(9000);
    ok &= memory != (void*)0;
    if (memory) {
        for (u32 i = 0; i < 9000; ++i) {
            memory[i] = (char)i;
        }
        for (u32 i = 0; i < 9000; ++i) {
            if (memory[i] != (char)i) {
                ok = 0;
            }
        }
        free(memory);
        char* again = malloc(9000);
        ok &= again == memory;
        free(again);
    }
    print(ok ? "User malloc checks: PASS\n" : "User malloc checks: FAIL\n");
    return ok ? 0 : 1;
#elif COMMAND == 7
    (void)argc;
    (void)argv;
    char* buffer = malloc(256);
    if (!buffer) {
        return 1;
    }
    print("stdin> ");
    int n = read_stdin(buffer, 256);
    if (n >= 0) {
        print("input: ");
        call(1, (u32)buffer, n, 0, 0);
    }
    free(buffer);
    return n < 0 ? 1 : 0;
#endif
}
