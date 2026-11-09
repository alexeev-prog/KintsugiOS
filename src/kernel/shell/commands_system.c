#include "../drivers/lowlevel_io.h"
#include "../drivers/terminal.h"
#include "../fs/vfs.h"
#include "../kernel/power.h"
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
    if (system_sync()) {
        printf("Disk synchronization failed; reboot cancelled\n");
        return SHELL_ERROR;
    }
    reboot();
    return SHELL_ERROR;
}

static int shutdown(int argc, char** argv) {
    UNUSED(argv);
    if (argc != 1) {
        return SHELL_USAGE;
    }
    system_shutdown();
}

static int halt(int argc, char** argv) {
    UNUSED(argv);
    if (argc != 1) {
        return SHELL_USAGE;
    }
    if (system_sync()) {
        printf("Disk synchronization failed; halt cancelled\n");
        return SHELL_ERROR;
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

static int sync_command(int argc, char** argv) {
    UNUSED(argv);
    if (argc != 1) {
        return SHELL_USAGE;
    }
    int result = system_sync();
    if (result) {
        printf("sync: %s\n", vfs_error(result));
    }
    return result ? SHELL_ERROR : SHELL_OK;
}

const shell_command_t shell_system_commands[] = {
    { "clear", "system", "clear", "Clear terminal", clear },
    { "reboot", "system", "reboot", "Reboot machine", reboot_system },
    { "shutdown", "system", "shutdown", "Sync disks and power off (ACPI)", shutdown },
    { "sync", "system", "sync", "Synchronize disk caches", sync_command },
    { "end", "system", "end", "Halt CPU", halt },
    { "sleep", "system", "sleep <ms>", "Wait milliseconds", sleep_command },
    { 0 }
};
