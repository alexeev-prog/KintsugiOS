#include "ktuilib.h"
#include "authabi.h"
#include "sysabi.h"

/* Kintsugi Service System (ksersys).
 *
 * Definitions live in /etc/services as plain text, in the Unix tradition:
 *   name path scope account autostart restart enabled [args...]
 * The kernel keeps running daemons as ordinary processes. ksersys supervises
 * them: it starts enabled autostart entries, reaps them and restarts those
 * marked for restart. The interactive window and the CLI operate on the same
 * file, so changes are persistent across boots. */

#ifndef SERVICE_DAEMON

#define MAX_SERVICES 16
#define NAME_LEN 24
#define PATH_LEN 96
#define ACCOUNT_LEN 16
#define ARGS_LEN 128

typedef struct {
    char name[NAME_LEN];
    char path[PATH_LEN];
    char account[ACCOUNT_LEN];
    char args[ARGS_LEN];
    int system, autostart, restart, enabled;
    int pid;
} service_def;

static service_def services[MAX_SERVICES];
static int service_count;
static int supervisor_stdout = -1;

static unsigned uid_now(void) { return (unsigned)syscall(KOS_SYS_GETUID, 0, 0, 0, 0); }

static void print(const char* text) {
    u32 n = 0;
    while (text[n]) ++n;
    write_all(1, text, n);
}

static void print_number(unsigned value) {
    char digits[11];
    int n = 10;
    digits[n] = 0;
    do { digits[--n] = (char)('0' + value % 10); value /= 10; } while (value);
    print(digits + n);
}

static int same(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static unsigned parse_number(const char* text) {
    unsigned value = 0;
    for (const char* p = text; *p >= '0' && *p <= '9'; ++p) {
        unsigned digit = (unsigned)(*p - '0');
        if (value > (0xfffffffu - digit) / 10u) return 0;
        value = value * 10 + digit;
    }
    return value;
}

static char* skip_spaces(char* p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

static int split_fields(char* line, char** fields, int capacity) {
    int count = 0;
    line = skip_spaces(line);
    while (*line && count < capacity) {
        fields[count++] = line;
        while (*line && *line != ' ' && *line != '\t') ++line;
        if (!*line) break;
        *line++ = 0;
        line = skip_spaces(line);
    }
    return count;
}

static void append(char* out, u32 capacity, u32* used, const char* text) {
    while (*text && *used + 1 < capacity) out[(*used)++] = *text++;
    out[*used] = 0;
}

static void append_uint(char* out, u32 capacity, u32* used, unsigned value) {
    char digits[11];
    int n = 10;
    digits[n] = 0;
    do { digits[--n] = (char)('0' + value % 10); value /= 10; } while (value);
    append(out, capacity, used, digits + n);
}

static int account_exists(const char* name) {
    if (!name || !*name) return 0;
    char data[1024];
    int size = syscall(2, (u32)"/etc/passwd", 0, (u32)data, sizeof(data) - 1);
    if (size <= 0) return 0;
    data[size] = 0;
    char* line = data;
    while (*line) {
        char* end = line;
        while (*end && *end != '\n' && *end != '\r') ++end;
        char saved = *end;
        *end = 0;
        char* colon = line;
        while (*colon && *colon != ':') ++colon;
        char was = *colon;
        *colon = 0;
        int found = same(line, name);
        *colon = was;
        if (found) return 1;
        if (!saved) break;
        line = end + 1;
    }
    return 0;
}

static int load_services(void) {
    char data[4096];
    int size = syscall(2, (u32)"/etc/services", 0, (u32)data, sizeof(data) - 1);
    service_count = 0;
    if (size < 0) return 0;
    data[size] = 0;
    char* line = data;
    while (*line && service_count < MAX_SERVICES) {
        char* end = line;
        while (*end && *end != '\n' && *end != '\r') ++end;
        char saved = *end;
        *end = 0;
        char* fields[12];
        char* p = skip_spaces(line);
        int count = (*p && *p != '#') ? split_fields(p, fields, 12) : 0;
        if (count >= 3) {
            service_def* s = &services[service_count++];
            tui_copy(s->name, fields[0], sizeof(s->name));
            tui_copy(s->path, fields[1], sizeof(s->path));
            s->system = same(fields[2], "system");
            if (count >= 4) tui_copy(s->account, fields[3], sizeof(s->account));
            else tui_copy(s->account, s->system ? "root" : "user", sizeof(s->account));
            s->autostart = count >= 5 ? (parse_number(fields[4]) != 0) : 0;
            s->restart = count >= 6 ? (parse_number(fields[5]) != 0) : 0;
            s->enabled = count >= 7 ? (parse_number(fields[6]) != 0) : 1;
            s->pid = 0;
            s->args[0] = 0;
            for (int i = 7; i < count; ++i) {
                u32 used = 0;
                while (s->args[used]) ++used;
                if (used) append(s->args, sizeof(s->args), &used, " ");
                append(s->args, sizeof(s->args), &used, fields[i]);
            }
        }
        if (!saved) break;
        line = end + 1;
    }
    return service_count;
}

static int save_services(void) {
    char data[4096];
    u32 used = 0;
    data[0] = 0;
    append(data, sizeof(data), &used, "# Kintsugi Service System definitions\n");
    append(data, sizeof(data), &used, "# name path scope account autostart restart enabled [args...]\n");
    for (int i = 0; i < service_count; ++i) {
        service_def* s = &services[i];
        append(data, sizeof(data), &used, s->name);
        append(data, sizeof(data), &used, " ");
        append(data, sizeof(data), &used, s->path);
        append(data, sizeof(data), &used, s->system ? " system " : " user ");
        append(data, sizeof(data), &used, s->account);
        append(data, sizeof(data), &used, " ");
        append_uint(data, sizeof(data), &used, (unsigned)s->autostart);
        append(data, sizeof(data), &used, " ");
        append_uint(data, sizeof(data), &used, (unsigned)s->restart);
        append(data, sizeof(data), &used, " ");
        append_uint(data, sizeof(data), &used, (unsigned)s->enabled);
        if (s->args[0]) { append(data, sizeof(data), &used, " "); append(data, sizeof(data), &used, s->args); }
        append(data, sizeof(data), &used, "\n");
    }
    if (uid_now() != 0) return -1;
    return syscall(10, (u32)"/etc/services", (u32)data, used, 0) < 0 ? -1 : 0;
}

static void service_log_path(service_def* s, char* out, u32 capacity) {
    tui_copy(out, "/var/log/", capacity);
    u32 used = 0;
    while (out[used]) ++used;
    append(out, capacity, &used, s->name);
    append(out, capacity, &used, ".log");
}

static void restore_stdout(void) {
    if (supervisor_stdout >= 0) {
        dup2(supervisor_stdout, 1);
        dup2(supervisor_stdout, 2);
    }
}

static int start_service(service_def* s) {
    if (s->pid > 0) return 0;
    if (uid_now() != 0 && s->system) return -1;
    if (!account_exists(s->account)) return -1;
    char current[32];
    current[0] = 0;
    syscall(KOS_SYS_GETUSER, (u32)current, sizeof(current), 0, 0);
    if (uid_now() != 0 && !same(s->account, current)) return -1;
    int drop = !same(s->account, "root") && !same(s->account, current);
    if (drop && syscall(KOS_SYS_ELEVATE, (u32)s->account, (u32)"", 0, 0)) {
        syscall(KOS_SYS_CANCEL_ELEVATE, 0, 0, 0, 0);
        return -1;
    }
    char log[160];
    service_log_path(s, log, sizeof(log));
    int log_fd = open(log, KOS_O_WRITE | KOS_O_CREATE | KOS_O_TRUNC);
    if (log_fd >= 0) { dup2(log_fd, 1); dup2(log_fd, 2); }
    char args_copy[ARGS_LEN];
    tui_copy(args_copy, s->args, sizeof(args_copy));
    char* argv[16];
    int argc = 0;
    argv[argc++] = s->name;
    char* p = args_copy;
    while (*p && argc < 15) {
        p = skip_spaces(p);
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') ++p;
        if (*p) *p++ = 0;
    }
    int pid = spawn(s->path, argc, argv);
    if (pid < 0) syscall(KOS_SYS_CANCEL_ELEVATE, 0, 0, 0, 0);
    else s->pid = pid;
    if (log_fd >= 0) close(log_fd);
    restore_stdout();
    return pid;
}

static int stop_service(service_def* s) {
    if (s->pid <= 0) return 0;
    int result = kill(s->pid);
    for (int attempt = 0; attempt < 30; ++attempt) {
        kos_process_info list[16];
        int count = syscall(16, (u32)list, 16, 0, 0);
        int alive = 0;
        for (int i = 0; i < count; ++i) {
            if (list[i].pid == (unsigned)s->pid && list[i].state != 4) { alive = 1; break; }
        }
        if (!alive) break;
        syscall(KOS_SYS_SLEEP, 20, 0, 0, 0);
    }
    wait(s->pid);
    s->pid = 0;
    return result;
}

/* Logging by replace is deliberate: redirecting fd 1 works for inherited
 * terminals, but a supervised daemon's own file object can lose its offset
 * after truncation. Rewriting a small text log is deterministic. */
static void log_line(const char* path, const char* text) {
    char data[2048];
    u32 used = 0;
    data[0] = 0;
    int size = syscall(2, (u32)path, 0, (u32)data, sizeof(data) - 1);
    if (size > 0) {
        used = (u32)size;
        data[used] = 0;
        while (used && data[used - 1] == '\n') --used;
        data[used] = 0;
    }
    if (used) append(data, sizeof(data), &used, "\n");
    append(data, sizeof(data), &used, text);
    append(data, sizeof(data), &used, "\n");
    syscall(10, (u32)path, (u32)data, used, 0);
}

/* The supervisor and the control UI are separate processes. A small status
 * file under /var/log is their shared Unix-style control plane. */
static void write_status(void) {
    char data[512];
    u32 used = 0;
    data[0] = 0;
    for (int i = 0; i < service_count; ++i) {
        if (services[i].pid <= 0) continue;
        append(data, sizeof(data), &used, services[i].name);
        append(data, sizeof(data), &used, " ");
        append_uint(data, sizeof(data), &used, (unsigned)services[i].pid);
        append(data, sizeof(data), &used, "\n");
    }
    syscall(10, (u32)"/var/log/ksersys.status", (u32)data, used, 0);
}

static service_def* find_service(const char* name);

static void load_status(void) {
    char data[512];
    int size = syscall(2, (u32)"/var/log/ksersys.status", 0, (u32)data, sizeof(data) - 1);
    if (size < 0) return;
    data[size] = 0;
    char* line = data;
    while (*line) {
        char* end = line;
        while (*end && *end != '\n' && *end != '\r') ++end;
        char saved = *end;
        *end = 0;
        char* fields[2];
        int count = split_fields(line, fields, 2);
        if (count == 2) {
            service_def* s = find_service(fields[0]);
            if (s) s->pid = (int)parse_number(fields[1]);
        }
        if (!saved) break;
        line = end + 1;
    }
}

static void setup_supervisor_io(void) {
    int null_fd = open("/dev/null", KOS_O_READ);
    if (null_fd >= 0) { dup2(null_fd, 0); close(null_fd); }
    supervisor_stdout = open("/var/log/ksersys.log", KOS_O_WRITE | KOS_O_CREATE | KOS_O_TRUNC);
    if (supervisor_stdout >= 0) { dup2(supervisor_stdout, 1); dup2(supervisor_stdout, 2); }
}

static void supervise(void) {
    setup_supervisor_io();
    load_services();
    log_line("/var/log/ksersys.log", "[ksersys] supervisor started");
    for (int i = 0; i < service_count; ++i) {
        if (services[i].enabled && services[i].autostart) {
            int pid = start_service(&services[i]);
            if (pid > 0) {
                char message[64];
                u32 used = 0;
                append(message, sizeof(message), &used, "[ksersys] started ");
                append(message, sizeof(message), &used, services[i].name);
                log_line("/var/log/ksersys.log", message);
            }
        }
    }
    write_status();
    for (;;) {
        syscall(KOS_SYS_SLEEP, 50, 0, 0, 0);
        kos_process_info list[16];
        int count = syscall(16, (u32)list, 16, 0, 0);
        for (int i = 0; i < service_count; ++i) {
            service_def* s = &services[i];
            if (s->pid <= 0) continue;
            int state = -1;
            for (int j = 0; j < count; ++j) {
                if (list[j].pid == (unsigned)s->pid) { state = (int)list[j].state; break; }
            }
            if (state >= 0 && state != 4) continue;
            wait(s->pid);
            char message[64];
            u32 used = 0;
            append(message, sizeof(message), &used, "[ksersys] service exited: ");
            append(message, sizeof(message), &used, s->name);
            log_line("/var/log/ksersys.log", message);
            s->pid = 0;
            if (s->enabled && s->restart) start_service(s);
            write_status();
        }
    }
}

static void refresh_states(void) {
    kos_process_info list[16];
    int count = syscall(16, (u32)list, 16, 0, 0);
    for (int i = 0; i < service_count; ++i) {
        int alive = 0;
        for (int j = 0; j < count; ++j) {
            if (list[j].pid == (unsigned)services[i].pid && list[j].state != 4) { alive = 1; break; }
        }
        if (!alive) services[i].pid = 0;
    }
}

static void draw_manager(int selected, const char* status, unsigned uid) {
    tui_clear(TUI_DESKTOP);
    tui_bar(0, "KSerSys - Kintsugi Service System");
    tui_box(2, 3, 76, 17, "/etc/services", TUI_BORDER);
    tui_text(5, 5, "NAME", TUI_KEYWORD);
    tui_text(17, 5, "PATH", TUI_KEYWORD);
    tui_text(47, 5, "SCOPE", TUI_KEYWORD);
    tui_text(56, 5, "FLAGS", TUI_KEYWORD);
    tui_text(65, 5, "PID", TUI_KEYWORD);
    for (int i = 0; i < service_count; ++i) {
        service_def* s = &services[i];
        int y = 7 + i;
        unsigned color = i == selected ? TUI_FOCUS : TUI_STATUS;
        tui_text(4, y, i == selected ? ">" : " ", i == selected ? TUI_VISUAL : TUI_STATUS);
        tui_text(6, y, s->name, color);
        tui_textn(17, y, s->path, 28, color);
        tui_text(47, y, s->system ? "system" : "user", s->system ? TUI_WARNING : TUI_SUCCESS);
        tui_text(56, y, s->enabled ? "E" : "-", s->enabled ? TUI_SUCCESS : TUI_COMMENT);
        tui_text(58, y, s->autostart ? "A" : "-", s->autostart ? TUI_SUCCESS : TUI_COMMENT);
        tui_text(60, y, s->restart ? "R" : "-", s->restart ? TUI_SUCCESS : TUI_COMMENT);
        if (s->pid > 0) { tui_text(63, y, "*", TUI_SUCCESS); tui_number(66, y, (unsigned)s->pid, TUI_NUMBER); }
        else tui_text(65, y, "stopped", TUI_COMMENT);
    }
    if (!service_count) tui_text(6, 8, "No definitions. Press A to add a service.", TUI_COMMENT);
    tui_text(5, 16, "E enabled  A autostart  R restart  Enter start/stop  Q quit", TUI_COMMENT);
    if (uid) tui_text(5, 17, "Read-only session: root is required to change services.", TUI_WARNING);
    tui_bar(21, status);
    tui_bar(22, "ksersys: start / stop / status / enable / disable / add also work from the shell");
    tui_status(24);
    tui_present(79, 24);
}

static void interactive(void) {
    unsigned uid = uid_now();
    load_services();
    int selected = 0;
    const char* status = "Up/Down select service";
    for (;;) {
        load_status();
        refresh_states();
        if (selected >= service_count) selected = service_count ? service_count - 1 : 0;
        draw_manager(selected, status, uid);
        int key = tui_key(1);
        if (key == 'q' || key == 27 || key == TUI_F10) return;
        if (key == TUI_UP && selected) --selected;
        else if (key == TUI_DOWN && selected + 1 < service_count) ++selected;
        else if (key == TUI_SCROLL_UP && selected) --selected;
        else if (key == TUI_SCROLL_DOWN && selected + 1 < service_count) ++selected;
        else if (key == '\n' && service_count) {
            service_def* s = &services[selected];
            if (s->pid > 0) { stop_service(s); status = "Service stopped"; }
            else if (start_service(s) > 0) status = "Service started";
            else status = "Start failed (check account and path)";
        } else if (key == 'E' || key == 'e') {
            if (uid) status = "Root required";
            else if (service_count) { services[selected].enabled = !services[selected].enabled; save_services(); status = "Definition saved"; }
        } else if (key == 'A' || key == 'a') {
            if (uid) { status = "Root required"; continue; }
            if (service_count >= MAX_SERVICES) { status = "Service table full"; continue; }
            char name[NAME_LEN] = "", path[PATH_LEN] = "", scope[8] = "user", account[ACCOUNT_LEN] = "user";
            if (!tui_prompt("Service name:", name, sizeof(name))) continue;
            if (!tui_prompt("Program path:", path, sizeof(path))) continue;
            if (!tui_prompt("Scope (system/user):", scope, sizeof(scope))) continue;
            if (!same(scope, "system")) tui_copy(scope, "user", sizeof(scope));
            if (same(scope, "system")) {
                if (!tui_prompt("Run as:", account, sizeof(account))) continue;
            } else tui_copy(account, "user", sizeof(account));
            service_def* s = &services[service_count++];
            tui_copy(s->name, name, sizeof(s->name));
            tui_copy(s->path, path, sizeof(s->path));
            tui_copy(s->account, account, sizeof(s->account));
            s->system = same(scope, "system");
            s->autostart = s->restart = 0;
            s->enabled = 1;
            s->pid = 0;
            s->args[0] = 0;
            save_services();
            status = "Service added";
        } else if (key == 'R' || key == 'r') {
            if (uid) status = "Root required";
            else if (service_count) {
                services[selected].restart = !services[selected].restart;
                save_services();
                status = "Restart policy saved";
            }
        }
    }
}

static service_def* find_service(const char* name) {
    for (int i = 0; i < service_count; ++i) if (same(services[i].name, name)) return &services[i];
    return 0;
}

static void cli(int argc, char** argv) {
    load_services();
    if (argc < 2 || same(argv[1], "status")) {
        load_status();
        refresh_states();
        print("NAME                 PATH                                   SCOPE  PID\n");
        for (int i = 0; i < service_count; ++i) {
            service_def* s = &services[i];
            print(s->name);
            u32 n = 0; while (n < 21 - tui_len(s->name)) { write_all(1, " ", 1); ++n; }
            print(s->path);
            n = 0; while (n + tui_len(s->path) < 52) { write_all(1, " ", 1); ++n; }
            print(s->system ? "system " : "user   ");
            if (s->pid > 0) print_number((unsigned)s->pid);
            else print("-");
            print("\n");
        }
        return;
    }
    const char* command = argv[1];
    service_def* s = argc >= 3 ? find_service(argv[2]) : 0;
    if (!s) { print("ksersys: unknown service\n"); return; }
    if (same(command, "start")) {
        if (start_service(s) > 0) print("started\n"); else print("start failed\n");
    } else if (same(command, "stop")) {
        stop_service(s); print("stopped\n");
    } else if (same(command, "restart")) {
        stop_service(s);
        if (start_service(s) > 0) print("restarted\n"); else print("start failed\n");
    } else if (same(command, "enable") || same(command, "disable")) {
        if (uid_now()) { print("permission denied (root only)\n"); return; }
        s->enabled = same(command, "enable");
        save_services();
        print(s->enabled ? "enabled\n" : "disabled\n");
    } else if (same(command, "add")) {
        if (uid_now()) { print("permission denied (root only)\n"); return; }
        if (argc < 5 || service_count >= MAX_SERVICES) { print("usage: ksersys add NAME PATH [system|user] [account]\n"); return; }
        service_def* fresh = &services[service_count++];
        tui_copy(fresh->name, argv[2], sizeof(fresh->name));
        tui_copy(fresh->path, argv[3], sizeof(fresh->path));
        const char* scope = argc >= 4 ? argv[4] : "user";
        fresh->system = same(scope, "system");
        tui_copy(fresh->account, argc >= 5 ? argv[5] : (fresh->system ? "root" : "user"), sizeof(fresh->account));
        fresh->autostart = fresh->restart = 0;
        fresh->enabled = 1;
        fresh->pid = 0;
        fresh->args[0] = 0;
        save_services();
        print("added\n");
    } else {
        print("usage: ksersys [status|start|stop|restart|enable|disable|add]\n");
    }
}

int main(int argc, char** argv) {
    if (argc >= 2 && same(argv[1], "--supervise")) { supervise(); return 0; }
    if (argc >= 2) cli(argc, argv);
    else interactive();
    return 0;
}

#else /* SERVICE_DAEMON: a tiny example daemon used by /etc/services */

static void dappend(char* out, u32 capacity, u32* used, const char* text) {
    while (*text && *used + 1 < capacity) out[(*used)++] = *text++;
    out[*used] = 0;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    for (;;) {
        syscall(KOS_SYS_SLEEP, 100, 0, 0, 0);
        char data[1024];
        u32 used = 0;
        data[0] = 0;
        int size = syscall(2, (u32)"/var/log/clockd.log", 0, (u32)data, sizeof(data) - 1);
        if (size > 0) {
            used = (u32)size;
            data[used] = 0;
            while (used && data[used - 1] == '\n') --used;
            data[used] = 0;
        }
        if (used >= 900) used = 0;
        if (used) dappend(data, sizeof(data), &used, "\n");
        dappend(data, sizeof(data), &used, "[clockd] heartbeat");
        dappend(data, sizeof(data), &used, "\n");
        syscall(10, (u32)"/var/log/clockd.log", (u32)data, used, 0);
    }
}

#endif
