#include "timer.h"

#include "../drivers/audio.h"
#include "../drivers/lowlevel_io.h"
#include "../drivers/terminal.h"
#include "../kklibc/function.h"
#include "isr.h"

u32 tick = 0;

static void timer_callback(registers_t regs) {
    tick++;
    audio_tick();
    if (!(tick % 50)) {
        terminal_get_state()->dirty = 1;
    }
    terminal_refresh();
    UNUSED(regs);
}

void init_timer(u32 freq) {
    /* Install the function we just wrote */
    register_interrupt_handler(IRQ0, timer_callback);

    /* Get the PIT value: hardware clock at 1193180 Hz */
    u32 divisor = 1193180 / freq;
    u8 low = (u8)(divisor & 0xFF);
    u8 high = (u8)((divisor >> 8) & 0xFF);
    /* Send the command */
    port_byte_out(0x43, 0x36); /* Command port */
    port_byte_out(0x40, low);
    port_byte_out(0x40, high);
}
