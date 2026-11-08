#ifndef SHELLABI_H
#define SHELLABI_H
#define KOS_SYS_SESSION 32
#define KOS_SYS_LAUNCH 33
#define KOS_SYS_BUILTIN 34

typedef struct {
    unsigned int count;
    const char* paths[8];
    unsigned int argc[8];
    char** argv[8];
    const char* output;
    unsigned int append, background;
} kos_launch;
#endif
