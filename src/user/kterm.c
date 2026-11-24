#include "ktuilib.h"

/* A small terminal session launcher. It provides a stable entry point for
 * future virtual terminals while remaining useful on the current VGA console. */
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    tui_clear(0x17);
    tui_bar(0, "KTERM - Kintsugi Terminal");
    tui_text(3, 3, "Terminal session", 0x1f);
    tui_text(3, 5, "F1 shell   F10 close", 0x1e);
    tui_bar(23, "Starting userspace shell...");
    tui_present(79, 24);
    char* args[] = { "SHELL", 0 };
    int pid = spawn("/bin/SHELL.ELF", 1, args);
    if (pid > 0) wait(pid);
    return pid < 0;
}
