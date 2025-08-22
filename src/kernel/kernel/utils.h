
#ifndef UTILS_H
#define UTILS_H

void print_freememaddr(char** args);
void shutdown_qemu(char** args);
void halt_cpu(char** args);
void help_command_shell(char** args);
void info_command_shell(char** args);
void arena_malloc_command_shell(char** args);
void test_mem_command(char** args);
void mem_dump(char** args);
void clear_screen_command(char** args);
void kmalloc_command(char** args);
void echo_command(char** args);
void free_command(char **args);

#endif
