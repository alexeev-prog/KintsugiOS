#include "runtime.h"
#include "desktopabi.h"

/* PID 1 supervises the userspace command interpreter. */
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    static const char message[] = "[INIT] userspace init ready\n";
    write_all(1, message, sizeof(message) - 1);
    unsigned char mode = 1;
    if (syscall(2, (u32)"/ETC/VIDEO.CFG", 0, (u32)&mode, 1) == 1 && mode < KOS_VIDEO_MODES)
        syscall(KOS_SYS_DISPLAY, mode, 0, 0, 0);
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
