
#ifndef UTILS_H
#define UTILS_H

void fibonacci_command(char **args);
void binary_pow_command(char **args);
void rand_comamnd(char **args);
void rand_range_command(char **args);
void reboot_command(char** args);
void sleep_command(char** args);
void shutdown_qemu(char** args);
void halt_cpu(char** args);
void help_command_shell(char** args);
void info_command_shell(char** args);
void mem_dump(char** args);
void clear_screen_command(char** args);
void kmalloc_command(char** args);
void echo_command(char** args);
void free_command(char **args);

#endif
