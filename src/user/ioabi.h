#ifndef USER_IOABI_H
#define USER_IOABI_H
#define KOS_FD_MAX 16
#define KOS_O_READ 1
#define KOS_O_WRITE 2
#define KOS_O_APPEND 4
#define KOS_O_CREATE 8
#define KOS_O_TRUNC 16
#define KOS_O_EXCL 32
#define KOS_EBADF (-13)
#define KOS_EMFILE (-14)
#define KOS_EPIPE (-15)
#define KOS_ESPIPE (-16)
#define KOS_EAGAIN (-17)
#define KOS_ECHILD (-18)
#define KOS_EPERM (-19)
#define KOS_ESRCH (-20)

enum {
    KOS_SYS_OPEN = 20,
    KOS_SYS_CLOSE,
    KOS_SYS_READ,
    KOS_SYS_WRITE,
    KOS_SYS_DUP2,
    KOS_SYS_PIPE,
    KOS_SYS_SEEK,
    KOS_SYS_FTRUNCATE,
    KOS_SYS_FSTAT,
    KOS_SYS_SYNC,
    KOS_SYS_KILL
};
#endif
