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

static int execute_simple(char* line) {
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

static int execute_pipeline(char* line) {
    char* stages[8] = { line };
    int count = 1, append = 0;
    char* output = NULL;
    char quote = 0;
    int operators = 0;
    for (char* p = line; *p; ++p) {
        if (*p == '\\' && quote != '\'') {
            if (p[1]) {
                ++p;
            }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            if (!quote) {
                quote = *p;
            } else if (quote == *p) {
                quote = 0;
            }
            continue;
        }
        if (quote) {
            continue;
        }
        if (*p == '|') {
            if (count == 8 || output) {
                return SHELL_USAGE;
            }
            *p = 0;
            stages[count++] = p + 1;
            operators = 1;
        } else if (*p == '>') {
            if (output) {
                return SHELL_USAGE;
            }
            *p = 0;
            append = p[1] == '>';
            if (append) {
                *++p = 0;
            }
            output = p + 1;
            operators = 1;
        }
    }
    if (!operators) {
        return execute_simple(line);
    }
    char* arguments[8][SHELL_MAX_ARGS + 1];
    char** argv[8];
    int argc[8];
    char names[8][VFS_PATH_MAX];
    char* paths[8];
    char* target[2];
    if (output) {
        if (shell_parse(output, target, 2) != 1) {
            return SHELL_USAGE;
        }
        output = target[0];
    }
    for (int i = 0; i < count; ++i) {
        argv[i] = arguments[i];
        argc[i] = shell_parse(stages[i], argv[i], SHELL_MAX_ARGS + 1);
        if (argc[i] < 1) {
            return SHELL_USAGE;
        }
        snprintf(names[i], sizeof(names[i]), strchr(argv[i][0], '/') ? "%s" : "/BIN/%s.ELF", argv[i][0]);
        paths[i] = names[i];
        vfs_stat_t stat;
        int result = vfs_stat(paths[i], &stat);
        if (result) {
            printf("exec: %s\n", vfs_error(result));
            return 127;
        }
    }
    int result = process_pipeline(count, paths, argc, argv, output, append);
    if (result < 0) {
        printf("pipeline: %s (%d)\n", vfs_error(result), result);
    }
    return result < 0 ? 126 : result;
}

/* Locate control operators without interpreting quoted or escaped text. */
int shell_execute(char* line) {
    char quote = 0;
    char* alternative = NULL;
    for (char* p = line; *p; ++p) {
        if (*p == '\\' && quote != '\'') {
            if (p[1]) {
                ++p;
            }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            if (!quote) {
                quote = *p;
            } else if (quote == *p) {
                quote = 0;
            }
            continue;
        }
        if (quote) {
            continue;
        }
        if (*p == '|' && p[1] == '|') {
            if (!alternative) {
                alternative = p;
            }
            ++p;
        } else if (*p == '<' || *p == '&') {
            printf("Unsupported shell operator: %c\n", *p);
            return last_status = SHELL_USAGE;
        }
    }
    if (quote) {
        return execute_simple(line);
    }
    if (alternative) {
        char* right = alternative + 2;
        while (*right == ' ' || *right == '\t') {
            ++right;
        }
        char* left = line;
        while (*left == ' ' || *left == '\t') {
            ++left;
        }
        if (left == alternative || !*right) {
            printf("Syntax error: missing command around ||\n");
            return last_status = SHELL_USAGE;
        }
        *alternative = 0;
        int result = execute_pipeline(line);
        return result ? shell_execute(right) : result;
    }
    return last_status = execute_pipeline(line);
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
