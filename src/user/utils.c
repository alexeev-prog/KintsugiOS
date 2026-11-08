#include "ktuilib.h"
#include "sysabi.h"

static void output(const char* s) { syscall(1, (u32)s, tui_len(s), 0, 0); }
static void number(u32 n) {
    char s[11]; u32 i = 10; s[i] = 0;
    do { s[--i] = '0' + n % 10; n /= 10; } while (n);
    output(s+i);
}
static int contains(const char* text, const char* query) {
    for (u32 i = 0;; ++i) {
        u32 j = 0;
        while (query[j] && text[i+j] && query[j] == text[i+j]) ++j;
        if (!query[j]) return 1;
        if (!text[i]) return 0;
    }
}
static int compare(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}
static int walk(const char* path, const char* query, u32 depth) {
    if (depth > 32) { output("Traversal depth limit exceeded\n"); return 1; }
    u32 cursor = 0; tui_entry entry; int result;
    while ((result = syscall(3, (u32)path, (u32)&cursor, (u32)&entry, 0)) > 0) {
        char child[256];
        if (tui_len(path) + tui_len(entry.name) + 2 > sizeof(child)) return 1;
        tui_path(child, path, entry.name);
#if UTIL == 1
        for (u32 i = 0; i < depth; ++i) output("  ");
        output(entry.name); output(entry.stat.directory ? "/\n" : "\n");
#else
        if (!query || contains(entry.name, query)) { output(child); output("\n"); }
#endif
        if (entry.stat.directory && walk(child, query, depth+1)) return 1;
    }
    return result < 0;
}

int main(int argc, char** argv) {
#if UTIL == 1 || UTIL == 2
    const char* path = argc > 1 ? argv[1] : "/";
    output(path); output("\n");
    return walk(path, argc > 2 ? argv[2] : 0, 0);
#elif UTIL == 7
    char data[1024]; u32 offset = 0; int n;
    while ((n = syscall(17, offset, (u32)data, sizeof(data), 0)) > 0) {
        syscall(1, (u32)data, n, 0, 0); offset += n;
    }
    return n < 0;
#elif UTIL == 8
    int result = syscall(18, 0, 0, 0, 0);
    output(result ? "kfsck: FAILED (unmounted, corrupt, I/O or memory error)\n"
                  : "kfsck: allocation chains, ownership and directories OK (read only)\n");
    return result ? 1 : 0;
#elif UTIL == 9
    kos_process_info list[16];
    int n = syscall(16, (u32)list, 16, 0, 0);
    output("PID PPID STATE TICKS HEAP_BYTES\n");
    const char* states[] = {"unused", "ready", "running", "waiting", "zombie"};
    for (int i = 0; i < n; ++i) {
        number(list[i].pid); output(" "); number(list[i].parent); output(" ");
        output(list[i].state < 5 ? states[list[i].state] : "?"); output(" ");
        number(list[i].ticks); output(" "); number(list[i].heap_bytes); output("\n");
    }
    return n < 0;
#else
    int index = UTIL == 3 ? 2 : 1;
    if (argc != index + 1) {
        output(UTIL == 3 ? "Usage: grep LITERAL FILE\n" : "Usage: tail|wc|sort FILE\n");
        return 2;
    }
    tui_stat stat;
    if (tui_stat_path(argv[index], &stat) || stat.directory || stat.size > 65536) {
        output("Cannot read file (maximum 64 KiB)\n"); return 1;
    }
    char* data = malloc(stat.size + 1);
    if (!data) return 1;
    u32 offset = 0;
    while (offset < stat.size) {
        u32 size = stat.size - offset; if (size > 4096) size = 4096;
        int n = syscall(2, (u32)argv[index], offset, (u32)(data+offset), size);
        if (n <= 0) { free(data); return 1; }
        offset += n;
    }
    data[stat.size] = 0;
#if UTIL == 4
    u32 start = stat.size, lines = 0;
    if (start && data[start-1] == '\n') --start;
    while (start) { --start; if (data[start] == '\n' && ++lines == 10) { ++start; break; } }
    for (u32 i = start; i < stat.size;) {
        u32 n = stat.size-i; if (n > 4096) n = 4096;
        syscall(1, (u32)(data+i), n, 0, 0); i += n;
    }
#elif UTIL == 5
    u32 lines = 0, words = 0; int word = 0;
    for (u32 i = 0; i < stat.size; ++i) {
        if (data[i] == '\n') ++lines;
        int in = (unsigned char)data[i] > 32;
        if (in && !word) ++words;
        word = in;
    }
    number(lines); output(" "); number(words); output(" "); number(stat.size); output("\n");
#else
    char** lines = malloc(4096 * sizeof(char*));
    if (!lines) { free(data); return 1; }
    u32 count = 0;
    for (u32 i = 0; i < stat.size;) {
        if (count == 4096) { output("Too many lines (limit 4096)\n"); free(lines); free(data); return 1; }
        lines[count++] = data+i;
        while (i < stat.size && data[i] != '\n') ++i;
        if (i < stat.size) data[i++] = 0;
    }
#if UTIL == 6
    for (u32 i = 1; i < count; ++i) {
        char* value = lines[i]; u32 j = i;
        while (j && compare(lines[j-1], value) > 0) { lines[j] = lines[j-1]; --j; }
        lines[j] = value;
    }
#endif
    int matched = 0;
    for (u32 i = 0; i < count; ++i) {
        if (UTIL != 3 || contains(lines[i], argv[1])) {
            /* Console writes are bounded to 4096 bytes. */
            u32 n = tui_len(lines[i]);
            for (u32 pos = 0; pos < n;) {
                u32 size = n-pos; if (size > 4096) size = 4096;
                syscall(1, (u32)(lines[i]+pos), size, 0, 0); pos += size;
            }
            output("\n"); matched = 1;
        }
    }
    free(lines);
    if (UTIL == 3 && !matched) { free(data); return 1; }
#endif
    free(data);
    return 0;
#endif
}
