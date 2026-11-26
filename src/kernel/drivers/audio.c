#include "audio.h"

#include "../kernel/log.h"
#include "lowlevel_io.h"

/* Two backends:
 *  - OPL2 / YM3812 (QEMU "-device adlib"): 9-voice FM synthesis with real
 *    per-channel volume and several instrument timbres. Preferred.
 *  - PC speaker (PIT channel 2): 1-bit mono fallback, mute-only volume.
 */

static u32 volume = 70;
static int enabled;
static int opl2_present;

/* ---- OPL2 low level ---- */
#define OPL2_ADDR 0x388
#define OPL2_DATA 0x389

static void opl2_delay(int n) {
    while (n--) {
        port_byte_out(0x80, 0);
    }
}

static void opl2_write(u8 reg, u8 val) {
    port_byte_out(OPL2_ADDR, reg);
    opl2_delay(4);
    port_byte_out(OPL2_DATA, val);
    opl2_delay(16);
}

/* Channel -> modulator / carrier register offsets. */
static const u8 mod_off[9] = { 0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12 };
static const u8 car_off[9] = { 0x03, 0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x13, 0x14, 0x15 };

typedef struct {
    u8 mod_mult, mod_tl, mod_ar, mod_dr, mod_sl, mod_rr, mod_wave;
    u8 car_mult, car_ar, car_dr, car_sl, car_rr, car_wave;
    u8 feedback, connection;
} opl2_inst;

/* 0 koto/pluck, 1 shakuhachi/flute, 2 bell, 3 bass. */
static const opl2_inst insts[4] = {
    { 1, 18, 15, 8, 0, 8, 0, 1, 15, 7, 0, 8, 0, 3, 0 },
    { 1, 42, 6,  4, 2, 6, 0, 1, 6,  4, 2, 6, 0, 0, 1 },
    { 7, 0,  15, 5, 8, 9, 0, 1, 15, 6, 6, 9, 0, 0, 0 },
    { 1, 30, 12, 6, 3, 5, 0, 0, 12, 5, 3, 5, 0, 3, 0 },
};

static struct {
    int active;
    u32 remaining;
} voice[9];

static void opl2_freq(u32 freq, u32* fn, u32* bl) {
    if (freq < 20) {
        freq = 20;
    }
    if (freq > 6000) {
        freq = 6000;
    }
    u32 block = 1, fnum = 0;
    for (block = 1; block <= 7; ++block) {
        fnum = (freq << (20 - block)) / 49716;
        if (fnum <= 1023) {
            break;
        }
    }
    if (fnum > 1023) {
        fnum = 1023;
    }
    *fn = fnum;
    *bl = block;
}

static u8 carrier_level(void) {
    /* 0 = loudest, 63 = silent. Master volume maps to attenuation. */
    u32 lvl = 63 - (63 * volume / 100);
    return (u8)lvl;
}

static void opl2_setup_channel(u32 c, const opl2_inst* in) {
    u8 m = mod_off[c], k = car_off[c];
    opl2_write(0x20 + m, (in->mod_mult & 15) | 0x20); /* EG sustain */
    opl2_write(0x40 + m, in->mod_tl & 63);
    opl2_write(0x60 + m, (in->mod_ar << 4) | in->mod_dr);
    opl2_write(0x80 + m, (in->mod_sl << 4) | in->mod_rr);
    opl2_write(0xE0 + m, in->mod_wave & 3);
    opl2_write(0x20 + k, (in->car_mult & 15) | 0x20);
    opl2_write(0x40 + k, carrier_level());
    opl2_write(0x60 + k, (in->car_ar << 4) | in->car_dr);
    opl2_write(0x80 + k, (in->car_sl << 4) | in->car_rr);
    opl2_write(0xE0 + k, in->car_wave & 3);
    opl2_write(0xC0 + c, (in->feedback << 1) | (in->connection & 1));
}

static void opl2_key(u32 c, u32 freq, int on) {
    u32 fn, bl;
    opl2_freq(freq, &fn, &bl);
    opl2_write(0xA0 + c, fn & 255);
    opl2_write(0xB0 + c, ((fn >> 8) & 3) | (bl << 2) | (on ? 0x20 : 0));
}

static void opl2_all_off(void) {
    for (u32 c = 0; c < 9; ++c) {
        opl2_write(0xB0 + c, 0);
        voice[c].active = 0;
        voice[c].remaining = 0;
    }
}

static int opl2_detect(void) {
    opl2_write(0x04, 0x60);
    opl2_write(0x04, 0x80);
    u8 before = port_byte_in(OPL2_ADDR);
    opl2_write(0x02, 0xFF);
    opl2_write(0x04, 0x21);
    opl2_delay(200);
    u8 after = port_byte_in(OPL2_ADDR);
    opl2_write(0x04, 0x60);
    opl2_write(0x04, 0x80);
    /* A stale timer bit before the test makes the signature ambiguous. */
    return (before & 0xE0) != 0xC0 && (after & 0xE0) == 0xC0;
}

static void opl2_init(void) {
    opl2_write(0x01, 0x20); /* enable waveform select */
    opl2_write(0x08, 0x00);
    opl2_write(0xBD, 0x00); /* melodic mode, no percussion */
    opl2_all_off();
}

/* ---- PC speaker fallback ---- */
static u32 spk_remaining;

static void speaker_tone(u32 frequency, u32 milliseconds) {
    if (!frequency) {
        port_byte_out(0x61, port_byte_in(0x61) & 0xfc);
        spk_remaining = 0;
        return;
    }
    if (!volume || frequency < 20 || frequency > 20000) {
        return;
    }
    u32 divisor = 1193180 / frequency;
    port_byte_out(0x43, 0xb6);
    port_byte_out(0x42, (u8)divisor);
    port_byte_out(0x42, (u8)(divisor >> 8));
    port_byte_out(0x61, port_byte_in(0x61) | 3);
    spk_remaining = milliseconds / 20 + 1;
}

/* ---- public API ---- */
void audio_init(void) {
    enabled = 1;
    opl2_present = opl2_detect();
    if (opl2_present) {
        opl2_init();
    }
    log_event(
        "INFO", "audio", opl2_present ? "OPL2 FM synthesizer detected" : "OPL2 absent, PC speaker fallback");
}

int audio_is_opl2(void) {
    return opl2_present;
}

void audio_volume(u32 value) {
    volume = value > 100 ? 100 : value;
    if (opl2_present) {
        /* Re-apply carrier attenuation to every live voice. */
        for (u32 c = 0; c < 9; ++c) {
            if (voice[c].active) {
                opl2_write(0x40 + car_off[c], carrier_level());
            }
        }
        /* Volume 0 is implemented by full carrier attenuation: live notes
         * keep their key-on state and resume when the volume is raised. */
    } else if (!volume) {
        port_byte_out(0x61, port_byte_in(0x61) & 0xfc);
    }
}

u32 audio_get_volume(void) {
    return volume;
}

int audio_is_enabled(void) {
    return enabled;
}

void audio_tone_inst(u32 frequency, u32 milliseconds, u32 instrument) {
    if (!enabled) {
        return;
    }
    if (!frequency) {
        if (opl2_present) {
            opl2_all_off();
        } else {
            speaker_tone(0, 0);
        }
        return;
    }
    if (!volume) {
        return;
    }
    if (instrument > 3) {
        instrument = 0;
    }
    if (opl2_present) {
        /* Find a free voice, else steal the one closest to finishing. */
        u32 c = 9, best = 0xffffffffu;
        for (u32 i = 0; i < 9; ++i) {
            if (!voice[i].active) {
                c = i;
                break;
            }
            if (voice[i].remaining < best) {
                best = voice[i].remaining;
                c = i;
            }
        }
        if (c == 9) {
            c = 0;
        }
        opl2_setup_channel(c, &insts[instrument]);
        opl2_key(c, frequency, 1);
        voice[c].active = 1;
        voice[c].remaining = milliseconds / 20 + 1;
    } else {
        speaker_tone(frequency, milliseconds);
    }
}

void audio_tone(u32 frequency, u32 milliseconds) {
    audio_tone_inst(frequency, milliseconds, 0);
}

void audio_tick(void) {
    if (opl2_present) {
        for (u32 c = 0; c < 9; ++c) {
            if (voice[c].active && voice[c].remaining && !--voice[c].remaining) {
                opl2_key(c, 0, 0);
                voice[c].active = 0;
            }
        }
    } else if (spk_remaining && !--spk_remaining) {
        port_byte_out(0x61, port_byte_in(0x61) & 0xfc);
    }
}
