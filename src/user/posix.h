#ifndef USER_POSIX_H
#define USER_POSIX_H

/* A deliberately small POSIX-facing surface on top of the KintsugiOS ABI.
 * Names and argument shapes follow POSIX where the kernel can honour them;
 * unsupported facilities are not silently faked. */
#include "../shared/signal.h"
#include "authabi.h"
#include "runtime.h"
#include "sysabi.h"

typedef int pid_t;
typedef unsigned uid_t;
typedef unsigned gid_t;
typedef unsigned mode_t;

static inline pid_t posix_getpid(void) {
    return syscall(15, 0, 0, 0, 0);
}

static inline uid_t getuid(void) {
    return (uid_t)syscall(KOS_SYS_GETUID, 0, 0, 0, 0);
}

static inline gid_t getgid(void) {
    return (gid_t)syscall(KOS_SYS_GETGID, 0, 0, 0, 0);
}

/* waitpid reports the child's status through the POSIX status pointer. The
 * kernel still reaps direct children only, and options are ignored. */
static inline pid_t waitpid(pid_t pid, int* status, int options) {
    (void)options;
    int result = wait(pid);
    if (status) {
        *status = result;
    }
    return pid;
}

static inline int chdir(const char* path) {
    return syscall(11, (u32)path, 3, 0, 0);
}

static inline int getcwd(char* buffer, u32 size) {
    return syscall(4, (u32)buffer, size, 0, 0);
}

static inline int mkdir(const char* path, mode_t mode) {
    (void)mode;
    return syscall(11, (u32)path, 0, 0, 0);
}

static inline int rmdir(const char* path) {
    return syscall(11, (u32)path, 2, 0, 0);
}

static inline int unlink(const char* path) {
    return syscall(11, (u32)path, 1, 0, 0);
}

static inline int rename_posix(const char* from, const char* to) {
    return syscall(12, (u32)from, (u32)to, 1, 0);
}

static inline int stat_posix(const char* path, kos_stat* info) {
    return syscall(11, (u32)path, 4, (u32)info, 0);
}

static inline int sleep_ms_posix(u32 milliseconds) {
    return syscall(KOS_SYS_SLEEP, milliseconds, 0, 0, 0);
}

static inline void usleep(u32 microseconds) {
    u32 ms = microseconds / 1000;
    if (!ms && microseconds) {
        ms = 1;
    }
    sleep_ms_posix(ms);
}
#endif
