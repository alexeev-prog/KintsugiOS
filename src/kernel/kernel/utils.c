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
    halted_cpu_screen_clear();
	kprint_colored("Kintsugi  OS 0.1.0\n\n", BLUE_ON_WHITE_CLR_CODE);
	kprint_colored("Halted CPU Blue Screen\n", BLUE_ON_WHITE_CLR_CODE);
	kprint_colored("CPU is halted.\n\n", BLUE_ON_WHITE_CLR_CODE);
	kprint_colored("asm volatile(\"hlt\")", BLUE_ON_WHITE_CLR_CODE);

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

	kprint("Page address: ");
	kprint(page_str);
	kprint("\nPhysical address: ");
	kprint(phys_str);
	kprint("\n");
}

void mem_dump() {
	kmemdump();
}

void test_mem_command() {
    void* ptr1 = kmalloc_new(64);
    void* ptr2 = kmalloc_new(128);
    void* ptr3 = kmalloc_new(256);

	char buf1[32] = "";
	char buf2[32] = "";
	char buf3[32] = "";

    kprint("Allocated blocks:\n");

    kprint("Ptr1: ");
    hex_to_ascii((int)ptr1, buf1);
    kprint(buf1);
    kprint("\n");

    kprint("Ptr2: ");
    hex_to_ascii((int)ptr2, buf2);
    kprint(buf2);
    kprint("\n");

    kprint("Ptr3: ");
    hex_to_ascii((int)ptr3, buf3);
    kprint(buf3);
    kprint("\n");

    kfree(ptr2);

    kprint("Freed ptr2\n\n");
    kmemdump();
}
