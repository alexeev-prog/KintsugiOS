#include "runtime.h"
#include "sysabi.h"
#include "authabi.h"
#include "desktopabi.h"
#include "tuiabi.h"

/* kintsugifetch - neofetch-style CLI card: ASCII art, ANSI colors and stats.
 * This deliberately writes plain terminal text instead of presenting a TUI
 * screen, so it can be piped, redirected and used from scripts. */

static void text(const char* s) {
    u32 n = 0;
    while (s[n]) ++n;
    write_all(1, s, n);
}

static void color(unsigned code) {
    char sequence[8];
    sequence[0] = 27; sequence[1] = '[';
    u32 n = 2;
    if (code >= 10) sequence[n++] = (char)('0' + code / 10);
    sequence[n++] = (char)('0' + code % 10);
    sequence[n++] = 'm';
    write_all(1, sequence, n);
}

static void number(u32 value) {
    char digits[11];
    int n = 10;
    digits[n] = 0;
    do { digits[--n] = (char)('0' + value % 10); value /= 10; } while (value);
    text(digits + n);
}

static void label(const char* name, const char* value, unsigned tone) {
    color(36); text("   "); text(name); text(": "); color(0);
    color(tone); text(value); color(0); text("\n");
}

static void label_number(const char* name, u32 value, const char* suffix, unsigned tone) {
    color(36); text("   "); text(name); text(": "); color(0);
    color(tone); number(value); text(" "); text(suffix); color(0); text("\n");
}

static void bar(const char* name, u32 used, u32 total, unsigned tone) {
    color(36); text("   "); text(name); text(": ["); color(0);
    u32 filled = total ? used * 12 / total : 0;
    if (filled > 12) filled = 12;
    color(tone);
    for (u32 i = 0; i < filled; ++i) text("#");
    color(8);
    for (u32 i = filled; i < 12; ++i) text("-");
    color(0); text("] ");
    number(total ? used * 100 / total : 0); text("%\n");
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    kos_desktop_info desktop = {0};
    syscall(KOS_SYS_DESKTOP, (u32)&desktop, 0, 0, 0);
    kos_disk_info disk = {0};
    syscall(19, (u32)&disk, 0, 0, 0);
    ktui_stats memory = {0};
    syscall(9, (u32)&memory, 0, 0, 0);
    u32 audio[3] = {0};
    syscall(KOS_SYS_SOUND_INFO, (u32)audio, 0, 0, 0);
    char user[32] = "unknown";
    syscall(KOS_SYS_GETUSER, (u32)user, sizeof(user), 0, 0);

    static const char* const art[] = {
        "      .-''''-.",
        "    .'  .--.  '.",
        "   /   /    \\   \\",
        "  |   |  ()  |   |",
        "   \\   \\    /   /",
        "    '.  '--'  .'",
        "      '-....-'",
        "   KINTSUGI / i386"
    };
    static const unsigned art_colors[] = { 6, 6, 6, 3, 6, 6, 6, 2 };

    char identity[48];
    u32 used = 0;
    while (user[used] && used + 1 < sizeof(identity)) { identity[used] = user[used]; ++used; }
    const char* host = "@kintsugi";
    while (*host && used + 1 < sizeof(identity)) identity[used++] = *host++;
    identity[used] = 0;

    char resolution[32];
    u32 at = 0;
    u32 values[2] = { desktop.width, desktop.height };
    for (int v = 0; v < 2; ++v) {
        if (v) resolution[at++] = 'x';
        u32 divisor = 1000;
        while (divisor && values[v] / divisor == 0) divisor /= 10;
        if (!divisor) resolution[at++] = '0';
        while (divisor) { resolution[at++] = (char)('0' + (values[v] / divisor) % 10); divisor /= 10; }
    }
    resolution[at] = 0;

    u32 free_pages = memory.free_pages ? memory.free_pages : desktop.free_kib / 4;
    u32 total_pages = memory.ram_kib ? memory.ram_kib / 4 : free_pages + memory.allocated_pages;
    u32 ram_used = total_pages > free_pages ? total_pages - free_pages : 0;
    u32 disk_used = disk.clusters > disk.free_clusters ? disk.clusters - disk.free_clusters : 0;

    text("\n");
    for (u32 i = 0; i < 8; ++i) {
        color(art_colors[i]); text(art[i]); color(0);
        text("    ");
        if (i == 0) label("user", identity, 2);
        else if (i == 1) label("os", "KintsugiOS v0.7.0a", 3);
        else if (i == 2) label("kernel", "monolithic / POSIX VFS", 7);
        else if (i == 3) label("shell", "Keramika Shell", 7);
        else if (i == 4) label("display", resolution, 6);
        else if (i == 5) label("fs", disk.fs[0] ? disk.fs : "unknown", 5);
        else if (i == 6) label("audio", audio[2] ? "OPL2 FM (9 voices)" : "PC speaker", 1);
        else label_number("uptime", desktop.uptime, "s", 7);
    }
    text("\n");
    label_number("tasks", desktop.processes, "running", 4);
    label_number("memory free", desktop.free_kib, "KiB", 4);
    bar("RAM", ram_used, total_pages ? total_pages : 1, 2);
    bar("DISK", disk_used, disk.clusters ? disk.clusters : 1, 3);
    text("\n");
    color(8); text("   F12: layout  |  help: shell builtins  |  Q: not applicable here\n"); color(0);
    text("\n");
    return 0;
}
