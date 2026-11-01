#include "../kklibc/kklibc.h"
#include "shell.h"

static int help(int argc, char** argv) {
    if (argc > 2) {
        return SHELL_USAGE;
    }
    printf("Keramika Shell Help\n");
    if (argc == 1) {
        printf("help <command|category>\nCategories: system fs diag memory misc\n");
        return SHELL_OK;
    }
    int found = 0;
    for (u32 i = 0;; ++i) {
        const shell_command_t* command = shell_command_at(i);
        if (!command) {
            break;
        }
        if (!strcmp(argv[1], command->name) || !strcmp(argv[1], command->category)) {
            printf("%-28s %s\n", command->usage, command->description);
            found = 1;
        }
    }
    return found ? SHELL_OK : SHELL_USAGE;
}

static int echo(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        printf("%s%s", i > 1 ? " " : "", argv[i]);
    }
    printf("\n");
    return SHELL_OK;
}

static int random_number(int argc, char** argv) {
    if (argc != 2) {
        return SHELL_USAGE;
    }
    u32 seed = strtoint(argv[1]);
    printf("%d\n", rand(&seed));
    return SHELL_OK;
}

static int power(int argc, char** argv) {
    if (argc != 3 || strtoint(argv[2]) < 0) {
        return SHELL_USAGE;
    }
    printf("%d\n", binary_pow(strtoint(argv[1]), strtoint(argv[2])));
    return SHELL_OK;
}

static int random_range(int argc, char** argv) {
    if (argc != 4) {
        return SHELL_USAGE;
    }
    u32 seed = strtoint(argv[1]);
    u32 min = strtoint(argv[2]), max = strtoint(argv[3]);
    if (max < min || max - min == 0xffffffffu) {
        return SHELL_USAGE;
    }
    printf("%d\n", rand_range(&seed, min, max));
    return SHELL_OK;
}

const shell_command_t shell_misc_commands[] = {
    { "help", "misc", "help [category|command]", "Thematic help", help },
    { "echo", "misc", "echo [text ...]", "Print arguments", echo },
    { "rand", "misc", "rand <seed>", "Random number", random_number },
    { "randrange", "misc", "randrange <seed> <min> <max>", "Random number in range", random_range },
    { "binpow", "misc", "binpow <base> <exponent>", "Integer power", power },
    { 0 }
};
