#include "ktuilib.h"

static void copy(char* out, const char* in) {
    while ((*out++ = *in++)) {}
}

int main(int argc, char** argv) {
    char path[256];
    if (argc != 2) {
        write_all(1, "Usage: kman TOPIC\n", 18);
        return 2;
    }
    copy(path, "/MAN/");
    u32 n = tui_len(path);
    for (u32 i = 0; argv[1][i] && n + i + 5 < sizeof(path); ++i) {
        char c = argv[1][i];
        path[n + i] = c >= 'a' && c <= 'z' ? c - 32 : c;
    }
    n += tui_len(argv[1]);
    copy(path + n, ".MAN");
    char* args[] = { "PAGER", path, 0 };
    int pid = spawn("/BIN/PAGER.ELF", 2, args);
    return pid < 0 ? 1 : wait(pid);
}
