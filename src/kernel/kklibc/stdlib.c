#include "ctypes.h"
#include "stdlib.h"

void reboot() {
    unsigned char reset_value = 0x06;
    __asm__ __volatile__ (
       "outb %0, %1"
       :
       : "a" (reset_value), "d" (0xCF9)
       : "memory"
    );
}

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
    // C guarantees that '0'-'9' have consecutive values
    while (str[i] >= '0' && str[i] <= '9') {
        rc *= 10;
        rc += str[i] - '0';
        ++i;
    }

    return rc;
}

void hex_to_ascii(int n, char str[]) {
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

void strcpy(char *dest, char *src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

int hex_strtoint(char *str) {
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
void reverse(char s[]) {
    int c, i, j;
    for (i = 0, j = strlen(s)-1; i < j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

/* K&R */
int strlen(char s[]) {
    int i = 0;
    while (s[i] != '\0') ++i;
    return i;
}

void append(char s[], char n) {
    int len = strlen(s);
    s[len] = n;
    s[len+1] = '\0';
}

void backspace(char s[]) {
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
