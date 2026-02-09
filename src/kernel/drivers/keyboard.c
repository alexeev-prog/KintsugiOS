/*------------------------------------------------------------------------------
 *  Kintsugi OS Drivers source code
 *  File: kernel/drivers/keyboard.c
 *  Title: Функции работы с клавиатурой
 *  Author: alexeev-prog
 *  License: MIT License
 * ------------------------------------------------------------------------------
 *	Description: null
 * ----------------------------------------------------------------------------*/

#include "keyboard.h"

#include "../cpu/isr.h"
#include "../kernel/kernel.h"
#include "../kklibc/function.h"
#include "../kklibc/stdlib.h"
#include "lowlevel_io.h"
#include "screen.h"
#include "terminal.h"

#define BACKSPACE 0x0E
#define ENTER 0x1C

extern int shell_cursor_offset;
extern int shell_prompt_offset;

static char key_buffer[256];

#define SC_MAX 57

// Состояния модификаторов
static int shift_pressed = 0;
static int caps_lock = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;

// Таблицы символов для разных состояний
const char sc_ascii_lower[] = { '?', '?', '1', '2', '3', '4', '5', '6', '7', '8', '9',  '0', '-', '=',  '?',
                                '?', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',  '[', ']', '?',  '?',
                                'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', '?', '\\', 'z',
                                'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', '?', '?',  '?', ' ' };

const char sc_ascii_upper[] = { '?', '?', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '?',
                                '?', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '?', '?',
                                'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', '?', '|', 'Z',
                                'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', '?', '?', '?', ' ' };

static void keyboard_callback(registers_t regs) {
    u8 scancode = port_byte_in(0x60);

    // Обработка отпускания клавиш (старший бит установлен)
    if (scancode & 0x80) {
        u8 released_key = scancode & 0x7F;

        switch (released_key) {
            case 0x2A:    // Left Shift
            case 0x36:    // Right Shift
                shift_pressed = 0;
                return;
            case 0x1D:    // Ctrl
                ctrl_pressed = 0;
                break;
            case 0x38:    // Alt
                alt_pressed = 0;
                break;
        }
    }
    // Обработка нажатия клавиш
    else {
        switch (scancode) {
            case 0x2A:    // Left Shift
            case 0x36:    // Right Shift
                shift_pressed = 1;
                break;
            case 0x1D:    // Ctrl
                ctrl_pressed = 1;
                break;
            case 0x38:    // Alt
                alt_pressed = 1;
                break;
            case 0x3A:    // Caps Lock
                caps_lock = !caps_lock;
                break;
            case 0x48:    // Стрелка вверх
                if (shift_pressed) {
                    // Shift+Вверх - прокрутка терминала
                    terminal_handle_arrow_up();
                } else {
                    // Просто стрелка вверх - история команд
                    // TODO: реализовать историю
                }
                break;

            case 0x50:    // Стрелка вниз
                if (shift_pressed) {
                    // Shift+Вниз - прокрутка терминала
                    terminal_handle_arrow_down();
                } else {
                    // Просто стрелка вниз - история команд
                    // TODO: реализовать историю
                }
                break;
            default:
                if (scancode > SC_MAX) {
                    return;
                }

                char letter;
                int uppercase = shift_pressed ^ caps_lock;    // XOR для правильной логики

                // Выбираем правильную таблицу символов
                if (uppercase) {
                    letter = sc_ascii_upper[scancode];
                } else {
                    letter = sc_ascii_lower[scancode];
                }

                // Специальная обработка для комбинаций с Ctrl
                if (ctrl_pressed) {
                    // Ctrl+ прерывания
                    if (letter == 'c') {
                        kprint("^C");
                        // todo: обрывание процесса
                        key_buffer[0] = '\0';
                        return;
                    }
                }

                // Остальная обработка как раньше
                if (scancode == BACKSPACE) {
                    if (get_cursor_offset() <= shell_prompt_offset) {
                        return;
                    }
                    backspace(key_buffer);
                    kprint_backspace();

                    /* Также обрабатываем для общего ввода */
                    handle_input_char(0x08);
                } else if (scancode == ENTER) {
                    kprint("\n");
                    user_input(key_buffer);
                    key_buffer[0] = '\0';

                    /* Также обрабатываем для общего ввода */
                    handle_input_char('\n');
                } else {
                    if (strlen(key_buffer) < sizeof(key_buffer) - 1) {
                        append(key_buffer, letter);
                        char str[2] = { letter, '\0' };
                        kprint(str);

                        /* Также обрабатываем для общего ввода */
                        handle_input_char(letter);
                    }
                }
                break;
        }
    }
    UNUSED(regs);
}

void init_keyboard() {
    register_interrupt_handler(IRQ1, keyboard_callback);
}
