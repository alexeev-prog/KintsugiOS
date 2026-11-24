#ifndef USER_AUTHABI_H
#define USER_AUTHABI_H

/* Account and credential syscalls (KFS3 user namespace). */
#define KOS_SYS_GETUID 56
#define KOS_SYS_GETGID 57
#define KOS_SYS_LOGIN 58
#define KOS_SYS_ELEVATE 59
#define KOS_SYS_CANCEL_ELEVATE 60
#define KOS_SYS_USERADD 61
#define KOS_SYS_USERPASSWD 62
#define KOS_SYS_REBOOT 63
#define KOS_SYS_GETUSER 64
#define KOS_SYS_AUTH 65
#define KOS_SYS_NEEDAUTH 66
#define KOS_SYS_CHMOD 68
#define KOS_SYS_CHOWN 69

#endif
