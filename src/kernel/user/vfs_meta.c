#include "vfs_meta.h"

#include "../fs/vfs.h"
#include "../kklibc/kklibc.h"

static vfs_meta_entry entries[VFS_META_MAX];
static const char* META_PATH = "/etc/fsmeta";

static void copy_path(char* out, const char* path) {
    u32 n = 0;
    while (path[n] && n + 1 < 256) {
        out[n] = path[n];
        ++n;
    }
    out[n] = 0;
}

static int same_path(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static u32 parse_uid(const char* text) {
    u32 value = 0;
    for (const char* p = text; *p >= '0' && *p <= '9'; ++p) {
        value = value * 10 + (u32)(*p - '0');
    }
    return value;
}

static u16 parse_octal(const char* text) {
    u32 value = 0;
    for (const char* p = text; *p >= '0' && *p <= '7'; ++p) {
        value = value * 8 + (u32)(*p - '0');
    }
    return (u16)(value & 0777);
}

int vfs_meta_get(const char* path, u32* uid, u16* mode) {
    if (!path) {
        return 0;
    }
    for (int i = 0; i < VFS_META_MAX; ++i) {
        if (entries[i].used && same_path(entries[i].path, path)) {
            if (uid) {
                *uid = entries[i].uid;
            }
            if (mode) {
                *mode = entries[i].mode;
            }
            return 1;
        }
    }
    return 0;
}

int vfs_meta_set(const char* path, u32 uid, u16 mode) {
    if (!path || !*path) {
        return VFS_EINVAL;
    }
    for (int i = 0; i < VFS_META_MAX; ++i) {
        if (entries[i].used && same_path(entries[i].path, path)) {
            entries[i].uid = uid;
            entries[i].mode = mode & 0777;
            return vfs_meta_save();
        }
    }
    for (int i = 0; i < VFS_META_MAX; ++i) {
        if (!entries[i].used) {
            copy_path(entries[i].path, path);
            entries[i].uid = uid;
            entries[i].mode = mode & 0777;
            entries[i].used = 1;
            return vfs_meta_save();
        }
    }
    return VFS_ENOSPC;
}

int vfs_meta_load(void) {
    char data[4096];
    int size = vfs_read(META_PATH, 0, data, sizeof(data) - 1);
    for (int i = 0; i < VFS_META_MAX; ++i) {
        entries[i].used = 0;
    }
    if (size <= 0) {
        return size;
    }
    data[size] = 0;
    char* line = data;
    while (*line) {
        char* end = line;
        while (*end && *end != '\n' && *end != '\r') {
            ++end;
        }
        char saved = *end;
        *end = 0;
        char* p = line;
        while (*p == ' ') {
            ++p;
        }
        char* uid_start = p;
        while (*p >= '0' && *p <= '9') {
            ++p;
        }
        char uid_save = *p;
        if (p == uid_start) {
            if (!saved) {
                break;
            }
            line = end + 1;
            continue;
        }
        *p = 0;
        u32 uid = parse_uid(uid_start);
        p = uid_save ? p + 1 : p;
        while (*p == ' ') {
            ++p;
        }
        char* mode_start = p;
        while (*p >= '0' && *p <= '7') {
            ++p;
        }
        char mode_save = *p;
        if (p == mode_start) {
            *uid_start = uid_save;
            if (!saved) {
                break;
            }
            line = end + 1;
            continue;
        }
        *p = 0;
        u16 mode = parse_octal(mode_start);
        p = mode_save ? p + 1 : p;
        while (*p == ' ') {
            ++p;
        }
        char* path = p;
        if (*path) {
            for (int i = 0; i < VFS_META_MAX; ++i) {
                if (!entries[i].used) {
                    entries[i].uid = uid;
                    entries[i].mode = mode;
                    copy_path(entries[i].path, path);
                    entries[i].used = 1;
                    break;
                }
            }
        }
        *uid_start = uid_save;
        if (!saved) {
            break;
        }
        line = end + 1;
    }
    return 0;
}

static void append_decimal(char* out, u32 capacity, u32* used, u32 value) {
    char digits[11];
    int n = 10;
    digits[n] = 0;
    do {
        digits[--n] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n < 11 && digits[n] && *used + 1 < capacity) {
        out[(*used)++] = digits[n++];
    }
    out[*used] = 0;
}

static void append_octal(char* out, u32 capacity, u32* used, u16 value) {
    for (int shift = 6; shift >= 0; shift -= 3) {
        if (*used + 1 >= capacity) {
            break;
        }
        out[(*used)++] = (char)('0' + ((value >> shift) & 7));
    }
    out[*used] = 0;
}

int vfs_meta_save(void) {
    char data[4096];
    u32 used = 0;
    data[0] = 0;
    for (int i = 0; i < VFS_META_MAX; ++i) {
        if (!entries[i].used) {
            continue;
        }
        append_decimal(data, sizeof(data), &used, entries[i].uid);
        if (used + 1 >= sizeof(data)) {
            break;
        }
        data[used++] = ' ';
        data[used] = 0;
        append_octal(data, sizeof(data), &used, entries[i].mode);
        if (used + 1 >= sizeof(data)) {
            break;
        }
        data[used++] = ' ';
        data[used] = 0;
        int n = 0;
        while (entries[i].path[n] && used + 1 < sizeof(data)) {
            data[used++] = entries[i].path[n++];
        }
        data[used] = 0;
        if (used + 1 < sizeof(data)) {
            data[used++] = '\n';
            data[used] = 0;
        }
    }
    return vfs_replace(META_PATH, data, used);
}
