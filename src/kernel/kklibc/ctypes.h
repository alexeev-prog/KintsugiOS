/*------------------------------------------------------------------------------
 *  Kintsugi OS KKLIBC source code
 *  File: kklibc/ctypes.h
 *  Title: Библиотека для работы с типами C (заголовочный файл)
 *	Description: null
 * ----------------------------------------------------------------------------*/

#ifndef KKLIBC_CTYPES_H
#define KKLIBC_CTYPES_H

typedef unsigned int u32;
typedef int s32;
typedef unsigned short u16;
typedef short s16;
typedef unsigned char u8;
typedef char s8;

#define NULL ((void*)0)
#define true 1
#define false 0

#define low_16(address) (u16)((address) & 0xFFFF)
#define high_16(address) (u16)(((address) >> 16) & 0xFFFF)

#define KB (1024)
#define MB (1024 * 1024)
#define GB (1024 * 1024 * 1024)
#define TB (1024 * 1024 * 1024 * 1024)
#define PB (1024 * 1024 * 1024 * 1024 * 1024)
#define EB (1024 * 1024 * 1024 * 1024 * 1024 * 1024)
#define ZB (1024 * 1024 * 1024 * 1024 * 1024 * 1024 * 1024)
#define YB (1024 * 1024 * 1024 * 1024 * 1024 * 1024 * 1024 * 1024)

#define ASSERT(b) ((b) ? (void)0 : panic_assert(__FILE__, __LINE__, #b))

void panic_assert(const char* file, u32 line, const char* desc);

int isalnum(int c);
int isalpha(int c);
int isblank(int c);
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);

// 7.4.2 Character case mapping functions
int tolower(int c);
int toupper(int c);

#endif
