/*------------------------------------------------------------------------------
*  Kintsugi OS Drivers source code
*  File: kernel/drivers/keyboard.c
*  Title: Функции работы с клавиатурой
*	Description: null
* ----------------------------------------------------------------------------*/


#include "keyboard.h"
#include "lowlevel_io.h"
#include "../cpu/isr.h"
#include "screen.h"
#include "../kklibc/stdlib.h"
#include "../kklibc/function.h"
#include "../kernel/kernel.h"

#define BACKSPACE 0x0E
#define ENTER 0x1C

extern int shell_cursor_offset;
extern int shell_prompt_offset;

static char key_buffer[256];

#define SC_MAX 57
const char *sc_name[] = { "ERROR", "Esc", "1", "2", "3", "4", "5", "6",
    "7", "8", "9", "0", "-", "=", "Backspace", "Tab", "Q", "W", "E",
        "R", "T", "Y", "U", "I", "O", "P", "[", "]", "Enter", "Lctrl",
        "A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'", "`",
        "LShift", "\\", "Z", "X", "C", "V", "B", "N", "M", ",", ".",
        "/", "RShift", "Keypad *", "LAlt", "Spacebar"};

const char sc_ascii[] = { '?', '?', '1', '2', '3', '4', '5', '6',
    '7', '8', '9', '0', '-', '=', '?', '?', 'Q', 'W', 'E', 'R', 'T', 'Y',
        'U', 'I', 'O', 'P', '[', ']', '?', '?', 'A', 'S', 'D', 'F', 'G',
        'H', 'J', 'K', 'L', ';', '\'', '`', '?', '\\', 'Z', 'X', 'C', 'V',
        'B', 'N', 'M', ',', '.', '/', '?', '?', '?', ' '};

static void keyboard_callback(registers_t regs) {
    /* PIC выходы сканкодов в порту 0x60 */
    u8 scancode = port_byte_in(0x60);
    int current_offset = get_cursor_offset();

    if (scancode > SC_MAX) return;
    if (scancode == BACKSPACE) {
        // kprintf("%d %d\n", current_offset, shell_prompt_offset);
        // return;
        if (current_offset <= shell_prompt_offset) {
            return;
        }

        backspace(key_buffer);
        kprint_backspace();
    } else if (scancode == ENTER) {
        kprint("\n");
        user_input(key_buffer); /* функция под управлением ядра */
        key_buffer[0] = '\0';
    } else {
        char letter = sc_ascii[(int)scancode];
        /* Запоминает только полученный char[] */
        char str[2] = {letter, '\0'};
        if(strlen(key_buffer) < sizeof(key_buffer) - 1) {
            append(key_buffer, letter);
            kprint(str);
            shell_cursor_offset = get_cursor_offset();
        } else {
            kprint("Keyboard buffer is overflow.");
        }
    }
    UNUSED(regs);
}

void init_keyboard() {
   register_interrupt_handler(IRQ1, keyboard_callback);
}
