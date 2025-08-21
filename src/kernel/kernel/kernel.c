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
#include "../libc/string.h"

void kmain() {
	// Запускаемая функция ядра //
	clear_screen();

	isr_install();
	kprint("ISR Installed\n");
	irq_install();
	kprint("IRQ Installed\n");

	// Приглашение
	kprint("Success loaded! Welcome to Kintsugi OS\n");
	kprint("Copyright (C) alexeev-prog\nRepository: https://github.com/alexeev-prog/KintsugiOS\n");

	// Уведомление о старте оболочки командной строки
	kprint("\nKeramikaShell v0.1.0 "
	        "Type END to halt the CPU\n"
	        "Type HELP to view commands\n\n!#> ");
}

void user_input(char *input) {
	// Массив структур команд, состоящий из самой команды, подсказки и указателя до void-функции
	struct { char *text, *hint; void (*command)(); } commands[] = {
		//   Команда            		Подсказка для команды                   Указатель до функции
		{.text="END",		   		.hint="HALT CPU", 						.command=&halt_cpu},
		{.text="CLEAR", 	   		.hint="Clear screen",					.command=&clear_screen},
		{.text="KMALLOC", 		   	.hint="Kernel Page Malloc",				.command=&malloc_command_shell},
		{.text="QEMUSHUTDOWN",		.hint="Shutdown QEMU",					.command=&shutdown_qemu},
		{.text="INFO",				.hint="Get info",						.command=&info_command_shell},
		{.text="FREEMEMADDR",		.hint="Get free mem addr",				.command=&print_freememaddr}
	};
	// TODO: добавить поддержку аргументов

	int executed = 0;

	const int commands_length = sizeof(commands) / sizeof(commands[0]);

	for (int i = 0; i < commands_length; ++i) {
		if (strcmp(input, commands[i].text) == 0) {
			commands[i].command();
			executed = 1;
			break;
		}
	}

	if (strcmp(input, "HELP") == 0) {
		kprintln("Keramika Shell Help");
		for (int i = 0; i < commands_length; ++i) {
			kprint(commands[i].text);
			kprint(" - ");
			kprintln(commands[i].hint);
		}
		executed = 1;
	}

	if (executed == 0 && strcmp(input, "") != 0) {
		kprint_colored("[ERROR] INVALID COMMAND: ", 4);
    	kprint(input);
	}

    // Вывод строки шелла
    kprint("\n!#> ");
}
