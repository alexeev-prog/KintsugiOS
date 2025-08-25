/*------------------------------------------------------------------------------
 *  Kintsugi OS Drivers source code
 *  File: drivers/screen.c
 *  Title: Функции работы с экраном
 *  Last Change Date: 30 October 2023, 12:28 (UTC)
 *  Author: alexeev-prog
 *  License: GNU GPL v3
 * ------------------------------------------------------------------------------
 *	Description: null
 * ----------------------------------------------------------------------------*/

#include "screen.h"
#include "../kklibc/ctypes.h"
#include "../kklibc/stdlib.h"
#include "lowlevel_io.h"

/* Декларирования частных функций */
void set_cursor_offset(int offset);
int print_char(char c, int col, int row, char attr);
int get_offset(int col, int row);
int get_offset_row(int offset);
int get_offset_col(int offset);

/**********************************************************
 * Публичные функции API ядра                             *
 **********************************************************/

/**
 * Вывод сообщения в специфической локации
 * Если col, row отрицательные, то используем текущий оффсет
 */
void kprint_at(char *message, int col, int row, int color) {
  struct {
    int clrcode;
    int color;
  } colors[] = {
      // структура цветов, clrcode - код цвета для передачи и color - сам цвет
      {.clrcode = WHITE_ON_BLACK_CLR_CODE, .color = WHITE_ON_BLACK},
      {.clrcode = BLUE_ON_BLACK_CLR_CODE, .color = BLUE_ON_BLACK},
      {.clrcode = GREEN_ON_BLACK_CLR_CODE, .color = GREEN_ON_BLACK},
      {.clrcode = CYAN_ON_BLACK_CLR_CODE, .color = CYAN_ON_BLACK},
      {.clrcode = RED_ON_BLACK_CLR_CODE, .color = RED_ON_BLACK},
      {.clrcode = MAGENTA_ON_BLACK_CLR_CODE, .color = MAGENTA_ON_BLACK},
      {.clrcode = BROWN_ON_BLACK_CLR_CODE, .color = BROWN_ON_BLACK},
      {.clrcode = LGREY_ON_BLACK_CLR_CODE, .color = LGREY_ON_BLACK},
      {.clrcode = DGREY_ON_BLACK_CLR_CODE, .color = DGREY_ON_BLACK},
      {.clrcode = LBLUE_ON_BLACK_CLR_CODE, .color = LBLUE_ON_BLACK},
      {.clrcode = LGREEN_ON_BLACK_CLR_CODE, .color = LGREEN_ON_BLACK},
      {.clrcode = LCYAN_ON_BLACK_CLR_CODE, .color = LCYAN_ON_BLACK},
      {.clrcode = LRED_ON_BLACK_CLR_CODE, .color = LRED_ON_BLACK},
      {.clrcode = LMAGENTA_ON_BLACK_CLR_CODE, .color = LMAGENTA_ON_BLACK},
      {.clrcode = YELLOW_ON_BLACK_CLR_CODE, .color = YELLOW_ON_BLACK},
      {.clrcode = WHITE_ON_BLUE_CLR_CODE, .color = WHITE_ON_BLUE},
      {.clrcode = WHITE_ON_RED_CLR_CODE, .color = WHITE_ON_RED},
      {.clrcode = RED_ON_WHITE_CLR_CODE, .color = RED_ON_WHITE},
      {.clrcode = BLUE_ON_WHITE_CLR_CODE, .color = BLUE_ON_WHITE},
  };

  const int colors_length = sizeof(colors) / sizeof(colors[0]);

  /* Установка курсора и оффсета если если col, row отрицательные */
  int offset;
  if (col >= 0 && row >= 0)
    offset = get_offset(col, row);
  else {
    offset = get_cursor_offset();
    row = get_offset_row(offset);
    col = get_offset_col(offset);
  }
  /* "Прокрутка" сообщения и его вывод */
  int i = 0;
  while (message[i] != 0) {

    for (int j = 0; j < colors_length; ++j) {
      if (color == colors[j].clrcode) {
        offset = print_char(message[i++], col, row, colors[j].color);
        break;
      }
    }

    /* Вычисление row/col для следующей итерации */
    row = get_offset_row(offset);
    col = get_offset_col(offset);
  }
}

void kprint(char *message) {
  // Вывод текста. Цвет по умолчанию - белый на черном (код 0)
  kprint_at(message, -1, -1, 0);
}

void kprintln(char *message) {
  // Вывод текста. Цвет по умолчанию - белый на черном (код 0)
  kprint_at(message, -1, -1, 0);
  kprint("\n");
}

void kprintln_colored(char *message, int color) {
  /* Цветной вывод текста. Принимает также, в отличии от kprint, код цвета.*/
  kprint_at(message, -1, -1, color);
  kprint("\n");
}

void kprint_colored(char *message, int color) {
  /* Цветной вывод текста. Принимает также, в отличии от kprint, код цвета.*/
  kprint_at(message, -1, -1, color);
}

void kprint_backspace() {
  int offset = get_cursor_offset() - 2;
  int row = get_offset_row(offset);
  int col = get_offset_col(offset);

  // print_char(0x08, col, row, WHITE_ON_BLACK);

  print_char(' ', col, row, WHITE_ON_BLACK);

  set_cursor_offset(offset);
}

void halted_cpu_screen_clear() {
  int screen_size = MAX_COLS * MAX_ROWS;
  int i;
  u8 *screen = (u8 *)VIDEO_ADDRESS;

  for (i = 0; i < screen_size; i++) {
    screen[i * 2] = ' ';
    screen[i * 2 + 1] = WHITE_ON_BLUE;
  }
  set_cursor_offset(get_offset(0, 0));
}

void panic_red_screen(char *title, char *description) {
  int screen_size = MAX_COLS * MAX_ROWS;
  u8 *screen = (u8 *)VIDEO_ADDRESS;

  for (int i = 0; i < screen_size; ++i) {
    screen[i * 2] = ' ';
    screen[i * 2 + 1] = WHITE_ON_RED;
  }

  set_cursor_offset(get_offset(0, 0));

  kprint_colored("KINTSUGIOS KERNEL PANIC RED SCREEN\n\n",
                 WHITE_ON_RED_CLR_CODE);
  kprint_colored(title, WHITE_ON_RED_CLR_CODE);
  kprint_colored("\n\n", WHITE_ON_RED_CLR_CODE);
  kprint_colored(description, WHITE_ON_RED_CLR_CODE);

  asm volatile("hlt");
}

/**********************************************************
 * Приватные функции ядра                                 *
 **********************************************************/

/**
 * Функции вывода строки для ядра, использующие видео-память
 *
 * Если 'col' и 'row' отрицательные, мы пишем на текущей позиции курсора
 * Если 'attr' равен 0 он использует 'белое на чернм' по умолчанию
 * Возвращает оффсет следующего символа
 * Устанавливает курсор на оффсете
 */
int print_char(char c, int col, int row, char attr) {
  u8 *vidmem = (u8 *)VIDEO_ADDRESS;
  if (!attr)
    attr = WHITE_ON_BLUE;

  /* Контроль ошибок: вывод крассной 'E' если координаты неверные */
  if (col >= MAX_COLS || row >= MAX_ROWS) {
    vidmem[2 * (MAX_COLS) * (MAX_ROWS)-2] = 'E';
    vidmem[2 * (MAX_COLS) * (MAX_ROWS)-1] = RED_ON_WHITE;
    return get_offset(col, row);
  }

  int offset;
  if (col >= 0 && row >= 0)
    offset = get_offset(col, row);
  else
    offset = get_cursor_offset();

  if (c == '\n') {
    row = get_offset_row(offset);
    offset = get_offset(0, row + 1);
  } else if (c == 0x08) { /* Backspace */
    vidmem[offset] = ' ';
    vidmem[offset + 1] = attr;
  } else {
    vidmem[offset] = c;
    vidmem[offset + 1] = attr;
    offset += 2;
  }

  /* Проверяем, больше ли оффсет экрана, и скроллим */
  if (offset >= MAX_ROWS * MAX_COLS * 2) {
    int i;
    for (i = 1; i < MAX_ROWS; i++)
      memory_copy((u8 *)(get_offset(0, i) + VIDEO_ADDRESS),
                  (u8 *)(get_offset(0, i - 1) + VIDEO_ADDRESS), MAX_COLS * 2);

    /* Пустая последняя строка */
    char *last_line =
        (char *)(get_offset(0, MAX_ROWS - 1) + (u8 *)VIDEO_ADDRESS);
    for (i = 0; i < MAX_COLS * 2; i++)
      last_line[i] = 0;

    offset -= 2 * MAX_COLS;
  }

  set_cursor_offset(offset);
  return offset;
}

int get_cursor_offset() {
  /* Используем VGA порты для получения текущей позиции курсора
   * 1. (data 14) Получаем высший байт оффсета курсора
   * 2. (data 15) Получаем низший байт
   */
  port_byte_out(REG_SCREEN_CTRL, 14);
  int offset = port_byte_in(REG_SCREEN_DATA) << 8; /* High byte: << 8 */
  port_byte_out(REG_SCREEN_CTRL, 15);
  offset += port_byte_in(REG_SCREEN_DATA);
  return offset * 2; /* Позиция * размер клетки */
}

void set_cursor_offset(int offset) {
  offset /= 2;
  port_byte_out(REG_SCREEN_CTRL, 14);
  port_byte_out(REG_SCREEN_DATA, (u8)(offset >> 8));
  port_byte_out(REG_SCREEN_CTRL, 15);
  port_byte_out(REG_SCREEN_DATA, (u8)(offset & 0xff));
}

void clear_screen() {
  int screen_size = MAX_COLS * MAX_ROWS;
  int i;
  u8 *screen = (u8 *)VIDEO_ADDRESS;

  for (i = 0; i < screen_size; i++) {
    screen[i * 2] = ' ';
    screen[i * 2 + 1] = WHITE_ON_BLACK;
  }
  set_cursor_offset(get_offset(0, 0));
}

int get_offset(int col, int row) { return 2 * (row * MAX_COLS + col); }
int get_offset_row(int offset) { return offset / (2 * MAX_COLS); }
int get_offset_col(int offset) {
  return (offset - (get_offset_row(offset) * 2 * MAX_COLS)) / 2;
}
