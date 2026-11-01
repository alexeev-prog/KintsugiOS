#ifndef SHELL_H
#define SHELL_H

#include "../kklibc/ctypes.h"

#define SHELL_LINE_SIZE 256
#define SHELL_MAX_ARGS 32

typedef enum {
    SHELL_OK = 0,
    SHELL_ERROR = 1,
    SHELL_USAGE = 2,
    SHELL_NOT_FOUND = 127
} shell_status_t;

typedef struct {
    const char* name;
    const char* category;
    const char* usage;
    const char* description;
    int (*run)(int argc, char** argv);
} shell_command_t;

extern const shell_command_t shell_system_commands[];
extern const shell_command_t shell_fs_commands[];
extern const shell_command_t shell_diag_commands[];
extern const shell_command_t shell_misc_commands[];

void shell_init(void);
void shell_poll(void);
int shell_execute(char* line);
int shell_parse(char* line, char** argv, int capacity);
int shell_last_status(void);
const shell_command_t* shell_command_at(u32 index);
void shell_complete(char* line, u32* length, u32* cursor);
void line_editor_init(void);
void line_editor_key(int key);

#endif
