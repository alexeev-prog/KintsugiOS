#include "kfs3.h"

#include "../../user/ioabi.h"
#include "../drivers/terminal.h"
#include "../kklibc/kklibc.h"
#include "../mm/memory.h"
#include "../user/auth.h"
#include "../user/process.h"
#include "../user/vfs_meta.h"
#include "vfs.h"

static int active;

static char lower_ascii(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static int same_ci(const char* a, const char* b) {
    while (*a && lower_ascii(*a) == lower_ascii(*b)) {
        ++a;
        ++b;
    }
    return lower_ascii(*a) == lower_ascii(*b);
}

/* Prefix match on whole path components. */
static int prefix(const char* path, const char* root) {
    u32 n = strlen(root);
    return !strncmp(path, root, n) && (path[n] == '/' || !path[n]);
}

static int prefix_ci(const char* path, const char* root) {
    for (u32 i = 0; root[i]; ++i) {
        if (!path[i] || lower_ascii(path[i]) != lower_ascii(root[i])) {
            return 0;
        }
    }
    u32 n = strlen(root);
    return path[n] == '/' || !path[n];
}

void kfs3_init(void) {
    active = 1;
    vfs_stat_t stat;
    if (vfs_stat("/proc", &stat)) {
        vfs_mkdir("/proc");
    }
    if (vfs_stat("/root", &stat)) {
        vfs_mkdir("/root");
    }
    if (vfs_stat("/home", &stat)) {
        vfs_mkdir("/home");
    }
    for (int i = 0; i < auth_count(); ++i) {
        const auth_user_t* user = auth_at(i);
        if (user && user->uid != 0) {
            kfs3_ensure_home(user->name);
        }
    }
}

int kfs3_is_active(void) {
    return active;
}

/* POSIX paths are already lowercase on disk. Older images used uppercase
 * legacy directories; the backend matches names case-insensitively. */
int kfs3_translate(const char* in, char out[256]) {
    if (!in || !*in) {
        return VFS_EINVAL;
    }
    if (!active) {
        strcpy(out, in);
        return 0;
    }
    static const char* const from[] = { "/home", "/etc",  "/var",  "/bin",  "/tmp", "/usr",
                                        "/dev",  "/proc", "/root", "/boot", "/man", "/sbin" };
    static const char* const to[] = { "/home", "/etc",  "/var",  "/bin",  "/tmp", "/usr",
                                      "/dev",  "/proc", "/root", "/boot", "/man", "/bin" };
    for (u32 i = 0; i < sizeof(from) / sizeof(from[0]); ++i) {
        u32 n = strlen(from[i]);
        if (!strncmp(in, from[i], n) && (in[n] == '/' || !in[n])) {
            strcpy(out, to[i]);
            strncat(out, in + n, 255 - strlen(out));
            return 0;
        }
    }
    strcpy(out, in);
    return 0;
}

int kfs3_ensure_home(const char* user) {
    if (!user || !*user) {
        return VFS_EINVAL;
    }
    char path[256];
    strcpy(path, "/home/");
    strncat(path, user, sizeof(path) - strlen(path) - 1);
    vfs_stat_t stat;
    if (vfs_stat(path, &stat)) {
        return vfs_mkdir(path);
    }
    return 0;
}

/* Owner of a path in the KFS3 namespace. */
static u32 owner_of(const char* absolute) {
    if (prefix(absolute, "/root")) {
        return 0;
    }
    if (prefix(absolute, "/home")) {
        const char* name = absolute + 5;
        while (*name == '/') {
            ++name;
        }
        if (!*name) {
            return 0;
        }
        char user[AUTH_NAME_MAX];
        u32 n = 0;
        while (name[n] && name[n] != '/' && n + 1 < sizeof(user)) {
            user[n] = name[n];
            ++n;
        }
        user[n] = 0;
        const auth_user_t* account = auth_find(user);
        if (account) {
            return account->uid;
        }
    }
    return 0;
}

u32 kfs3_owner(const char* absolute) {
    return owner_of(absolute);
}

static u16 mode_of(const char* absolute, int writable) {
    if (prefix(absolute, "/tmp")) {
        return writable ? 0666 : 0777;
    }
    if (prefix(absolute, "/home")) {
        const char* name = absolute + 5;
        while (*name == '/') {
            ++name;
        }
        int nested = 0;
        for (const char* p = name; *p; ++p) {
            if (*p == '/') {
                nested = 1;
            }
        }
        if (!nested && !*name) {
            return writable ? 0755 : 0755;
        }
        return writable ? 0600 : 0700;
    }
    if (prefix(absolute, "/root")) {
        return writable ? 0600 : 0700;
    }
    if (prefix(absolute, "/etc") && same_ci(absolute, "/etc/shadow")) {
        return 0600;
    }
    if (prefix(absolute, "/proc")) {
        return writable ? 0400 : 0555;
    }
    if (prefix(absolute, "/dev")) {
        return writable ? 0666 : 0755;
    }
    if (auth_is_system_path(absolute)) {
        return writable ? 0644 : 0755;
    }
    return writable ? 0644 : 0755;
}

int kfs3_permit(const char* absolute, int write) {
    if (!active) {
        return 1;
    }
    u32 euid = process_euid();
    if (euid == 0) {
        return 1;
    }
    if (!absolute) {
        return 0;
    }
    /* Device endpoints with world access. */
    if (prefix_ci(absolute, "/dev/NULL") || prefix_ci(absolute, "/dev/ZERO")
        || prefix_ci(absolute, "/dev/RANDOM") || prefix_ci(absolute, "/dev/URANDOM")
        || prefix_ci(absolute, "/dev/TTY") || prefix_ci(absolute, "/dev/CONSOLE")) {
        return 1;
    }
    if (prefix(absolute, "/proc")) {
        return write ? 0 : 1;
    }
    if (prefix(absolute, "/etc") && same_ci(absolute, "/etc/shadow")) {
        return 0;
    }
    u32 owner = owner_of(absolute);
    u16 mode = mode_of(absolute, write);
    u32 meta_uid;
    u16 meta_mode;
    if (vfs_meta_get(absolute, &meta_uid, &meta_mode)) {
        owner = meta_uid;
        mode = meta_mode;
    }
    if (euid == owner) {
        return (mode & (write ? 0200 : 0400)) != 0;
    }
    return (mode & (write ? 0002 : 0004)) != 0;
}

/* ---- synthetic files ---------------------------------------------------- */

enum {
    PSEUDO_NONE = 0,
    PSEUDO_DEV,
    PSEUDO_DEV_NULL,
    PSEUDO_DEV_ZERO,
    PSEUDO_DEV_RANDOM,
    PSEUDO_DEV_URANDOM,
    PSEUDO_DEV_TTY,
    PSEUDO_PROC,
    PSEUDO_PROC_EVENTS,
    PSEUDO_PROC_FILE,
    PSEUDO_PROC_PID,
    PSEUDO_PROC_PID_FILE
};

typedef struct {
    int kind;
    u32 pid;
    int file;
} pseudo_path_t;

static int proc_file(const char* name) {
    if (same_ci(name, "status")) {
        return 1;
    }
    if (same_ci(name, "cmdline")) {
        return 2;
    }
    if (same_ci(name, "comm")) {
        return 3;
    }
    return 0;
}

static int parse_u32(const char* text, u32* value) {
    if (!text || !*text) {
        return 0;
    }
    u32 v = 0;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
        v = v * 10 + (u32)(*p - '0');
    }
    *value = v;
    return 1;
}

static pseudo_path_t classify(const char* path) {
    pseudo_path_t out = { PSEUDO_NONE, 0, 0 };
    if (!path) {
        return out;
    }
    if (same_ci(path, "/dev")) {
        out.kind = PSEUDO_DEV;
        return out;
    }
    if (prefix_ci(path, "/dev/NULL") && !path[9]) {
        out.kind = PSEUDO_DEV_NULL;
        return out;
    }
    if (prefix_ci(path, "/dev/ZERO") && !path[9]) {
        out.kind = PSEUDO_DEV_ZERO;
        return out;
    }
    if (prefix_ci(path, "/dev/RANDOM") && !path[11]) {
        out.kind = PSEUDO_DEV_RANDOM;
        return out;
    }
    if (prefix_ci(path, "/dev/URANDOM") && !path[12]) {
        out.kind = PSEUDO_DEV_URANDOM;
        return out;
    }
    if ((prefix_ci(path, "/dev/TTY") && !path[8]) || (prefix_ci(path, "/dev/CONSOLE") && !path[12])) {
        out.kind = PSEUDO_DEV_TTY;
        return out;
    }
    if (same_ci(path, "/proc")) {
        out.kind = PSEUDO_PROC;
        return out;
    }
    if (prefix_ci(path, "/proc")) {
        const char* rest = path + 5;
        while (*rest == '/') {
            ++rest;
        }
        if (!*rest) {
            out.kind = PSEUDO_PROC;
            return out;
        }
        char component[32];
        u32 n = 0;
        while (rest[n] && rest[n] != '/' && n + 1 < sizeof(component)) {
            component[n] = rest[n];
            ++n;
        }
        component[n] = 0;
        const char* tail = rest + n;
        while (*tail == '/') {
            ++tail;
        }
        if (same_ci(component, "events")) {
            if (!*tail) {
                out.kind = PSEUDO_PROC_EVENTS;
            }
            return out;
        }
        if (same_ci(component, "uptime") || same_ci(component, "meminfo") || same_ci(component, "version")
            || same_ci(component, "cpuinfo") || same_ci(component, "filesystems")
            || same_ci(component, "mounts")) {
            if (!*tail) {
                out.kind = PSEUDO_PROC_FILE;
                out.file = same_ci(component, "uptime") ? 1
                    : same_ci(component, "meminfo")     ? 2
                    : same_ci(component, "version")     ? 3
                    : same_ci(component, "cpuinfo")     ? 4
                    : same_ci(component, "filesystems") ? 5
                                                        : 6;
            }
            return out;
        }
        u32 pid = 0;
        if (same_ci(component, "self")) {
            pid = process_pid();
        } else if (!parse_u32(component, &pid)) {
            return out;
        }
        if (!pid && !same_ci(component, "self")) {
            return out;
        }
        if (!process_pid_exists(pid)) {
            return out;
        }
        if (!*tail) {
            out.kind = PSEUDO_PROC_PID;
            out.pid = pid;
            return out;
        }
        int file = proc_file(tail);
        if (file) {
            out.kind = PSEUDO_PROC_PID_FILE;
            out.pid = pid;
            out.file = file;
        }
    }
    return out;
}

static int is_dir(const pseudo_path_t* p) {
    return p->kind == PSEUDO_DEV || p->kind == PSEUDO_PROC || p->kind == PSEUDO_PROC_PID;
}

int kfs3_pseudo_stat(const char* absolute, kos_stat* stat) {
    if (!active) {
        return 0;
    }
    pseudo_path_t p = classify(absolute);
    if (p.kind == PSEUDO_NONE) {
        return 0;
    }
    stat->directory = is_dir(&p);
    stat->readonly = p.kind != PSEUDO_DEV_NULL && p.kind != PSEUDO_DEV_TTY;
    switch (p.kind) {
        case PSEUDO_PROC_PID_FILE:
            stat->size = p.file == 1 ? 256 : 96;
            break;
        case PSEUDO_PROC_EVENTS:
            stat->size = 1024;
            break;
        case PSEUDO_PROC_FILE:
            stat->size = p.file == 1 ? 32 : p.file == 2 ? 256 : p.file == 3 ? 48 : 256;
            break;
        default:
            stat->size = 0;
            break;
    }
    return 1;
}

static u32 append_text(char* out, u32 capacity, u32 used, const char* text) {
    while (*text && used + 1 < capacity) {
        out[used++] = *text++;
    }
    out[used] = 0;
    return used;
}

static u32 proc_pid_content(u32 pid, int file, char* out, u32 capacity) {
    if (file == 1) {
        char tmp[256];
        int n = process_proc_status(pid, tmp, sizeof(tmp));
        if (n <= 0) {
            return 0;
        }
        memcpy(out, tmp, (u32)n < capacity ? (u32)n : capacity);
        return (u32)n < capacity ? (u32)n : capacity;
    }
    if (file == 3) {
        char name[32];
        if (!process_proc_name(pid, name, sizeof(name))) {
            return 0;
        }
        u32 used = append_text(out, capacity, 0, name);
        return append_text(out, capacity, used, "\n");
    }
    char tmp[128];
    int n = process_proc_cmdline(pid, tmp, sizeof(tmp));
    if (n <= 0) {
        return 0;
    }
    memcpy(out, tmp, (u32)n < capacity ? (u32)n : capacity);
    return (u32)n < capacity ? (u32)n : capacity;
}

int kfs3_pseudo_read(const char* absolute, u32 offset, void* buffer, u32 capacity, int* handled) {
    *handled = 0;
    if (!active) {
        return 0;
    }
    pseudo_path_t p = classify(absolute);
    if (p.kind == PSEUDO_NONE) {
        return 0;
    }
    *handled = 1;
    char scratch[512];
    u32 length = 0;
    scratch[0] = 0;
    if (p.kind == PSEUDO_DEV_NULL) {
        return 0;
    }
    if (p.kind == PSEUDO_DEV_ZERO) {
        memset(buffer, 0, capacity);
        return (int)capacity;
    }
    if (p.kind == PSEUDO_DEV_RANDOM || p.kind == PSEUDO_DEV_URANDOM) {
        u8* bytes = buffer;
        u32 state = (u32)buffer ^ process_ticks() * 2654435761u;
        for (u32 i = 0; i < capacity; ++i) {
            state = state * 1664525u + 1013904223u;
            bytes[i] = (u8)(state >> 16);
        }
        return (int)capacity;
    }
    if (p.kind == PSEUDO_DEV_TTY) {
        return 0;
    }
    if (p.kind == PSEUDO_PROC_EVENTS) {
        length = (u32)vfs_events_read(scratch, sizeof(scratch) - 1);
        scratch[length] = 0;
    } else if (p.kind == PSEUDO_PROC_FILE) {
        if (p.file == 1) {
            u32 ticks = process_ticks();
            length = (u32)snprintf(scratch, sizeof(scratch), "%u.%02u\n", ticks / 50, (ticks % 50) * 2);
        } else if (p.file == 2) {
            memory_stats_t m = memory_stats();
            length = (u32)snprintf(
                scratch,
                sizeof(scratch),
                "MemTotal: %u kB\nMemFree: %u kB\nMemUsed: %u kB\n",
                m.usable_kib,
                m.free_pages * 4,
                m.allocated_pages * 4);
        } else if (p.file == 3) {
            length = (u32)snprintf(scratch, sizeof(scratch), "KintsugiOS KFS3 POSIX layer\n");
        } else if (p.file == 4) {
            length = (u32)snprintf(scratch, sizeof(scratch),
                                   "processor\t: 0\nvendor_id\t: KintsugiOS\ncpu family\t: i386\n"
                                   "model name\t: 32-bit protected mode\nbogomips\t: 50.00\n");
        } else if (p.file == 5) {
            length = (u32)snprintf(
                scratch,
                sizeof(scratch),
                "kfs2\tnative\nkfs3\tposix overlay\nfat12\tlegacy\n" "nodev\tproc\nnodev\tdevtmpfs\n");
        } else {
            length = (u32)snprintf(
                scratch,
                sizeof(scratch),
                "/dev/root / kfs3 rw 0 0\nproc /proc proc ro 0 0\n" "devtmpfs /dev devtmpfs rw 0 0\n");
        }
    } else if (p.kind == PSEUDO_PROC_PID_FILE) {
        length = proc_pid_content(p.pid, p.file, scratch, sizeof(scratch));
    } else {
        return 0;
    }
    if (offset >= length) {
        return 0;
    }
    u32 n = length - offset;
    if (n > capacity) {
        n = capacity;
    }
    memcpy(buffer, scratch + offset, n);
    return (int)n;
}

int kfs3_pseudo_write(const char* absolute, u32 offset, const void* buffer, u32 size, int* handled) {
    (void)offset;
    *handled = 0;
    if (!active) {
        return 0;
    }
    pseudo_path_t p = classify(absolute);
    if (p.kind == PSEUDO_NONE) {
        return 0;
    }
    *handled = 1;
    if (p.kind == PSEUDO_DEV_NULL) {
        return (int)size;
    }
    if (p.kind == PSEUDO_DEV_TTY) {
        const char* text = buffer;
        for (u32 i = 0; i < size; ++i) {
            terminal_putchar(text[i]);
        }
        terminal_refresh();
        return (int)size;
    }
    return VFS_EROFS;
}

static void fill_entry(void* entry, int long_entry, const char* name, int directory, u32 size) {
    if (long_entry) {
        kos_long_dirent* out = entry;
        memset(out, 0, sizeof(*out));
        strncat(out->name, name, KOS_LONG_NAME_MAX - 1);
        out->stat.directory = (u8)directory;
        out->stat.size = size;
    } else {
        kos_dirent* out = entry;
        memset(out, 0, sizeof(*out));
        strncat(out->name, name, sizeof(out->name) - 1);
        out->stat.directory = (u8)directory;
        out->stat.size = size;
    }
}

static int pseudo_readdir_common(const char* absolute, u32* cursor, void* entry, int long_entry) {
    if (!active) {
        return 0;
    }
    pseudo_path_t p = classify(absolute);
    if (p.kind == PSEUDO_DEV) {
        static const char* const devices[] = { "NULL", "ZERO", "RANDOM", "URANDOM", "TTY", "CONSOLE" };
        if (*cursor >= sizeof(devices) / sizeof(devices[0])) {
            return 0;
        }
        fill_entry(entry, long_entry, devices[*cursor], 0, 0);
        ++*cursor;
        return 1;
    }
    if (p.kind == PSEUDO_PROC) {
        static const char* const files[] = { "SELF",    "UPTIME",      "MEMINFO", "VERSION",
                                             "CPUINFO", "FILESYSTEMS", "MOUNTS",  "EVENTS" };
        u32 index = *cursor;
        if (index < sizeof(files) / sizeof(files[0])) {
            fill_entry(entry, long_entry, files[index], index == 0, 0);
            ++*cursor;
            return 1;
        }
        index -= sizeof(files) / sizeof(files[0]);
        u32 pid = process_pid_at(index);
        if (!pid) {
            return 0;
        }
        char name[16];
        snprintf(name, sizeof(name), "%u", pid);
        fill_entry(entry, long_entry, name, 1, 0);
        ++*cursor;
        return 1;
    }
    if (p.kind == PSEUDO_PROC_PID) {
        static const char* const files[] = { "STATUS", "CMDLINE", "COMM" };
        if (*cursor >= 3) {
            return 0;
        }
        fill_entry(entry, long_entry, files[*cursor], 0, 0);
        ++*cursor;
        return 1;
    }
    return 0;
}

int kfs3_pseudo_readdir(const char* absolute, u32* cursor, kos_dirent* entry) {
    return pseudo_readdir_common(absolute, cursor, entry, 0);
}

int kfs3_pseudo_readdir_long(const char* absolute, u32* cursor, kos_long_dirent* entry) {
    return pseudo_readdir_common(absolute, cursor, entry, 1);
}
