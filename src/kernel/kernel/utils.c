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
#include "../libc/stdio.h"

void print_freememaddr(char** args) {
	get_freememaddr();
}

void clear_screen_command(char** args) {
	clear_screen();
}

void shutdown_qemu(char** args) {
	outports(0x604, 0x2000);
}

void halt_cpu(char** args) {
    halted_cpu_screen_clear();
	kprint_colored("Kintsugi  OS 0.1.0\n\n", BLUE_ON_WHITE_CLR_CODE);
	kprint_colored("Halted CPU Blue Screen\n", BLUE_ON_WHITE_CLR_CODE);
	kprint_colored("CPU is halted.\n\n", BLUE_ON_WHITE_CLR_CODE);
	kprint_colored("asm volatile(\"hlt\")", BLUE_ON_WHITE_CLR_CODE);

	asm volatile("hlt");
}

void help_command_shell(char** args) {
	kprint("END - stopping the CPU\n"
	    	"INFO - info about OS\n"
	    	"PAGE - to request a kmalloc()\n"
	    	"CLEAR - clear the screen\n"
	    	"SHUTDOWN - shutdown QEMU\n");
}

void info_command_shell(char** args) {
	kprint("Kintsugi OS 0.1.0 by alexeev-prog\n");
}

void arena_malloc_command_shell(char** args) {
	if (args[0] == NULL) {
		kprint("AMALLOC usage: AMALLOC <page size>");
		return;
	}

	u32 phys_addr;
	u32 page = kmalloc(strtoint(args[0]), 1, &phys_addr);
	char page_str[16] = "";
	hex_to_ascii(page, page_str);
	char phys_str[16] = "";
	hex_to_ascii(phys_addr, phys_str);

	kprintf("ARENA MALLOC %d bytes\n", strtoint(args[0]));
	kprintf("Page address: %s\nPhysical address: %s\n", page_str, phys_str);
}

void mem_dump(char** args) {
	kmemdump();
}

void kmalloc_command(char** args) {
	if (args[0] == NULL) {
		kprint("KMALLOC usage: KMALLOC <bytes>");
		return;
	}

	int size = strtoint(args[0]);
	void* ptr = kmalloc_new(size);

	char buf1[32] = "";
	hex_to_ascii((int)ptr, buf1);

	kprintf("Allocate %d bytes.\nPointer: %s\n", size, buf1);
}

void test_mem_command(char** args) {
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
