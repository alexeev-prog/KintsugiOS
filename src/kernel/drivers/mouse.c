#include "mouse.h"
#include "lowlevel_io.h"
#include "framebuffer.h"
#include "terminal.h"
#include "../cpu/isr.h"

static kos_mouse state = {640, 400, 0, 0, 0};
static u8 packet[3], index;
const kos_mouse* mouse_state(void) { return &state; }
static int writable(void) {
    for (u32 i = 0; i < 100000; ++i) if (!(port_byte_in(0x64) & 2)) return 1;
    return 0;
}
static int readable(void) {
    for (u32 i = 0; i < 100000; ++i) if (port_byte_in(0x64) & 1) return 1;
    return 0;
}
static int command(u8 value) {
    if (!writable()) return 0;
    port_byte_out(0x64, 0xd4);
    if (!writable()) return 0;
    port_byte_out(0x60, value);
    return readable() && port_byte_in(0x60) == 0xfa;
}
void mouse_byte(u8 byte) {
    if (!index && !(byte & 8)) return;
    packet[index++] = byte;
    if (index != 3) return;
    index = 0;
    if (packet[0] & 0xc0) return;
    int dx = packet[1] - ((packet[0] & 0x10) ? 256 : 0);
    int dy = packet[2] - ((packet[0] & 0x20) ? 256 : 0);
    state.x += dx; state.y -= dy;
    const framebuffer_info_t* fb = framebuffer_get_info();
    int width = fb->available ? (int)fb->width : 640, height = fb->available ? (int)fb->height : 400;
    if (state.x < 0) state.x = 0;
    if (state.y < 0) state.y = 0;
    if (state.x >= width) state.x = width - 1;
    if (state.y >= height) state.y = height - 1;
    state.buttons = packet[0] & 7;
    ++state.sequence;
    terminal_get_state()->dirty = 1;
}
static void callback(registers_t r) {
    (void)r;
    if ((port_byte_in(0x64) & 0x21) == 0x21) mouse_byte(port_byte_in(0x60));
}
void mouse_init(void) {
    if (!writable()) return;
    port_byte_out(0x64, 0xa8);
    if (!writable()) return;
    port_byte_out(0x64, 0x20);
    if (!readable()) return;
    u8 config = port_byte_in(0x60);
    if (!writable()) return;
    port_byte_out(0x64, 0x60);
    if (!writable()) return;
    port_byte_out(0x60, (config | 2) & ~0x20);
    register_interrupt_handler(IRQ12, callback);
    if (command(0xf6) && command(0xf4)) state.available = 1;
}
