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

/* Extended directory ABI: long names are supported by KFS2; this record
 * remains available for legacy FAT12 programs. */
#define KOS_LONG_NAME_MAX 256

typedef struct {
    char name[KOS_LONG_NAME_MAX];
    kos_stat stat;
} kos_long_dirent;

#define KOS_SYS_READDIR_LONG 44

#endif
