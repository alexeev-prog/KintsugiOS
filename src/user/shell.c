#include "fsabi.h"
#include "runtime.h"
#include "shellabi.h"
#include "authabi.h"
#include "../shared/text.h"

static char line[256], history[16][256], draft[256];
static unsigned length, cursor, rendered, histories, position;
static int status;
static char prompt_mark = '$';
static int script_argc, script_depth;
static char** script_argv;
static int execute(char* text);
static void print(const char* s);
static void copy(char* d, const char* s);

static unsigned current_uid(void) {
    return (unsigned)syscall(KOS_SYS_GETUID, 0, 0, 0, 0);
}

static const char* current_user(void) {
    static char name[32];
    name[0] = 0;
    if (syscall(KOS_SYS_GETUSER, (u32)name, sizeof(name), 0, 0)) copy(name, "user");
    return name;
}

static void read_secret(char* buffer, unsigned capacity) {
    unsigned n = 0;
    buffer[0] = 0;
    for (;;) {
        int key = syscall(8, 1, 0, 0, 0);
        if (key == '\n' || key == '\r') break;
        if (key == 3) { n = 0; break; }
        if (key == 8) {
            if (n) {
                --n;
                write_all(1, "\b \b", 3);
            }
            continue;
        }
        if (key >= 32 && key < 127 && n + 1 < capacity) {
            buffer[n++] = (char)key;
            write_all(1, "*", 1);
        }
    }
    buffer[n] = 0;
    write_all(1, "\n", 1);
}

static void prompt(void) {
    char cwd[256];
    if (syscall(4, (u32)cwd, sizeof(cwd), 0, 0)) {
        copy(cwd, "?");
    }
    print("\n[ ");
    print(current_user());
    print("@kintsugi ");
    print(cwd);
    print(" ]\n");
    { char mark[2] = { prompt_mark, 0 }; print(mark); }
    print(" ");
}

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
        copy(out, "/bin/HIERO.ELF");
        return;
    }
    if (equal(name, "sh")) {
        copy(out, "/bin/SHELL.ELF");
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
    copy(out, "/bin/");
    copy(out + 5, name);
    copy(out + len(out), ".ELF");
}

typedef struct {
    char* args[8][32];
    char names[8][256];
} command_frame;

static int execute_inner(char* text, command_frame* frame);

static int find_unquoted(char* text, const char* token) {
    char quote = 0;
    u32 width = len(token);
    for (u32 i = 0; text[i]; ++i) {
        if ((text[i] == '\'' || text[i] == '"')) {
            if (!quote) quote = text[i]; else if (quote == text[i]) quote = 0;
        }
        int same = !quote;
        for (u32 j = 0; same && j < width; ++j) if (text[i + j] != token[j]) same = 0;
        if (same) return (int)i;
    }
    return -1;
}

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

/* ---- shell control flow ------------------------------------------------- */

static char* skip_ws(char* p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

static int starts_word(char* text, const char* word) {
    text = skip_ws(text);
    unsigned n = 0;
    while (word[n]) ++n;
    for (unsigned i = 0; i < n; ++i) if (text[i] != word[i]) return 0;
    char after = text[n];
    return after == 0 || after == ' ' || after == '\t' || after == ';';
}

/* Splits a command line on top-level semicolons, honouring quotes. */
static int split_semi(char* text, char** parts, int capacity) {
    int count = 0;
    char quote = 0;
    parts[count++] = skip_ws(text);
    for (char* p = text; *p && count < capacity; ++p) {
        if (*p == '\\' && quote != '\'' && p[1]) { ++p; continue; }
        if (*p == '\'' || *p == '"') {
            if (!quote) quote = *p; else if (quote == *p) quote = 0;
            continue;
        }
        if (!quote && *p == ';') {
            *p = 0;
            parts[count++] = skip_ws(p + 1);
        }
    }
    return count;
}

static void join_parts(char** parts, int from, int to, char* out, u32 capacity) {
    u32 used = 0;
    out[0] = 0;
    for (int i = from; i < to; ++i) {
        if (!parts[i] || !*parts[i]) continue;
        if (used) { if (used + 1 >= capacity) break; out[used++] = ' '; out[used] = 0; }
        const char* p = parts[i];
        while (*p && used + 1 < capacity) out[used++] = *p++;
        out[used] = 0;
    }
}

static int decimal(const char* text) {
    int value = 0, negative = 0;
    if (*text == '-') { negative = 1; ++text; }
    if (!*text) return 0;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return 0;
        value = value * 10 + (*p - '0');
    }
    return negative ? -value : value;
}

static int exec_control(char* text) {
    char* parts[64];
    int count = split_semi(text, parts, 64);
    if (!count) return 0;

    if (starts_word(parts[0], "if")) {
        int then_at = -1, else_at = -1, fi_at = -1;
        for (int i = 1; i < count; ++i) {
            if (starts_word(parts[i], "then")) then_at = i;
            else if (starts_word(parts[i], "else")) else_at = i;
            else if (starts_word(parts[i], "fi")) fi_at = i;
        }
        if (then_at < 0 || fi_at < 0 || fi_at <= then_at) return 2;
        char condition[512];
        join_parts(parts, 1, then_at, condition, sizeof(condition));
        int status = execute(condition);
        int from = status == 0 ? then_at + 1 : (else_at >= 0 ? else_at + 1 : fi_at);
        int to = status == 0 ? (else_at >= 0 ? else_at : fi_at) : fi_at;
        char branch[1024];
        join_parts(parts, from, to, branch, sizeof(branch));
        if (!*branch) return status;
        return execute(branch);
    }

    if (starts_word(parts[0], "while")) {
        int do_at = -1, done_at = -1;
        for (int i = 1; i < count; ++i) {
            if (starts_word(parts[i], "do")) do_at = i;
            else if (starts_word(parts[i], "done")) done_at = i;
        }
        if (do_at < 0 || done_at < 0 || done_at <= do_at) return 2;
        char condition[512], body[1024];
        join_parts(parts, 1, do_at, condition, sizeof(condition));
        join_parts(parts, do_at + 1, done_at, body, sizeof(body));
        int status = 1;
        for (int guard = 0; guard < 1000; ++guard) {
            if (execute(condition) != 0) break;
            status = execute(body);
        }
        return status;
    }

    if (starts_word(parts[0], "for")) {
        int in_at = -1, do_at = -1, done_at = -1;
        for (int i = 1; i < count; ++i) {
            if (starts_word(parts[i], "in")) in_at = i;
            else if (starts_word(parts[i], "do")) do_at = i;
            else if (starts_word(parts[i], "done")) done_at = i;
        }
        if (in_at < 0 || do_at < 0 || done_at < 0 || do_at <= in_at || done_at <= do_at) return 2;
        char variable[32] = "";
        char* head = skip_ws(parts[0] + 3);
        u32 n = 0;
        while (*head && *head != ' ' && *head != '\t' && n + 1 < sizeof(variable)) variable[n++] = *head++;
        variable[n] = 0;
        if (!variable[0]) return 2;
        char values[512], body[1024];
        join_parts(parts, in_at + 1, do_at, values, sizeof(values));
        join_parts(parts, do_at + 1, done_at, body, sizeof(body));
        int status = 0;
        char* cursor = values;
        while (*cursor) {
            cursor = skip_ws(cursor);
            if (!*cursor) break;
            char* start = cursor;
            while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
            char saved = *cursor;
            *cursor = 0;
            setenv(variable, start);
            status = execute(body);
            if (!saved) break;
            ++cursor;
        }
        return status;
    }
    return -1; /* not a control-flow statement */
}

static int execute_inner(char* text, command_frame* frame) {
    if (starts_word(text, "if") || starts_word(text, "for") || starts_word(text, "while")) {
        int result = exec_control(text);
        if (result >= 0) return result;
    }
    char quote = 0;
    int background = 0;
    /* Command lists and a single parenthesized group are intentionally parsed
     * before pipelines. This keeps each subcommand on the same shell context. */
    int conditional = find_unquoted(text, "||");
    if (conditional >= 0) {
        text[conditional] = 0; text[conditional + 1] = 0;
        int result = execute_raw(text);
        return result ? execute_raw(text + conditional + 2) : result;
    }
    conditional = find_unquoted(text, "&&");
    if (conditional >= 0) {
        text[conditional] = 0; text[conditional + 1] = 0;
        int result = execute_raw(text);
        return result ? result : execute_raw(text + conditional + 2);
    }
    for (char* p = text; *p; ++p) {
        if (*p == '\\' && quote != '\'' && p[1]) { ++p; continue; }
        if (*p == '\'' || *p == '"') {
            if (!quote) quote = *p; else if (quote == *p) quote = 0;
            continue;
        }
        if (!quote && *p == ';') {
            *p = 0;
            int result = execute_raw(text);
            char* next = p + 1;
            while (*next == ' ' || *next == '\t') ++next;
            return *next ? execute_raw(next) : result;
        }
    }
    char* begin = text;
    while (*begin == ' ' || *begin == '\t') ++begin;
    unsigned length = len(begin);
    if (length >= 2 && begin[0] == '(' && begin[length - 1] == ')') {
        begin[length - 1] = 0;
        return execute_raw(begin + 1);
    }
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
        if ((*p == '|' || *p == '&') && p[1] == *p) {
            char operator = *p;
            char* left = text;
            while (*left == ' ' || *left == '\t') {
                ++left;
            }
            char* right = p + 2;
            while (*right == ' ' || *right == '\t') {
                ++right;
            }
            if (left == p || !*right) {
                print("Syntax error: missing command around operator\n");
                return 2;
            }
            *p = 0;
            int result = execute_raw(text);
            return operator == '|' ? (result ? execute_raw(p + 2) : result)
                                   : (result ? result : execute_raw(p + 2));
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
            request.paths[0] = "/bin/SHELL.ELF";
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
            request.paths[0] = "/bin/SHELL.ELF";
            request.argv[0] = script_args;
            request.argc[0] = argc + 1;
            int result = syscall(KOS_SYS_LAUNCH, (u32)&request, 0, 0, 0);
            return result < 0 ? 126 : result;
        }
        if (equal(argv[0], "true")) return 0;
        if (equal(argv[0], "false")) return 1;
        if (equal(argv[0], "test") || equal(argv[0], "[")) {
            int start = equal(argv[0], "[") ? 1 : 0;
            int end = argc;
            if (start && end > start && equal(argv[end - 1], "]")) --end;
            int items = end - start;
            if (items <= 0) return 1;
            char** v = argv + start;
            if (items == 1) return *v[0] ? 0 : 1;
            if (items == 2) {
                if (equal(v[0], "-n")) return *v[1] ? 0 : 1;
                if (equal(v[0], "-z")) return *v[1] ? 1 : 0;
                if (v[0][0] == '-' && (v[0][1] == 'e' || v[0][1] == 'f' || v[0][1] == 'd')) {
                    kos_stat status;
                    int result = syscall(11, (u32)v[1], 4, (u32)&status, 0);
                    if (result) return 1;
                    if (v[0][1] == 'f') return status.directory ? 1 : 0;
                    if (v[0][1] == 'd') return status.directory ? 0 : 1;
                    return 0;
                }
                return 2;
            }
            if (items == 3) {
                if (equal(v[1], "=")) return equal(v[0], v[2]) ? 0 : 1;
                if (equal(v[1], "!=")) return equal(v[0], v[2]) ? 1 : 0;
                int a = decimal(v[0]), b = decimal(v[2]);
                if (equal(v[1], "-eq")) return a == b ? 0 : 1;
                if (equal(v[1], "-ne")) return a != b ? 0 : 1;
                if (equal(v[1], "-lt")) return a < b ? 0 : 1;
                if (equal(v[1], "-le")) return a <= b ? 0 : 1;
                if (equal(v[1], "-gt")) return a > b ? 0 : 1;
                if (equal(v[1], "-ge")) return a >= b ? 0 : 1;
            }
            return 2;
        }
        if (equal(argv[0], "help")) {
            print("Keramika Shell Help\n");
            if (argc == 2 && equal(argv[1], "bin")) {
                print("Installed /BIN entries:\n");
                kos_dirent entry;
                u32 cursor_bin = 0;
                while (syscall(3, (u32)"/bin", (u32)&cursor_bin, (u32)&entry, 0) > 0) {
                    if (!entry.stat.directory) { print("  "); print(entry.name); print("\n"); }
                }
            } else if (argc == 2 && equal(argv[1], "fs")) {
                print("write <file> <text>       Replace file contents\n");
                print("read <file>               Print file contents\n");
                print("mkdir <dir>               Create directory\n");
                print("rm <path>                 Remove file or directory\n");
                print("copy <src> <dst>          Copy a file\n");
                print("move <src> <dst>          Move or rename a file\n");
                print("cat <file>                Show file contents\n");
                print("cd <dir>                  Change directory\n");
            } else {
                print("Commands: cd pwd echo help source sh env export unset exit true false test [\n");
                print("Identity: whoami id sugo [shell|<command>]\n");
                print("Users:   useradd userpswd rootpswd userchange (root)\n");
                print("Files:   ls cat write read mkdir rm copy move\n");
                print("Tools:   tree find grep tail wc sort kfck proc posixinfo kill df du\n");
                print("Coreutils: basename dirname printf date uname sleep touch head seq\n");
                print("           printenv cut tr nl rev tac uniq tee yes expr factor\n");
                print("           cksum od strings cmp realpath truncate mktemp df du\n");
                print("Perms:   chmod <mode> <path>  chown <user|uid> <path>\n");
                print("Apps:    ktui kiwi kc kuned kcsv kservices ksersys klisten kfetch\n");
                print("         krrawl kcalc kman kset kpaint kinfo kterm ktmux\n");
                print("Control: if COND; then CMD; else CMD; fi | for V in A B; do CMD; done\n");
                print("         while COND; do CMD; done | CMD && CMD | CMD || CMD | CMD; CMD\n");
                print("Launch: name [args] or /bin/NAME.ELF [args]\n");
            }
            return 0;
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
        if (equal(argv[0], "whoami")) {
            print(current_user());
            print("\n");
            return 0;
        }
        if (equal(argv[0], "id")) {
            print("uid=");
            number(current_uid());
            print(" gid=");
            number((unsigned)syscall(KOS_SYS_GETGID, 0, 0, 0, 0));
            print(" user=");
            print(current_user());
            print("\n");
            return 0;
        }
        if (equal(argv[0], "sugo")) {
            if (current_uid() != 0) {
                char password[64];
                print("Password for root: ");
                read_secret(password, sizeof(password));
                if (syscall(KOS_SYS_ELEVATE, (u32)"root", (u32)password, 0, 0)) {
                    print("sugo: authentication failed\n");
                    return 1;
                }
            }
            /* Kernel builtins are not ELFs. Ask the kernel to run this one
             * with the elevation that was just armed; 127 means "no such
             * builtin", so fall through to the normal /BIN lookup. */
            if (argc > 1) {
                int builtin = syscall(KOS_SYS_BUILTIN, 3, argc - 1, (u32)(argv + 1), 0);
                if (builtin != 127) {
                    if (builtin < 0) {
                        print("sugo: ");
                        print(kos_error(builtin));
                        print("\n");
                    }
                    return builtin;
                }
            }
            char target[256];
            request.count = 1;
            if (argc == 1 || equal(argv[1], "shell") || equal(argv[1], "sh")) {
                copy(target, "/bin/SHELL.ELF");
                args[0][0] = "shell";
                args[0][1] = 0;
                request.argc[0] = 1;
            } else {
                path(target, argv[1]);
                request.argc[0] = argc - 1;
                for (int i = 1; i < argc; ++i) args[0][i - 1] = argv[i];
                args[0][argc - 1] = 0;
            }
            request.paths[0] = target;
            request.argv[0] = args[0];
            int result = syscall(KOS_SYS_LAUNCH, (u32)&request, 0, 0, 0);
            if (result < 0) {
                syscall(KOS_SYS_CANCEL_ELEVATE, 0, 0, 0, 0);
                print("sugo: launch failed\n");
                return 126;
            }
            return result;
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
            if (result == KOS_EPERM) {
                print("permission denied (root only) - use 'sugo <command>'\n");
            } else if (result == 127) {
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
        copy(directory, "/bin");
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
    unsigned start = 0;
    while (text_columns(line + start, cursor - start) > 73) start = text_next(line, start);
    unsigned finish = start, visible = 0;
    while (finish < length && visible < 74) { finish = text_next(line, finish); ++visible; }
    /* The editing row must keep exactly the same mark as the printed prompt:
     * '$' for a user, '#' for root. It used to flip to the legacy '!#>'. */
    char frame[512];
    frame[0] = '\r'; frame[1] = prompt_mark; frame[2] = ' ';
    unsigned n = 3;
    for (unsigned i = start; i < finish; ++i) {
        frame[n++] = line[i];
    }
    for (unsigned i = visible; i < rendered; ++i) {
        frame[n++] = ' ';
    }
    unsigned end = visible > rendered ? visible : rendered;
    for (unsigned i = text_columns(line + start, cursor - start); i < end; ++i) {
        frame[n++] = '\b';
    }
    write_all(1, frame, n);
    rendered = visible;
}

int main(int argc, char** argv) {
    if (!getenv("PATH")) {
        setenv("PATH", "/bin:/usr/bin");
    }
    unsigned uid = current_uid();
    prompt_mark = uid == 0 ? '#' : '$';
    char account[32];
    account[0] = 0;
    if (syscall(KOS_SYS_GETUSER, (u32)account, sizeof(account), 0, 0)) copy(account, uid ? "user" : "root");
    if (uid == 0) {
        setenv("USER", "root");
        setenv("LOGNAME", "root");
        setenv("HOME", "/root");
    } else if (!getenv("USER")) {
        setenv("USER", account);
        setenv("LOGNAME", account);
    }
    if (!getenv("HOME")) {
        setenv("HOME", uid ? "/home/user" : "/root");
    }
    if (!getenv("TERM")) {
        setenv("TERM", "kintsugi");
    }
    if (argc == 1) {
        const char* home = getenv("HOME");
        if (home) syscall(11, (u32)home, 3, 0, 0);
        int profile = open("/etc/PROFILE", KOS_O_READ);
        if (profile >= 0) {
            close(profile);
            run_script("/etc/PROFILE", 0, (char**)0);
        }
    }
    if (argc > 1) {
        return run_script(argv[1], argc - 1, argv + 1);
    }
    print("\033[36m+-- KintsugiOS / Keramika Shell - userspace -------------------------------+\033[0m\n");
    print("  help: commands | Tab: complete | Ctrl+C: cancel | kiwi: desktop\n");
    prompt();
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
            prompt();
            continue;
        }
        if (key == 3) {
            print("^C");
            prompt();
            length = cursor = rendered = 0;
            line[0] = 0;
            continue;
        }
        char encoded[4];
        unsigned add = text_key(key, encoded);
        if (key == 258 && cursor) {
            cursor = text_prev(line, cursor);
        } else if (key == 259 && cursor < length) {
            cursor = text_next(line, cursor);
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
            unsigned end = key == 8 ? cursor : text_next(line, cursor);
            if (key == 8) {
                cursor = text_prev(line, cursor);
            }
            unsigned count = end - cursor;
            for (unsigned i = cursor; i <= length - count; ++i) {
                line[i] = line[i + count];
            }
            length -= count;
        } else if (add && length + add < 256) {
            for (unsigned i = length + 1; i > cursor; --i) {
                line[i + add - 1] = line[i - 1];
            }
            for (unsigned i = 0; i < add; ++i) line[cursor++] = encoded[i];
            length += add;
        }
        redraw();
    }
}
