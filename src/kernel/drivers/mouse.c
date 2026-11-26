#include "mouse.h"

#include "../cpu/isr.h"
#include "../kernel/log.h"
#include "framebuffer.h"
#include "lowlevel_io.h"
#include "terminal.h"

static kos_mouse state = { 640, 400, 0, 0, 0, 0 };
static u8 packet[4], index, packet_bytes = 3;

const kos_mouse* mouse_state(void) {
    return &state;
}

int mouse_take_wheel(void) {
    int value = state.wheel;
    state.wheel = 0;
    return value;
}

static int writable(void) {
    for (u32 i = 0; i < 100000; ++i) {
        if (!(port_byte_in(0x64) & 2)) {
            return 1;
        }
    }
    return 0;
}

static int readable(void) {
    for (u32 i = 0; i < 100000; ++i) {
        if (port_byte_in(0x64) & 1) {
            return 1;
        }
    }
    return 0;
}

static int command(u8 value) {
    if (!writable()) {
        return 0;
    }
    port_byte_out(0x64, 0xd4);
    if (!writable()) {
        return 0;
    }
    port_byte_out(0x60, value);
    return readable() && port_byte_in(0x60) == 0xfa;
}

/* IMPS/2 wheel probe: set 8 counts/mm three times, then read the device ID.
 * A wheel mouse answers 0x03 and sends a fourth packet byte with dz. */
static int probe_wheel(void) {
    /* IntelliMouse unlock: Set Sample Rate 200, 100, 80, then Read ID.
     * 0xF3 is AUX_SET_SAMPLE; 0xE8 would set resolution instead. */
    static const u8 rates[3] = { 200, 100, 80 };
    for (int i = 0; i < 3; ++i) {
        if (!command(0xf3) || !command(rates[i])) {
            return 0;
        }
    }
    if (!command(0xf2) || !readable()) {
        return 0;
    }
    u8 id = port_byte_in(0x60);
    if (id == 0x03) {
        packet_bytes = 4;
        return 1;
    }
    return 0;
}

void mouse_byte(u8 byte) {
    if (!index && !(byte & 8)) {
        return;
    }
    if (index >= packet_bytes) {
        index = 0;
    }
    packet[index++] = byte;
    if (index != packet_bytes) {
        return;
    }
    index = 0;
    if (packet[0] & 0xc0) {
        return;
    }
    int dx = packet[1] - ((packet[0] & 0x10) ? 256 : 0);
    int dy = packet[2] - ((packet[0] & 0x20) ? 256 : 0);
    state.x += dx;
    state.y -= dy;
    const framebuffer_info_t* fb = framebuffer_get_info();
    int width = fb->available ? (int)fb->width : 640, height = fb->available ? (int)fb->height : 400;
    if (state.x < 0) {
        state.x = 0;
    }
    if (state.y < 0) {
        state.y = 0;
    }
    if (state.x >= width) {
        state.x = width - 1;
    }
    if (state.y >= height) {
        state.y = height - 1;
    }
    state.buttons = packet[0] & 7;
    if (packet_bytes == 4) {
        int dz = (int)(signed char)packet[3];
        if (dz > 4) {
            dz = 4;
        }
        if (dz < -4) {
            dz = -4;
        }
        state.wheel += dz;
        if (state.wheel > 32) {
            state.wheel = 32;
        }
        if (state.wheel < -32) {
            state.wheel = -32;
        }
    }
    ++state.sequence;
    terminal_get_state()->dirty = 1;
}

static void callback(registers_t r) {
    (void)r;
    if ((port_byte_in(0x64) & 0x21) == 0x21) {
        mouse_byte(port_byte_in(0x60));
    }
}

void mouse_init(void) {
    if (!writable()) {
        return;
    }
    port_byte_out(0x64, 0xa8);
    if (!writable()) {
        return;
    }
    port_byte_out(0x64, 0x20);
    if (!readable()) {
        return;
    }
    u8 config = port_byte_in(0x60);
    if (!writable()) {
        return;
    }
    port_byte_out(0x64, 0x60);
    if (!writable()) {
        return;
    }
    port_byte_out(0x60, (config | 2) & ~0x20);
    register_interrupt_handler(IRQ12, callback);
    if (command(0xf6)) {
        probe_wheel();
        if (command(0xf4)) {
            state.available = 1;
        }
        log_event(
            "INFO",
            "mouse",
            packet_bytes == 4 ? "IMPS/2 wheel enabled (4-byte packets)" : "PS/2 mouse (3-byte packets)");
    }
}
