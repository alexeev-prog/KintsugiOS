#include "authabi.h"
#include "desktopabi.h"
#include "ktuilib.h"
#include "runtime.h"
#include "sysabi.h"

/* PID 1: root service supervisor. It presents a clean full-screen TUI login,
 * then spawns a shell with the selected account's credentials. init itself
 * keeps uid 0 so it can always supervise and present a fresh login after
 * logout. */

#define USERS_MAX 8
#define NAME_MAX 32
#define HOME_MAX 64

typedef struct {
    char name[NAME_MAX];
    char home[HOME_MAX];
    u32 uid;
} account_t;

static account_t accounts[USERS_MAX];
static int account_count;

static u32 length(const char* text) {
    u32 n = 0;
    while (text[n]) {
        ++n;
    }
    return n;
}

static int equal(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static char lower_char(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static int equal_ci(const char* a, const char* b) {
    while (*a && lower_char(*a) == lower_char(*b)) {
        ++a;
        ++b;
    }
    return lower_char(*a) == lower_char(*b);
}

static void copy(char* out, const char* text, u32 capacity) {
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

static int read_text(const char* path, char* buffer, u32 capacity) {
    int result = syscall(2, (u32)path, 0, (u32)buffer, capacity - 1);
    if (result < 0) {
        return result;
    }
    buffer[result] = 0;
    return result;
}

static void trim(char* text) {
    u32 n = length(text);
    while (n && (text[n - 1] == '\n' || text[n - 1] == '\r' || text[n - 1] == ' ')) {
        text[--n] = 0;
    }
}

static void load_accounts(void) {
    char data[1024];
    account_count = 0;
    if (read_text("/etc/PASSWD", data, sizeof(data)) <= 0) {
        copy(accounts[0].name, "user", NAME_MAX);
        copy(accounts[0].home, "/home/user", HOME_MAX);
        accounts[0].uid = 1000;
        account_count = 1;
        return;
    }
    char* line = data;
    while (*line && account_count < USERS_MAX) {
        char* end = line;
        while (*end && *end != '\n' && *end != '\r') {
            ++end;
        }
        char saved = *end;
        *end = 0;
        if (*line) {
            char* fields[6];
            int count = 0;
            char* p = line;
            while (count < 6) {
                fields[count++] = p;
                while (*p && *p != ':') {
                    ++p;
                }
                if (!*p) {
                    break;
                }
                *p++ = 0;
            }
            if (count >= 4 && fields[0][0]) {
                account_t* account = &accounts[account_count++];
                copy(account->name, fields[0], NAME_MAX);
                account->uid = 0;
                for (const char* d = fields[1]; *d >= '0' && *d <= '9'; ++d) {
                    account->uid = account->uid * 10 + (u32)(*d - '0');
                }
                copy(account->home, fields[3], HOME_MAX);
            }
        }
        if (!saved) {
            break;
        }
        line = end + 1;
    }
    if (!account_count) {
        copy(accounts[0].name, "user", NAME_MAX);
        copy(accounts[0].home, "/home/user", HOME_MAX);
        accounts[0].uid = 1000;
        account_count = 1;
    }
}

static int password_required(const char* name) {
    return syscall(KOS_SYS_NEEDAUTH, (u32)name, 0, 0, 0) != 0;
}

/* Volume is a file like any other setting: /etc/volume.cfg is applied before
 * the login TUI, so SoundCtl/volumectl changes survive a reboot. */
static void load_volume(void) {
    char text[16];
    if (read_text("/etc/volume.cfg", text, sizeof(text)) <= 0) {
        return;
    }
    trim(text);
    unsigned value = 0;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return;
        }
        unsigned digit = (unsigned)(*p - '0');
        if (value > (100u - digit) / 10u) {
            return;
        }
        value = value * 10 + digit;
    }
    syscall(KOS_SYS_SOUND_VOLUME, value, 0, 0, 0);
}

/* The entire screen is owned by the login TUI: no boot log, no shell banner,
 * no status panel. The mask hides the password while still using the shared
 * input editor for UTF-8-safe editing keys. */
static void draw_login(
    int selected, const char* message, int bad, int password_mode, u32 password_length, u32 cursor) {
    tui_clear(TUI_DESKTOP);
    tui_bar(0, "KintsugiOS - Login");
    tui_box(16, 3, 48, 18, "Account", TUI_BORDER);
    tui_text(20, 5, "Select an account:", TUI_KEYWORD);
    for (int i = 0; i < account_count; ++i) {
        int y = 7 + i;
        unsigned color = i == selected ? TUI_FOCUS : TUI_STATUS;
        tui_text(19, y, i == selected ? ">" : " ", i == selected ? TUI_VISUAL : TUI_STATUS);
        tui_text(22, y, accounts[i].name, color);
        tui_number(54, y, accounts[i].uid, TUI_COMMENT);
    }
    if (password_mode) {
        tui_text(20, 16, "Password:", TUI_KEYWORD);
        tui_fill(31, 16, 28, 1, TUI_INPUT);
        for (u32 i = 0; i < password_length && i < 28; ++i) {
            tui_text(31 + (int)i, 16, "*", TUI_INPUT);
        }
        tui_text(31 + (int)(cursor < 28 ? cursor : 27), 16, "_", TUI_FOCUS);
    } else {
        tui_text(20, 16, "Up/Down: select   Enter: log in", TUI_COMMENT);
    }
    if (message && *message) {
        tui_text(20, 18, message, bad ? TUI_ERROR : TUI_WARNING);
    }
    tui_bar(23, "KFS3 POSIX layer | /etc/passwd");
    tui_present(79, 24);
}

static int wait_for_account(void) {
    int selected = 0;
    char last[NAME_MAX] = "";
    if (read_text("/etc/LASTUSER", last, sizeof(last)) > 0) {
        trim(last);
        for (int i = 0; i < account_count; ++i) {
            if (equal(accounts[i].name, last)) {
                selected = i;
            }
        }
    }
    for (;;) {
        draw_login(selected, "", 0, 0, 0, 0);
        int key = syscall(8, 1, 0, 0, 0);
        if (key == TUI_UP) {
            selected = selected ? selected - 1 : account_count - 1;
        } else if (key == TUI_DOWN) {
            selected = selected + 1 < account_count ? selected + 1 : 0;
        } else if (key == '\n' || key == '\r' || key == ' ') {
            return selected;
        }
    }
}

static int authenticate(int selected) {
    account_t* account = &accounts[selected];
    if (!password_required(account->name)) {
        return 1;
    }
    char password[64];
    for (int attempt = 0; attempt < 3; ++attempt) {
        tui_input input = { password, sizeof(password), 0 };
        password[0] = 0;
        for (;;) {
            draw_login(selected, "Password:", 0, 1, length(password), input.cursor);
            int key = syscall(8, 1, 0, 0, 0);
            if (key == 27 || key == 3) {
                return 0;
            }
            if (key == '\n' || key == '\r') {
                break;
            }
            tui_input_key(&input, key);
        }
        if (!syscall(KOS_SYS_AUTH, (u32)account->name, (u32)password, 0, 0)) {
            return 1;
        }
        draw_login(selected, "Login incorrect. Try again.", 1, 0, 0, 0);
        syscall(KOS_SYS_SLEEP, 30, 0, 0, 0);
    }
    draw_login(selected, "Login incorrect.", 1, 0, 0, 0);
    syscall(KOS_SYS_SLEEP, 60, 0, 0, 0);
    return 0;
}

static void log_in(int selected) {
    account_t* account = &accounts[selected];
    /* Root asks the kernel for a one-shot credential drop for the child. */
    if (syscall(KOS_SYS_ELEVATE, (u32)account->name, (u32) "", 0, 0)) {
        syscall(KOS_SYS_CANCEL_ELEVATE, 0, 0, 0, 0);
        return;
    }
    char path[64];
    copy(path, account->home, sizeof(path));
    syscall(11, (u32)path, 3, 0, 0);
    setenv("USER", account->name);
    setenv("LOGNAME", account->name);
    setenv("HOME", account->home);
    setenv("SHELL", "/bin/SHELL.ELF");
    setenv("PATH", "/bin:/usr/bin");
    if (account->uid == 0) {
        /* root's home is /root; keep the POSIX alias visible to programs. */
        setenv("HOME", "/root");
        syscall(11, (u32) "/root", 3, 0, 0);
    }
    syscall(10, (u32) "/etc/LASTUSER", (u32)account->name, length(account->name), 0);
    /* Hand a clean terminal to the shell: the login screen must not remain
     * behind the prompt after authentication. */
    tui_clear(0x07);
    tui_present(0, 0);
    char* args[] = { "shell", 0 };
    int pid = spawn("/bin/SHELL.ELF", 1, args);
    if (pid >= 0) {
        wait(pid);
    }
    syscall(11, (u32) "/", 3, 0, 0);
    setenv("USER", "root");
    setenv("HOME", "/root");
}

/* PID 1 owns the service supervisor: it is root and gets its own process
 * group, so it can run daemons without competing with the login TUI. */
static int ksersys_pid;

static void ensure_ksersys(void) {
    if (ksersys_pid > 0) {
        kos_process_info list[16];
        int count = syscall(16, (u32)list, 16, 0, 0);
        for (int i = 0; i < count; ++i) {
            if (list[i].pid == (unsigned)ksersys_pid && list[i].state != 4
                && equal_ci(list[i].name, "ksersys")) {
                return;
            }
        }
    }
    char* args[] = { "ksersys", "--supervise", 0 };
    ksersys_pid = spawn("/bin/KSERSYS.ELF", 2, args);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    unsigned char settings[5] = { 1, 5, 1, 0, 1 }; /* theme 5 = Mono, font size 100% */
    int loaded = syscall(2, (u32) "/etc/VIDEO.CFG", 0, (u32)settings, sizeof(settings));
    if (loaded >= 1 && settings[0] < KOS_VIDEO_MODES) {
        syscall(KOS_SYS_DISPLAY, settings[0], 0, 0, 0);
    }
    if (loaded >= 2 && settings[1] < KOS_FRAMEBUFFER_THEMES) {
        syscall(KOS_SYS_FRAMEBUFFER_THEME, settings[1], 0, 0, 0);
    }
    if (loaded >= 3 && settings[2] < KOS_FRAMEBUFFER_SCALES) {
        syscall(KOS_SYS_FRAMEBUFFER_SCALE, settings[2], 0, 0, 0);
    }
    if (loaded >= 4 && settings[3] < KOS_FRAMEBUFFER_FONTS) {
        syscall(KOS_SYS_FRAMEBUFFER_FONT, settings[3], 0, 0, 0);
    }
    if (loaded >= 5 && settings[4] < KOS_FRAMEBUFFER_FONT_SIZES) {
        syscall(KOS_SYS_FRAMEBUFFER_FONT_SIZE, settings[4], 0, 0, 0);
    }
    load_volume();
    load_accounts();
    for (;;) {
        ensure_ksersys();
        int selected = wait_for_account();
        if (!authenticate(selected)) {
            continue;
        }
        log_in(selected);
        syscall(32, 0, 0, 0, 0);
    }
}
