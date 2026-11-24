#include "ktuilib.h"
#include "sysabi.h"
#include "authabi.h"

/* KServices - service manager, account browser and scheduler view.
 * System services are root-only; the kernel enforces this again for any
 * privileged action. */

typedef struct {
    const char* name;
    const char* path;
    const char* desc;
    int system;
    int running;
} service_t;

static service_t services[] = {
    { "login",     "/bin/INIT.ELF",    "Session supervisor",   1, 0 },
    { "diskman",   "/bin/KDISKMAN.ELF", "Disk management",     1, 0 },
    { "sysmon",    "/bin/KINFO.ELF",   "System monitor",       1, 0 },
    { "shell",     "/bin/SHELL.ELF",   "Command interpreter",  0, 0 },
    { "mixer",     "/bin/SOUNDCTL.ELF", "Audio mixer",         0, 0 },
    { "kmusa",     "/bin/KMUSA.ELF",   "Music generator",      0, 0 },
    { "terminal",  "/bin/KTERM.ELF",   "Terminal session",     0, 0 },
    { "workspace", "/bin/KIWI.ELF",    "Window workspace",     0, 0 }
};
#define NSERVICES (sizeof(services) / sizeof(services[0]))

typedef struct {
    char name[32];
    char home[64];
    u32 uid, gid, flags;
} account_t;

static account_t accounts[8];
static int account_count;

static u32 current_uid(void) {
    return (u32)syscall(KOS_SYS_GETUID, 0, 0, 0, 0);
}

static const char* current_name(void) {
    static char name[32];
    name[0] = 0;
    if (syscall(KOS_SYS_GETUSER, (u32)name, sizeof(name), 0, 0)) tui_copy(name, "user", sizeof(name));
    return name;
}

static u32 parse_u32(const char* text) {
    u32 value = 0;
    while (*text >= '0' && *text <= '9') value = value * 10 + (u32)(*text++ - '0');
    return value;
}

static void load_accounts(void) {
    account_count = 0;
    char data[1024];
    int n = syscall(2, (u32)"/etc/PASSWD", 0, (u32)data, sizeof(data) - 1);
    if (n <= 0) return;
    data[n] = 0;
    char* line = data;
    while (*line && account_count < 8) {
        char* end = line;
        while (*end && *end != '\n' && *end != '\r') ++end;
        char saved = *end;
        *end = 0;
        if (*line) {
            char* fields[6];
            int count = 0;
            char* p = line;
            while (count < 6) {
                fields[count++] = p;
                while (*p && *p != ':') ++p;
                if (!*p) break;
                *p++ = 0;
            }
            if (count >= 4 && fields[0][0]) {
                account_t* account = &accounts[account_count++];
                tui_copy(account->name, fields[0], sizeof(account->name));
                account->uid = parse_u32(fields[1]);
                account->gid = parse_u32(fields[2]);
                tui_copy(account->home, fields[3], sizeof(account->home));
                account->flags = count >= 6 ? parse_u32(fields[5]) : 0;
            }
        }
        if (!saved) break;
        line = end + 1;
    }
}

static void draw_services(int first, int count, int selected, int root, const char* status) {
    tui_box(3, 4, 74, 16, "Services - system entries require root", TUI_BORDER);
    tui_text(6, 5, "NAME", TUI_KEYWORD);
    tui_text(19, 5, "SCOPE", TUI_KEYWORD);
    tui_text(29, 5, "DESCRIPTION", TUI_KEYWORD);
    for (int i = 0; i < count; ++i) {
        int index = first + i;
        int y = 7 + i * 2;
        unsigned color = i == selected ? TUI_VISUAL : TUI_STATUS;
        tui_text(4, y, i == selected ? ">" : " ", color);
        tui_text(6, y, services[index].name, color);
        tui_text(19, y, services[index].system ? "system" : "user",
                 services[index].system ? TUI_WARNING : TUI_SUCCESS);
        tui_text(29, y, services[index].desc, 0x1f);
        tui_text(56, y, services[index].path, TUI_COMMENT);
        tui_text(74, y, services[index].running ? "*" : " ", TUI_SUCCESS);
    }
    if (!root) tui_text(6, 18, "Read-only view: run 'sugo kservices' for system actions.", TUI_COMMENT);
    tui_bar(22, status);
    tui_bar(23, root ? "Enter launch  S system  U user  A accounts  P scheduler  Q quit"
                     : "S system  U user  A accounts  P scheduler  Q quit");
}

static void draw_accounts(int selected, const char* status) {
    tui_box(3, 4, 74, 16, "Accounts - /etc/passwd", TUI_BORDER);
    tui_text(6, 5, "USER", TUI_KEYWORD);
    tui_text(22, 5, "UID", TUI_KEYWORD);
    tui_text(30, 5, "GID", TUI_KEYWORD);
    tui_text(38, 5, "HOME", TUI_KEYWORD);
    tui_text(64, 5, "TYPE", TUI_KEYWORD);
    if (!account_count) tui_text(6, 7, "Cannot read /etc/passwd", TUI_ERROR);
    for (int i = 0; i < account_count; ++i) {
        int y = 7 + i * 2;
        unsigned color = i == selected ? TUI_VISUAL : TUI_STATUS;
        tui_text(5, y, i == selected ? ">" : " ", color);
        tui_text(6, y, accounts[i].name, color);
        tui_number(22, y, accounts[i].uid, TUI_NUMBER);
        tui_number(30, y, accounts[i].gid, TUI_NUMBER);
        tui_text(38, y, accounts[i].home, TUI_COMMENT);
        tui_text(64, y, accounts[i].uid == 0 ? "root" : "user", accounts[i].uid == 0 ? TUI_WARNING : TUI_SUCCESS);
        if (accounts[i].uid == current_uid()) tui_text(72, y, "you", TUI_KEYWORD);
    }
    tui_text(6, 18, "Passwords live in /etc/shadow (root-only, mode 0600).", TUI_COMMENT);
    tui_bar(22, status);
    tui_bar(23, "S services  U user  A accounts  P scheduler  Q quit");
}

static void draw_scheduler(const char* status) {
    tui_box(3, 4, 74, 16, "Scheduler - priority round robin", TUI_BORDER);
    kos_process_info list[16];
    int n = syscall(16, (u32)list, 16, 0, 0);
    if (n < 0) n = 0;
    tui_text(5, 6, "PID", TUI_KEYWORD);
    tui_text(11, 6, "UID", TUI_KEYWORD);
    tui_text(17, 6, "PPID", TUI_KEYWORD);
    tui_text(24, 6, "STATE", TUI_KEYWORD);
    tui_text(35, 6, "TICKS", TUI_KEYWORD);
    tui_text(45, 6, "NAME", TUI_KEYWORD);
    static const char* const states[] = { "-", "ready", "run", "wait", "zombie" };
    for (int i = 0; i < n && i < 11; ++i) {
        int y = 8 + i;
        tui_number(5, y, list[i].pid, TUI_NUMBER);
        tui_number(11, y, list[i].uid, list[i].uid == 0 ? TUI_WARNING : TUI_NUMBER);
        tui_number(17, y, list[i].parent, TUI_NUMBER);
        tui_text(24, y, list[i].state <= 4 ? states[list[i].state] : "?", TUI_STATUS);
        tui_number(35, y, list[i].ticks, TUI_NUMBER);
        tui_textn(45, y, list[i].name, 28, 0x1f);
    }
    tui_text(5, 18, "uid 0 processes are privileged; users cannot signal them.", TUI_COMMENT);
    tui_bar(22, status);
    tui_bar(23, "S services  U user  A accounts  P scheduler  Q quit");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    enum { TAB_SYSTEM, TAB_USER, TAB_ACCOUNTS, TAB_SCHEDULER };
    static const char* const labels[] = { "System", "User", "Accounts", "Scheduler" };
    int tab = 0, selected = 0;
    const char* status = "Ready";
    u32 root = current_uid() == 0;
    load_accounts();
    for (;;) {
        tui_clear(TUI_DESKTOP);
        tui_bar(0, "KServices - services / accounts / scheduler");
        tui_tabs(1, labels, 4, (unsigned)tab);
        tui_text(56, 2, "session:", TUI_COMMENT);
        tui_text(65, 2, current_name(), root ? TUI_WARNING : TUI_SUCCESS);
        int count = tab == TAB_SYSTEM ? 3 : tab == TAB_USER ? 5 : tab == TAB_ACCOUNTS ? account_count : 1;
        if (selected >= count) selected = count > 0 ? count - 1 : 0;
        if (tab == TAB_SYSTEM) draw_services(0, 3, selected, (int)root, status);
        else if (tab == TAB_USER) draw_services(3, 5, selected, (int)root, status);
        else if (tab == TAB_ACCOUNTS) draw_accounts(selected, status);
        else draw_scheduler(status);
        tui_status(24);
        tui_present(79, 24);
        int key = tui_key(1);
        if (key == 'q' || key == 27 || key == TUI_F10) return 0;
        if (key == 's' || key == 'S') { tab = TAB_SYSTEM; selected = 0; status = "System services"; continue; }
        if (key == 'u' || key == 'U') { tab = TAB_USER; selected = 0; status = "User services"; continue; }
        if (key == 'a' || key == 'A') { tab = TAB_ACCOUNTS; selected = 0; status = "Accounts"; continue; }
        if (key == 'p' || key == 'P') { tab = TAB_SCHEDULER; status = "Scheduler"; continue; }
        if (key == TUI_LEFT) { tab = (tab + 3) % 4; selected = 0; continue; }
        if (key == TUI_RIGHT || key == '\t') { tab = (tab + 1) % 4; selected = 0; continue; }
        if (tab == TAB_SYSTEM || tab == TAB_USER) {
            if (key == TUI_UP && selected) --selected;
            if (key == TUI_DOWN && selected + 1 < count) ++selected;
            if (key == '\n') {
                int index = tab == TAB_SYSTEM ? selected : selected + 3;
                if (services[index].system && !root) {
                    status = "Permission denied: system service needs root (sugo kservices)";
                    tui_sound(2);
                } else if (services[index].running) {
                    status = "Service already running";
                } else {
                    services[index].running = 1;
                    status = "Launching service...";
                    tui_sound(1);
                    tui_clear(TUI_DESKTOP);
                    tui_present(79, 24);
                    int result = tui_launch(services[index].path, 0);
                    services[index].running = 0;
                    status = result < 0 ? "Service could not be launched" : "Service finished";
                }
            }
        } else if (tab == TAB_ACCOUNTS) {
            if (key == TUI_UP && selected) --selected;
            if (key == TUI_DOWN && selected + 1 < count) ++selected;
        }
    }
}
