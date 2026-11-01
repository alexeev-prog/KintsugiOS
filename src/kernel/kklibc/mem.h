#ifndef KKLIBC_MEM_H
#define KKLIBC_MEM_H
#include "ctypes.h"
void* memset(void* dest, int value, unsigned int size);
void* memcpy(void* dest, const void* source, unsigned int size);
#endif
