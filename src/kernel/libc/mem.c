/*------------------------------------------------------------------------------
*  Kintsugi OS C Libraries source code
*  File: libc/mem.c
*  Title: Функции работы с памятью
*	Description: null
* ----------------------------------------------------------------------------*/


#include "mem.h"
#include "../drivers/screen.h"
#include "../libc/string.h"

void memory_copy(u8 *source, u8 *dest, int nbytes) {
    int i;
    for (i = 0; i < nbytes; i++) {
        *(dest + i) = *(source + i);
    }
}

void memory_set(u8 *dest, u8 val, u32 len) {
    u8 *temp = (u8 *)dest;
    for ( ; len != 0; len--) *temp++ = val;
}

// TODO: Улучшить kmalloc, добавить kfree
/* Это должно быть вычислено во время соединения, но жестко запрограммировано
 * на данный момент значение в порядке. Ядро начинается
 с 0x1000, как определено в Makefile */
u32 free_mem_addr_guard1 = 0xDEADBEEF;
u32 free_mem_addr = 0x10000;
u32 free_mem_addr_guard2 = 0xCAFEBABE;

/* Реализация - это просто указатель на некоторую свободную память, которая продолжает расти */
u32 kmalloc(u32 size, int align, u32 *phys_addr) {

    if(free_mem_addr_guard1 != 0xDEADBEEF || free_mem_addr_guard2 != 0xCAFEBABE) {
        kprint("PANIC: Memory corruption detected around free_mem_addr!\n");
        // Зависаем или перезагружаемся
        // asm volatile("hlt");
    }
    /* Страницы выровнены по 4K, или 0x1000 */
    if (align == 1 && (free_mem_addr & 0xFFFFF000)) {
        free_mem_addr &= 0xFFFFF000;
        free_mem_addr += 0x1000;
    }
    /* Сохранить также физический адрес */
    if (phys_addr) *phys_addr = free_mem_addr;

    kprint("kmalloc: allocating ");
    char size_str[16] = "";
    hex_to_ascii(size, size_str);
    kprint(size_str);
    kprint(" bytes at ");
    print_freememaddr();
    kprint("\n");

    u32 ret = free_mem_addr;
    free_mem_addr += size;
    return ret;
}

void print_freememaddr() {
    char free_mem_addr_str[16] = "";
	hex_to_ascii(free_mem_addr, free_mem_addr_str);

	kprint(free_mem_addr_str);
    // kprint("\n");
}
