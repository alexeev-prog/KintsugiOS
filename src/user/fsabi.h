#ifndef USER_FSABI_H
#define USER_FSABI_H

/* i386 ABI: 32-bit unsigned size, two byte flags, natural alignment. */
typedef struct {
    unsigned int size;
    unsigned char directory, readonly;
} kos_stat;

typedef struct {
    char name[13];
    kos_stat stat;
} kos_dirent;

/* Extended directory ABI. The legacy 8.3 record above remains available for
 * old programs; new applications should use this record and syscall 44. */
#define KOS_LONG_NAME_MAX 256
typedef struct {
    char name[KOS_LONG_NAME_MAX];
    kos_stat stat;
} kos_long_dirent;

#define KOS_SYS_READDIR_LONG 44

#endif
