#include "shell.h"

#include "../drivers/keyboard.h"
#include "../drivers/terminal.h"
#include "../fs/vfs.h"
#include "../kernel/log.h"
#include "../kklibc/kklibc.h"
#include "../user/process.h"

static int last_status;

const shell_command_t* shell_command_at(u32 index) {
    const shell_command_t* groups[] = {
        shell_system_commands, shell_fs_commands, shell_diag_commands, shell_misc_commands
    };
    for (u32 group = 0; group < sizeof(groups) / sizeof(groups[0]); ++group) {
        for (const shell_command_t* command = groups[group]; command->name; ++command) {
            if (!index--) {
                return command;
            }
        }
    }
    return NULL;
}

/* In-place lexer: quotes concatenate, backslash escapes outside single quotes. */
int shell_parse(char* line, char** argv, int capacity) {
    char* read = line;
    char* write = line;
    int argc = 0;
    while (*read) {
        while (*read == ' ' || *read == '\t') {
            ++read;
        }
        if (!*read) {
            break;
        }
        if (argc == capacity - 1) {
            return -1;
        }
        argv[argc++] = write;
        char quote = 0;
        while (*read) {
            char c = *read++;
            if (c == '\\' && quote != '\'') {
                if (!*read) {
                    return -1;
                }
                *write++ = *read++;
            } else if (c == '\'' || c == '"') {
                if (!quote) {
                    quote = c;
                } else if (quote == c) {
                    quote = 0;
                } else {
                    *write++ = c;
                }
            } else if (!quote && (c == ' ' || c == '\t')) {
                break;
            } else {
                *write++ = c;
            }
        }
        if (quote) {
            return -1;
        }
        *write++ = 0;
    }
    argv[argc] = NULL;
    return argc;
}

int shell_last_status(void) {
    return last_status;
}

int shell_execute(char* line) {
    char* argv[SHELL_MAX_ARGS + 1];
    int argc = shell_parse(line, argv, SHELL_MAX_ARGS + 1);
    if (argc < 0) {
        printf("Syntax error: quotes, escape or too many arguments\n");
        return last_status = SHELL_USAGE;
    }
    if (!argc) {
        return last_status;
    }
    /* External commands take precedence; builtins remain a rescue fallback. */
    char executable[VFS_PATH_MAX];
    if (strchr(argv[0], '/')) {
        snprintf(executable, sizeof(executable), "%s", argv[0]);
    } else {
        snprintf(executable, sizeof(executable), "/BIN/%s.ELF", argv[0]);
    }
    vfs_stat_t executable_stat;
    int external = strchr(argv[0], '/') || !strcmp(argv[0], "echo") || !strcmp(argv[0], "cat")
        || !strcmp(argv[0], "ls") || !strcmp(argv[0], "pwd");
    int builtin = 0;
    for (u32 i = 0; shell_command_at(i); ++i) {
        if (!strcmp(argv[0], shell_command_at(i)->name)) {
            builtin = 1;
        }
    }
    external |= !builtin;
    if (external && !vfs_stat(executable, &executable_stat)) {
        int result = process_run(executable, argc, argv);
        if (result < 0) {
            printf("exec: %s\n", vfs_error(result));
            result = 126;
        }
        return last_status = result;
    }
    if (strchr(argv[0], '/')) {
        printf("exec: path not found: %s\n", argv[0]);
        return last_status = SHELL_NOT_FOUND;
    }
    for (u32 i = 0;; ++i) {
        const shell_command_t* command = shell_command_at(i);
        if (!command) {
            break;
        }
        if (!strcmp(argv[0], command->name)) {
            int result = command->run(argc, argv);
            if (result == SHELL_USAGE) {
                printf("Usage: %s\n", command->usage);
            } else if (result) {
                printf("%s: operation failed (%d)\n", command->name, result);
            }
            return last_status = result;
        }
    }
    printf("Unknown command: %s\n", argv[0]);
    return last_status = SHELL_NOT_FOUND;
}

void shell_init(void) {
    terminal_print("Keramika Shell - type help\n");
    log_write("[INFO] shell ready\n");
    line_editor_init();
}

void shell_poll(void) {
    int key;
    while ((key = keyboard_read_event()) != 0) {
        line_editor_key(key);
    }
}
