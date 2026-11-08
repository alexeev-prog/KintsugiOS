#ifndef USER_PROCESS_H
#define USER_PROCESS_H
#include "../kklibc/ctypes.h"
void process_init(void);
int process_run(const char* path, int argc, char** argv);
int process_active(void);
void process_tick(int user_mode);
int process_interrupt(void);
__attribute__((noreturn)) void process_exit(int status);
#endif
