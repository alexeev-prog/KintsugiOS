#ifndef STRINGS_H
#define STRINGS_H

void int_to_ascii(int n, char str[]);
void hex_to_ascii(int n, char str[]);
int strtoint(char* str);
void reverse(char s[]);
int strlen(char s[]);
void backspace(char s[]);
void append(char s[], char n);
int strcmp(char s1[], char s2[]);
void strcpy(char *dest, char *src);
unsigned int is_delim(char c, char *delim);
char *strtok(char *src_str, char *delim);

#endif
