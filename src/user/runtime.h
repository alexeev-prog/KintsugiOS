#ifndef USER_RUNTIME_H
#define USER_RUNTIME_H
#include "ioabi.h"
int open(const char* path, unsigned int flags);
int close(int fd);
int read(int fd, void* buffer, unsigned int size);
int write(int fd, const void* buffer, unsigned int size);
int dup2(int source, int destination);
int pipe(int pair[2]);
int seek(int fd, unsigned int offset);
int write_all(int fd, const void* buffer, unsigned int size);
typedef unsigned int u32;
int syscall(u32 number, u32 a, u32 b, u32 c, u32 d);
void* malloc(u32 size);
void free(void* pointer);
/* Blocking line input, includes newline, returns 0 on Ctrl+D. No trailing NUL. */
int read_stdin(void* buffer, u32 capacity);
/* spawn returns PID or negative error; wait returns child's exit status. */
int spawn(const char* path, int argc, char** argv);
int wait(int pid);
int getpid(void);
#endif
