#include "runtime.h"

static int same(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

int main(int argc, char** argv) {
    char* data = malloc(65536);
    if (!data) {
        return 1;
    }
    u32 used = 0;
    int n = 0;
    while (used < 65536 && (n = syscall(17, used, (u32)(data + used), 4096, 0)) > 0) {
        used += n;
    }
    int result = 0;
    if (argc == 3 && same(argv[1], "--save")) {
        result = syscall(10, (u32)argv[2], (u32)data, used, 0) ? 1 : 0;
    } else if (argc == 1) {
        result = write_all(1, data, used) < 0;
    } else {
        write_all(2, "journal [--save /BOOT.LOG]\n", 26);
        result = 2;
    }
    free(data);
    return result;
}
