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

void info_command_shell(char** args) {
	// kprint("Kintsugi OS 0.1.0 by alexeev-prog\n");

	kprint("   __    _      __                _          \n"
		   "  / /__ (_)__  / /____ __ _____ _(_) ___  ___\n"
           " /  '_// / _ \\/ __(_-</ // / _ `/ / / _ \\(_-<\n"
           "/_/\\_\\/_/_//_/\\__/___/\\_,_/\\_, /_/  \\___/___/\n"
           "                          /___/              \n");

	meminfo_t meminfo = get_meminfo();

	kprint("MEMORY\n");
	kprintf("HEAP (%d): start at %d, minimal block size %d\n", meminfo.heap_size, meminfo.heap_start, meminfo.block_size);
	kprintf("Total used: %d\n", meminfo.total_used);
	kprintf("Total free: %d\n", meminfo.total_free);
	kprintf("Block count: %d\n", meminfo.block_count);
}

void mem_dump(char** args) {
	kmemdump();
}

void echo_command(char **args) {
    for (int i = 0; args[i] != NULL; i++) {
        kprintf("%s ", args[i]);
    }
    kprint("\n");
}

void free_command(char **args) {
    if (!args[0]) {
        kprint("FREE usage: FREE <hex_address>\n");
        return;
    }

    char *addr_str = args[0];
    if (addr_str[0] == '0' && addr_str[1] == 'x') {
        addr_str += 2;
    }

    u32 addr = hex_strtoint(addr_str);
    kfree((void*)addr);
    kprintf("Freed memory at %x\n", addr);
}

void kmalloc_command(char** args) {
	if (args[0] == NULL) {
		kprint("MALLOC usage: MALLOC <bytes>");
		return;
	}

	int size = strtoint(args[0]);
	void* ptr = kmalloc(size);

	char buf1[32] = "";
	hex_to_ascii((int)ptr, buf1);

	kprintf("Allocate %d bytes.\nPointer: %s\n", size, buf1);
}

void test_mem_command(char** args) {
    void* ptr1 = kmalloc(64);
    void* ptr2 = kmalloc(128);
    void* ptr3 = kmalloc(256);

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
