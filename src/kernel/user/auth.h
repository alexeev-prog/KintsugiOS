#ifndef USER_AUTH_H
#define USER_AUTH_H
#include "../kklibc/ctypes.h"

/* KFS3 account database. Records live in /etc/PASSWD and /etc/SHADOW so the
 * user namespace follows the "everything is a file" rule. Passwords are never
 * stored in clear text: an iterated salted FNV-1a digest is kept in SHADOW. */
#define AUTH_MAX 8
#define AUTH_NAME_MAX 32
#define AUTH_HOME_MAX 64
#define AUTH_SHELL_MAX 24
#define AUTH_PASS_MAX 64
#define AUTH_HASH_HEX 9

#define AUTH_FLAG_SYSTEM 1u
#define AUTH_FLAG_LOCKED 2u

typedef struct {
    u32 uid, gid, flags;
    char name[AUTH_NAME_MAX];
    char home[AUTH_HOME_MAX];
    char shell[AUTH_SHELL_MAX];
    char salt[AUTH_HASH_HEX];
    char hash[AUTH_HASH_HEX];
    u8 has_password, failures;
    u32 lock_until;
} auth_user_t;

void auth_init(void);
int auth_count(void);
const auth_user_t* auth_at(int index);
const auth_user_t* auth_find(const char* name);
const auth_user_t* auth_by_uid(u32 uid);
const auth_user_t* auth_current(void);
u32 auth_current_uid(void);
int auth_verify(const char* name, const char* password);
int auth_needs_password(const char* name);
int auth_add(const char* name, const char* password, u32 flags);
int auth_set_password(const char* name, const char* password);
int auth_set_locked(const char* name, int locked);
int auth_save(void);
/* Root-owned system namespace. */
int auth_is_system_path(const char* absolute);
const char* auth_name_for_uid(u32 uid);
#endif
