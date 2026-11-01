#include "../fs/fat12.h"
#include "../kklibc/kklibc.h"
#include "shell.h"

void shell_complete(char* line, u32* length, u32* cursor) {
    /* Complete a simple token at its end. Quoted/escaped tokens are left intact. */
    if (*cursor < *length && line[*cursor] != ' ') {
        return;
    }
    u32 start = *cursor;
    while (start && line[start - 1] != ' ') {
        --start;
    }
    for (u32 i = 0; i < *cursor; ++i) {
        if (line[i] == '\'' || line[i] == '"' || line[i] == '\\') {
            return;
        }
    }
    int files = start != 0;
    if (files && line[start] == '/') {
        ++start;
    }
    u32 prefix = *cursor - start;
    char common[SHELL_LINE_SIZE];
    u32 common_length = 0, matches = 0;
    for (u32 i = 0;; ++i) {
        char name[13];
        const char* candidate;
        if (files) {
            if (!fat12_root_name(i, name)) {
                break;
            }
            candidate = name;
        } else {
            const shell_command_t* command = shell_command_at(i);
            if (!command) {
                break;
            }
            candidate = command->name;
        }
        u32 n = 0;
        while (n < prefix && candidate[n] && tolower(candidate[n]) == tolower(line[start + n])) {
            ++n;
        }
        if (n != prefix) {
            continue;
        }
        if (!matches++) {
            strcpy(common, candidate);
            common_length = strlen(common);
        } else {
            n = 0;
            while (n < common_length && candidate[n] && common[n] == candidate[n]) {
                ++n;
            }
            common_length = n;
        }
    }
    if (!matches || common_length < prefix) {
        return;
    }
    u32 extra = common_length - prefix;
    if (*length + extra >= SHELL_LINE_SIZE) {
        return;
    }
    for (u32 i = *length + 1; i > *cursor; --i) {
        line[i + extra - 1] = line[i - 1];
    }
    memcpy(line + *cursor, common + prefix, extra);
    *length += extra;
    *cursor += extra;
}
