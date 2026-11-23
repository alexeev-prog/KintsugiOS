#include "runtime.h"
#include "desktopabi.h"

/* PID 1 supervises the userspace command interpreter. */
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    static const char message[] =
        "\033[36mKintsugiOS\033[0m\n"
        "  Keramika desktop environment starting...\n"
        "  ring3 userspace online | TUI framebuffer ready\n"
        "[INIT] userspace init ready\n\n";
    write_all(1, message, sizeof(message) - 1);
    unsigned char settings[4] = {1, 1, 1, 0};
    int loaded = syscall(2, (u32)"/ETC/VIDEO.CFG", 0, (u32)settings, sizeof(settings));
    if (loaded >= 1 && settings[0] < KOS_VIDEO_MODES) syscall(KOS_SYS_DISPLAY, settings[0], 0, 0, 0);
    if (loaded >= 2 && settings[1] < KOS_FRAMEBUFFER_THEMES) syscall(KOS_SYS_FRAMEBUFFER_THEME, settings[1], 0, 0, 0);
    if (loaded >= 3 && settings[2] < KOS_FRAMEBUFFER_SCALES) syscall(KOS_SYS_FRAMEBUFFER_SCALE, settings[2], 0, 0, 0);
    if (loaded >= 4 && settings[3] < KOS_FRAMEBUFFER_FONTS) syscall(KOS_SYS_FRAMEBUFFER_FONT, settings[3], 0, 0, 0);
    for (;;) {
        char* args[] = { "shell", 0 };
        int pid = spawn("/BIN/SHELL.ELF", 1, args);
        if (pid < 0) {
            return 1;
        }
        wait(pid);
        syscall(32, 0, 0, 0, 0);
    }
}
