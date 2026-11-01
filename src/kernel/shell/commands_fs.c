#include "../fs/fat12.h"
#include "../fs/vfs.h"
#include "../kklibc/kklibc.h"
#include "shell.h"

static int report(int result) {
    if (result >= 0) {
        return SHELL_OK;
    }
    printf("FS: %s\n", vfs_error(result));
    return SHELL_ERROR;
}

static int list(int argc, char** argv) {
    if (argc > 2) {
        return SHELL_USAGE;
    }
    u32 cursor = 0;
    vfs_dirent_t entry;
    int result;
    printf("Directory: %s\n", argc == 2 ? argv[1] : vfs_getcwd());
    while ((result = vfs_readdir(argc == 2 ? argv[1] : ".", &cursor, &entry)) > 0) {
        printf("%-12s %s %u bytes\n", entry.name, entry.stat.directory ? "<DIR>" : "     ", entry.stat.size);
    }
    return report(result);
}

static int cat(int argc, char** argv) {
    if (argc != 2) {
        return SHELL_USAGE;
    }
    char buffer[513];
    u32 offset = 0;
    int result;
    while ((result = vfs_read(argv[1], offset, buffer, 512)) > 0) {
        buffer[result] = 0;
        printf("%s", buffer);
        offset += result;
    }
    return report(result);
}

static int create(int argc, char** argv) {
    return argc == 2 ? report(vfs_create(argv[1])) : SHELL_USAGE;
}

static int remove_file(int argc, char** argv) {
    return argc == 2 ? report(vfs_unlink(argv[1])) : SHELL_USAGE;
}

static int mkdir_command(int argc, char** argv) {
    return argc == 2 ? report(vfs_mkdir(argv[1])) : SHELL_USAGE;
}

static int rmdir_command(int argc, char** argv) {
    return argc == 2 ? report(vfs_rmdir(argv[1])) : SHELL_USAGE;
}

static int cd(int argc, char** argv) {
    return argc <= 2 ? report(vfs_chdir(argc == 2 ? argv[1] : "/")) : SHELL_USAGE;
}

static int pwd(int argc, char** argv) {
    UNUSED(argv);
    if (argc != 1) {
        return SHELL_USAGE;
    }
    printf("%s\n", vfs_getcwd());
    return SHELL_OK;
}

static int text_write(int argc, char** argv, int append) {
    if (argc < 3) {
        return SHELL_USAGE;
    }
    char text[SHELL_LINE_SIZE];
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
    return report(append ? vfs_append(argv[1], text, length) : vfs_replace(argv[1], text, length));
}

static int write_file(int argc, char** argv) {
    return text_write(argc, argv, 0);
}

static int append_file(int argc, char** argv) {
    return text_write(argc, argv, 1);
}

static int copy(int argc, char** argv) {
    return argc == 3 ? report(vfs_copy(argv[1], argv[2])) : SHELL_USAGE;
}

static int move(int argc, char** argv) {
    return argc == 3 ? report(vfs_rename(argv[1], argv[2])) : SHELL_USAGE;
}

static int truncate_file(int argc, char** argv) {
    if (argc != 3 || !*argv[2]) {
        return SHELL_USAGE;
    }
    u32 size = 0;
    for (const char* p = argv[2]; *p; ++p) {
        if (*p < '0' || *p > '9' || size > (0x7fffffffu - (*p - '0')) / 10) {
            return SHELL_USAGE;
        }
        size = size * 10 + *p - '0';
    }
    return report(vfs_truncate(argv[1], size));
}

static int stat_file(int argc, char** argv) {
    if (argc != 2) {
        return SHELL_USAGE;
    }
    vfs_stat_t stat;
    int result = vfs_stat(argv[1], &stat);
    if (!result) {
        printf("%s size=%u readonly=%u\n", stat.directory ? "directory" : "file", stat.size, stat.readonly);
    }
    return report(result);
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
    static u8* loaded;
    if (argc != 2) {
        return SHELL_USAGE;
    }
    vfs_stat_t stat;
    int result = vfs_stat(argv[1], &stat);
    if (result) {
        return report(result);
    }
    if (stat.directory) {
        return report(VFS_EISDIR);
    }
    u8* buffer = kmalloc(stat.size ? stat.size : 1);
    if (!buffer) {
        return report(VFS_ENOMEM);
    }
    result = vfs_read(argv[1], 0, buffer, stat.size);
    if (result < 0 || (u32)result != stat.size) {
        kfree(buffer);
        return report(result < 0 ? result : VFS_ECORRUPT);
    }
    kfree(loaded);
    loaded = buffer;
    printf("Loaded %u bytes at %x (replaced by next load)\n", stat.size, (u32)loaded);
    return SHELL_OK;
}

static int fs_test(int argc, char** argv) {
    if (argc != 2 || strcmp(argv[1], "confirm")) {
        return SHELL_USAGE;
    }
    int result = vfs_mkdir("/VFSTEST");
    if (result) {
        return report(result);
    }
    u8 source[1300], target[1300];
    for (u32 i = 0; i < sizeof(source); ++i) {
        source[i] = (u8)(i * 17);
    }
    int ok = !vfs_replace("/VFSTEST/DATA.BIN", source, sizeof(source));
    vfs_file_t file;
    if (ok) {
        ok = !vfs_open("/VFSTEST/DATA.BIN", &file);
    }
    if (ok) {
        ok = !vfs_seek(&file, 499) && vfs_file_read(&file, target, 700) == 700
            && !memcmp(target, source + 499, 700);
    }
    if (ok) {
        ok = !vfs_seek(&file, 510) && vfs_file_write(&file, "TEST", 4) == 4;
    }
    if (ok) {
        memcpy(source + 510, "TEST", 4);
        ok = vfs_read("/VFSTEST/DATA.BIN", 0, target, sizeof(target)) == (int)sizeof(target)
            && !memcmp(source, target, sizeof(target));
    }
    vfs_close(&file);
    if (ok) {
        ok = !vfs_truncate("/VFSTEST/DATA.BIN", 1600);
    }
    if (ok) {
        memset(target, 1, 300);
        ok = vfs_read("/VFSTEST/DATA.BIN", 1300, target, 300) == 300;
        for (u32 i = 0; i < 300; ++i) {
            if (target[i]) {
                ok = 0;
            }
        }
    }
    /* More than one cluster of directory entries exercises directory growth. */
    for (u32 i = 0; i < 140 && ok; ++i) {
        char path[40];
        snprintf(path, sizeof(path), "/VFSTEST/F%u.TXT", i);
        ok = !vfs_create(path);
    }
    u32 cursor = 0, count = 0;
    vfs_dirent_t entry;
    while ((result = vfs_readdir("/VFSTEST", &cursor, &entry)) > 0) {
        ++count;
    }
    ok &= !result && count == 141;
    for (u32 i = 0; i < 140; ++i) {
        char path[40];
        snprintf(path, sizeof(path), "/VFSTEST/F%u.TXT", i);
        if (vfs_unlink(path)) {
            ok = 0;
        }
    }
    if (vfs_unlink("/VFSTEST/DATA.BIN")) {
        ok = 0;
    }
    if (vfs_rmdir("/VFSTEST")) {
        ok = 0;
    }
    printf("Filesystem self-test: %s\n", ok ? "PASS" : "FAIL");
    return ok ? SHELL_OK : SHELL_ERROR;
}

const shell_command_t shell_fs_commands[] = {
    { "fstest", "fs", "fstest confirm", "Test file API in new /VFSTEST directory", fs_test },
    { "ls", "fs", "ls [path]", "List directory", list },
    { "cd", "fs", "cd [path]", "Change current directory", cd },
    { "pwd", "fs", "pwd", "Print current directory", pwd },
    { "mkdir", "fs", "mkdir <path>", "Create directory", mkdir_command },
    { "rmdir", "fs", "rmdir <path>", "Remove empty directory", rmdir_command },
    { "cat", "fs", "cat <file>", "Read file", cat },
    { "stat", "fs", "stat <path>", "File information", stat_file },
    { "load", "fs", "load <file>", "Load into managed buffer", load_file },
    { "create", "fs", "create <file>", "Create empty file", create },
    { "del", "fs", "del <file>", "Delete file", remove_file },
    { "write", "fs", "write <file> <text>", "Replace file contents", write_file },
    { "append", "fs", "append <file> <text>", "Append to existing file", append_file },
    { "truncate", "fs", "truncate <file> <bytes>", "Resize file, zero-fill growth", truncate_file },
    { "cp", "fs", "cp <source> <destination>", "Copy file (no overwrite)", copy },
    { "mv", "fs", "mv <source> <destination>", "Move or rename (no overwrite)", move },
    { "fat12info", "fs", "fat12info", "Volume information", fs_info },
    { 0 }
};
