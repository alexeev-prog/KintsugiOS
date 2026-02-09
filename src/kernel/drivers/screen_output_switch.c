#include "screen_output_switch.h"

#include "screen.h"
#include "terminal.h"

u8 g_output_mode = OUTPUT_MODE_SCREEN;

void output_set_mode(u8 mode) {
    g_output_mode = mode;
}
