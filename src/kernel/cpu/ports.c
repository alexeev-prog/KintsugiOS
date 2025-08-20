#include "ports.h"

/**
 * Чтение байтов из специфичного порта
 */
u8 port_byte_in (u16 port) {
    u8 result;
    /* Встроенный синтаксис ассемблера
     */
    __asm__("in %%dx, %%al" : "=a" (result) : "d" (port));
    return result;
}

void port_byte_out (u16 port, u8 data) {
    /* Обратите внимание, что здесь оба регистра сопоставляются 
    * с переменными C и ничего не возвращается, таким образом, в 
    * синтаксисе asm нет равенства '='. Однако мы видим запятую, 
    * поскольку во входных данных есть две переменные, а 
    * в области "return" их нет
     */
    __asm__ __volatile__("out %%al, %%dx" : : "a" (data), "d" (port));
}

void outports(u16 port, u16 data) {
    __asm__ __volatile__("outw %1, %0" : : "dN" (port), "a" (data));
}

u16 port_word_in (u16 port) {
    u16 result;
    __asm__("in %%dx, %%ax" : "=a" (result) : "d" (port));
    return result;
}

void port_word_out (u16 port, u16 data) {
    __asm__ __volatile__("out %%ax, %%dx" : : "a" (data), "d" (port));
}
