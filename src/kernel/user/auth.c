#include "auth.h"

#include "../../user/ioabi.h"
#include "../fs/kfs3.h"
#include "../fs/vfs.h"
#include "../kernel/log.h"
#include "../kklibc/kklibc.h"
#include "process.h"

#define AUTH_PASSWD_PATH "/etc/PASSWD"
#define AUTH_SHADOW_PATH "/etc/SHADOW"
#define AUTH_FILE_MAX 2048

static auth_user_t users[AUTH_MAX];
static int user_count;
static u32 salt_counter = 0x13572468u;

static int text_length(const char* text, u32 limit) {
    u32 n = 0;
    while (n < limit && text[n]) {
        ++n;
    }
    return (int)n;
}

static int same(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int valid_name(const char* name) {
    if (!name || !*name) {
        return 0;
    }
    u32 n = 0;
    for (; name[n]; ++n) {
        char c = name[n];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return 0;
        }
        if (n >= AUTH_NAME_MAX - 1) {
            return 0;
        }
    }
    return n > 0;
}

static void copy_text(char* out, const char* text, u32 capacity) {
    u32 n = 0;
    if (!capacity) {
        return;
    }
    while (text[n] && n + 1 < capacity) {
        out[n] = text[n];
        ++n;
    }
    out[n] = 0;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void to_hex(char out[AUTH_HASH_HEX], u32 value) {
    static const char digits[] = "0123456789abcdef";
    for (int i = 7; i >= 0; --i) {
        out[i] = digits[value & 15];
        value >>= 4;
    }
    out[8] = 0;
}

static u32 from_hex(const char* text) {
    u32 value = 0;
    for (int i = 0; i < 8; ++i) {
        int digit = hex_value(text[i]);
        if (digit < 0) {
            return 0;
        }
        value = (value << 4) | (u32)digit;
    }
    return value;
}

/* Iterated salted digest. Slow enough to make casual offline guessing
 * unattractive while staying bounded for a 50 Hz single-CPU machine. */
static u32 digest(const char* password, const char* salt) {
    u32 h = 2166136261u;
    for (const char* p = salt; *p; ++p) {
        h ^= (u8)*p;
        h *= 16777619u;
    }
    for (u32 round = 0; round < 512; ++round) {
        for (const char* p = password; *p; ++p) {
            h ^= (u8)*p;
            h *= 16777619u;
            h ^= h >> 13;
        }
        h ^= round & 255;
        h *= 16777619u;
    }
    return h;
}

static void make_salt(auth_user_t* user, const char* name) {
    u32 h = 2166136261u;
    for (const char* p = name; *p; ++p) {
        h ^= (u8)*p;
        h *= 16777619u;
    }
    h ^= (u32)process_ticks();
    h += ++salt_counter * 2654435761u;
    to_hex(user->salt, h);
}

static void assign_password(auth_user_t* user, const char* password) {
    if (!password || !*password) {
        user->has_password = 0;
        to_hex(user->hash, 0);
        return;
    }
    u32 n = 0;
    while (password[n]) {
        ++n;
    }
    if (n > AUTH_PASS_MAX) {
        n = AUTH_PASS_MAX;
    }
    char bounded[AUTH_PASS_MAX + 1];
    memcpy(bounded, password, n);
    bounded[n] = 0;
    user->has_password = 1;
    to_hex(user->hash, digest(bounded, user->salt));
}

static int read_file(const char* path, char* buffer, u32 capacity) {
    vfs_stat_t stat;
    int result = vfs_stat(path, &stat);
    if (result) {
        return result;
    }
    if (stat.directory || stat.size >= capacity) {
        return VFS_EINVAL;
    }
    result = vfs_read(path, 0, buffer, stat.size);
    if (result < 0) {
        return result;
    }
    buffer[result] = 0;
    return result;
}

/* Split one colon-separated record in place. Returns field count. */
static int split(char* line, char** fields, int capacity) {
    int count = 0;
    char* p = line;
    while (count < capacity) {
        fields[count++] = p;
        while (*p && *p != ':') {
            ++p;
        }
        if (!*p) {
            break;
        }
        *p++ = 0;
    }
    return count;
}

static u32 parse_u32(const char* text) {
    u32 value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (u32)(*text - '0');
        ++text;
    }
    return value;
}

static void defaults(void) {
    memset(users, 0, sizeof(users));
    user_count = 2;
    users[0].uid = 0;
    users[0].gid = 0;
    users[0].flags = AUTH_FLAG_SYSTEM;
    copy_text(users[0].name, "root", AUTH_NAME_MAX);
    copy_text(users[0].home, "/root", AUTH_HOME_MAX);
    copy_text(users[0].shell, "/bin/shell", AUTH_SHELL_MAX);
    make_salt(&users[0], "root");
    assign_password(&users[0], "root");

    users[1].uid = 1000;
    users[1].gid = 1000;
    users[1].flags = 0;
    copy_text(users[1].name, "user", AUTH_NAME_MAX);
    copy_text(users[1].home, "/home/user", AUTH_HOME_MAX);
    copy_text(users[1].shell, "/bin/shell", AUTH_SHELL_MAX);
    make_salt(&users[1], "user");
    assign_password(&users[1], "");
}

void auth_init(void) {
    char passwd[AUTH_FILE_MAX], shadow[AUTH_FILE_MAX];
    memset(users, 0, sizeof(users));
    user_count = 0;
    int have_passwd = read_file(AUTH_PASSWD_PATH, passwd, sizeof(passwd)) >= 0;
    int have_shadow = read_file(AUTH_SHADOW_PATH, shadow, sizeof(shadow)) >= 0;
    if (have_passwd) {
        char* line = passwd;
        while (*line && user_count < AUTH_MAX) {
            char* end = line;
            while (*end && *end != '\n' && *end != '\r') {
                ++end;
            }
            char saved = *end;
            *end = 0;
            char* fields[6];
            int n = split(line, fields, 6);
            if (n >= 4 && valid_name(fields[0])) {
                auth_user_t* user = &users[user_count++];
                copy_text(user->name, fields[0], AUTH_NAME_MAX);
                user->uid = parse_u32(fields[1]);
                user->gid = parse_u32(fields[2]);
                copy_text(user->home, fields[3], AUTH_HOME_MAX);
                if (n >= 5 && *fields[4]) {
                    copy_text(user->shell, fields[4], AUTH_SHELL_MAX);
                } else {
                    copy_text(user->shell, "/bin/shell", AUTH_SHELL_MAX);
                }
                user->flags = n >= 6 ? parse_u32(fields[5]) : 0;
                make_salt(user, user->name);
            }
            if (!saved) {
                break;
            }
            line = end + 1;
        }
    }
    if (have_shadow) {
        char* line = shadow;
        while (*line) {
            char* end = line;
            while (*end && *end != '\n' && *end != '\r') {
                ++end;
            }
            char saved = *end;
            *end = 0;
            char* fields[3];
            int n = split(line, fields, 3);
            if (n >= 3) {
                auth_user_t* user = 0;
                for (int i = 0; i < user_count; ++i) {
                    if (same(users[i].name, fields[0])) {
                        user = &users[i];
                        break;
                    }
                }
                if (user) {
                    copy_text(user->salt, fields[1], AUTH_HASH_HEX);
                    copy_text(user->hash, fields[2], AUTH_HASH_HEX);
                    user->has_password = fields[2][0] != 0;
                    if (!user->salt[0]) {
                        make_salt(user, user->name);
                    }
                }
            }
            if (!saved) {
                break;
            }
            line = end + 1;
        }
    }
    if (!user_count || !auth_find("root") || !auth_find("user")) {
        defaults();
        auth_save();
    } else if (!have_shadow) {
        /* Never leave accounts without a credential store: an absent
         * /etc/SHADOW must not silently turn passwords off. */
        auth_user_t* root = (auth_user_t*)auth_find("root");
        auth_user_t* user = (auth_user_t*)auth_find("user");
        if (root) {
            make_salt(root, "root");
            assign_password(root, "root");
        }
        if (user) {
            make_salt(user, "user");
            assign_password(user, "");
        }
        auth_save();
    }
}

int auth_save(void) {
    char passwd[AUTH_FILE_MAX], shadow[AUTH_FILE_MAX];
    u32 p = 0, s = 0;
    passwd[0] = 0;
    shadow[0] = 0;
    for (int i = 0; i < user_count; ++i) {
        auth_user_t* user = &users[i];
        int n = snprintf(
            passwd + p,
            sizeof(passwd) - p,
            "%s:%u:%u:%s:%s:%u\n",
            user->name,
            user->uid,
            user->gid,
            user->home,
            user->shell,
            user->flags);
        if (n < 0 || (u32)n >= sizeof(passwd) - p) {
            return VFS_ENOSPC;
        }
        p += (u32)n;
        n = snprintf(
            shadow + s,
            sizeof(shadow) - s,
            "%s:%s:%s\n",
            user->name,
            user->salt,
            user->has_password ? user->hash : "");
        if (n < 0 || (u32)n >= sizeof(shadow) - s) {
            return VFS_ENOSPC;
        }
        s += (u32)n;
    }
    int result = vfs_replace(AUTH_PASSWD_PATH, passwd, p);
    if (result) {
        return result;
    }
    return vfs_replace(AUTH_SHADOW_PATH, shadow, s);
}

int auth_count(void) {
    return user_count;
}

const auth_user_t* auth_at(int index) {
    return index >= 0 && index < user_count ? &users[index] : 0;
}

const auth_user_t* auth_find(const char* name) {
    if (!name) {
        return 0;
    }
    for (int i = 0; i < user_count; ++i) {
        if (same(users[i].name, name)) {
            return &users[i];
        }
    }
    return 0;
}

const auth_user_t* auth_by_uid(u32 uid) {
    for (int i = 0; i < user_count; ++i) {
        if (users[i].uid == uid) {
            return &users[i];
        }
    }
    return 0;
}

const char* auth_name_for_uid(u32 uid) {
    const auth_user_t* user = auth_by_uid(uid);
    return user ? user->name : "nobody";
}

const auth_user_t* auth_current(void) {
    return auth_by_uid(process_euid());
}

u32 auth_current_uid(void) {
    return process_euid();
}

int auth_verify(const char* name, const char* password) {
    auth_user_t* user = (auth_user_t*)auth_find(name);
    if (!user || (user->flags & AUTH_FLAG_LOCKED)) {
        return KOS_EPERM;
    }
    if (user->lock_until && process_ticks() < user->lock_until) {
        return KOS_EPERM;
    }
    int ok;
    if (!user->has_password) {
        ok = !password || !*password;
    } else {
        if (!password) {
            password = "";
        }
        u32 n = 0;
        while (password[n]) {
            ++n;
        }
        if (n > AUTH_PASS_MAX) {
            return KOS_EPERM;
        }
        char bounded[AUTH_PASS_MAX + 1];
        memcpy(bounded, password, n);
        bounded[n] = 0;
        u32 expected = from_hex(user->hash);
        u32 actual = digest(bounded, user->salt);
        /* Constant-time comparison. */
        u32 diff = expected ^ actual;
        ok = diff == 0;
    }
    if (ok) {
        user->failures = 0;
        user->lock_until = 0;
        return 0;
    }
    if (user->failures < 255) {
        ++user->failures;
    }
    if (user->failures >= 5) {
        user->lock_until = process_ticks() + 500;
        user->failures = 0;
    }
    return KOS_EPERM;
}

/* Side-effect free probe used by the login screen: does this account require
 * a password at all? Never counts as a failed authentication attempt. */
int auth_needs_password(const char* name) {
    const auth_user_t* user = auth_find(name);
    return user && user->has_password ? 1 : 0;
}

static int next_uid(void) {
    u32 uid = 1000;
    for (;;) {
        int used = 0;
        for (int i = 0; i < user_count; ++i) {
            if (users[i].uid == uid) {
                used = 1;
                break;
            }
        }
        if (!used) {
            return (int)uid;
        }
        ++uid;
    }
}

int auth_add(const char* name, const char* password, u32 flags) {
    if (!valid_name(name) || auth_find(name)) {
        return auth_find(name) ? VFS_EEXIST : VFS_EINVAL;
    }
    if (user_count >= AUTH_MAX) {
        return VFS_ENOSPC;
    }
    auth_user_t* user = &users[user_count];
    memset(user, 0, sizeof(*user));
    user->uid = (u32)next_uid();
    user->gid = user->uid;
    user->flags = flags;
    copy_text(user->name, name, AUTH_NAME_MAX);
    snprintf(user->home, AUTH_HOME_MAX, "/home/%s", name);
    copy_text(user->shell, "/bin/shell", AUTH_SHELL_MAX);
    make_salt(user, name);
    assign_password(user, password);
    ++user_count;
    int result = auth_save();
    if (result) {
        --user_count;
        return result;
    }
    kfs3_ensure_home(name);
    char message[96];
    snprintf(message, sizeof(message), "[AUTH] useradd %s uid=%u\n", name, user->uid);
    log_write(message);
    return (int)user->uid;
}

int auth_set_password(const char* name, const char* password) {
    auth_user_t* user = (auth_user_t*)auth_find(name);
    if (!user) {
        return VFS_ENOENT;
    }
    if (!password || !*password) {
        return VFS_EINVAL;
    }
    u32 n = 0;
    while (password[n]) {
        ++n;
    }
    if (n > AUTH_PASS_MAX) {
        return VFS_EINVAL;
    }
    assign_password(user, password);
    user->failures = 0;
    user->lock_until = 0;
    log_event("INFO", "auth", "password updated for %s", name);
    return auth_save();
}

int auth_set_locked(const char* name, int locked) {
    auth_user_t* user = (auth_user_t*)auth_find(name);
    if (!user) {
        return VFS_ENOENT;
    }
    if (locked) {
        user->flags |= AUTH_FLAG_LOCKED;
    } else {
        user->flags &= ~AUTH_FLAG_LOCKED;
    }
    return auth_save();
}

int auth_is_system_path(const char* absolute) {
    static const char* const roots[] = { "/etc", "/bin", "/usr", "/boot", "/var", "/root", "/man" };
    for (u32 i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        u32 n = text_length(roots[i], 64);
        if (!strncmp(absolute, roots[i], n) && (absolute[n] == '/' || !absolute[n])) {
            return 1;
        }
    }
    return 0;
}
