#include "vfs.h"
#include "kfs3.h"

#include "../../user/errors.h"
#include "../kklibc/kklibc.h"
#include "../user/process.h"
static const vfs_ops_t* backend;
static u32 generation;
static char cwd[VFS_PATH_MAX] = "/";

/* Tiny change journal. It is a file (/proc/events), not an ioctl API: reading
 * it returns newline-separated "OP path" records, oldest first. */
#define VFS_EVENT_BYTES 1024
static char event_data[VFS_EVENT_BYTES];
static u32 event_used;

static void event_log(const char* operation, const char* path) {
    char line[VFS_PATH_MAX + 24];
    int n = snprintf(line, sizeof(line), "%s %s\n", operation, path);
    if (n <= 0) return;
    u32 length = (u32)n;
    if (length >= sizeof(line)) length = sizeof(line) - 1;
    if (event_used + length > sizeof(event_data)) {
        event_used = 0; /* bounded ring: drop old history rather than overflow */
    }
    memcpy(event_data + event_used, line, length);
    event_used += length;
}

int vfs_events_read(char* buffer, u32 capacity) {
    u32 n = event_used < capacity ? event_used : capacity;
    if (n) memcpy(buffer, event_data, n);
    return (int)n;
}

typedef struct {
    u32 references;
    u32 mount_generation;
    const vfs_ops_t* backend;
    u8 pseudo;
    char path[VFS_PATH_MAX];
} vfs_open_object_t;

void vfs_mount(const vfs_ops_t* ops) {
    backend = ops;
    ++generation;
    strcpy(cwd, "/");
}

const char* vfs_getcwd(void) {
    char* local = process_cwd();
    return local && local[0] ? local : cwd;
}

int vfs_open(const char* path, vfs_file_t* file) {
    if (!file) {
        return VFS_EINVAL;
    }
    file->open = 0;
    file->object = NULL;
    vfs_stat_t stat;
    int result = vfs_stat(path, &stat);
    if (result) {
        return result;
    }
    if (stat.directory) {
        return VFS_EISDIR;
    }
    char normalized[VFS_PATH_MAX];
    result = vfs_normalize(path, normalized);
    if (result) {
        return result;
    }
    vfs_open_object_t* object = kmalloc(sizeof(*object));
    if (!object) return VFS_ENOMEM;
    object->references = 1;
    object->mount_generation = generation;
    object->backend = backend;
    kos_stat pseudo;
    object->pseudo = (u8)kfs3_pseudo_stat(normalized, &pseudo);
    strcpy(object->path, normalized);
    file->object = object;
    file->offset = 0;
    file->generation = generation;
    file->open = 1;
    return 0;
}

int vfs_file_read(vfs_file_t* file, void* buffer, u32 capacity) {
    if (!file || !file->open || !file->object
        || ((vfs_open_object_t*)file->object)->mount_generation != generation) {
        return VFS_EINVAL;
    }
    vfs_open_object_t* object = file->object;
    int result;
    if (object->pseudo) {
        int handled = 0;
        result = kfs3_pseudo_read(object->path, file->offset, buffer, capacity, &handled);
    } else {
        result = object->backend->read(object->path, file->offset, buffer, capacity);
    }
    if (result > 0) {
        file->offset += result;
    }
    return result;
}

int vfs_file_write(vfs_file_t* file, const void* buffer, u32 size) {
    if (!file || !file->open || !file->object
        || ((vfs_open_object_t*)file->object)->mount_generation != generation) {
        return VFS_EINVAL;
    }
    vfs_open_object_t* object = file->object;
    int result;
    if (object->pseudo) {
        int handled = 0;
        result = kfs3_pseudo_write(object->path, file->offset, buffer, size, &handled);
    } else {
        result = object->backend->write ? object->backend->write(object->path, file->offset, buffer, size)
                                        : vfs_write(object->path, file->offset, buffer, size);
    }
    if (result >= 0) file->offset += result;
    return result;
}

int vfs_seek(vfs_file_t* file, u32 offset) {
    if (!file || !file->open || !file->object
        || ((vfs_open_object_t*)file->object)->mount_generation != generation
        || offset > 0x7fffffffu) {
        return VFS_EINVAL;
    }
    file->offset = offset;
    return 0;
}

void vfs_close(vfs_file_t* file) {
    if (file && file->open) {
        vfs_open_object_t* object = file->object;
        if (object && --object->references == 0) kfree(object);
        file->object = NULL;
        file->open = 0;
    }
}

void vfs_file_clone(vfs_file_t* destination, const vfs_file_t* source) {
    *destination = *source;
    if (destination->open && destination->object) {
        ++((vfs_open_object_t*)destination->object)->references;
    }
}

int vfs_file_stat(vfs_file_t* file, vfs_stat_t* stat) {
    if (!file || !file->open || !file->object || !stat
        || ((vfs_open_object_t*)file->object)->mount_generation != generation) return VFS_EINVAL;
    vfs_open_object_t* object = file->object;
    kos_stat pseudo;
    if (object->pseudo) {
        kfs3_pseudo_stat(object->path, &pseudo);
        *stat = pseudo;
        return 0;
    }
    return object->backend->stat(object->path, stat);
}

int vfs_file_truncate(vfs_file_t* file, u32 size) {
    if (!file || !file->open || !file->object
        || ((vfs_open_object_t*)file->object)->mount_generation != generation) return VFS_EINVAL;
    vfs_open_object_t* object = file->object;
    return vfs_truncate(object->path, size);
}

const char* vfs_file_path(const vfs_file_t* file) {
    if (!file || !file->open || !file->object) return NULL;
    return ((vfs_open_object_t*)file->object)->path;
}

int vfs_normalize(const char* path, char out[VFS_PATH_MAX]) {
    if (!path || !*path) {
        return VFS_EINVAL;
    }
    /* POSIX path translation (KFS3) happens before legacy normalization. */
    char posix[VFS_PATH_MAX];
    if (kfs3_is_active() && !kfs3_translate(path, posix)) path = posix;
    u32 length = 1;
    out[0] = '/';
    out[1] = 0;
    if (*path != '/') {
        strcpy(out, vfs_getcwd());
        length = strlen(out);
    }
    while (*path) {
        while (*path == '/') {
            ++path;
        }
        if (!*path) {
            break;
        }
        const char* start = path;
        while (*path && *path != '/') {
            ++path;
        }
        u32 n = path - start;
        if (n == 1 && *start == '.') {
            continue;
        }
        if (n == 2 && start[0] == '.' && start[1] == '.') {
            while (length > 1 && out[length - 1] != '/') {
                --length;
            }
            if (length > 1) {
                --length;
            }
            out[length] = 0;
            continue;
        }
        if (n >= VFS_NAME_MAX || length + n + (length > 1) >= VFS_PATH_MAX) {
            return VFS_EINVAL;
        }
        if (length > 1) {
            out[length++] = '/';
        }
        /* Preserve case: KFS2 supports long mixed-case names. FAT12 converts
         * components to 8.3 itself in short_name(). */
        for (u32 i = 0; i < n; ++i) {
            out[length++] = start[i];
        }
        out[length] = 0;
    }
    return 0;
}

#define RESOLVE(path) \
    char absolute[VFS_PATH_MAX]; \
    if (!backend) \
        return VFS_EIO; \
    int result = vfs_normalize(path, absolute); \
    if (result) \
    return result

int vfs_stat(const char* path, vfs_stat_t* stat) {
    if (!stat) {
        return VFS_EINVAL;
    }
    RESOLVE(path);
    kos_stat pseudo;
    if (kfs3_pseudo_stat(absolute, &pseudo)) {
        *stat = pseudo;
        return 0;
    }
    return backend->stat(absolute, stat);
}

int vfs_chdir(const char* path) {
    RESOLVE(path);
    vfs_stat_t stat;
    result = vfs_stat(absolute, &stat);
    if (result) {
        return result;
    }
    if (!stat.directory) {
        return VFS_ENOTDIR;
    }
    char* local = process_cwd();
    strcpy(local ? local : cwd, absolute);
    return 0;
}

int vfs_read(const char* path, u32 offset, void* buffer, u32 capacity) {
    if ((!buffer && capacity) || capacity > 0x7fffffffu) {
        return VFS_EINVAL;
    }
    RESOLVE(path);
    int handled = 0;
    int pseudo = kfs3_pseudo_read(absolute, offset, buffer, capacity, &handled);
    if (handled) {
        return pseudo;
    }
    return backend->read(absolute, offset, buffer, capacity);
}

int vfs_replace(const char* path, const void* data, u32 size) {
    if (!data && size) {
        return VFS_EINVAL;
    }
    RESOLVE(path);
    int handled = 0;
    int pseudo = kfs3_pseudo_write(absolute, 0, data, size, &handled);
    if (handled) {
        return pseudo;
    }
    result = backend->replace(absolute, data, size);
    if (!result) event_log("WRITE", absolute);
    return result;
}

static int modify(const char* path, u32 offset, const void* data, u32 size, int truncate, int append) {
    vfs_stat_t stat;
    int result = vfs_stat(path, &stat);
    if (result) {
        return result;
    }
    if (stat.directory) {
        return VFS_EISDIR;
    }
    if (stat.readonly) {
        return VFS_EROFS;
    }
    if (append) {
        offset = stat.size;
    }
    if ((!data && size && !truncate) || offset > 0x7fffffffu || size > 0x7fffffffu - offset) {
        return VFS_EINVAL;
    }
    if (!truncate && !size) {
        return 0;
    }
    u32 length = truncate ? size : (offset + size > stat.size ? offset + size : stat.size);
    if (length > HEAP_SIZE / 2) {
        return VFS_ENOSPC;
    }
    u8* buffer = kmalloc(length ? length : 1);
    if (!buffer) {
        return VFS_ENOMEM;
    }
    memset(buffer, 0, length);
    u32 keep = stat.size < length ? stat.size : length;
    result = vfs_read(path, 0, buffer, keep);
    if (result >= 0 && (u32)result != keep) {
        result = VFS_ECORRUPT;
    }
    if (result >= 0) {
        if (!truncate && size) {
            memcpy(buffer + offset, data, size);
        }
        result = vfs_replace(path, buffer, length);
    }
    kfree(buffer);
    return result;
}

int vfs_write(const char* p, u32 o, const void* d, u32 n) {
    return modify(p, o, d, n, 0, 0);
}

int vfs_append(const char* p, const void* d, u32 n) {
    return modify(p, 0, d, n, 0, 1);
}

int vfs_truncate(const char* p, u32 n) {
    return modify(p, 0, NULL, n, 1, 0);
}

int vfs_create(const char* path) {
    RESOLVE(path);
    kos_stat pseudo;
    if (kfs3_pseudo_stat(absolute, &pseudo)) return VFS_EEXIST;
    result = backend->create(absolute, 0);
    if (!result) event_log("CREATE", absolute);
    return result;
}

int vfs_mkdir(const char* path) {
    RESOLVE(path);
    kos_stat pseudo;
    if (kfs3_pseudo_stat(absolute, &pseudo)) return VFS_EEXIST;
    result = backend->create(absolute, 1);
    if (!result) event_log("MKDIR", absolute);
    return result;
}

static int ancestor(const char* parent, const char* child) {
    u32 n = strlen(parent), length = strlen(child);
    return length >= n && !memcmp(parent, child, n) && (!child[n] || child[n] == '/' || n == 1);
}

int vfs_unlink(const char* path) {
    RESOLVE(path);
    kos_stat pseudo;
    if (kfs3_pseudo_stat(absolute, &pseudo)) return VFS_EROFS;
    result = backend->remove(absolute, 0);
    if (!result) event_log("UNLINK", absolute);
    return result;
}

int vfs_rmdir(const char* path) {
    RESOLVE(path);
    kos_stat pseudo;
    if (kfs3_pseudo_stat(absolute, &pseudo)) return VFS_EROFS;
    if (ancestor(absolute, cwd)) {
        return VFS_EBUSY;
    }
    result = backend->remove(absolute, 1);
    if (!result) event_log("RMDIR", absolute);
    return result;
}

int vfs_rename(const char* source, const char* destination) {
    RESOLVE(source);
    char target[VFS_PATH_MAX];
    result = vfs_normalize(destination, target);
    if (result) {
        return result;
    }
    kos_stat pseudo;
    if (kfs3_pseudo_stat(absolute, &pseudo) || kfs3_pseudo_stat(target, &pseudo)) return VFS_EROFS;
    if (ancestor(absolute, cwd)) {
        return VFS_EBUSY;
    }
    result = backend->rename(absolute, target);
    if (!result) event_log("RENAME", absolute);
    return result;
}

int vfs_readdir(const char* path, u32* cursor, vfs_dirent_t* entry) {
    if (!cursor || !entry) {
        return VFS_EINVAL;
    }
    RESOLVE(path);
    kos_stat pseudo;
    if (kfs3_pseudo_stat(absolute, &pseudo)) {
        if (!pseudo.directory) return VFS_ENOTDIR;
        return kfs3_pseudo_readdir(absolute, cursor, entry);
    }
    return backend->readdir(absolute, cursor, entry);
}

int vfs_readdir_long(const char* path, u32* cursor, vfs_long_dirent_t* entry) {
    if (!cursor || !entry) return VFS_EINVAL;
    RESOLVE(path);
    kos_stat pseudo;
    if (kfs3_pseudo_stat(absolute, &pseudo)) {
        if (!pseudo.directory) return VFS_ENOTDIR;
        return kfs3_pseudo_readdir_long(absolute, cursor, entry);
    }
    if (backend->readdir_long) return backend->readdir_long(absolute, cursor, entry);
    vfs_dirent_t legacy;
    result = backend->readdir(absolute, cursor, &legacy);
    if (result > 0) {
        memset(entry, 0, sizeof(*entry));
        memcpy(entry->name, legacy.name, sizeof(legacy.name));
        entry->stat = legacy.stat;
    }
    return result;
}

int vfs_copy(const char* source, const char* destination) {
    vfs_stat_t stat;
    int result = vfs_stat(source, &stat);
    if (result) {
        return result;
    }
    if (stat.directory) {
        return VFS_EISDIR;
    }
    vfs_stat_t target;
    result = vfs_stat(destination, &target);
    if (!result) {
        return VFS_EEXIST;
    }
    if (result != VFS_ENOENT) {
        return result;
    }
    u8* data = kmalloc(stat.size ? stat.size : 1);
    if (!data) {
        return VFS_ENOMEM;
    }
    result = vfs_read(source, 0, data, stat.size);
    if (result >= 0) {
        result = (u32)result == stat.size ? vfs_replace(destination, data, stat.size) : VFS_ECORRUPT;
    }
    kfree(data);
    return result;
}

const char* vfs_error(int e) {
    return kos_error_text(e);
}
