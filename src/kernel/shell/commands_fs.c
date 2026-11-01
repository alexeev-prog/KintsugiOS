#include "../fs/fat12.h"
#include "../kklibc/kklibc.h"
#include "shell.h"

static const char* root_path(const char* path) {
    if (*path == '/') {
        ++path;
    }
    return path;
}

static int list(int argc, char** argv) {
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "/") && strcmp(argv[1], "."))) {
        return SHELL_USAGE;
    }
    if (!fat12_is_mounted()) {
        printf("FAT12 not mounted\n");
        return SHELL_ERROR;
    }
    fat12_list_root();
    return SHELL_OK;
}

static int cat(int argc, char** argv) {
    if (argc != 2) {
        return SHELL_USAGE;
    }
    fat12_dir_entry_t entry;
    const char* path = root_path(argv[1]);
    if (!fat12_find_file(path, &entry)) {
        return SHELL_ERROR;
    }
    if (entry.file_size >= HEAP_SIZE - sizeof(mem_block_t)) {
        return SHELL_ERROR;
    }
    u8* buffer = kmalloc(entry.file_size + 1);
    if (!buffer) {
        return SHELL_ERROR;
    }
    int result = fat12_read_file(path, buffer);
    if (!result) {
        buffer[entry.file_size] = 0;
        printf("%s", buffer);
    }
    kfree(buffer);
    return result ? SHELL_ERROR : SHELL_OK;
}

static int create(int argc, char** argv) {
    if (argc != 2) {
        return SHELL_USAGE;
    }
    return fat12_create_file(root_path(argv[1])) ? SHELL_ERROR : SHELL_OK;
}

static int remove_file(int argc, char** argv) {
    if (argc != 2) {
        return SHELL_USAGE;
    }
    return fat12_delete_file(root_path(argv[1])) ? SHELL_ERROR : SHELL_OK;
}

static int write_file(int argc, char** argv) {
    if (argc < 3) {
        return SHELL_USAGE;
    }
    u8 text[SHELL_LINE_SIZE];
    u32 length = 0;
    for (int i = 2; i < argc; ++i) {
        if (i > 2) {
            if (length == sizeof(text)) {
                return SHELL_USAGE;
            }
            text[length++] = ' ';
        }
        for (const char* p = argv[i]; *p; ++p) {
            if (length == sizeof(text)) {
                return SHELL_USAGE;
            }
            text[length++] = *p;
        }
    }
    return fat12_write_file(root_path(argv[1]), text, length) ? SHELL_ERROR : SHELL_OK;
}

static int fs_info(int argc, char** argv) {
    UNUSED(argv);
    if (argc != 1) {
        return SHELL_USAGE;
    }
    print_fat12_info();
    return fat12_is_mounted() ? SHELL_OK : SHELL_ERROR;
}

static int load_file(int argc, char** argv) {
    /* Do not expose unrestricted writes to arbitrary kernel addresses. */
    static u8* loaded;
    if (argc != 2) {
        return SHELL_USAGE;
    }
    fat12_dir_entry_t entry;
    const char* path = root_path(argv[1]);
    if (!fat12_find_file(path, &entry)) {
        return SHELL_ERROR;
    }
    u8* buffer = kmalloc(entry.file_size ? entry.file_size : 1);
    if (!buffer) {
        return SHELL_ERROR;
    }
    if (fat12_read_file(path, buffer)) {
        kfree(buffer);
        return SHELL_ERROR;
    }
    kfree(loaded);
    loaded = buffer;
    printf("Loaded %d bytes at %x (replaced by next load)\n", entry.file_size, (u32)loaded);
    return SHELL_OK;
}

const shell_command_t shell_fs_commands[] = {
    { "ls", "fs", "ls [/]", "List root directory", list },
    { "cat", "fs", "cat <file>", "Read file", cat },
    { "load", "fs", "load <file>", "Load into managed buffer", load_file },
    { "create", "fs", "create <file>", "Create empty file", create },
    { "del", "fs", "del <file>", "Delete file", remove_file },
    { "write", "fs", "write <file> <text>", "Replace file contents", write_file },
    { "fat12info", "fs", "fat12info", "Volume information", fs_info },
    { 0 }
};
