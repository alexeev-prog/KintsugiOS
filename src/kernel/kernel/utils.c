/*------------------------------------------------------------------------------
*  Kintsugi OS Kernel source code
*  File:	kernel/utils.c
*  Title:	Вспомогательные программы и утилиты для ядра
* Description: null
* ----------------------------------------------------------------------------*/

#include "utils.h"
#include "../drivers/screen.h"
#include "../libc/mem.h"
#include "../libc/string.h"


void help_command_shell() {
	kprint("END - stopping the CPU\n"
	    	"INFO - info about OS\n"
	    	"PAGE - to request a kmalloc()\n"
	    	"CLEAR - clear the screen\n"
	    	"SHUTDOWN - shutdown QEMU\n");
}

void info_command_shell() {
	kprint("Kintsugi OS 0.1.0 by alexeev-prog\n");
}

void malloc_command_shell() {
	// kmalloc()
	u32 phys_addr;
	u32 page = kmalloc(1000, 1, &phys_addr);
	char page_str[16] = "";
	hex_to_ascii(page, page_str);
	char phys_str[16] = "";
	hex_to_ascii(phys_addr, phys_str);
	kprint("Page: ");
	kprint(page_str);
	kprint(", physical address: ");
	kprint(phys_str);
	kprint("\n");
}
