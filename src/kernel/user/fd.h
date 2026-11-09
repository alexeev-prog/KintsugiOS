#ifndef USER_FD_H
#define USER_FD_H
#include "../../user/fsabi.h"
#include "../../user/ioabi.h"
#include "../kklibc/ctypes.h"
typedef struct open_description open_description_t;

typedef struct {
    open_description_t* entries[KOS_FD_MAX];
} fd_table_t;

int fd_init(fd_table_t* table);
void fd_clone(fd_table_t* destination, const fd_table_t* source);
void fd_destroy(fd_table_t* table);
int fd_open(fd_table_t* table, const char* path, u32 flags);
int fd_close(fd_table_t* table, int fd);
int fd_dup2(fd_table_t* table, int source, int destination);
int fd_pipe(fd_table_t* table, int pair[2]);
int fd_read(fd_table_t* table, int fd, void* buffer, u32 size);
int fd_write(fd_table_t* table, int fd, const void* buffer, u32 size);
int fd_seek(fd_table_t* table, int fd, u32 offset);
int fd_truncate(fd_table_t* table, int fd, u32 size);
int fd_stat(fd_table_t* table, int fd, kos_stat* stat);
#endif
