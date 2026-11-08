#include "vfs.h"

#include "../kklibc/kklibc.h"
#include "../user/process.h"
static const vfs_ops_t* backend;
static u32 generation;
static char cwd[VFS_PATH_MAX] = "/";

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
    vfs_stat_t stat;
    int result = vfs_stat(path, &stat);
    if (result) {
        return result;
    }
    if (stat.directory) {
        return VFS_EISDIR;
    }
    result = vfs_normalize(path, file->path);
    if (result) {
        return result;
    }
    file->offset = 0;
    file->generation = generation;
    file->open = 1;
    return 0;
}

int vfs_file_read(vfs_file_t* file, void* buffer, u32 capacity) {
    if (!file || !file->open || file->generation != generation) {
        return VFS_EINVAL;
    }
    int result = vfs_read(file->path, file->offset, buffer, capacity);
    if (result > 0) {
        file->offset += result;
    }
    return result;
}

int vfs_file_write(vfs_file_t* file, const void* buffer, u32 size) {
    if (!file || !file->open || file->generation != generation) {
        return VFS_EINVAL;
    }
    int result = vfs_write(file->path, file->offset, buffer, size);
    if (!result) {
        file->offset += size;
        return size;
    }
    return result;
}

int vfs_seek(vfs_file_t* file, u32 offset) {
    if (!file || !file->open || file->generation != generation || offset > 0x7fffffffu) {
        return VFS_EINVAL;
    }
    file->offset = offset;
    return 0;
}

void vfs_close(vfs_file_t* file) {
    if (file) {
        file->open = 0;
    }
}

int vfs_normalize(const char* path, char out[VFS_PATH_MAX]) {
    if (!path || !*path) {
        return VFS_EINVAL;
    }
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
        for (u32 i = 0; i < n; ++i) {
            out[length++] = toupper(start[i]);
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
    return backend->stat(absolute, stat);
}

int vfs_chdir(const char* path) {
    RESOLVE(path);
    vfs_stat_t stat;
    result = backend->stat(absolute, &stat);
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
    return backend->read(absolute, offset, buffer, capacity);
}

int vfs_replace(const char* path, const void* data, u32 size) {
    if (!data && size) {
        return VFS_EINVAL;
    }
    RESOLVE(path);
    return backend->replace(absolute, data, size);
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
    return backend->create(absolute, 0);
}

int vfs_mkdir(const char* path) {
    RESOLVE(path);
    return backend->create(absolute, 1);
}

static int ancestor(const char* parent, const char* child) {
    u32 n = strlen(parent), length = strlen(child);
    return length >= n && !memcmp(parent, child, n) && (!child[n] || child[n] == '/' || n == 1);
}

int vfs_unlink(const char* path) {
    RESOLVE(path);
    result = backend->remove(absolute, 0);
    ++generation;
    return result;
}

int vfs_rmdir(const char* path) {
    RESOLVE(path);
    if (ancestor(absolute, cwd)) {
        return VFS_EBUSY;
    }
    return backend->remove(absolute, 1);
}

int vfs_rename(const char* source, const char* destination) {
    RESOLVE(source);
    char target[VFS_PATH_MAX];
    result = vfs_normalize(destination, target);
    if (result) {
        return result;
    }
    if (ancestor(absolute, cwd)) {
        return VFS_EBUSY;
    }
    result = backend->rename(absolute, target);
    ++generation;
    return result;
}

int vfs_readdir(const char* path, u32* cursor, vfs_dirent_t* entry) {
    if (!cursor || !entry) {
        return VFS_EINVAL;
    }
    RESOLVE(path);
    return backend->readdir(absolute, cursor, entry);
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
    switch (e) {
        case VFS_ENOENT:
            return "Not found";
        case VFS_EIO:
            return "Disk I/O error or filesystem unavailable";
        case VFS_EINVAL:
            return "Invalid path or argument (8.3 names required)";
        case VFS_ENOSPC:
            return "Disk or directory full";
        case VFS_EEXIST:
            return "Already exists";
        case VFS_ENOTDIR:
            return "Not a directory";
        case VFS_EISDIR:
            return "Is a directory";
        case VFS_ENOTEMPTY:
            return "Directory not empty";
        case VFS_EROFS:
            return "Read-only";
        case VFS_ECORRUPT:
            return "Corrupt filesystem";
        case VFS_ENOMEM:
            return "Out of memory";
        case VFS_EBUSY:
            return "Directory is in use";
        default:
            return "Filesystem error";
    }
}
