#ifndef STRINGS_H
#define STRINGS_H

#include "ctypes.h"

u32 rand_range(u32* state, u32 min, u32 max);
u32 rand(u32* state);
void reboot();
void wait(int ms);
void memory_copy(u8 *source, u8 *dest, int nbytes);
void memory_set(u8 *dest, u8 val, u32 len);
void u32memory_set(u32 *dest, u32 val, u32 len);
void int_to_ascii(int n, char str[]);
void hex_to_ascii(int n, char str[]);
int strtoint(char* str);
void reverse(char s[]);
int strlen(char s[]);
void backspace(char s[]);
void append(char s[], char n);
int strcmp(char s1[], char s2[]);
int hex_strtoint(char *str);
void strcpy(char *dest, char *src);
unsigned int is_delim(char c, char *delim);
char *strtok(char *src_str, char *delim);

#endif
