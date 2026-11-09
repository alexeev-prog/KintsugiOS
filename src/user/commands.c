/* Freestanding user programs: no kernel headers or direct device access. */
#include "fsabi.h"
#include "runtime.h"

typedef kos_stat stat_t;
typedef kos_dirent dirent_t;

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
    write_all(1, s, length(s));
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
#if COMMAND == 10
    (void)argc;
    (void)argv;
    int ends[2];
    char bytes[4];
    if (pipe(ends) || write(ends[1], "abc", 3) != 3 || read(ends[0], bytes, 3) != 3 || bytes[0] != 'a'
        || bytes[2] != 'c') {
        return 1;
    }
    if (close(ends[1]) || read(ends[0], bytes, 1) != 0 || close(ends[0])) {
        return 2;
    }
    if (pipe(ends) || close(ends[0]) || write(ends[1], "x", 1) != KOS_EPIPE || close(ends[1])) {
        return 3;
    }
    if (close(-1) != KOS_EBADF || dup2(-1, 1) != KOS_EBADF) {
        return 4;
    }
    int fd = open("/README.TXT", KOS_O_READ);
    if (fd < 0 || dup2(fd, 15) != 15 || read(fd, bytes, 1) != 1 || seek(15, 0) || read(fd, bytes, 1) != 1
        || bytes[0] != 'K' || close(fd) || close(15)) {
        return 5;
    }
    if (wait(0x7fffffff) != KOS_ECHILD || kill(getpid()) != KOS_EPERM || kill(0x7fffffff) != KOS_ESRCH) {
        return 6;
    }
    for (int i = 0; i < 8; ++i) {
        char* args[] = { "SPIN", 0 };
        int pid = spawn("/BIN/SPIN.ELF", 1, args);
        if (pid < 0 || kill(pid) || wait(pid) != 130 || wait(pid) != KOS_ECHILD) {
            return 13;
        }
    }
    /* Existing descriptors survive failed namespace changes. */
    fd = open("/README.TXT", KOS_O_READ);
    if (fd < 0 || !call(11, (u32) "/MISSING.ZZZ", 1, 0, 0) || read(fd, bytes, 1) != 1 || close(fd)) {
        return 7;
    }
    /* Only run mutating checks when the fixture was supplied by the test harness. */
    fd = open("/FDTEST.TXT", KOS_O_READ | KOS_O_WRITE);
    if (fd >= 0) {
        kos_stat stat;
        if (ftruncate(fd, 3) || fstat(fd, &stat) || stat.size != 3 || seek(fd, 1) || write(fd, "Z", 1) != 1
            || seek(fd, 0) || read(fd, bytes, 3) != 3 || bytes[1] != 'Z' || close(fd)) {
            return 8;
        }
        if (open("/FDTEST.TXT", KOS_O_WRITE | KOS_O_CREATE | KOS_O_EXCL) != -5) {
            return 9;
        }
        fd = open("/FDTEST.TXT", KOS_O_WRITE | KOS_O_APPEND);
        if (fd < 0 || write(fd, "!", 1) != 1 || fstat(fd, &stat) || stat.size != 4 || close(fd)) {
            return 10;
        }
        fd = open("/FDNEW.TXT", KOS_O_WRITE | KOS_O_CREATE | KOS_O_TRUNC);
        if (fd < 0 || write(fd, "new", 3) != 3 || close(fd) || sync()) {
            return 11;
        }
        if (call(11, (u32) "/FDNEW.TXT", 1, 0, 0)) {
            return 12;
        }
    }
    print("FD checks: PASS\n");
    return 0;
#elif COMMAND == 8
    (void)argc;
    (void)argv;
    print("spin: Ctrl+C to stop\n");
    for (;;) {
        __asm__ volatile("" ::: "memory");
    }
#elif COMMAND == 9
    (void)argc;
    (void)argv;
    char buffer[1024];
    u32 offset = 0;
    int count;
    while ((count = call(17, offset, (u32)buffer, sizeof(buffer), 0)) > 0) {
        call(1, (u32)buffer, count, 0, 0);
        offset += count;
    }
    return count < 0;
#elif COMMAND == 1
    for (int i = 1; i < argc; ++i) {
        if (i > 1) {
            print(" ");
        }
        print(argv[i]);
    }
    print("\n");
    return 0;
#elif COMMAND == 2
    if (argc > 2) {
        print("Usage: cat [file]\n");
        return 2;
    }
    char buffer[512];
    int fd = argc == 2 ? open(argv[1], KOS_O_READ) : 0;
    if (fd < 0) {
        return 1;
    }
    int n;
    while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
        if (write_all(1, buffer, n) < 0) {
            if (fd) {
                close(fd);
            }
            return 1;
        }
    }
    if (fd) {
        close(fd);
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
