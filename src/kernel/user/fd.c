#include "fd.h"

#include "../drivers/keyboard.h"
#include "../drivers/terminal.h"
#include "../fs/vfs.h"
#include "../kklibc/kklibc.h"
#include "process.h"

#define PIPE_SIZE 4096

enum {
    FILE_OBJECT,
    TERMINAL_OBJECT,
    PIPE_READER,
    PIPE_WRITER
};

typedef struct {
    u32 readers, writers, head, length;
    u8 data[PIPE_SIZE];
} pipe_t;

struct open_description {
    u32 references, kind, flags;
    vfs_file_t file;
    pipe_t* pipe;
};

static open_description_t* create(u32 kind, u32 flags) {
    open_description_t* object = kmalloc(sizeof(*object));
    if (object) {
        memset(object, 0, sizeof(*object));
        object->references = 1;
        object->kind = kind;
        object->flags = flags;
    }
    return object;
}

static int vacant(fd_table_t* table) {
    for (int i = 0; i < KOS_FD_MAX; ++i) {
        if (!table->entries[i]) {
            return i;
        }
    }
    return KOS_EMFILE;
}

int fd_close(fd_table_t* table, int fd) {
    if (fd < 0 || fd >= KOS_FD_MAX || !table->entries[fd]) {
        return KOS_EBADF;
    }
    open_description_t* object = table->entries[fd];
    table->entries[fd] = NULL;
    if (--object->references) {
        return 0;
    }
    if (object->kind == FILE_OBJECT) {
        vfs_close(&object->file);
    }
    if (object->pipe) {
        pipe_t* p = object->pipe;
        if (object->kind == PIPE_READER) {
            --p->readers;
        } else {
            --p->writers;
        }
        process_wake(p);
        if (!p->readers && !p->writers) {
            kfree(p);
        }
    }
    kfree(object);
    return 0;
}

void fd_destroy(fd_table_t* table) {
    for (int i = 0; i < KOS_FD_MAX; ++i) {
        if (table->entries[i]) {
            fd_close(table, i);
        }
    }
}

int fd_init(fd_table_t* table) {
    memset(table, 0, sizeof(*table));
    for (int i = 0; i < 3; ++i) {
        table->entries[i] = create(TERMINAL_OBJECT, i ? KOS_O_WRITE : KOS_O_READ);
        if (!table->entries[i]) {
            fd_destroy(table);
            return VFS_ENOMEM;
        }
    }
    return 0;
}

void fd_clone(fd_table_t* destination, const fd_table_t* source) {
    for (int i = 0; i < KOS_FD_MAX; ++i) {
        destination->entries[i] = source->entries[i];
        if (destination->entries[i]) {
            ++destination->entries[i]->references;
        }
    }
}

int fd_dup2(fd_table_t* table, int source, int destination) {
    if (source < 0 || source >= KOS_FD_MAX || !table->entries[source] || destination < 0
        || destination >= KOS_FD_MAX) {
        return KOS_EBADF;
    }
    if (source == destination) {
        return destination;
    }
    if (table->entries[destination]) {
        fd_close(table, destination);
    }
    table->entries[destination] = table->entries[source];
    ++table->entries[destination]->references;
    return destination;
}

int fd_open(fd_table_t* table, const char* path, u32 flags) {
    if (!(flags & 3) || (flags & ~63u) || ((flags & (KOS_O_APPEND | KOS_O_TRUNC)) && !(flags & KOS_O_WRITE))
        || ((flags & KOS_O_EXCL) && !(flags & KOS_O_CREATE))) {
        return VFS_EINVAL;
    }
    int fd = vacant(table);
    if (fd < 0) {
        return fd;
    }
    open_description_t* object = create(FILE_OBJECT, flags);
    if (!object) {
        return VFS_ENOMEM;
    }
    int result = vfs_open(path, &object->file);
    if (!result && (flags & KOS_O_EXCL)) {
        result = VFS_EEXIST;
    }
    if (result == VFS_ENOENT && (flags & KOS_O_CREATE)) {
        result = vfs_create(path);
        if (!result) {
            result = vfs_open(path, &object->file);
        }
    }
    if (!result && (flags & KOS_O_WRITE)) {
        vfs_stat_t stat;
        result = vfs_stat(path, &stat);
        if (!result && stat.readonly) {
            result = VFS_EROFS;
        }
    }
    if (!result && (flags & KOS_O_TRUNC)) {
        result = vfs_truncate(path, 0);
    }
    if (result) {
        kfree(object);
        return result;
    }
    table->entries[fd] = object;
    return fd;
}

int fd_pipe(fd_table_t* table, int pair[2]) {
    int reader = vacant(table);
    if (reader < 0) {
        return reader;
    }
    open_description_t* r = create(PIPE_READER, KOS_O_READ);
    open_description_t* w = create(PIPE_WRITER, KOS_O_WRITE);
    pipe_t* pipe = kmalloc(sizeof(*pipe));
    if (!r || !w || !pipe) {
        kfree(r);
        kfree(w);
        kfree(pipe);
        return VFS_ENOMEM;
    }
    table->entries[reader] = r;
    int writer = vacant(table);
    if (writer < 0) {
        table->entries[reader] = NULL;
        kfree(r);
        kfree(w);
        kfree(pipe);
        return writer;
    }
    memset(pipe, 0, sizeof(*pipe));
    pipe->readers = pipe->writers = 1;
    r->pipe = w->pipe = pipe;
    table->entries[writer] = w;
    pair[0] = reader;
    pair[1] = writer;
    return 0;
}

static open_description_t* get(fd_table_t* table, int fd, u32 mode) {
    if (fd < 0 || fd >= KOS_FD_MAX) {
        return NULL;
    }
    open_description_t* object = table->entries[fd];
    return object && (object->flags & mode) ? object : NULL;
}

int fd_read(fd_table_t* table, int fd, void* buffer, u32 size) {
    open_description_t* object = get(table, fd, KOS_O_READ);
    if (!object) {
        return KOS_EBADF;
    }
    if (!size) {
        return 0;
    }
    if (object->kind == FILE_OBJECT) {
        return vfs_file_read(&object->file, buffer, size);
    }
    if (object->kind == TERMINAL_OBJECT) {
        if (!process_foreground()) {
            return VFS_EBUSY;
        }
        int key = keyboard_read_event();
        if (!key) {
            process_block(NULL);
            return KOS_EAGAIN;
        }
        if (key == 4) {
            return 0;
        }
        if (key > 255) {
            return KOS_EAGAIN;
        }
        *(u8*)buffer = key;
        return 1;
    }
    pipe_t* p = object->pipe;
    if (!p->length) {
        if (!p->writers) {
            return 0;
        }
        process_block(p);
        return KOS_EAGAIN;
    }
    if (size > p->length) {
        size = p->length;
    }
    for (u32 i = 0; i < size; ++i) {
        ((u8*)buffer)[i] = p->data[(p->head + i) % PIPE_SIZE];
    }
    p->head = (p->head + size) % PIPE_SIZE;
    p->length -= size;
    process_wake(p);
    return size;
}

int fd_write(fd_table_t* table, int fd, const void* buffer, u32 size) {
    open_description_t* object = get(table, fd, KOS_O_WRITE);
    if (!object) {
        return KOS_EBADF;
    }
    if (!size) {
        return 0;
    }
    if (object->kind == FILE_OBJECT) {
        if (object->flags & KOS_O_APPEND) {
            vfs_stat_t stat;
            int result = vfs_stat(object->file.path, &stat);
            if (!result) {
                result = vfs_seek(&object->file, stat.size);
            }
            return result ? result : vfs_file_write(&object->file, buffer, size);
        }
        return vfs_file_write(&object->file, buffer, size);
    }
    if (object->kind == TERMINAL_OBJECT) {
        for (u32 i = 0; i < size; ++i) {
            terminal_putchar(((const char*)buffer)[i]);
        }
        terminal_refresh();
        return size;
    }
    pipe_t* p = object->pipe;
    if (!p->readers) {
        return KOS_EPIPE;
    }
    if (p->length == PIPE_SIZE) {
        process_block(p);
        return KOS_EAGAIN;
    }
    if (size > PIPE_SIZE - p->length) {
        size = PIPE_SIZE - p->length;
    }
    for (u32 i = 0; i < size; ++i) {
        p->data[(p->head + p->length + i) % PIPE_SIZE] = ((const u8*)buffer)[i];
    }
    p->length += size;
    process_wake(p);
    return size;
}

int fd_seek(fd_table_t* table, int fd, u32 offset) {
    if (fd < 0 || fd >= KOS_FD_MAX || !table->entries[fd]) {
        return KOS_EBADF;
    }
    open_description_t* object = table->entries[fd];
    return object->kind == FILE_OBJECT ? vfs_seek(&object->file, offset) : KOS_ESPIPE;
}

int fd_truncate(fd_table_t* table, int fd, u32 size) {
    open_description_t* object = get(table, fd, KOS_O_WRITE);
    if (!object) {
        return KOS_EBADF;
    }
    if (object->kind != FILE_OBJECT) {
        return KOS_ESPIPE;
    }
    int result = vfs_seek(&object->file, object->file.offset);
    return result ? result : vfs_truncate(object->file.path, size);
}

int fd_stat(fd_table_t* table, int fd, kos_stat* stat) {
    if (fd < 0 || fd >= KOS_FD_MAX || !table->entries[fd]) {
        return KOS_EBADF;
    }
    open_description_t* object = table->entries[fd];
    if (object->kind != FILE_OBJECT) {
        return KOS_ESPIPE;
    }
    int result = vfs_seek(&object->file, object->file.offset);
    return result ? result : vfs_stat(object->file.path, stat);
}
