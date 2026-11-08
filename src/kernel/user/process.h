#ifndef USER_PROCESS_H
#define USER_PROCESS_H
#include "../kklibc/ctypes.h"
void process_init(void);
int process_run(const char* path, int argc, char** argv);
int process_pipeline(int count, char** paths, int* argc, char*** argv, const char* output, int append);
int process_active(void);
int process_interrupt(void);
void process_check_interrupt(void);
void process_tick(int user_mode);
void process_block(const void* object);
void process_wake(const void* object);
int process_foreground(void);
__attribute__((noreturn)) void process_exit(int status);
#endif
