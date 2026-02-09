#ifndef OUTPUT_SWITCH_H
#define OUTPUT_SWITCH_H

#include "../kklibc/ctypes.h"

/* Режимы вывода */
#define OUTPUT_MODE_SCREEN 0
#define OUTPUT_MODE_TERMINAL 1

extern u8 g_output_mode;

/* Функция переключения режима */
void output_set_mode(u8 mode);

/* Вспомогательные макросы */
#define IS_TERMINAL_MODE() (g_output_mode == OUTPUT_MODE_TERMINAL)
#define IS_SCREEN_MODE() (g_output_mode == OUTPUT_MODE_SCREEN)

#endif
