#ifndef USER_PROCESS_H
#define USER_PROCESS_H
#include "../kklibc/ctypes.h"
void process_init(void);
int process_run(const char* path, int argc, char** argv);
int process_boot_init(void);
void process_jobs(void);
int process_wait_jobs(void);
int process_pipeline(
    int count, char** paths, int* argc, char*** argv, const char* output, int append, int background);
int process_active(void);
int process_interrupt(void);
void process_check_interrupt(void);
void process_tick(int user_mode);
void process_block(const void* object);
void process_wake(const void* object);
int process_foreground(void);
char* process_cwd(void);
void process_shutdown(void);
__attribute__((noreturn)) void process_exit(int status);

/* POSIX-like credentials and /proc introspection. */
u32 process_pid(void);
u32 process_euid(void);
u32 process_egid(void);
int process_set_credentials(u32 uid, u32 gid);
void process_set_pending_uid(u32 uid);
int process_take_pending_uid(u32* uid);
u32 process_ticks(void);
u32 process_pid_exists(u32 pid);
u32 process_pid_at(u32 index);
int process_proc_status(u32 pid, char* out, u32 capacity);
int process_proc_cmdline(u32 pid, char* out, u32 capacity);
int process_proc_name(u32 pid, char* out, u32 capacity);
#endif
