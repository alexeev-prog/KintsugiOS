#include "runtime.h"
#include "fsabi.h"
#include "shellabi.h"

static char line[256], history[16][256], draft[256];
static unsigned length, cursor, rendered, histories, position;
static int status;
static unsigned len(const char* s) { unsigned n = 0; while (s[n]) ++n; return n; }
static void copy(char* d, const char* s) { while ((*d++ = *s++)) {} }
static int equal(const char* a, const char* b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static void print(const char* s) { write_all(1, s, len(s)); }
static void number(unsigned n) { char b[11]; unsigned i = 10; b[i] = 0; do { b[--i] = '0' + n % 10; n /= 10; } while (n); print(b + i); }
static int parse(char* text, char** argv, int capacity) {
    char* r = text; char* w = text; int n = 0;
    while (*r) {
        while (*r == ' ' || *r == '\t') ++r;
        if (!*r) break;
        if (n == capacity) return -1;
        argv[n++] = w; char quote = 0;
        while (*r) {
            char c = *r++;
            if (c == '\\' && quote != '\'') { if (!*r) return -1; *w++ = *r++; }
            else if (c == '\'' || c == '"') {
                if (!quote) quote = c; else if (quote == c) quote = 0; else *w++ = c;
            } else if (!quote && (c == ' ' || c == '\t')) break;
            else *w++ = c;
        }
        if (quote) return -1;
        *w++ = 0;
    }
    return n;
}
static void path(char* out, const char* name) {
    for (const char* p = name; *p; ++p) if (*p == '/') { copy(out, name); return; }
    copy(out, "/BIN/"); copy(out + 5, name); copy(out + len(out), ".ELF");
}
static int execute(char* text) {
    char quote = 0; int background = 0;
    for (char* p = text; *p; ++p) {
        if (*p == '\\' && quote != '\'') { if (p[1]) ++p; continue; }
        if (*p == '\'' || *p == '"') { if (!quote) quote = *p; else if (quote == *p) quote = 0; continue; }
        if (quote) continue;
        if (*p == '|' && p[1] == '|') {
            char* left = text; while (*left == ' ' || *left == '\t') ++left;
            char* right = p + 2; while (*right == ' ' || *right == '\t') ++right;
            if (left == p || !*right) { print("Syntax error: missing command around ||\n"); return 2; }
            *p = 0; int result = execute(text);
            return result ? execute(p + 2) : result;
        }
        if (*p == '&') {
            char* tail = p + 1; while (*tail == ' ' || *tail == '\t') ++tail;
            if (*tail) { print("Syntax error: trailing & required\n"); return 2; }
            *p = 0; background = 1; break;
        }
        if (*p == '<') { print("Unsupported shell operator\n"); return 2; }
    }
    if (quote) { print("Syntax error: quotes\n"); return 2; }
    kos_launch request = { 0 }; request.count = 1; request.background = background;
    char* stages[8] = { text }; char* output = 0; quote = 0;
    for (char* p = text; *p; ++p) {
        if (*p == '\\' && quote != '\'') { if (p[1]) ++p; continue; }
        if (*p == '\'' || *p == '"') { if (!quote) quote = *p; else if (quote == *p) quote = 0; continue; }
        if (quote) continue;
        if (*p == '|') { if (request.count == 8 || output) return 2; *p = 0; stages[request.count++] = p + 1; }
        else if (*p == '>') {
            if (output) return 2;
            *p = 0; if (p[1] == '>') { request.append = 1; *++p = 0; } output = p + 1;
        }
    }
    char* args[8][32]; char names[8][256];
    for (unsigned i = 0; i < request.count; ++i) {
        int argc = parse(stages[i], args[i], 32);
        if (argc < 1) return argc < 0 || request.count > 1 || background ? 2 : status;
        request.argc[i] = argc; request.argv[i] = args[i];
        if (len(args[i][0]) > 240) return 2;
        path(names[i], args[i][0]); request.paths[i] = names[i];
    }
    if (output) { char* target[1]; if (parse(output, target, 1) != 1) return 2; request.output = target[0]; }
    if (request.count == 1 && !output && !background) {
        if (equal(args[0][0], "status")) { print("status="); number(status); print("\n"); return 0; }
        if (equal(args[0][0], "exit")) syscall(0, 0, 0, 0, 0);
        if (equal(args[0][0], "jobs") || equal(args[0][0], "wait"))
            return syscall(KOS_SYS_BUILTIN, equal(args[0][0], "jobs") ? 1 : 2, 0, 0, 0);
        kos_stat st;
        if (equal(args[0][0], "fault") || syscall(11, (u32)names[0], 4, (u32)&st, 0)) {
            int result = syscall(KOS_SYS_BUILTIN, 0, request.argc[0], (u32)args[0], 0);
            if (result == 127) print("Unknown command\n");
            return result;
        }
    }
    int result = syscall(KOS_SYS_LAUNCH, (u32)&request, 0, 0, 0);
    if (result < 0) { print("exec: launch failed\n"); return 126; }
    return result;
}
static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static void complete(void) {
    unsigned start = cursor;
    while (start && line[start - 1] != ' ') --start;
    for (unsigned i = 0; i < cursor; ++i) if (line[i] == '\'' || line[i] == '"' || line[i] == '\\') return;
    char directory[256] = ".", common[256]; unsigned matches = 0, common_length = 0;
    int command = start == 0;
    if (command) copy(directory, "/BIN");
    for (unsigned i = start; i < cursor; ++i) if (line[i] == '/') {
        unsigned n = i - start + 1; for (unsigned j = 0; j < n; ++j) directory[j] = line[start + j];
        directory[n] = 0;
    }
    for (unsigned i = start; i < cursor; ++i) if (line[i] == '/') { start = i + 1; command = 0; }
    unsigned prefix = cursor - start, offset = 0; kos_dirent entry;
    while (syscall(3, (u32)directory, (u32)&offset, (u32)&entry, 0) > 0) {
        char candidate[256]; copy(candidate, entry.name); unsigned n = len(candidate);
        if (command) { if (n < 4 || !equal(candidate + n - 4, ".ELF")) continue; candidate[n - 4] = 0; }
        for (unsigned i = 0; candidate[i]; ++i) candidate[i] = lower(candidate[i]);
        unsigned i = 0; while (i < prefix && candidate[i] && candidate[i] == lower(line[start + i])) ++i;
        if (i != prefix) continue;
        if (!matches++) { copy(common, candidate); common_length = len(common); }
        else { i = 0; while (i < common_length && candidate[i] == common[i]) ++i; common_length = i; }
    }
    if (!matches || common_length < prefix || length + common_length - prefix >= 256) return;
    unsigned extra = common_length - prefix;
    for (unsigned i = length + 1; i > cursor; --i) line[i + extra - 1] = line[i - 1];
    for (unsigned i = 0; i < extra; ++i) line[cursor + i] = common[prefix + i];
    length += extra; cursor += extra;
}
static void redraw(void) {
    print("\r!#> "); print(line);
    for (unsigned i = length; i < rendered; ++i) print(" ");
    unsigned end = length > rendered ? length : rendered;
    for (unsigned i = cursor; i < end; ++i) print("\b");
    rendered = length;
}
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    print("Keramika Shell - userspace - type help\n!#> ");
    syscall(KOS_SYS_SESSION, 1, 0, 0, 0);
    for (;;) {
        int key = syscall(8, 1, 0, 0, 0);
        if (key == '\n') {
            cursor = length; redraw(); print("\n");
            if (length) {
                if (histories == 16) { for (unsigned i = 1; i < 16; ++i) copy(history[i - 1], history[i]); --histories; }
                copy(history[histories++], line); status = execute(line);
            }
            syscall(KOS_SYS_SESSION, 0, 0, 0, 0);
            length = cursor = rendered = 0; line[0] = 0; position = histories; print("\n!#> "); continue;
        }
        if (key == 3) { print("^C\n!#> "); length = cursor = rendered = 0; line[0] = 0; continue; }
        if (key == 258 && cursor) --cursor;
        else if (key == 259 && cursor < length) ++cursor;
        else if (key == 260) cursor = 0;
        else if (key == 261) cursor = length;
        else if (key == 256 || key == 257) {
            if (position == histories) copy(draft, line);
            if (key == 256 && position) --position;
            if (key == 257 && position < histories) ++position;
            copy(line, position == histories ? draft : history[position]); cursor = length = len(line);
        } else if (key == '\t') complete();
        else if ((key == 8 && cursor) || (key == 262 && cursor < length)) {
            if (key == 8) --cursor;
            for (unsigned i = cursor; i < length; ++i) line[i] = line[i + 1];
            --length;
        } else if (key >= 32 && key < 127 && length < 255) {
            for (unsigned i = length + 1; i > cursor; --i) line[i] = line[i - 1];
            line[cursor++] = key; ++length;
        }
        redraw();
    }
}
