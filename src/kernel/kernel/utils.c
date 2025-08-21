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
#include "../cpu/ports.h"

void shutdown_qemu() {
	outports(0x604, 0x2000);
}

void halt_cpu() {
	kprint("Stopping the CPU. Bye! (recommended shutdown PC)\n");
    halted_cpu_screen_clear();
	kprint_colored("Kintsugi  OS 0.1.0\n", BLUE_ON_WHITE_CLR_CODE);
	kprint_colored("HLT STOPPING CPU\n\n", BLUE_ON_WHITE_CLR_CODE);

	kprint_colored("Kintsugi OS Kernel Blue Screen\n\n", WHITE_ON_BLUE_CLR_CODE);
	kprint_colored(" >>> Sended END: halting the CPU\n", WHITE_ON_BLUE_CLR_CODE);
	kprint_colored("HLT interrupt assmebly: error code 0x0000000000 (HLTCPU)\n", WHITE_ON_BLUE_CLR_CODE);
	kprint_colored("Recomended: shutdown PC\n\n", WHITE_ON_BLUE_CLR_CODE);
	kprint_colored("[WARNING] This message is normal. Just shutdown or reboot", WHITE_ON_BLUE_CLR_CODE);
	asm volatile("hlt");
}

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
	u32 phys_addr;
	u32 page = kmalloc(16, 1, &phys_addr);
	char page_str[16] = "";
	hex_to_ascii(page, page_str);
	char phys_str[16] = "";
	hex_to_ascii(phys_addr, phys_str);

	kprint(page_str);
	kprint("\n");
	kprint(phys_str);
}
