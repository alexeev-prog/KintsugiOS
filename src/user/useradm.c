#include "runtime.h"
#include "authabi.h"

/* Account administration tools. One source is linked into four binaries:
 * USERADD, USERPSWD, ROOTPSWD and USERCHANGE. Authorization is enforced again
 * by the kernel; the checks here only produce friendly diagnostics. */

#ifndef USER_TOOL
#define USER_TOOL 1
#endif

#define TOOL_USERADD 1
#define TOOL_USERPSWD 2
#define TOOL_ROOTPSWD 3
#define TOOL_USERCHANGE 4

static void print(const char* text) {
    write_all(1, text, 0);
    u32 n = 0;
    while (text[n]) ++n;
    write_all(1, text, n);
}

static u32 text_length(const char* text) {
    u32 n = 0;
    while (text[n]) ++n;
    return n;
}

static int same(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static void copy(char* out, const char* text, u32 capacity) {
    u32 n = 0;
    if (!capacity) return;
    while (text[n] && n + 1 < capacity) { out[n] = text[n]; ++n; }
    out[n] = 0;
}

static void trim(char* text) {
    u32 n = text_length(text);
    while (n && (text[n - 1] == '\n' || text[n - 1] == '\r' || text[n - 1] == ' ')) text[--n] = 0;
}

static unsigned uid(void) {
    return (unsigned)syscall(KOS_SYS_GETUID, 0, 0, 0, 0);
}

static __attribute__((unused)) const char* account_name(void) {
    static char name[32];
    name[0] = 0;
    if (syscall(KOS_SYS_GETUSER, (u32)name, sizeof(name), 0, 0)) copy(name, "user", sizeof(name));
    return name;
}

static int read_secret(char* buffer, u32 capacity) {
    u32 n = 0;
    buffer[0] = 0;
    for (;;) {
        int key = syscall(8, 1, 0, 0, 0);
        if (key == '\n' || key == '\r') break;
        if (key == 3) return 0;
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
    return n > 0;
}

static __attribute__((unused)) int read_line(char* buffer, u32 capacity) {
    int n = read_stdin(buffer, capacity);
    if (n <= 0) return 0;
    if ((u32)n >= capacity) n = (int)capacity - 1;
    buffer[n] = 0;
    trim(buffer);
    return buffer[0] != 0;
}

static __attribute__((unused)) int ask_password(const char* label, char* out, u32 capacity) {
    char first[64], second[64];
    print(label);
    if (!read_secret(first, sizeof(first))) return 0;
    print("Repeat: ");
    if (!read_secret(second, sizeof(second))) return 0;
    if (!same(first, second)) {
        print("Passwords do not match\n");
        return 0;
    }
    copy(out, first, capacity);
    return out[0] != 0;
}

static __attribute__((unused)) int account_exists(const char* name) {
    char data[1024];
    int n = syscall(2, (u32)"/etc/PASSWD", 0, (u32)data, sizeof(data) - 1);
    if (n <= 0) return 0;
    data[n] = 0;
    char* line = data;
    while (*line) {
        char* end = line;
        while (*end && *end != '\n' && *end != '\r') ++end;
        char saved = *end;
        *end = 0;
        char* colon = line;
        while (*colon && *colon != ':') ++colon;
        if (*colon) *colon = 0;
        if (line[0] && same(line, name)) return 1;
        if (!saved) break;
        line = end + 1;
    }
    return 0;
}

static __attribute__((unused)) int root_only(const char* tool) {
    if (uid() == 0) return 0;
    print(tool);
    print(": permission denied (root only). Use: sugo ");
    print(tool);
    print(" ...\n");
    return 1;
}

static __attribute__((unused)) int tool_useradd(int argc, char** argv) {
    if (root_only("useradd")) return 1;
    char name[32];
    if (argc > 1) {
        copy(name, argv[1], sizeof(name));
    } else {
        print("New username: ");
        if (!read_line(name, sizeof(name))) return 1;
    }
    char password[64];
    if (!ask_password("New password: ", password, sizeof(password))) {
        print("useradd: a password is required\n");
        return 1;
    }
    int result = syscall(KOS_SYS_USERADD, (u32)name, (u32)password, 0, 0);
    if (result < 0) {
        print("useradd: cannot create account (invalid name, exists or no space)\n");
        return 1;
    }
    print("useradd: created ");
    print(name);
    print(" with home /home/");
    print(name);
    print("\n");
    return 0;
}

static __attribute__((unused)) int tool_userpswd(int argc, char** argv) {
    char target[32];
    if (argc > 1) copy(target, argv[1], sizeof(target));
    else copy(target, account_name(), sizeof(target));
    if (uid() != 0 && !same(target, account_name())) {
        print("userpswd: you may only change your own password\n");
        return 1;
    }
    if (!account_exists(target)) {
        print("userpswd: no such user\n");
        return 1;
    }
    char previous[64] = "";
    if (uid() != 0) {
        print("Current password: ");
        if (!read_secret(previous, sizeof(previous))) return 1;
    }
    char password[64];
    if (!ask_password("New password: ", password, sizeof(password))) {
        print("userpswd: empty password rejected\n");
        return 1;
    }
    int result = syscall(KOS_SYS_USERPASSWD, (u32)target, (u32)previous, (u32)password, 0);
    if (result) {
        print("userpswd: password change failed (wrong old password?)\n");
        return 1;
    }
    print("userpswd: password updated for ");
    print(target);
    print("\n");
    return 0;
}

static __attribute__((unused)) int tool_rootpswd(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (root_only("rootpswd")) return 1;
    char password[64];
    if (!ask_password("New root password: ", password, sizeof(password))) {
        print("rootpswd: empty password rejected\n");
        return 1;
    }
    int result = syscall(KOS_SYS_USERPASSWD, (u32)"root", (u32)"", (u32)password, 0);
    if (result) {
        print("rootpswd: failed\n");
        return 1;
    }
    print("rootpswd: root password updated\n");
    return 0;
}

static __attribute__((unused)) int tool_userchange(int argc, char** argv) {
    if (root_only("userchange")) return 1;
    char name[32];
    if (argc > 1) copy(name, argv[1], sizeof(name));
    else {
        print("Switch to user: ");
        if (!read_line(name, sizeof(name))) return 1;
    }
    if (!account_exists(name)) {
        print("userchange: no such user\n");
        return 1;
    }
    if (syscall(10, (u32)"/etc/LASTUSER", (u32)name, text_length(name), 0)) {
        print("userchange: cannot store the login preference\n");
        return 1;
    }
    sync();
    print("userchange: rebooting and logging in as ");
    print(name);
    print("\n");
    syscall(KOS_SYS_REBOOT, 0, 0, 0, 0);
    print("userchange: reboot failed\n");
    return 1;
}

int main(int argc, char** argv) {
#if USER_TOOL == TOOL_USERADD
    return tool_useradd(argc, argv);
#elif USER_TOOL == TOOL_USERPSWD
    return tool_userpswd(argc, argv);
#elif USER_TOOL == TOOL_ROOTPSWD
    return tool_rootpswd(argc, argv);
#elif USER_TOOL == TOOL_USERCHANGE
    return tool_userchange(argc, argv);
#else
    (void)argc;
    (void)argv;
    return 1;
#endif
}
