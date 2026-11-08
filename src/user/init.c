#include "runtime.h"

/* PID 1 supervises the userspace command interpreter. */
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    static const char message[] = "[INIT] userspace init ready\n";
    write_all(1, message, sizeof(message) - 1);
    for (;;) {
        char* args[] = { "shell", 0 };
        int pid = spawn("/BIN/SHELL.ELF", 1, args);
        if (pid < 0) return 1;
        wait(pid);
        syscall(32, 0, 0, 0, 0);
    }
}
