#include "audio.h"
#include "lowlevel_io.h"

static u32 volume = 70, remaining;
static int enabled;

void audio_init(void) { enabled = 1; }
void audio_volume(u32 value) { volume = value > 100 ? 100 : value; if (!volume) port_byte_out(0x61, port_byte_in(0x61) & 0xfc); }
u32 audio_get_volume(void) { return volume; }
int audio_is_enabled(void) { return enabled; }
void audio_tone(u32 frequency, u32 milliseconds) {
    if (!enabled || !volume || !frequency || frequency > 20000) return;
    u32 divisor = 1193180 / frequency;
    port_byte_out(0x43, 0xb6);
    port_byte_out(0x42, (u8)divisor);
    port_byte_out(0x42, (u8)(divisor >> 8));
    port_byte_out(0x61, port_byte_in(0x61) | 3);
    remaining = milliseconds / 20 + 1;
}
void audio_tick(void) {
    if (remaining && !--remaining) port_byte_out(0x61, port_byte_in(0x61) & 0xfc);
}
