#ifndef USER_RUNTIME_H
#define USER_RUNTIME_H
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
