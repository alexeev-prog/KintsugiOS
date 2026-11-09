#ifndef ENVABI_H
#define ENVABI_H
#define KOS_SYS_ENV_GET 35
#define KOS_SYS_ENV_SET 36
#define KOS_ENV_MAX 24
#define KOS_ENV_NAME 32
#define KOS_ENV_VALUE 192

typedef struct {
    char name[KOS_ENV_NAME];
    char value[KOS_ENV_VALUE];
} kos_env_entry;
#endif
