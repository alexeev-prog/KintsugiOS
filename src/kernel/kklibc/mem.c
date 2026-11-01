#include "mem.h"

/* Заполняет область памяти указанным значением */
void* memset(void* s, int c, unsigned int n) {
    unsigned char* p = (unsigned char*)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

/* Копирует блок памяти из источника в назначение */
void* memcpy(void* dest, const void* src, unsigned int n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void memory_copy(u8* source, u8* dest, int nbytes) {    // копируем память
    int i;
    for (i = 0; i < nbytes; i++) {
        *(dest + i) = *(source + i);
    }
}

void memory_set(u8* dest, u8 val, u32 len) {    // задаем память
    u8* temp = (u8*)dest;
    for (; len != 0; len--) {
        *temp++ = val;
    }
}

void u32memory_set(u32* dest, u32 val, u32 len) {    // задаем память u32
    u32* temp = (u32*)dest;
    for (; len != 0; len--) {
        *temp++ = val;
    }
}
