#include "../drivers/lowlevel_io.h"
#include "../drivers/terminal.h"
#include "../kklibc/kklibc.h"
#include "shell.h"

static int clear(int argc, char** argv) {
    UNUSED(argv);
    if (argc != 1) {
        return SHELL_USAGE;
    }
    terminal_clear();
    return SHELL_OK;
}

static int reboot_system(int argc, char** argv) {
    UNUSED(argv);
    if (argc != 1) {
        return SHELL_USAGE;
    }
    reboot();
    return SHELL_ERROR;
}

static int shutdown(int argc, char** argv) {
    UNUSED(argv);
    if (argc != 1) {
        return SHELL_USAGE;
    }
    port_word_out(0x604, 0x2000);
    return SHELL_ERROR;
}

static int halt(int argc, char** argv) {
    UNUSED(argv);
    if (argc != 1) {
        return SHELL_USAGE;
    }
    printf("CPU halted\n");
    __asm__ volatile("cli");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static int sleep_command(int argc, char** argv) {
    if (argc != 2 || strtoint(argv[1]) < 0) {
        return SHELL_USAGE;
    }
    wait(strtoint(argv[1]));
    return SHELL_OK;
}

const shell_command_t shell_system_commands[] = {
    { "clear", "system", "clear", "Clear terminal", clear },
    { "reboot", "system", "reboot", "Reboot machine", reboot_system },
    { "qemushutdown", "system", "qemushutdown", "Shutdown QEMU", shutdown },
    { "end", "system", "end", "Halt CPU", halt },
    { "sleep", "system", "sleep <ms>", "Wait milliseconds", sleep_command },
    { 0 }
};
