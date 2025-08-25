/*------------------------------------------------------------------------------
 *  Kintsugi OS KKLIBC source code
 *  File: kklibc/stdlib.c
 *  Title: Стандартный набор методов
 *	Description: null
 * ----------------------------------------------------------------------------*/
#include "stdlib.h"
#include "ctypes.h"
#include "stdio.h"

void booltochar(u8 value, u8 *str) {
  if (value) {
    strcpy(str, (u8 *)"true");
  } else {
    strcpy(str, (u8 *)"false");
  }
}

void itoa(int num, char *str, int base) {
  int i = 0;
  u8 isNegative = 0;

  if (num == 0) {
    str[i++] = '0';
    str[i] = '\0';
    return;
  }

  if (num < 0 && base == 10) {
    isNegative = 1;
    num = -num;
  }

  while (num != 0) {
    int rem = num % base;
    str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
    num = num / base;
  }

  if (isNegative) {
    str[i++] = '-';
  }

  str[i] = '\0';

  // Reverse the string
  int start = 0;
  int end = i - 1;
  while (start < end) {
    char temp = str[start];
    str[start] = str[end];
    str[end] = temp;
    start++;
    end--;
  }
}

// Helper function for unsigned integer to string conversion
void utoa(u32 num, char *str, int base) {
  int i = 0;

  if (num == 0) {
    str[i++] = '0';
    str[i] = '\0';
    return;
  }

  while (num != 0) {
    int rem = num % base;
    str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
    num = num / base;
  }

  str[i] = '\0';

  // Reverse the string
  int start = 0;
  int end = i - 1;
  while (start < end) {
    char temp = str[start];
    str[start] = str[end];
    str[end] = temp;
    start++;
    end--;
  }
}

int atoi(const char *str) {
  int result = 0;
  int sign = 1;

  while (*str == ' ' || *str == '\t' || *str == '\n') {
    str++;
  }

  if (*str == '-') {
    sign = -1;
    str++;
  } else if (*str == '+') {
    str++;
  }

  while (*str >= '0' && *str <= '9') {
    result = result * 10 + (*str - '0');
    str++;
  }

  return result * sign;
}

u32 xorshift32(u32 *state) { // генератор псевдослучайных чисел xorshift32
  u32 x = state[0];
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  state[0] = x;
  return x;
}

// рандомное число в диапазоне
u32 rand_range(u32 *state, u32 min, u32 max) {
  if (max < min) {
    return 0;
  }

  return min + (xorshift32(state) % (max - min + 1));
}

u32 rand(u32 *state) { return xorshift32(state); }

// reboot
void reboot() {
  unsigned char reset_value = 0x06;
  __asm__ __volatile__("outb %0, %1"
                       :
                       : "a"(reset_value), "d"(0xCF9)
                       : "memory");
}

// wait ожидание
void wait(int ms) {
  volatile int count;
  while (ms--) {
    count = 100000;
    while (count--) {
      __asm__("nop");
    }
  }
}

/* Заполняет область памяти указанным значением */
void *memset(void *s, int c, unsigned int n) {
  unsigned char *p = (unsigned char *)s;
  while (n--) {
    *p++ = (unsigned char)c;
  }
  return s;
}

/* Копирует блок памяти из источника в назначение */
void *memcpy(void *dest, const void *src, unsigned int n) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;
  while (n--) {
    *d++ = *s++;
  }
  return dest;
}

void memory_copy(u8 *source, u8 *dest, int nbytes) { // копируем память
  int i;
  for (i = 0; i < nbytes; i++) {
    *(dest + i) = *(source + i);
  }
}

void memory_set(u8 *dest, u8 val, u32 len) { // задаем память
  u8 *temp = (u8 *)dest;
  for (; len != 0; len--)
    *temp++ = val;
}

void u32memory_set(u32 *dest, u32 val, u32 len) { // задаем память u32
  u32 *temp = (u32 *)dest;
  for (; len != 0; len--)
    *temp++ = val;
}

/**
 * K&R Реализация
 */
void int_to_ascii(int n, char str[]) {
  int i, sign;
  if ((sign = n) < 0)
    n = -n;
  i = 0;
  do {
    str[i++] = n % 10 + '0';
  } while ((n /= 10) > 0);

  if (sign < 0)
    str[i++] = '-';
  str[i] = '\0';

  reverse(str);
}

int strtoint(char *str) {
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
    if (tmp == 0 && zeros == 0)
      continue;
    zeros = 1;
    if (tmp > 0xA)
      append(str, tmp - 0xA + 'a');
    else
      append(str, tmp + '0');
  }

  tmp = n & 0xF;
  if (tmp >= 0xA)
    append(str, tmp - 0xA + 'a');
  else
    append(str, tmp + '0');
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
    if (*str >= '0' && *str <= '9')
      result += *str - '0';
    else if (*str >= 'a' && *str <= 'f')
      result += *str - 'a' + 10;
    else if (*str >= 'A' && *str <= 'F')
      result += *str - 'A' + 10;
    str++;
  }
  return result;
}

/* K&R */
void reverse(char s[]) { // реверс
  int c, i, j;
  for (i = 0, j = strlen(s) - 1; i < j; i++, j--) {
    c = s[i];
    s[i] = s[j];
    s[j] = c;
  }
}

/* K&R */
int strlen(char s[]) { // длина строки
  int i = 0;
  while (s[i] != '\0')
    ++i;
  return i;
}

void append(char s[], char n) { // добавление новой строки в исходную
  int len = strlen(s);
  s[len] = n;
  s[len + 1] = '\0';
}

void backspace(char s[]) { // бекспейс
  int len = strlen(s);

  s[len - 1] = '\0';
}

/* K&R
 * Возвращает <0 если s1<s2, 0 если s1==s2, >0 если s1>s2 */
int strcmp(char s1[], char s2[]) {
  int i;
  for (i = 0; s1[i] == s2[i]; i++) {
    if (s1[i] == '\0')
      return 0;
  }
  return s1[i] - s2[i];
}

unsigned int is_delim(char c, char *delim) {
  while (*delim != '\0') {
    if (c == *delim)
      return 1;

    delim++;
  }
  return 0;
}

char *strtok(char *src_str, char *delim) {
  static char *backup_string; // бекап строки

  if (!src_str) {
    src_str = backup_string;
  }

  if (!src_str) {
    return NULL;
  }

  while (1) {
    if (is_delim(*src_str, delim)) {
      src_str++;
      continue;
    }

    if (*src_str == '\0') {
      // конец строки
      return NULL;
    }

    break;
  }
  char *ret = src_str;

  while (1) {
    if (*src_str == '\0') {
      /*конец входной строки
      и следующее выполнение возвратит NULL*/
      backup_string = src_str;
      return ret;
    }

    if (is_delim(*src_str, delim)) {
      *src_str = '\0';
      backup_string = src_str + 1;
      return ret;
    }

    src_str++;
  }
}

char *strncpy(char *dest, const char *src, unsigned int n) {
  unsigned int i;
  for (i = 0; i < n && src[i] != '\0'; i++)
    dest[i] = src[i];
  for (; i < n; i++)
    dest[i] = '\0';
  return dest;
}

// Реализация strncat
char *strncat(char *dest, const char *src, unsigned int n) {
  unsigned int dest_len = strlen(dest);
  unsigned int i;
  for (i = 0; i < n && src[i] != '\0'; i++)
    dest[dest_len + i] = src[i];
  dest[dest_len + i] = '\0';
  return dest;
}

// Реализация strncmp
int strncmp(const char *s1, const char *s2, unsigned int n) {
  unsigned int i;
  for (i = 0; i < n && s1[i] != '\0' && s2[i] != '\0'; i++) {
    if (s1[i] != s2[i])
      return s1[i] - s2[i];
  }
  if (i < n) {
    return s1[i] - s2[i];
  }
  return 0;
}

// Реализация strchr
char *strchr(const char *s, int c) {
  while (*s != '\0') {
    if (*s == (char)c)
      return (char *)s;
    s++;
  }
  if (c == '\0')
    return (char *)s;
  return NULL;
}

// Реализация strstr
char *strstr(const char *haystack, const char *needle) {
  if (*needle == '\0')
    return (char *)haystack;

  int needle_len = strlen(needle);
  int haystack_len = strlen(haystack);

  if (needle_len == 0)
    return (char *)haystack;

  for (int i = 0; i <= haystack_len - needle_len; i++) {
    if (strncmp(haystack + i, needle, needle_len) == 0)
      return (char *)(haystack + i);
  }
  return NULL;
}

// Вспомогательная функция для форматирования строки с ограничением размера
static int vsnprintf(char *buf, unsigned int size, const char *fmt,
                     va_list args) {
  unsigned int i = 0;
  char num_buf[32];
  char c;
  const char *s;

  while (*fmt && i < size - 1) {
    if (*fmt != '%') {
      buf[i++] = *fmt++;
      continue;
    }

    fmt++;
    switch (*fmt) {
    case 'd': {
      int num = va_arg(args, int);
      int_to_ascii(num, num_buf);
      s = num_buf;
      while (*s && i < size - 1)
        buf[i++] = *s++;
      break;
    }
    case 'x': {
      int num = va_arg(args, int);
      hex_to_ascii(num, num_buf);
      s = num_buf;
      while (*s && i < size - 1)
        buf[i++] = *s++;
      break;
    }
    case 's': {
      s = va_arg(args, char *);
      while (*s && i < size - 1)
        buf[i++] = *s++;
      break;
    }
    case 'c': {
      c = (char)va_arg(args, int);
      if (i < size - 1)
        buf[i++] = c;
      break;
    }
    default: {
      if (i < size - 1)
        buf[i++] = '%';
      if (i < size - 1)
        buf[i++] = *fmt;
      break;
    }
    }
    fmt++;
  }
  buf[i] = '\0';
  return i;
}

// Реализация sprintf
int sprintf(char *buf, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int ret = vsnprintf(buf, (unsigned int)-1, fmt, args);
  va_end(args);
  return ret;
}

// Реализация snprintf
int snprintf(char *buf, unsigned int size, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int ret = vsnprintf(buf, size, fmt, args);
  va_end(args);
  return ret;
}

u32 strspn(const u8 *str1, const u8 *str2) {
  const u8 *s1 = str1;
  const u8 *s2;

  while (*s1 != '\0') {
    s2 = str2;
    while (*s2 != '\0') {
      if (*s1 == *s2)
        break;

      s2++;
    }

    if (*s2 == '\0')
      break;

    s1++;
  }

  return s1 - str1;
}

u32 strcspn(const u8 *str1, const u8 *str2) {
  const u8 *s1 = str1;
  const u8 *s2;

  while (*s1 != '\0') {
    s2 = str2;
    while (*s2 != '\0') {
      if (*s1 == *s2)
        return s1 - str1;

      s2++;
    }
    s1++;
  }

  return s1 - str1;
}

u8 *strpbrk(const u8 *str1, const u8 *str2) {

  while (*str1 != '\0') {

    const u8 *s2 = str2;
    while (*s2 != '\0') {
      if (*str1 == *s2)
        return (u8 *)str1;

      s2++;
    }
    str1++;
  }

  return NULL;
}

void *memmove(void *dest, const void *src, u32 n) {
  u8 *d = (u8 *)dest;
  const u8 *s = (const u8 *)src;

  if (s < d && d < s + n) {
    d += n;
    s += n;
    while (n--)
      *(--d) = *(--s);

  } else {
    for (u32 i = 0; i < n; i++)
      d[i] = s[i];
  }

  return dest;
}

int memcmp(const void *str1, const void *str2, u32 n) {
  const u8 *s1 = (const u8 *)str1;
  const u8 *s2 = (const u8 *)str2;

  for (u32 i = 0; i < n; i++) {
    if (s1[i] != s2[i])
      return s1[i] - s2[i];
  }

  return (int)NULL;
}

void *memchr(const void *str, int c, u32 n) {
  const u8 *s = (const u8 *)str;

  for (u32 i = 0; i < n; i++) {
    if (s[i] == (u8)c)
      return (void *)(s + i);
  }

  return NULL;
}
