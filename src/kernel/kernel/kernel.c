/*------------------------------------------------------------------------------
*  Kintsugi OS Kernel source code
*  File:	kernel/kernel/kernel.c
*  Title:	Программа на Си, в которую мы загрузимся после boot'а. (ядро)
* Description: null
* ----------------------------------------------------------------------------*/


#include "../libc/mem.h"
#include "../cpu/isr.h"
#include "../drivers/screen.h"
#include "kernel.h"
#include "utils.h"
#include "../libc/stdio.h"
#include "../libc/mem.h"
#include "../libc/string.h"

void kmain() {
	// Запускаемая функция ядра //
	clear_screen();

	isr_install();
	kprint("ISR Installed\n");
	irq_install();
	kprint("IRQ Installed\n");

	heap_init();

	// Приглашение
	kprint("Success loaded! Welcome to Kintsugi OS\n");
	kprint("Copyright (C) alexeev-prog\nRepository: https://github.com/alexeev-prog/KintsugiOS\n");

	// Уведомление о старте оболочки командной строки
	kprint("\nKeramika Shell v0.1.0 "
	        "Type END to halt the CPU\n"
	        "Type HELP to view commands\n\n!#> ");
}

char** get_args(char *input) {
	char *token = strtok(input, " ");
	char** args;
	int arg_counter = 0;

    while(token) {
        token = strtok(NULL, " ");

		args[arg_counter] = token;
		arg_counter++;
    }

	kfree(token);

	return args;
}

void user_input(char *input) {
	// Массив структур команд, состоящий из самой команды, подсказки и указателя до void-функции
	struct { char *text, *hint; void (*command)(char**); } commands[] = {
		//   Команда            		Подсказка для команды                  	 					Указатель до функции
		{.text="END",		   		.hint="HALT CPU", 											.command=&halt_cpu},
		{.text="CLEAR", 	   		.hint="Clear screen",										.command=&clear_screen_command},
		{.text="AMALLOC", 		   	.hint="Kernel Arena Malloc. Usage: AMALLOC <size>",			.command=&arena_malloc_command_shell},
		{.text="QEMUSHUTDOWN",		.hint="Shutdown QEMU",										.command=&shutdown_qemu},
		{.text="INFO",				.hint="Get info",											.command=&info_command_shell},
		{.text="FREEMEMADDR",		.hint="Get free mem addr",									.command=&print_freememaddr},
		{.text="TESTMEM", 			.hint="Test memory",										.command=&test_mem_command},
		{.text="MEMDUMP", 			.hint="Dump memory",										.command=&mem_dump},
		{.text="MALLOC",			.hint="Alloc memory. Usage: MALLOC <size>",					.command=&kmalloc_command},
		{.text="FREE",				.hint="Free memory. Usage: FREE <address>",					.command=&free_command},
		{.text="ECHO",				.hint="Echo an text",										.command=&echo_command}
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

	if (strcmp(input, "HELP") == 0) {
		kprintln("Keramika Shell Help");
		for (int i = 0; i < commands_length; ++i) {
			kprintf("%s - %s\n", commands[i].text, commands[i].hint);
		}
		executed = 1;
	}

	if (executed == 0 && strcmp(input, "") != 0) {
		kprintf_colored("Invalid command: %s", RED_ON_BLACK_CLR_CODE, input);
	}

    // Вывод строки шелла
    kprint("\n!#> ");
}
