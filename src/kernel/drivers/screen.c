/*------------------------------------------------------------------------------
*  ObsiFish OS Drivers source code
*  File: drivers/screen.c
*  Title: Функции работы с экраном
*  Last Change Date: 30 October 2023, 12:28 (UTC)
*  Author: alexeev-prog
*  License: GNU GPL v3
* ------------------------------------------------------------------------------
*	Description: null
* ----------------------------------------------------------------------------*/


#include "screen.h"
#include "../cpu/ports.h"
#include "../libc/mem.h"

/* Декларирования частных функций */
int get_cursor_offset();
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
    	if (color == 0) {
	        offset = print_char(message[i++], col, row, WHITE_ON_BLACK);
		} else if (color == 1) {
	        offset = print_char(message[i++], col, row, BLUE_ON_BLACK);
		} else if (color == 2) {
			offset = print_char(message[i++], col, row, GREEN_ON_BLACK);
		} else if (color == 3) {
			offset = print_char(message[i++], col, row, CYAN_ON_BLACK);
		} else if (color == 4) {
			offset = print_char(message[i++], col, row, RED_ON_BLACK);
		} else if (color == 5) {
			offset = print_char(message[i++], col, row, MAGENTA_ON_BLACK);
		} else if (color == 6) {
			offset = print_char(message[i++], col, row, BROWN_ON_BLACK);
		} else if (color == 7) {
			offset = print_char(message[i++], col, row, LGREY_ON_BLACK);
		} else if (color == 8) {
			offset = print_char(message[i++], col, row, DGREY_ON_BLACK);
		} else if (color == 9) {
			offset = print_char(message[i++], col, row, LBLUE_ON_BLACK);
		} else if (color == 10) {
			offset = print_char(message[i++], col, row, LGREEN_ON_BLACK);
		} else if (color == 11) {
			offset = print_char(message[i++], col, row, LCYAN_ON_BLACK);
		} else if (color == 12) {
			offset = print_char(message[i++], col, row, LRED_ON_BLACK);
		} else if (color == 13) {
			offset = print_char(message[i++], col, row, LMAGENTA_ON_BLACK);
		} else if (color == 14) {
			offset = print_char(message[i++], col, row, YELLOW_ON_BLACK);
		} else if (color == 15) {
			offset = print_char(message[i++], col, row, WHITE_ON_BLUE);			
		} else if (color == 16) {
			offset = print_char(message[i++], col, row, WHITE_ON_RED);
		} else if (color == 17) {
			offset = print_char(message[i++], col, row, RED_ON_WHITE);			
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

void kprint_colored(char *message, int color) {
	/* Цветной вывод текста. Принимает также, в отличии от kprint, код цвета.
	*/
	kprint_at(message, -1, -1, color);
}

void kprint_backspace() {
	// Вывод бекспейса (удаления символа)
    int offset = get_cursor_offset()-2;
    int row = get_offset_row(offset);
    int col = get_offset_col(offset);
    // TODO: подумать над заменением WHITE_ON_BLACK единой переменной
    print_char(0x08, col, row, WHITE_ON_BLACK);
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
    u8 *vidmem = (u8*) VIDEO_ADDRESS;
    if (!attr) attr = WHITE_ON_BLUE;

    /* Контроль ошибок: вывод крассной 'E' если координаты неверные */
    if (col >= MAX_COLS || row >= MAX_ROWS) {
        vidmem[2*(MAX_COLS)*(MAX_ROWS)-2] = 'E';
        vidmem[2*(MAX_COLS)*(MAX_ROWS)-1] = RED_ON_WHITE;
        return get_offset(col, row);
    }

    int offset;
    if (col >= 0 && row >= 0) offset = get_offset(col, row);
    else offset = get_cursor_offset();

    if (c == '\n') {
        row = get_offset_row(offset);
        offset = get_offset(0, row+1);
    } else if (c == 0x08) { /* Backspace */
        vidmem[offset] = ' ';
        vidmem[offset+1] = attr;
    } else {
        vidmem[offset] = c;
        vidmem[offset+1] = attr;
        offset += 2;
    }

    /* Проверяем, больше ли оффсет экрана, и скроллим */
    if (offset >= MAX_ROWS * MAX_COLS * 2) {
        int i;
        for (i = 1; i < MAX_ROWS; i++) 
            memory_copy((u8*)(get_offset(0, i) + VIDEO_ADDRESS),
                        (u8*)(get_offset(0, i-1) + VIDEO_ADDRESS),
                        MAX_COLS * 2);

        /* Пустая последняя строка */
        char *last_line = (char*) (get_offset(0, MAX_ROWS-1) + (u8*) VIDEO_ADDRESS);
        for (i = 0; i < MAX_COLS * 2; i++) last_line[i] = 0;

        offset -= 2 * MAX_COLS;
    }

    set_cursor_offset(offset);
    return offset;
}

void rsod_clear_screen() {
	// Red screen of death
    int screen_size = MAX_COLS * MAX_ROWS;
    int i;
    u8 *screen = (u8*) VIDEO_ADDRESS;

    for (i = 0; i < screen_size; i++) {
        screen[i*2] = ' ';
        screen[i*2+1] = WHITE_ON_RED;
    }
    set_cursor_offset(get_offset(0, 0));
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
    u8 *screen = (u8*) VIDEO_ADDRESS;

    for (i = 0; i < screen_size; i++) {
        screen[i*2] = ' ';
        screen[i*2+1] = WHITE_ON_BLACK;
    }
    set_cursor_offset(get_offset(0, 0));
}


int get_offset(int col, int row) { return 2 * (row * MAX_COLS + col); }
int get_offset_row(int offset) { return offset / (2 * MAX_COLS); }
int get_offset_col(int offset) { return (offset - (get_offset_row(offset)*2*MAX_COLS))/2; }


// Старый код
// #include "screen.h"
// #include "lowlevel_io.h"
// #include "../common.h"
// 
// void	kprint(u8 *str)
// {
// 	/* Функция печати строки */
// 	
// 	// u8 *str: указатель на строку (на первый символ строки). Строка должна
// 	// быть null-terminated.
// 
// 	while (*str)
// 	{
// 		putchar(*str, WHITE_ON_BLACK);
// 		str++;
// 	}
// }
// 
// void	putchar(u8 character, u8 attribute_byte)
// {
// 	/* Более высокоуровневая функция печати символа */
// 
// 	// u8 character: байт, соответствующий символу
// 	// u8 attribute_byte: байт, соответствующий цвету текста/фона символа
// 
// 	u16 offset;
// 
// 	offset = get_cursor();
// 	if (character == '\n')
// 	{
// 		// Переводим строку.
// 		if ((offset / 2 / MAX_COLS) == (MAX_ROWS - 1)) 
// 			scroll_line();
// 		else
// 			set_cursor((offset - offset % MAX_COLS) + MAX_COLS*2);
// 	}
// 	else 
// 	{
// 		if (offset == (MAX_COLS * MAX_ROWS * 2)) scroll_line();
// 		write(character, attribute_byte, offset);
// 		set_cursor(offset+2);
// 	}
// }
// 
// void	scroll_line()
// {
// 	/* Функция скроллинга */
// 
// 	u8 i = 1;		// Начинаем со второй строки.
// 	u16 last_line;	// Начало последней строки.
// 
// 	while (i < MAX_ROWS)
// 	{
// 		memcpy(
// 			(u8 *)(VIDEO_ADDRESS + (MAX_COLS * i * 2)),
// 			(u8 *)(VIDEO_ADDRESS + (MAX_COLS * (i-1) * 2)),
// 			(MAX_COLS*2)
// 		);
// 		i++;
// 	}
// 
// 	last_line = (MAX_COLS*MAX_ROWS*2) - MAX_COLS*2;
// 	i = 0;
// 	while (i < MAX_COLS)
// 	{
// 		write('\0', WHITE_ON_BLACK, (last_line + i * 2));
// 		i++;
// 	}
// 	set_cursor(last_line);
// }
// 
// void	clear_screen()
// {
// 	/* Функция очистки экрана */
// 
// 	u16	offset = 0;
// 	while (offset < (MAX_ROWS * MAX_COLS * 2))
// 	{
// 		write('\0', WHITE_ON_BLACK, offset);
// 		offset += 2;
// 	}
// 	set_cursor(0);
// }
// 
// void	write(u8 character, u8 attribute_byte, u16 offset)
// {
// 	/* Функция печати символа на экран с помощью VGA по адресу 0xb8000 */
// 
// 	// u8 character: байт, соответствующий символу
// 	// u8 attribute_byte: байт, соответствующий цвету текста/фона символа
// 	// u16 offset: смещение (позиция), по которому нужно распечатать символ
// 	
// 	u8 *vga = (u8 *) VIDEO_ADDRESS;
// 	vga[offset] = character;
// 	vga[offset + 1] = attribute_byte;
// }
// 
// u16		get_cursor()
// {
// 	/* Функция, возвращающая позицию курсора (char offset). */
// 
// 	port_byte_out(REG_SCREEN_CTRL, 14);				// Запрашиваем верхний байт
// 	u8 high_byte = port_byte_in(REG_SCREEN_DATA);	// Принимаем его
// 	port_byte_out(REG_SCREEN_CTRL, 15);				// Запрашиваем нижний байт
// 	u8 low_byte = port_byte_in(REG_SCREEN_DATA);	// Принимаем и его
// 	// Возвращаем смещение умножая его на 2, т.к. порты возвращают смещение в
// 	// клетках экрана (cell offset), а нам нужно в символах (char offset), т.к.
// 	// на каждый символ у нас 2 байта
// 	return (((high_byte << 8) + low_byte) * 2);
// }
// 
// void	set_cursor(u16 pos)
// {
// 	/* Функция, устаналивающая курсор по смещнию (позиции) pos */
// 
// 	pos /= 2;	// конвертируем в cell offset (в позицию по клеткам, а не
// 				// символам)
// 	// Устанавливаем позицию курсора
// 	port_byte_out(REG_SCREEN_CTRL, 14);			// Указываем, что будем
// 												// передавать верхний байт
// 	port_byte_out(REG_SCREEN_DATA, (pos >> 8));	// Передаем верхний байт
// 	port_byte_out(REG_SCREEN_CTRL, 15);			// Указываем, что будем
// 												// передавать нижний байт
// 	port_byte_out(REG_SCREEN_DATA, (pos & 0xff));	// передаем нижний байт
// }
