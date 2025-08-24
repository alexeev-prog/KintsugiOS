/*------------------------------------------------------------------------------
*  Kintsugi OS Kernel source code
*  File:	kernel/utils.c
*  Title:	Вспомогательные программы и утилиты для ядра
* Description: null
* ----------------------------------------------------------------------------*/

#include "utils.h"
#include "../drivers/screen.h"
#include "../kklibc/kklibc.h"
#include "../cpu/ports.h"

void fibonacci_command(char **args) {
    if (!args[0]) {
        kprint("fib usage: fib <num>");
        return;
    }

    int num = strtoint(args[0]);

    u32 fib = fibonacci(num);

    printf("fib(%d) = %d", num, fib);
}

void binary_pow_command(char **args) {
    if (!args[0] || !args[1]) {
        kprint("binpow usage: binpow <base> <exponent>");
        return;
    }

    int b = strtoint(args[0]);
    u32 e = strtoint(args[1]);

    int powered = binary_pow(b, e);

    printf("%d ** %d = %d", b, e, powered);
}

void rand_comamnd(char **args) {
    if (!args[0]) {
        kprint("rand usage: rand <seed>");
        return;
    }

    u32 seed = strtoint(args[0]);

    printf("%d", rand(&seed));
}

void rand_range_command(char **args) {
    if (!args[0] || !args[1] || !args[2]) {
        kprint("randrange usage: randrange <seed> <min> <max>");
        return;
    }

    u32 seed = strtoint(args[0]);
    u32 min = strtoint(args[1]);
    u32 max = strtoint(args[2]);

    printf("%d", rand_range(&seed, min, max));
}

void reboot_command(char** args) {
    reboot();
}

void sleep_command(char** args) {
    if (!args[0]) {
        kprint("sleep usage: sleep <ms>");
        return;
    }

    wait(strtoint(args[0]));
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
	kprint("Kintsugi OS 0.1.0 by alexeev-prog\n");

	kprint("   __    _      __                _          \n"
		   "  / /__ (_)__  / /____ __ _____ _(_) ___  ___\n"
           " /  '_// / _ \\/ __(_-</ // / _ `/ / / _ \\(_-<\n"
           "/_/\\_\\/_/_//_/\\__/___/\\_,_/\\_, /_/  \\___/___/\n"
           "                          /___/              \n");

	meminfo_t meminfo = get_meminfo();

	kprint("MEMORY\n");
	printf("HEAP (%d): start at %d, minimal block size %d\n", meminfo.heap_size, meminfo.heap_start, meminfo.block_size);
	printf("Total used: %d\n", meminfo.total_used);
	printf("Total free: %d\n", meminfo.total_free);
	printf("Block count: %d", meminfo.block_count);
}

void mem_dump(char** args) {
	kmemdump();
}

void echo_command(char **args) {
    for (int i = 0; args[i] != NULL; i++) {
        printf("%s ", args[i]);
    }
}

void free_command(char **args) {
    if (!args[0]) {
        kprint("free usage: free <hex_address>");
        return;
    }

    char *addr_str = args[0];
    if (addr_str[0] == '0' && addr_str[1] == 'x') {
        addr_str += 2;
    }

    u32 addr = hex_strtoint(addr_str);
    kfree((void*)addr);
    printf("Freed memory at %x", addr);
}

void kmalloc_command(char** args) {
	if (args[0] == NULL) {
		kprint("malloc usage: malloc <bytes>");
		return;
	}

	int size = strtoint(args[0]);
	void* ptr = (void*)kmalloc(size);

	char buf1[32] = "";
	hex_to_ascii((int)ptr, buf1);

	printf("Allocate %d bytes.\nPointer: %s", size, buf1);
}
