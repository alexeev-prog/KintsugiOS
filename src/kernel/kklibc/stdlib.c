#include "ctypes.h"
#include "stdlib.h"
#include "stdio.h"

u32 xorshift32(u32* state) { // генератор псевдослучайных чисел xorshift32
    u32 x = state[0];
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state[0] = x;
    return x;
}

// рандомное число в диапазоне
u32 rand_range(u32* state, u32 min, u32 max) {
    if (max < min) {
        return 0;
    }

    return min + (xorshift32(state) % (max - min + 1));
}

u32 rand(u32* state) {
    return xorshift32(state);
}

// reboot
void reboot() {
    unsigned char reset_value = 0x06;
    __asm__ __volatile__ (
       "outb %0, %1"
       :
       : "a" (reset_value), "d" (0xCF9)
       : "memory"
    );
}

// wait ожидание
void wait(int ms) {
    volatile int count;
    while (ms--)
    {
        count = 100000;
        while (count--)
        {
            __asm__("nop");
        }
    }
}

void memory_copy(u8 *source, u8 *dest, int nbytes) { // копируем память
    int i;
    for (i = 0; i < nbytes; i++) {
        *(dest + i) = *(source + i);
    }
}

void memory_set(u8 *dest, u8 val, u32 len) { // задаем память
    u8 *temp = (u8 *)dest;
    for ( ; len != 0; len--) *temp++ = val;
}

void u32memory_set(u32 *dest, u32 val, u32 len) { // задаем память u32
    u8 *temp = (u8 *)dest;
    for ( ; len != 0; len--) *temp++ = val;
}

/**
 * K&R Реализация
 */
void int_to_ascii(int n, char str[]) {
    int i, sign;
    if ((sign = n) < 0) n = -n;
    i = 0;
    do {
        str[i++] = n % 10 + '0';
    } while ((n /= 10) > 0);

    if (sign < 0) str[i++] = '-';
    str[i] = '\0';

    reverse(str);
}

int strtoint(char* str) {
    int rc = 0;
    unsigned i = 0;
    while (str[i] >= '0' && str[i] <= '9') { // перевод строк в числа
        rc *= 10;
        rc += str[i] - '0';
        ++i;
    }

    return rc;
}

void hex_to_ascii(int n, char str[]) { // из hex в строку
    append(str, '0');
    append(str, 'x');

    char zeros = 0;

    s32 tmp;
    int i;
    for (i = 28; i > 0; i -= 4) {
        tmp = (n >> i) & 0xF;
        if (tmp == 0 && zeros == 0) continue;
        zeros = 1;
        if (tmp > 0xA) append(str, tmp - 0xA + 'a');
        else append(str, tmp + '0');
    }

    tmp = n & 0xF;
    if (tmp >= 0xA) append(str, tmp - 0xA + 'a');
    else append(str, tmp + '0');
}

void strcpy(char *dest, char *src) { // копирование строки
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

int hex_strtoint(char *str) { // строка в число hex
    int result = 0;
    while (*str) {
        result *= 16;
        if (*str >= '0' && *str <= '9') result += *str - '0';
        else if (*str >= 'a' && *str <= 'f') result += *str - 'a' + 10;
        else if (*str >= 'A' && *str <= 'F') result += *str - 'A' + 10;
        str++;
    }
    return result;
}

/* K&R */
void reverse(char s[]) { // реверс
    int c, i, j;
    for (i = 0, j = strlen(s)-1; i < j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

/* K&R */
int strlen(char s[]) { // длина строки
    int i = 0;
    while (s[i] != '\0') ++i;
    return i;
}

void append(char s[], char n) { // добавление новой строки в исходную
    int len = strlen(s);
    s[len] = n;
    s[len+1] = '\0';
}

void backspace(char s[]) { // бекспейс
    int len = strlen(s);

    s[len-1] = '\0';
}

/* K&R
 * Возвращает <0 если s1<s2, 0 если s1==s2, >0 если s1>s2 */
int strcmp(char s1[], char s2[]) {
    int i;
    for (i = 0; s1[i] == s2[i]; i++) {
        if (s1[i] == '\0') return 0;
    }
    return s1[i] - s2[i];
}

unsigned int is_delim(char c, char *delim) {
    while(*delim != '\0') {
        if(c == *delim)
            return 1;

        delim++;
    }
    return 0;
}

char *strtok(char *src_str, char *delim) {
    static char *backup_string; // бекап строки

    if(!src_str) {
        src_str = backup_string;
    }

    if(!src_str) {
        return NULL;
    }

    while(1) {
        if(is_delim(*src_str, delim)) {
            src_str++;
            continue;
        }

        if(*src_str == '\0') {
            // конец строки
            return NULL;
        }

        break;
    }
    char *ret = src_str;

    while(1) {
        if(*src_str == '\0') {
            /*конец входной строки
            и следующее выполнение возвратит NULL*/
            backup_string = src_str;
            return ret;
        }

        if(is_delim(*src_str, delim)) {
            *src_str = '\0';
            backup_string = src_str + 1;
            return ret;
        }

        src_str++;
    }
}
