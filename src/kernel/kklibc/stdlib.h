/*------------------------------------------------------------------------------
 *  Kintsugi OS KKLIBC source code
 *  File: kklibc/stdlib.h
 *  Title: Стандартный набор методов (заголовочный файл)
 *	Description: null
 * ----------------------------------------------------------------------------*/
#ifndef STDLIB_H
#define STDLIB_H

#include "ctypes.h"

/**
 * @brief Конвертирует булеву переменную в строку
 *
 * @param value булева переменная
 * @param str
 **/
void booltochar(u8 value, u8* str);

void itoa(int num, char* str, int base);
void utoa(u32 num, char* str, int base);
u32 rand_range(u32* state, u32 min, u32 max);
u32 rand(u32* state);
void reboot();
void wait(int ms);
void int_to_ascii(int n, char str[]);
void hex_to_ascii(int n, char str[]);
int strtoint(char* str);
void reverse(char s[]);
int strlen(char s[]);
void backspace(char s[]);
void append(char s[], char n);
int hex_strtoint(char* str);
unsigned int is_delim(char c, char* delim);

int sprintf(char* buf, const char* fmt, ...);
int snprintf(char* buf, unsigned int size, const char* fmt, ...);

// String
u32 strspn(const u8* str1, const u8* str2);
u32 strcspn(const u8* str1, const u8* str2);
u8* strpbrk(const u8* str1, const u8* str2);
char* strtok(char* src_str, char* delim);
char* strncpy(char* dest, const char* src, unsigned int n);
char* strncat(char* dest, const char* src, unsigned int n);
int strncmp(const char* s1, const char* s2, unsigned int n);
char* strchr(const char* s, int c);
void strcpy(char* dest, char* src);
int strcmp(char s1[], char s2[]);
char* strstr(const char* haystack, const char* needle);

// Memory

/**
 * @brief Перемещение памяти
 *
 * @param dest Указатель на цель
 * @param src Исходный указатель
 * @param n Количество
 * @return void*
 **/
void* memmove(void* dest, const void* src, u32 n);

/**
 * @brief Копирование памяти
 *
 * @param ptr1 Первый указатель
 * @param ptr2 Второй указатель
 * @param n Количество
 * @return int
 **/
int memcmp(const void* ptr1, const void* ptr2, u32 n);
void* memchr(const void* ptr, int c, u32 n);
void* memset(void* s, int c, unsigned int n);
void* memcpy(void* dest, const void* src, unsigned int n);
void memory_copy(u8* source, u8* dest, int nbytes);
void memory_set(u8* dest, u8 val, u32 len);


void u32memory_set(u32* dest, u32 val, u32 len);

#endif
