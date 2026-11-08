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

#endif
