/* Freestanding user programs: no kernel headers or direct device access. */
#include "desktopabi.h"
#include "fsabi.h"
#include "posix.h"
#include "runtime.h"

typedef kos_stat stat_t;
typedef kos_dirent dirent_t;

static int call(u32 number, u32 a, u32 b, u32 c, u32 d) {
    return syscall(number, a, b, c, d);
}

static int call_decimal(const char* text) {
    int value = 0, negative = 0;
    if (*text == '-') {
        negative = 1;
        ++text;
    }
    if (!*text) {
        return 0;
    }
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
        value = value * 10 + (*p - '0');
    }
    return negative ? -value : value;
}

static int equal_name(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
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

static int name_compare(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a >= 'a' && *a <= 'z' ? *a - 32 : *a;
        char cb = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
        if (ca != cb) {
            return (unsigned char)ca - (unsigned char)cb;
        }
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int read_all(const char* path, char* buffer, u32 capacity) {
    int fd = open(path, KOS_O_READ);
    if (fd < 0) {
        return -1;
    }
    u32 used = 0;
    int count;
    while (used + 1 < capacity && (count = read(fd, buffer + used, capacity - used - 1)) > 0) {
        used += (u32)count;
    }
    close(fd);
    buffer[used] = 0;
    return (int)used;
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
    if (wait(0x7fffffff) != KOS_ECHILD || kill(1) != KOS_EPERM || kill(0x7fffffff) != KOS_ESRCH) {
        return 6;
    }
    for (int i = 0; i < 8; ++i) {
        char* args[] = { "SPIN", 0 };
        int pid = spawn("/bin/SPIN.ELF", 1, args);
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
    if (argc > 3 || (argc == 3 && (argv[1][0] != '-' || argv[1][1] != 'l' || argv[1][2]))) {
        print("Usage: ls [-l] [path]\n");
        return 2;
    }
    int long_names = argc > 1 && argv[1][0] == '-';
    const char* directory = argc == 3 ? argv[2] : (long_names ? "." : argc == 2 ? argv[1] : ".");
    u32 cursor = 0;
    /* The user stack is deliberately small.  A long dirent is 264 bytes;
     * keeping 64 of them on the stack used to overwrite the return path and
     * made the garbage appear after otherwise valid names. */
    static kos_long_dirent entries[64];
    for (u32 clear = 0; clear < 64; ++clear) {
        entries[clear].name[0] = 0;
        entries[clear].stat = (stat_t){ 0, 0, 0 };
    }
    u32 count = 0, max_name = 1, max_size = 1;
    int n;
    while (count < 64
           && (n = call(KOS_SYS_READDIR_LONG, (u32)directory, (u32)&cursor, (u32)&entries[count], 0)) > 0) {
        entries[count].name[KOS_LONG_NAME_MAX - 1] = 0;
        u32 name = length(entries[count].name), size = entries[count].stat.size;
        if (entries[count].stat.directory) {
            ++name;
        }
        if (name > max_name) {
            max_name = name;
        }
        u32 digits = 1;
        while (size >= 10) {
            size /= 10;
            ++digits;
        }
        if (digits > max_size) {
            max_size = digits;
        }
        ++count;
    }
    if (n < 0) {
        print("ls: filesystem unavailable or invalid directory\n");
        return 1;
    }
    for (u32 i = 1; i < count; ++i) {
        kos_long_dirent value = entries[i];
        u32 j = i;
        while (j && name_compare(entries[j - 1].name, value.name) > 0) {
            entries[j] = entries[j - 1];
            --j;
        }
        entries[j] = value;
    }
    if (long_names) {
        for (u32 i = 0; i < count; ++i) {
            print(entries[i].name);
            if (entries[i].stat.directory) {
                print("/");
            }
            print("\n");
        }
        return 0;
    }
    /* Unix-style grid: names only, with directories marked by a slash. */
    u32 column_width = max_name + 2;
    u32 columns = column_width ? 80 / column_width : 1;
    if (!columns) {
        columns = 1;
    }
    for (u32 i = 0; i < count; ++i) {
        if (entries[i].stat.directory) {
            print("\033[34m");
        } else {
            u32 name_length = length(entries[i].name);
            if (name_length > 4 && entries[i].name[name_length - 4] == '.'
                && entries[i].name[name_length - 3] == 'E' && entries[i].name[name_length - 2] == 'L'
                && entries[i].name[name_length - 1] == 'F') {
                print("\033[32m");
            } else {
                print("\033[37m");
            }
        }
        print(entries[i].name);
        if (entries[i].stat.directory) {
            print("/");
        }
        print("\033[0m");
        if ((i + 1) % columns) {
            u32 used = length(entries[i].name) + (entries[i].stat.directory ? 1 : 0);
            for (u32 j = used; j < column_width; ++j) {
                print(" ");
            }
        } else {
            print("\n");
        }
    }
    if (count && count % columns) {
        print("\n");
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
#elif COMMAND == 11
    if (argc < 2 || argc > 3) {
        print("Usage: volumectl up|down|set <0..100>\n");
        return 2;
    }
    u32 info[3] = { 0 };
    syscall(KOS_SYS_SOUND_INFO, (u32)info, 0, 0, 0);
    u32 value = info[0];
    if (argv[1][0] == 'u' && argv[1][1] == 'p' && !argv[1][2]) {
        value = value + 10 > 100 ? 100 : value + 10;
    } else if (argv[1][0] == 'd' && argv[1][1] == 'o' && !argv[1][2]) {
        value = value < 10 ? 0 : value - 10;
    } else if (argv[1][0] == 's' && argv[1][1] == 'e' && argv[1][2] == 't' && argc == 3) {
        value = 0;
        for (u32 i = 0; argv[2][i]; ++i) {
            if (argv[2][i] < '0' || argv[2][i] > '9') {
                return 2;
            }
            unsigned digit = (unsigned)(argv[2][i] - '0');
            if (value > (100u - digit) / 10u) {
                return 2;
            }
            value = value * 10 + digit;
        }
    } else {
        print("Usage: volumectl up|down|set <0..100>\n");
        return 2;
    }
    syscall(KOS_SYS_SOUND_VOLUME, value, 0, 0, 0);
    print("Volume: ");
    number(value);
    print("%\n");
    return 0;
#elif COMMAND == 12
    /* posixinfo - small demonstration of the POSIX-facing wrappers. */
    char cwd[256];
    print("pid=");
    number((u32)posix_getpid());
    print("\n");
    print("uid=");
    number(getuid());
    print(" gid=");
    number(getgid());
    print("\n");
    if (!getcwd(cwd, sizeof(cwd))) {
        print("cwd=");
        print(cwd);
        print("\n");
    }
    if (argc == 2) {
        kos_stat info;
        if (!stat_posix(argv[1], &info)) {
            print("stat=");
            print(argv[1]);
            print(info.directory ? " <dir>" : " <file>");
            print(" bytes=");
            number(info.size);
            print("\n");
        } else {
            print("stat: not found\n");
        }
    }
    return 0;
#elif COMMAND == 13
    /* kill - send a cooperative POSIX signal to a process. */
    if (argc < 2 || argc > 3) {
        print("Usage: kill <pid> [signal]\n");
        return 2;
    }
    u32 pid = 0, signal = 0;
    for (u32 i = 0; argv[1][i]; ++i) {
        if (argv[1][i] < '0' || argv[1][i] > '9') {
            return 2;
        }
        pid = pid * 10 + (u32)(argv[1][i] - '0');
    }
    if (argc == 3) {
        if (argv[2][0] == 'K' && argv[2][1] == 'I' && argv[2][2] == 'L' && argv[2][3] == 'L') {
            signal = SIGKILL;
        } else if (argv[2][0] == 'T' && argv[2][1] == 'E' && argv[2][2] == 'R' && argv[2][3] == 'M') {
            signal = SIGTERM;
        } else {
            for (u32 i = 0; argv[2][i]; ++i) {
                if (argv[2][i] < '0' || argv[2][i] > '9') {
                    return 2;
                }
                signal = signal * 10 + (u32)(argv[2][i] - '0');
            }
        }
    }
    int result = kill_sig((int)pid, (int)signal);
    if (result < 0) {
        print("kill: ");
        print(kos_error(result));
        print("\n");
        return 1;
    }
    return 0;
#elif COMMAND == 14
    /* chmod OCTAL_MODE path - stored as POSIX metadata in /etc/fsmeta. */
    if (argc != 3) {
        print("Usage: chmod <octal mode> <path>\n");
        return 2;
    }
    u32 mode = 0;
    for (u32 i = 0; argv[1][i]; ++i) {
        if (argv[1][i] < '0' || argv[1][i] > '7') {
            return 2;
        }
        mode = mode * 8 + (u32)(argv[1][i] - '0');
    }
    if (mode > 0777) {
        return 2;
    }
    int result = syscall(KOS_SYS_CHMOD, (u32)argv[2], mode, 0, 0);
    if (result < 0) {
        print("chmod: ");
        print(kos_error(result));
        print("\n");
        return 1;
    }
    return 0;
#elif COMMAND == 15
    /* chown USER|UID path - only root may change ownership. */
    if (argc != 3) {
        print("Usage: chown <user|uid> <path>\n");
        return 2;
    }
    u32 uid = 0;
    int numeric = 1;
    for (u32 i = 0; argv[1][i]; ++i) {
        if (argv[1][i] < '0' || argv[1][i] > '9') {
            numeric = 0;
        }
    }
    if (numeric) {
        for (u32 i = 0; argv[1][i]; ++i) {
            uid = uid * 10 + (u32)(argv[1][i] - '0');
        }
    } else {
        char data[1024];
        int size = syscall(2, (u32) "/etc/passwd", 0, (u32)data, sizeof(data) - 1);
        if (size <= 0) {
            print("chown: cannot read /etc/passwd\n");
            return 1;
        }
        data[size] = 0;
        char* line = data;
        int found = 0;
        while (*line && !found) {
            char* end = line;
            while (*end && *end != '\n' && *end != '\r') {
                ++end;
            }
            char saved = *end;
            *end = 0;
            char* colon = line;
            while (*colon && *colon != ':') {
                ++colon;
            }
            if (*colon) {
                *colon = 0;
                int same = 1;
                for (u32 i = 0; argv[1][i] || line[i]; ++i) {
                    if (argv[1][i] != line[i]) {
                        same = 0;
                        break;
                    }
                }
                if (same) {
                    char* digits = colon + 1;
                    while (*digits >= '0' && *digits <= '9') {
                        uid = uid * 10 + (u32)(*digits++ - '0');
                    }
                    found = 1;
                }
            }
            if (!saved) {
                break;
            }
            line = end + 1;
        }
        if (!found) {
            print("chown: unknown user\n");
            return 1;
        }
    }
    int result = syscall(KOS_SYS_CHOWN, (u32)argv[2], uid, 0, 0);
    if (result < 0) {
        print("chown: ");
        print(kos_error(result));
        print("\n");
        return 1;
    }
    return 0;
#elif COMMAND == 16
    /* basename PATH - last path component. */
    if (argc != 2) {
        print("Usage: basename <path>\n");
        return 2;
    }
    u32 n = length(argv[1]);
    while (n && argv[1][n - 1] == '/') {
        --n;
    }
    u32 start = n;
    while (start && argv[1][start - 1] != '/') {
        --start;
    }
    for (u32 i = start; i < n; ++i) {
        write_all(1, argv[1] + i, 1);
    }
    print("\n");
    return 0;
#elif COMMAND == 17
    /* dirname PATH - everything before the last component. */
    if (argc != 2) {
        print("Usage: dirname <path>\n");
        return 2;
    }
    u32 n = length(argv[1]);
    while (n > 1 && argv[1][n - 1] == '/') {
        --n;
    }
    u32 slash = n;
    while (slash > 1 && argv[1][slash - 1] != '/') {
        --slash;
    }
    if (!slash) {
        print(".\n");
        return 0;
    }
    u32 end = slash > 1 ? slash - 1 : 1;
    for (u32 i = 0; i < end; ++i) {
        write_all(1, argv[1] + i, 1);
    }
    print("\n");
    return 0;
#elif COMMAND == 18
    /* printf FORMAT [args...] - escapes and %s/%d substitution. */
    if (argc < 2) {
        print("Usage: printf <format> [args]\n");
        return 2;
    }
    u32 arg = 2;
    for (u32 i = 0; argv[1][i]; ++i) {
        if (argv[1][i] == '\\' && argv[1][i + 1]) {
            char escape = argv[1][++i];
            char out = escape == 'n' ? '\n' : escape == 't' ? '\t' : escape == 'r' ? '\r' : escape;
            write_all(1, &out, 1);
        } else if (argv[1][i] == '%' && argv[1][i + 1]) {
            char spec = argv[1][++i];
            if (spec == 's' && arg < (u32)argc) {
                print(argv[arg++]);
            } else if (spec == 'd' && arg < (u32)argc) {
                number((u32)call_decimal(argv[arg++]));
            } else if (spec == '%') {
                write_all(1, "%", 1);
            } else {
                write_all(1, "%", 1);
                write_all(1, &spec, 1);
            }
        } else {
            write_all(1, argv[1] + i, 1);
        }
    }
    return 0;
#elif COMMAND == 19
    /* date - uptime-based clock while no CMOS clock driver is present. */
    {
        kos_desktop_info info = { 0 };
        syscall(KOS_SYS_DESKTOP, (u32)&info, 0, 0, 0);
        u32 seconds = info.uptime;
        print("KintsugiOS uptime ");
        number(seconds / 86400);
        print("d ");
        number((seconds / 3600) % 24);
        print("h ");
        number((seconds / 60) % 60);
        print("m ");
        number(seconds % 60);
        print("s\n");
    }
    return 0;
#elif COMMAND == 20
    print("KintsugiOS 0.7.0a i386 (POSIX-style userspace)\n");
    return 0;
#elif COMMAND == 21
    if (argc != 2) {
        print("Usage: sleep <seconds>\n");
        return 2;
    }
    u32 value = 0;
    for (u32 i = 0; argv[1][i]; ++i) {
        if (argv[1][i] < '0' || argv[1][i] > '9') {
            return 2;
        }
        value = value * 10 + (u32)(argv[1][i] - '0');
    }
    syscall(KOS_SYS_SLEEP, value * 1000, 0, 0, 0);
    return 0;
#elif COMMAND == 22
    /* touch - create an empty file if it does not exist. */
    if (argc != 2) {
        print("Usage: touch <path>\n");
        return 2;
    }
    int fd = open(argv[1], KOS_O_WRITE | KOS_O_CREATE);
    if (fd < 0) {
        print("touch: cannot create file\n");
        return 1;
    }
    close(fd);
    return 0;
#elif COMMAND == 23
    /* head [-N] [FILE] - first N lines. */
    {
        u32 lines = 10, index = 1;
        if (argc > 1 && argv[1][0] == '-') {
            lines = 0;
            for (u32 i = 1; argv[1][i]; ++i) {
                if (argv[1][i] < '0' || argv[1][i] > '9') {
                    return 2;
                }
                lines = lines * 10 + (u32)(argv[1][i] - '0');
            }
            index = 2;
        }
        if ((u32)argc > index + 1) {
            print("Usage: head [-N] [FILE]\n");
            return 2;
        }
        int fd = (u32)argc == index + 1 ? open(argv[index], KOS_O_READ) : 0;
        if (fd < 0) {
            print("head: cannot open file\n");
            return 1;
        }
        char buffer[512];
        u32 shown = 0;
        int count;
        while (shown < lines && (count = read(fd, buffer, sizeof(buffer))) > 0) {
            for (int i = 0; i < count && shown < lines; ++i) {
                write_all(1, buffer + i, 1);
                if (buffer[i] == '\n') {
                    ++shown;
                }
            }
        }
        if (fd) {
            close(fd);
        }
        return 0;
    }
#elif COMMAND == 24
    /* seq START END - inclusive integer sequence. */
    if (argc != 3) {
        print("Usage: seq <start> <end>\n");
        return 2;
    }
    u32 first = 0, last = 0;
    for (u32 i = 0; argv[1][i]; ++i) {
        if (argv[1][i] < '0' || argv[1][i] > '9') {
            return 2;
        }
        first = first * 10 + (u32)(argv[1][i] - '0');
    }
    for (u32 i = 0; argv[2][i]; ++i) {
        if (argv[2][i] < '0' || argv[2][i] > '9') {
            return 2;
        }
        last = last * 10 + (u32)(argv[2][i] - '0');
    }
    for (u32 i = first; i <= last; ++i) {
        number(i);
        print("\n");
        if (i == 0xffffffffu) {
            break;
        }
    }
    return 0;
#elif COMMAND == 25
    /* printenv [NAME] - environment inspection. */
    kos_env_entry* entries = malloc(KOS_ENV_MAX * sizeof(kos_env_entry));
    if (!entries) {
        return 1;
    }
    if (syscall(KOS_SYS_ENV_GET, (u32)entries, 0, 0, 0)) {
        free(entries);
        return 1;
    }
    for (int i = 0; i < KOS_ENV_MAX; ++i) {
        if (!*entries[i].name) {
            continue;
        }
        if (argc == 1 || equal_name(entries[i].name, argv[1])) {
            print(entries[i].name);
            print("=");
            print(entries[i].value);
            print("\n");
        }
    }
    free(entries);
    return 0;
#elif COMMAND == 26
    /* du PATH - direct child sizes of a directory. */
    if (argc != 2) {
        print("Usage: du <directory>\n");
        return 2;
    }
    u32 cursor = 0;
    kos_dirent entry;
    u32 total = 0;
    while (syscall(3, (u32)argv[1], (u32)&cursor, (u32)&entry, 0) > 0) {
        if (!entry.stat.directory) {
            total += entry.stat.size;
        }
    }
    number(total);
    print("\t");
    print(argv[1]);
    print("\n");
    return 0;
#elif COMMAND == 27
    /* cut -d DELIM -f N FILE - single-field text cutting. */
    {
        char delimiter = ' ';
        u32 field = 1, index = 1;
        while (index < (u32)argc && argv[index][0] == '-') {
            if (argv[index][1] == 'd' && argv[index][2]) {
                delimiter = argv[index][2];
            } else if (argv[index][1] == 'f' && argv[index][2]) {
                field = 0;
                for (u32 i = 2; argv[index][i]; ++i) {
                    if (argv[index][i] < '0' || argv[index][i] > '9') {
                        return 2;
                    }
                    field = field * 10 + (u32)(argv[index][i] - '0');
                }
            } else {
                return 2;
            }
            if (argv[index][3]) {
                return 2;
            }
            ++index;
        }
        if (index >= (u32)argc) {
            return 2;
        }
        char* data = malloc(65537);
        if (!data) {
            return 1;
        }
        int size = read_all(argv[index], data, 65537);
        if (size < 0) {
            free(data);
            return 1;
        }
        u32 current = 1;
        for (int i = 0; i < size; ++i) {
            if (data[i] == delimiter) {
                ++current;
                continue;
            }
            if (data[i] == '\n' || data[i] == '\r') {
                current = 1;
                write_all(1, "\n", 1);
                continue;
            }
            if (current == field) {
                write_all(1, data + i, 1);
            }
        }
        free(data);
        return 0;
    }
#elif COMMAND == 28
    /* tr FROM TO - character translation on stdin. */
    if (argc != 3) {
        print("Usage: tr FROM TO\n");
        return 2;
    }
    {
        char data[4096];
        int count;
        while ((count = read(0, data, sizeof(data))) > 0) {
            for (int i = 0; i < count; ++i) {
                const char* from = argv[1];
                while (*from && *from != data[i]) {
                    ++from;
                }
                if (*from) {
                    u32 at = (u32)(from - argv[1]);
                    write_all(1, argv[2] + (at < length(argv[2]) ? at : length(argv[2]) - 1), 1);
                } else {
                    write_all(1, data + i, 1);
                }
            }
        }
    }
    return 0;
#elif COMMAND == 29
    /* nl FILE - number lines. */
    if (argc != 2) {
        print("Usage: nl <file>\n");
        return 2;
    }
    {
        char* data = malloc(65537);
        if (!data) {
            return 1;
        }
        int size = read_all(argv[1], data, 65537);
        if (size < 0) {
            free(data);
            return 1;
        }
        u32 line = 1;
        for (int i = 0; i < size; ++i) {
            if (i == 0 || data[i - 1] == '\n') {
                number(line++);
                print("\t");
            }
            write_all(1, data + i, 1);
        }
        free(data);
    }
    return 0;
#elif COMMAND == 30
    /* rev FILE - reverse characters of every line. */
    if (argc != 2) {
        print("Usage: rev <file>\n");
        return 2;
    }
    {
        char* data = malloc(65537);
        if (!data) {
            return 1;
        }
        int size = read_all(argv[1], data, 65537);
        if (size < 0) {
            free(data);
            return 1;
        }
        int start = 0;
        for (int i = 0; i <= size; ++i) {
            if (i == size || data[i] == '\n') {
                for (int j = i - 1; j >= start; --j) {
                    write_all(1, data + j, 1);
                }
                if (i < size) {
                    write_all(1, "\n", 1);
                }
                start = i + 1;
            }
        }
        free(data);
    }
    return 0;
#elif COMMAND == 31
    /* tac FILE - reverse line order. */
    if (argc != 2) {
        print("Usage: tac <file>\n");
        return 2;
    }
    {
        char* data = malloc(65537);
        if (!data) {
            return 1;
        }
        int size = read_all(argv[1], data, 65537);
        if (size < 0) {
            free(data);
            return 1;
        }
        int end = size;
        while (end > 0) {
            int start = end - 1;
            while (start > 0 && data[start - 1] != '\n') {
                --start;
            }
            for (int i = start; i < end; ++i) {
                write_all(1, data + i, 1);
            }
            if (start > 0) {
                write_all(1, "\n", 1);
            }
            end = start > 0 ? start - 1 : 0;
        }
        free(data);
    }
    return 0;
#elif COMMAND == 32
    /* uniq FILE - drop adjacent duplicate lines. */
    if (argc != 2) {
        print("Usage: uniq <file>\n");
        return 2;
    }
    {
        char* data = malloc(65537);
        if (!data) {
            return 1;
        }
        int size = read_all(argv[1], data, 65537);
        if (size < 0) {
            free(data);
            return 1;
        }
        int start = 0;
        while (start < size) {
            int end = start;
            while (end < size && data[end] != '\n') {
                ++end;
            }
            for (int i = start; i <= end && i < size; ++i) {
                write_all(1, data + i, 1);
            }
            int next = end + 1;
            while (next < size) {
                int stop = next;
                while (stop < size && data[stop] != '\n') {
                    ++stop;
                }
                int same = stop - next == end - start;
                if (same) {
                    for (int i = 0; i < stop - next; ++i) {
                        if (data[next + i] != data[start + i]) {
                            same = 0;
                            break;
                        }
                    }
                }
                if (!same) {
                    break;
                }
                next = stop + 1;
            }
            start = next;
        }
        free(data);
    }
    return 0;
#elif COMMAND == 33
    /* tee FILE - copy stdin to stdout and a file. */
    if (argc != 2) {
        print("Usage: tee <file>\n");
        return 2;
    }
    {
        int fd = open(argv[1], KOS_O_WRITE | KOS_O_CREATE | KOS_O_TRUNC);
        if (fd < 0) {
            print("tee: cannot open file\n");
            return 1;
        }
        char data[512];
        int count;
        while ((count = read(0, data, sizeof(data))) > 0) {
            write_all(1, data, (u32)count);
            write(fd, data, (u32)count);
        }
        close(fd);
    }
    return 0;
#elif COMMAND == 34
    /* yes [TEXT] - repeated line. */
    {
        const char* line = argc > 1 ? argv[1] : "y";
        u32 length = 0;
        while (line[length]) {
            ++length;
        }
        for (;;) {
            write_all(1, line, length);
            write_all(1, "\n", 1);
        }
    }
    return 0;
#elif COMMAND == 35
    /* expr A OP B - tiny integer expression evaluator. */
    if (argc != 4) {
        print("Usage: expr <int> <+|-|*|/|%> <int>\n");
        return 2;
    }
    int a = call_decimal(argv[1]), b = call_decimal(argv[3]);
    int result = 0;
    if (equal_name(argv[2], "+")) {
        result = a + b;
    } else if (equal_name(argv[2], "-")) {
        result = a - b;
    } else if (equal_name(argv[2], "*")) {
        result = a * b;
    } else if (equal_name(argv[2], "/")) {
        if (!b) {
            return 2;
        }
        result = a / b;
    } else if (equal_name(argv[2], "%")) {
        if (!b) {
            return 2;
        }
        result = a % b;
    } else {
        return 2;
    }
    if (result < 0) {
        print("-");
        result = -result;
    }
    number((u32)result);
    print("\n");
    return 0;
#elif COMMAND == 36
    /* factor N - trial-division factors. */
    if (argc != 2) {
        print("Usage: factor <n>\n");
        return 2;
    }
    u32 value = 0;
    for (u32 i = 0; argv[1][i]; ++i) {
        if (argv[1][i] < '0' || argv[1][i] > '9') {
            return 2;
        }
        value = value * 10 + (u32)(argv[1][i] - '0');
    }
    if (!value) {
        return 2;
    }
    number(value);
    print(":");
    for (u32 d = 2; d * d <= value; ++d) {
        while (value % d == 0) {
            print(" ");
            number(d);
            value /= d;
        }
    }
    if (value > 1) {
        print(" ");
        number(value);
    }
    print("\n");
    return 0;
#elif COMMAND == 37
    /* cksum FILE - additive 32-bit checksum and byte count. */
    if (argc != 2) {
        print("Usage: cksum <file>\n");
        return 2;
    }
    {
        char* data = malloc(65537);
        if (!data) {
            return 1;
        }
        int size = read_all(argv[1], data, 65537);
        if (size < 0) {
            free(data);
            return 1;
        }
        u32 sum = 0;
        for (int i = 0; i < size; ++i) {
            sum += (unsigned char)data[i];
        }
        number(sum);
        print(" ");
        number((u32)size);
        print(" ");
        print(argv[1]);
        print("\n");
        free(data);
    }
    return 0;
#elif COMMAND == 38
    /* od FILE - 16-byte hex dump. */
    if (argc != 2) {
        print("Usage: od <file>\n");
        return 2;
    }
    {
        char* data = malloc(65537);
        if (!data) {
            return 1;
        }
        int size = read_all(argv[1], data, 65537);
        if (size < 0) {
            free(data);
            return 1;
        }
        static const char hex[] = "0123456789abcdef";
        for (int i = 0; i < size; i += 16) {
            number((u32)i);
            print("  ");
            for (int j = 0; j < 16; ++j) {
                if (i + j < size) {
                    write_all(1, &hex[(unsigned char)data[i + j] >> 4], 1);
                    write_all(1, &hex[(unsigned char)data[i + j] & 15], 1);
                } else {
                    print("  ");
                }
                write_all(1, " ", 1);
            }
            print("\n");
        }
        free(data);
    }
    return 0;
#elif COMMAND == 39
    /* strings FILE - printable ASCII runs of at least 4 characters. */
    if (argc != 2) {
        print("Usage: strings <file>\n");
        return 2;
    }
    {
        char* data = malloc(65537);
        if (!data) {
            return 1;
        }
        int size = read_all(argv[1], data, 65537);
        if (size < 0) {
            free(data);
            return 1;
        }
        int start = -1;
        for (int i = 0; i <= size; ++i) {
            unsigned char c = i < size ? (unsigned char)data[i] : 0;
            int printable = c >= 32 && c < 127;
            if (printable && start < 0) {
                start = i;
            }
            if (!printable && start >= 0) {
                if (i - start >= 4) {
                    write_all(1, data + start, (u32)(i - start));
                    write_all(1, "\n", 1);
                }
                start = -1;
            }
        }
        free(data);
    }
    return 0;
#elif COMMAND == 40
    /* cmp A B - byte comparison. */
    if (argc != 3) {
        print("Usage: cmp <a> <b>\n");
        return 2;
    }
    {
        char *a = malloc(65537), *b = malloc(65537);
        if (!a || !b) {
            free(a);
            free(b);
            return 1;
        }
        int sa = read_all(argv[1], a, 65537), sb = read_all(argv[2], b, 65537);
        if (sa < 0 || sb < 0) {
            free(a);
            free(b);
            return 1;
        }
        int status = 0;
        if (sa != sb) {
            status = 1;
        }
        for (int i = 0; i < sa && i < sb; ++i) {
            if (a[i] != b[i]) {
                status = 1;
                break;
            }
        }
        if (status) {
            print("files differ\n");
        }
        free(a);
        free(b);
        return status;
    }
#elif COMMAND == 41
    /* realpath PATH - absolute normalized path. */
    if (argc != 2) {
        print("Usage: realpath <path>\n");
        return 2;
    }
    {
        char out[256];
        char source[256];
        if (argv[1][0] == '/') {
            u32 n = 0;
            while (argv[1][n] && n + 1 < sizeof(source)) {
                source[n] = argv[1][n];
                ++n;
            }
            source[n] = 0;
        } else {
            if (syscall(4, (u32)source, sizeof(source), 0, 0)) {
                print("realpath: cwd unavailable\n");
                return 1;
            }
            u32 used = 0;
            while (source[used]) {
                ++used;
            }
            if (source[used - 1] != '/') {
                source[used++] = '/';
            }
            u32 n = 0;
            while (argv[1][n] && used + n + 1 < sizeof(source)) {
                source[used + n] = argv[1][n];
                ++n;
            }
            source[used + n] = 0;
        }
        syscall(2, (u32)source, 0, (u32)out, sizeof(out) - 1);
        print(source);
        print("\n");
    }
    return 0;
#elif COMMAND == 42
    /* truncate -s SIZE FILE. */
    if (argc != 4 || !equal_name(argv[1], "-s")) {
        print("Usage: truncate -s <size> <file>\n");
        return 2;
    }
    u32 size = 0;
    for (u32 i = 0; argv[2][i]; ++i) {
        if (argv[2][i] < '0' || argv[2][i] > '9') {
            return 2;
        }
        size = size * 10 + (u32)(argv[2][i] - '0');
    }
    {
        int fd = open(argv[3], KOS_O_WRITE | KOS_O_CREATE);
        if (fd < 0) {
            print("truncate: cannot open file\n");
            return 1;
        }
        int result = ftruncate(fd, size);
        close(fd);
        if (result < 0) {
            print("truncate: failed\n");
            return 1;
        }
    }
    return 0;
#elif COMMAND == 43
    /* mktemp TEMPLATE - replace trailing XXXXXX with a pid-derived suffix. */
    if (argc != 2) {
        print("Usage: mktemp <templateXXXXXX>\n");
        return 2;
    }
    {
        u32 n = length(argv[1]);
        if (n < 6) {
            return 2;
        }
        char name[256];
        u32 used = 0;
        while (used + 1 < n - 6 + 1 && used + 1 < sizeof(name)) {
            name[used] = argv[1][used];
            ++used;
        }
        u32 pid = (u32)syscall(15, 0, 0, 0, 0);
        static const char digits[] = "0123456789abcdef";
        for (int i = 5; i >= 0; --i) {
            name[used++] = digits[(pid >> (i * 4)) & 15];
            if (used + 1 >= sizeof(name)) {
                break;
            }
        }
        name[used] = 0;
        int fd = open(name, KOS_O_WRITE | KOS_O_CREATE | KOS_O_EXCL);
        if (fd < 0) {
            print("mktemp: cannot create file\n");
            return 1;
        }
        close(fd);
        print(name);
        print("\n");
    }
    return 0;
#elif COMMAND == 44
    kos_mouse mouse;
    syscall(KOS_SYS_MOUSE, (u32)&mouse, 0, 0, 0);
    print("mouse x=");
    number((u32)(mouse.x < 0 ? -mouse.x : mouse.x));
    if (mouse.x < 0) { /* negative printed as unsigned; harmless for test */
    }
    print(" y=");
    number((u32)(mouse.y < 0 ? -mouse.y : mouse.y));
    print(" buttons=");
    number(mouse.buttons);
    print(" wheel=");
    number((u32)(mouse.wheel < 0 ? -mouse.wheel : mouse.wheel));
    print("\n");
    return 0;

#endif
}
