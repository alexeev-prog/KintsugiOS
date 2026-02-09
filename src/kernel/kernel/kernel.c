/*------------------------------------------------------------------------------
 *  Kintsugi OS Kernel source code
 *  File:	kernel/kernel/kernel.c
 *  Title:	Программа на Си, в которую мы загрузимся после boot'а. (ядро)
 * Description: null
 * ----------------------------------------------------------------------------*/

#include "kernel.h"

#include "../cpu/isr.h"
#include "../drivers/ata_pio.h"
#include "../drivers/screen.h"
#include "../drivers/screen_output_switch.h"
#include "../drivers/terminal.h"
#include "../fs/fat12.h"
#include "../kklibc/kklibc.h"
#include "sysinfo.h"
#include "utils.h"

#define MAX_ARGS 32

int shell_cursor_offset = 0;
int shell_prompt_offset = 0;

void kmain() {
    // clear_screen();

    kprint("Launch Kintsugi OS Kernel...\n");

    isr_install();
    irq_install();
    kprint("IRQ&ISR Installed\n");

    heap_init();

    detect_cpu();
    detect_memory();

    ata_pio_init();
    fat12_init();

    kprint("\nEnter to continue . . . ");

    char answer[1];
    read_line(answer, sizeof(answer));

    terminal_init();
    output_set_mode(OUTPUT_MODE_TERMINAL);
    terminal_refresh();

    printf("\nKintsugi OS %s (C) 2025\nRepository: " "https://github.com/alexeev-prog/KintsugiOS\n", VERSION);

    kprint("Keramika Shell " "Type HELP to view commands\n\n!#> ");

    shell_cursor_offset = get_cursor_offset();
    shell_prompt_offset = shell_cursor_offset;
}

char** get_args(char* input) {
    char* token = strtok(input, " ");
    static char* args[MAX_ARGS + 1];
    int arg_counter = 0;

    while (token) {
        token = strtok(NULL, " ");

        if (arg_counter + 1 > MAX_ARGS + 1) {
            printf_colored(
                "Error when parsing args: %d args exceed the limit %d\n",
                RED_ON_BLACK,
                arg_counter + 1,
                MAX_ARGS + 1);
            break;
        }

        args[arg_counter] = token;
        arg_counter++;
    }

    return args;
}

void user_input(char* input) {
    struct {
        char *text, *hint;
        void (*command)(char**);
    } commands[] = {
        { .text = "end",          .hint = "HALT CPU",                              .command = &halt_cpu                 },
        { .text = "clear",        .hint = "Clear screen",                          .command = &clear_screen_command     },
        { .text = "qemushutdown", .hint = "Shutdown QEMU",                         .command = &shutdown_qemu            },
        { .text = "info",         .hint = "Get info",                              .command = &info_command_shell       },
        { .text = "memdump",      .hint = "Dump memory",                           .command = &mem_dump                 },
        { .text = "malloc",       .hint = "Alloc memory. Usage: malloc <size>",    .command = &kmalloc_command          },
        { .text = "free",         .hint = "Free memory. Usage: free <address>",    .command = &free_command             },
        { .text = "echo",         .hint = "Echo an text",                          .command = &echo_command             },
        { .text = "sleep",        .hint = "Wait time. Usage: sleep <ms>",          .command = &sleep_command            },
        { .text = "reboot",       .hint = "Reboot system",                         .command = &reboot_command           },
        { .text = "rand",         .hint = "Gen random num. Usage: rand <seed>",    .command = &rand_command             },
        { .text = "randrange",
         .hint = "Get random num from range. Usage: randrange <seed> <min> <max>",
         .command = &rand_range_command                                                                                 },
        { .text = "binpow",
         .hint = "Binary power. Usage: binpow <base> <exponent>",
         .command = &binary_pow_command                                                                                 },
        { .text = "ls",           .hint = "List files",                            .command = &ls_command               },
        { .text = "cat",          .hint = "Show file content",                     .command = &cat_command              },
        { .text = "load",         .hint = "Load file to memory",                   .command = &load_command             },
        { .text = "fat12info",    .hint = "Print fat12 fs info",                   .command = &print_fat12_info_command }
    };

    int executed = 0;

    char** args = get_args(input);

    const int commands_length = sizeof(commands) / sizeof(commands[0]);

    for (int i = 0; i < commands_length; ++i) {
        if (strcmp(input, commands[i].text) == 0) {
            commands[i].command(args);
            executed = 1;
            break;
        }
    }

    if (strcmp(input, "help") == 0) {
        kprintln("Keramika Shell Help");
        for (int i = 0; i < commands_length; ++i) {
            printf("%-12s - %s\n", commands[i].text, commands[i].hint);
        }
        executed = 1;
    }

    if (executed == 0 && strcmp(input, "") != 0) {
        printf_colored("Invalid command: %s", RED_ON_BLACK, input);
    }

    // Вывод строки шелла
    kprint("\n!#> ");

    shell_cursor_offset = get_cursor_offset();
    shell_prompt_offset = shell_cursor_offset;
}
