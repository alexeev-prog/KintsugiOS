#include "fsabi.h"
#include "runtime.h"
#include "shellabi.h"

static char line[256], history[16][256], draft[256];
static unsigned length, cursor, rendered, histories, position;
static int status;
static int script_argc, script_depth;
static char** script_argv;
static int execute(char* text);
static void print(const char* s);

static int run_script(const char* file, int argc, char** argv) {
    if (script_depth == 8) {
        print("sh: nesting limit\n");
        return 2;
    }
    int fd = open(file, KOS_O_READ);
    if (fd < 0) {
        print("sh: cannot open script\n");
        return 1;
    }
    int old_argc = script_argc;
    char** old_argv = script_argv;
    script_argc = argc;
    script_argv = argv;
    ++script_depth;
    char command[256];
    unsigned used = 0;
    int overflow = 0, result = 0;
    char data[512];
    int count;
    while ((count = read(fd, data, sizeof(data))) > 0) {
        for (int i = 0; i < count; ++i) {
            char c = data[i];
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                if (overflow) {
                    print("sh: line too long\n");
                    result = 2;
                    goto done;
                }
                command[used] = 0;
                result = execute(command);
                status = result;
                used = 0;
            } else if (!c) {
                print("sh: binary script rejected\n");
                result = 2;
                goto done;
            } else if (used + 1 < sizeof(command)) {
                command[used++] = c;
            } else {
                overflow = 1;
            }
        }
    }
    if (count < 0) {
        result = 1;
    } else if (overflow) {
        print("sh: line too long\n");
        result = 2;
    } else if (used) {
        command[used] = 0;
        result = execute(command);
        status = result;
    }
done:
    close(fd);
    --script_depth;
    script_argc = old_argc;
    script_argv = old_argv;
    return result;
}

static unsigned len(const char* s) {
    unsigned n = 0;
    while (s[n]) {
        ++n;
    }
    return n;
}

static void copy(char* d, const char* s) {
    while ((*d++ = *s++)) {}
}

static int equal(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static void print(const char* s) {
    write_all(1, s, len(s));
}

static void number(unsigned n) {
    char b[11];
    unsigned i = 10;
    b[i] = 0;
    do {
        b[--i] = '0' + n % 10;
        n /= 10;
    } while (n);
    print(b + i);
}

static int parse(char* text, char** argv, int capacity) {
    char* r = text;
    char* w = text;
    int n = 0;
    while (*r) {
        while (*r == ' ' || *r == '\t') {
            ++r;
        }
        if (!*r) {
            break;
        }
        if (n == capacity) {
            return -1;
        }
        argv[n++] = w;
        char quote = 0;
        while (*r) {
            char c = *r++;
            if (c == '\\' && quote != '\'') {
                if (!*r) {
                    return -1;
                }
                *w++ = *r++;
            } else if (c == '\'' || c == '"') {
                if (!quote) {
                    quote = c;
                } else if (quote == c) {
                    quote = 0;
                } else {
                    *w++ = c;
                }
            } else if (!quote && (c == ' ' || c == '\t')) {
                break;
            } else {
                *w++ = c;
            }
        }
        if (quote) {
            return -1;
        }
        *w++ = 0;
    }
    return n;
}

static void path(char* out, const char* name) {
    if (equal(name, "hieroglyph")) {
        copy(out, "/BIN/HIERO.ELF");
        return;
    }
    if (equal(name, "sh")) {
        copy(out, "/BIN/SHELL.ELF");
        return;
    }
    for (const char* p = name; *p; ++p) {
        if (*p == '/') {
            copy(out, name);
            return;
        }
    }
    const char* search = getenv("PATH");
    if (search) {
        while (*search) {
            unsigned n = 0;
            while (search[n] && search[n] != ':') {
                ++n;
            }
            unsigned consumed = n;
            if (n && n + len(name) + 6 < 256) {
                for (unsigned i = 0; i < n; ++i) {
                    out[i] = search[i];
                }
                out[n++] = '/';
                copy(out + n, name);
                copy(out + n + len(name), ".ELF");
                kos_stat st;
                if (!syscall(11, (u32)out, 4, (u32)&st, 0) && !st.directory) {
                    return;
                }
            }
            search += consumed;
            if (*search == ':') {
                ++search;
            }
        }
    }
    copy(out, "/BIN/");
    copy(out + 5, name);
    copy(out + len(out), ".ELF");
}

typedef struct {
    char* args[8][32];
    char names[8][256];
} command_frame;

static int execute_inner(char* text, command_frame* frame);

static int execute_raw(char* text) {
    command_frame* frame = malloc(sizeof(*frame));
    if (!frame) {
        return 1;
    }
    int result = execute_inner(text, frame);
    free(frame);
    return result;
}

static int execute(char* text) {
    /* Expand into a bounded separate buffer. Values remain literal, never operators. */
    char* expanded = malloc(4096);
    if (!expanded) {
        return 1;
    }
    unsigned n = 0;
    char quote = 0;
    for (unsigned i = 0; text[i]; ++i) {
        char c = text[i];
        if (c == '#' && !quote && (!i || text[i - 1] == ' ' || text[i - 1] == '\t')) {
            break;
        }
        if (c == '\\' && quote != '\'' && text[i + 1]) {
            if (n + 2 >= 4096) {
                goto too_long;
            }
            expanded[n++] = c;
            expanded[n++] = text[++i];
            continue;
        }
        if ((c == '\'' || c == '"') && (!quote || quote == c)) {
            quote = quote ? 0 : c;
        }
        if (c == '$' && quote != '\'') {
            char name[32], number_buffer[12];
            unsigned j = 0;
            const char* value = "";
            if (text[i + 1] == '?' || text[i + 1] == '#' || (text[i + 1] >= '0' && text[i + 1] <= '9')) {
                c = text[++i];
                if (c >= '0' && c <= '9') {
                    value = c - '0' < script_argc ? script_argv[c - '0'] : "";
                } else {
                    unsigned v = c == '?' ? (unsigned)status : (script_argc ? (unsigned)script_argc - 1 : 0);
                    unsigned k = 11;
                    number_buffer[k] = 0;
                    do {
                        number_buffer[--k] = '0' + v % 10;
                        v /= 10;
                    } while (v);
                    value = number_buffer + k;
                }
            } else {
                int braces = text[i + 1] == '{';
                if (braces) {
                    ++i;
                }
                while ((text[i + 1] >= 'A' && text[i + 1] <= 'Z')
                       || (text[i + 1] >= 'a' && text[i + 1] <= 'z')
                       || (text[i + 1] >= '0' && text[i + 1] <= '9') || text[i + 1] == '_') {
                    if (j + 1 == sizeof(name)) {
                        goto too_long;
                    }
                    name[j++] = text[++i];
                }
                if (braces && text[++i] != '}') {
                    goto too_long;
                }
                if (!j) {
                    if (n + 1 >= 4096) {
                        goto too_long;
                    }
                    expanded[n++] = '$';
                    continue;
                }
                name[j] = 0;
                value = getenv(name);
                if (!value) {
                    value = "";
                }
            }
            if (!quote) {
                if (n + 1 >= 4096) {
                    goto too_long;
                }
                expanded[n++] = '"';
            }
            while (*value) {
                if (n + 3 >= 4096) {
                    goto too_long;
                }
                if (*value == '"' || *value == '\\') {
                    expanded[n++] = '\\';
                }
                expanded[n++] = *value++;
            }
            if (!quote) {
                expanded[n++] = '"';
            }
        } else {
            if (n + 1 >= 4096) {
                goto too_long;
            }
            expanded[n++] = c;
        }
    }
    expanded[n] = 0;
    int result = execute_raw(expanded);
    free(expanded);
    return result;
too_long:
    free(expanded);
    print("sh: invalid expansion or command too long\n");
    return 2;
}

static int execute_inner(char* text, command_frame* frame) {
    char quote = 0;
    int background = 0;
    for (char* p = text; *p; ++p) {
        if (*p == '\\' && quote != '\'') {
            if (p[1]) {
                ++p;
            }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            if (!quote) {
                quote = *p;
            } else if (quote == *p) {
                quote = 0;
            }
            continue;
        }
        if (quote) {
            continue;
        }
        if (*p == '|' && p[1] == '|') {
            char* left = text;
            while (*left == ' ' || *left == '\t') {
                ++left;
            }
            char* right = p + 2;
            while (*right == ' ' || *right == '\t') {
                ++right;
            }
            if (left == p || !*right) {
                print("Syntax error: missing command around ||\n");
                return 2;
            }
            *p = 0;
            int result = execute_raw(text);
            return result ? execute_raw(p + 2) : result;
        }
        if (*p == '&') {
            char* tail = p + 1;
            while (*tail == ' ' || *tail == '\t') {
                ++tail;
            }
            if (*tail) {
                print("Syntax error: trailing & required\n");
                return 2;
            }
            *p = 0;
            background = 1;
            break;
        }
        if (*p == '<') {
            print("Unsupported shell operator\n");
            return 2;
        }
    }
    if (quote) {
        print("Syntax error: quotes\n");
        return 2;
    }
    kos_launch request = { 0 };
    request.count = 1;
    request.background = background;
    char* stages[8] = { text };
    char* output = 0;
    quote = 0;
    for (char* p = text; *p; ++p) {
        if (*p == '\\' && quote != '\'') {
            if (p[1]) {
                ++p;
            }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            if (!quote) {
                quote = *p;
            } else if (quote == *p) {
                quote = 0;
            }
            continue;
        }
        if (quote) {
            continue;
        }
        if (*p == '|') {
            if (request.count == 8 || output) {
                return 2;
            }
            *p = 0;
            stages[request.count++] = p + 1;
        } else if (*p == '>') {
            if (output) {
                return 2;
            }
            *p = 0;
            if (p[1] == '>') {
                request.append = 1;
                *++p = 0;
            }
            output = p + 1;
        }
    }
    char*(*args)[32] = frame->args;
    char (*names)[256] = frame->names;
    for (unsigned i = 0; i < request.count; ++i) {
        int argc = parse(stages[i], args[i], 32);
        if (argc < 1) {
            return argc < 0 || request.count > 1 || background ? 2 : status;
        }
        request.argc[i] = argc;
        request.argv[i] = args[i];
        if (len(args[i][0]) > 240) {
            return 2;
        }
        path(names[i], args[i][0]);
        request.paths[i] = names[i];
    }
    if (output) {
        char* target[1];
        if (parse(output, target, 1) != 1) {
            return 2;
        }
        request.output = target[0];
    }
    if (request.count == 1 && !output && !background) {
        int argc = request.argc[0];
        char** argv = args[0];
        if (equal(argv[0], "source") || equal(argv[0], ".")) {
            return argc >= 2 ? run_script(argv[1], argc - 1, argv + 1) : 2;
        }
        if (equal(argv[0], "sh")) {
            if (argc < 2) {
                return 2;
            }
            request.paths[0] = "/BIN/SHELL.ELF";
            int result = syscall(KOS_SYS_LAUNCH, (u32)&request, 0, 0, 0);
            return result < 0 ? 126 : result;
        }
        unsigned command_length = len(argv[0]);
        if (command_length > 3
            && (equal(argv[0] + command_length - 3, ".sh") || equal(argv[0] + command_length - 3, ".SH"))) {
            char* script_args[33];
            script_args[0] = "sh";
            for (int i = 0; i < argc; ++i) {
                script_args[i + 1] = argv[i];
            }
            request.paths[0] = "/BIN/SHELL.ELF";
            request.argv[0] = script_args;
            request.argc[0] = argc + 1;
            int result = syscall(KOS_SYS_LAUNCH, (u32)&request, 0, 0, 0);
            return result < 0 ? 126 : result;
        }
        if (equal(argv[0], "env")) {
            kos_env_entry* entries = malloc(KOS_ENV_MAX * sizeof(kos_env_entry));
            if (!entries) {
                return 1;
            }
            if (syscall(KOS_SYS_ENV_GET, (u32)entries, 0, 0, 0)) {
                free(entries);
                return 1;
            }
            for (int i = 0; i < KOS_ENV_MAX; ++i) {
                if (*entries[i].name) {
                    print(entries[i].name);
                    print("=");
                    print(entries[i].value);
                    print("\n");
                }
            }
            free(entries);
            return 0;
        }
        if (equal(argv[0], "export")) {
            if (argc != 2) {
                return 2;
            }
            char* value = argv[1];
            while (*value && *value != '=') {
                ++value;
            }
            if (!*value) {
                return 2;
            }
            *value++ = 0;
            return setenv(argv[1], value) ? 1 : 0;
        }
        if (equal(argv[0], "unset")) {
            return argc == 2 ? (unsetenv(argv[1]) ? 1 : 0) : 2;
        }
        if (equal(argv[0], "cd") && argc == 1) {
            const char* home = getenv("HOME");
            return syscall(11, (u32)(home ? home : "/"), 3, 0, 0) ? 1 : 0;
        }
        if (equal(args[0][0], "status")) {
            print("status=");
            number(status);
            print("\n");
            return 0;
        }
        if (equal(args[0][0], "exit")) {
            unsigned code = status;
            if (argc > 2) {
                return 2;
            }
            if (argc == 2) {
                code = 0;
                for (char* p = argv[1]; *p; ++p) {
                    if (*p < '0' || *p > '9') {
                        return 2;
                    }
                    code = (code * 10 + *p - '0') & 255;
                }
            }
            syscall(0, code, 0, 0, 0);
        }
        if (equal(args[0][0], "jobs") || equal(args[0][0], "wait")) {
            return syscall(KOS_SYS_BUILTIN, equal(args[0][0], "jobs") ? 1 : 2, 0, 0, 0);
        }
        kos_stat st;
        if (equal(args[0][0], "fault") || syscall(11, (u32)names[0], 4, (u32)&st, 0)) {
            int result = syscall(KOS_SYS_BUILTIN, 0, request.argc[0], (u32)args[0], 0);
            if (result == 127) {
                print("Unknown command\n");
            }
            return result;
        }
    }
    int result = syscall(KOS_SYS_LAUNCH, (u32)&request, 0, 0, 0);
    if (result < 0) {
        print("exec: launch failed\n");
        return 126;
    }
    return result;
}

static char lower(char c) {
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static void complete(void) {
    unsigned start = cursor;
    while (start && line[start - 1] != ' ') {
        --start;
    }
    for (unsigned i = 0; i < cursor; ++i) {
        if (line[i] == '\'' || line[i] == '"' || line[i] == '\\') {
            return;
        }
    }
    char directory[256] = ".", common[256];
    unsigned matches = 0, common_length = 0;
    int command = start == 0;
    if (command) {
        copy(directory, "/BIN");
    }
    for (unsigned i = start; i < cursor; ++i) {
        if (line[i] == '/') {
            unsigned n = i - start + 1;
            for (unsigned j = 0; j < n; ++j) {
                directory[j] = line[start + j];
            }
            directory[n] = 0;
        }
    }
    for (unsigned i = start; i < cursor; ++i) {
        if (line[i] == '/') {
            start = i + 1;
            command = 0;
        }
    }
    unsigned prefix = cursor - start, offset = 0;
    kos_dirent entry;
    while (syscall(3, (u32)directory, (u32)&offset, (u32)&entry, 0) > 0) {
        char candidate[256];
        copy(candidate, entry.name);
        unsigned n = len(candidate);
        if (command) {
            if (n < 4 || !equal(candidate + n - 4, ".ELF")) {
                continue;
            }
            candidate[n - 4] = 0;
        }
        for (unsigned i = 0; candidate[i]; ++i) {
            candidate[i] = lower(candidate[i]);
        }
        unsigned i = 0;
        while (i < prefix && candidate[i] && candidate[i] == lower(line[start + i])) {
            ++i;
        }
        if (i != prefix) {
            continue;
        }
        if (!matches++) {
            copy(common, candidate);
            common_length = len(common);
        } else {
            i = 0;
            while (i < common_length && candidate[i] == common[i]) {
                ++i;
            }
            common_length = i;
        }
    }
    if (!matches || common_length < prefix || length + common_length - prefix >= 256) {
        return;
    }
    unsigned extra = common_length - prefix;
    for (unsigned i = length + 1; i > cursor; --i) {
        line[i + extra - 1] = line[i - 1];
    }
    for (unsigned i = 0; i < extra; ++i) {
        line[cursor + i] = common[prefix + i];
    }
    length += extra;
    cursor += extra;
}

static void redraw(void) {
    /* Keep the editing line on one physical row, including long commands. */
    unsigned start = cursor > 73 ? cursor - 73 : 0;
    unsigned visible = length - start;
    if (visible > 74) {
        visible = 74;
    }
    char frame[160] = "\r!#> ";
    unsigned n = 5;
    for (unsigned i = 0; i < visible; ++i) {
        frame[n++] = line[start + i];
    }
    for (unsigned i = visible; i < rendered; ++i) {
        frame[n++] = ' ';
    }
    unsigned end = visible > rendered ? visible : rendered;
    for (unsigned i = cursor - start; i < end; ++i) {
        frame[n++] = '\b';
    }
    write_all(1, frame, n);
    rendered = visible;
}

int main(int argc, char** argv) {
    if (!getenv("PATH")) {
        setenv("PATH", "/BIN");
    }
    if (!getenv("HOME")) {
        setenv("HOME", "/HOME");
    }
    if (!getenv("TERM")) {
        setenv("TERM", "kintsugi");
    }
    if (argc > 1) {
        return run_script(argv[1], argc - 1, argv + 1);
    }
    print("Keramika Shell - userspace - type help\n!#> ");
    syscall(KOS_SYS_SESSION, 1, 0, 0, 0);
    for (;;) {
        int key = syscall(8, 1, 0, 0, 0);
        if (key == '\n') {
            cursor = length;
            redraw();
            print("\n");
            if (length) {
                if (histories == 16) {
                    for (unsigned i = 1; i < 16; ++i) {
                        copy(history[i - 1], history[i]);
                    }
                    --histories;
                }
                copy(history[histories++], line);
                status = execute(line);
            }
            syscall(KOS_SYS_SESSION, 0, 0, 0, 0);
            length = cursor = rendered = 0;
            line[0] = 0;
            position = histories;
            print("\n!#> ");
            continue;
        }
        if (key == 3) {
            print("^C\n!#> ");
            length = cursor = rendered = 0;
            line[0] = 0;
            continue;
        }
        if (key == 258 && cursor) {
            --cursor;
        } else if (key == 259 && cursor < length) {
            ++cursor;
        } else if (key == 260) {
            cursor = 0;
        } else if (key == 261) {
            cursor = length;
        } else if (key == 256 || key == 257) {
            if (position == histories) {
                copy(draft, line);
            }
            if (key == 256 && position) {
                --position;
            }
            if (key == 257 && position < histories) {
                ++position;
            }
            copy(line, position == histories ? draft : history[position]);
            cursor = length = len(line);
        } else if (key == '\t') {
            complete();
        } else if ((key == 8 && cursor) || (key == 262 && cursor < length)) {
            if (key == 8) {
                --cursor;
            }
            for (unsigned i = cursor; i < length; ++i) {
                line[i] = line[i + 1];
            }
            --length;
        } else if (key >= 32 && key < 127 && length < 255) {
            for (unsigned i = length + 1; i > cursor; --i) {
                line[i] = line[i - 1];
            }
            line[cursor++] = key;
            ++length;
        }
        redraw();
    }
}
