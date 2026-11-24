#include "ktuilib.h"
#include "kms.h"

/* Shared audio helpers. KMusa generates motifs, KListen plays real .KMS
 * files from /etc/music, SoundCtl is the mixer/service panel. */

static void sleep_ms(u32 ms) { syscall(KOS_SYS_SLEEP, ms, 0, 0, 0); }
#if SOUND_APP != 2
static void tone(unsigned frequency, u32 ms, unsigned instrument) {
    syscall(KOS_SYS_SOUND_TONE, frequency, ms, instrument, 0);
}
#endif

#if SOUND_APP == 2
static u32 sound_volume(void) {
    u32 info[3] = {0};
    syscall(KOS_SYS_SOUND_INFO, (u32)info, 0, 0, 0);
    return info[0];
}
#endif

#if SOUND_APP == 2
static kms_track playlists[2];
static unsigned track_selected, track_position;
static int load_track(unsigned index) {
    static const char* const files[] = { "/etc/music/garden.kms", "/etc/music/sakura.kms" };
    return kms_load(files[index], &playlists[index]);
}
#endif

#if SOUND_APP != 2
static void play(const kms_note* notes, unsigned count, u32 tempo) {
    for (unsigned i = 0; i < count; ++i) {
        u32 duration = notes[i].duration ? notes[i].duration : tempo;
        if (notes[i].frequency) tone(notes[i].frequency, duration > 30 ? duration - 30 : duration,
                                     notes[i].instrument);
        sleep_ms(duration);
    }
}
#endif

#if SOUND_APP == 1
/* KMusa - procedural motif generator with OPL2 timbre variation. */
static unsigned seed = 0x9e3779b9u;
static unsigned rnd(unsigned n) { seed = seed * 1664525u + 1013904223u; return n ? (seed >> 16) % n : 0; }
static kms_note motif[16];
static void generate(void) {
    static const unsigned scale[] = {262, 294, 330, 392, 440, 523, 587, 659};
    for (int i = 0; i < 16; ++i) {
        motif[i].frequency = rnd(6) == 0 ? 0 : scale[rnd(8)];
        motif[i].duration = 220;
        motif[i].instrument = (unsigned)rnd(4);
    }
    motif[15].frequency = 262;
    motif[15].instrument = 3;
}
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    u32 info[3] = {0};
    syscall(KOS_SYS_SOUND_INFO, (u32)info, 0, 0, 0);
    generate();
    for (;;) {
        tui_clear(TUI_DESKTOP);
        tui_bar(0, "KMusa - Japanese motif generator");
        tui_box(6, 3, 68, 16, "Moonlit garden", TUI_BORDER);
        tui_text(10, 6, "Enter: new motif   P: play   Q: quit", TUI_STATUS);
        tui_text(10, 8, "Current motif:", TUI_KEYWORD);
        for (int i = 0; i < 16; ++i) {
            int x = 10 + (i % 8) * 8, y = 10 + (i / 8) * 2;
            if (motif[i].frequency) {
                tui_number(x, y, motif[i].frequency, TUI_NUMBER);
                tui_text(x + 5, y, kms_instrument_name(motif[i].instrument)[0] == 'k' ? "k" : "f", TUI_COMMENT);
            } else {
                tui_text(x, y, "-", TUI_COMMENT);
            }
        }
        tui_text(10, 15, info[2] ? "OPL2: 4 timbres, 9 voices" : "PC speaker: monophonic fallback", TUI_COMMENT);
        tui_text(10, 16, "Volume: volumectl up|down|set N", TUI_COMMENT);
        tui_status(24); tui_present(79, 24);
        int key = tui_key(1);
        if (key == 'q' || key == 27) return 0;
        if (key == '\n') generate();
        if (key == 'p' || key == '\n') play(motif, 16, 220);
    }
}
#elif SOUND_APP == 2
/* KListen - TUI player for text audio files under /etc/music. */
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    const char* titles[] = { "Moonlit garden", "Sakura drift" };
    load_track(0);
    int playing = 0;
    for (;;) {
        kms_track* track = &playlists[track_selected];
        tui_clear(TUI_DESKTOP);
        tui_bar(0, "KListen - TUI audio player");
        tui_box(6, 3, 68, 16, "Playlist (.KMS)", TUI_BORDER);
        tui_list_draw(10, 6, 60, 2, titles, 2, track_selected, 0, 1);
        tui_text(10, 10, playing ? "Playing" : "Stopped", playing ? TUI_SUCCESS : TUI_STATUS);
        tui_gauge(10, 12, 60, track_position, track->count ? track->count : 1);
        tui_text(10, 14, "Enter play/pause | Up/Down track | Q quit", TUI_STATUS);
        tui_text(10, 15, "Volume:", TUI_COMMENT);
        tui_number(18, 15, sound_volume(), TUI_NUMBER);
        tui_text(22, 15, "%", TUI_COMMENT);
        if (track->count && track_position < track->count) {
            tui_text(30, 15, track->title, TUI_KEYWORD);
            tui_text(60, 15, kms_instrument_name(track->notes[track_position].instrument), TUI_COMMENT);
        }
        tui_status(24); tui_present(79, 24);
        if (playing && track->count) {
            u32 duration = track->notes[track_position].duration;
            kms_step(track, &track_position);
            if (!duration) duration = track->default_duration;
            sleep_ms(duration + 40);
            int key = tui_key(0);
            if (key == ' ' || key == '\n') playing = 0;
            else if (key == 'q' || key == 27) return 0;
            else if (key == TUI_UP && track_selected) { --track_selected; track_position = 0; }
            else if (key == TUI_DOWN && track_selected < 1) { ++track_selected; track_position = 0; }
        } else {
            int key = tui_key(1);
            if (key == 'q' || key == 27) return 0;
            if (key == '\n' || key == ' ') {
                if (!load_track(track_selected)) { track->count = 0; track_position = 0; }
                playing = 1;
            }
            if (key == TUI_UP && track_selected) --track_selected;
            if (key == TUI_DOWN && track_selected < 1) ++track_selected;
        }
    }
}
#else
/* SoundCtl - mixer and service panel. */
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    u32 info[3] = {0};
    for (;;) {
        syscall(KOS_SYS_SOUND_INFO, (u32)info, 0, 0, 0);
        tui_clear(TUI_DESKTOP);
        tui_bar(0, "SoundCtl - audio service and volume");
        tui_box(10, 3, 60, 17, info[2] ? "OPL2 FM audio" : "PC speaker audio", TUI_BORDER);
        tui_text(15, 6, "Up/Down volume | T test | 1-4 instruments | M motif", TUI_STATUS);
        tui_text(15, 9, "Volume:", TUI_KEYWORD);
        tui_gauge(25, 9, 30, info[0], 100);
        tui_number(57, 9, info[0], TUI_NUMBER);
        tui_text(61, 9, "%", TUI_STATUS);
        tui_text(15, 12, info[1] ? "Audio service: online" : "Audio service: offline",
                 info[1] ? TUI_SUCCESS : TUI_ERROR);
        tui_text(15, 14, info[2] ? "Backend: OPL2 (polyphonic, real volume)" : "Backend: PC speaker (mono)", TUI_COMMENT);
        tui_text(15, 16, info[0] ? "Output: unmuted" : "Output: muted", info[0] ? TUI_STATUS : TUI_WARNING);
        tui_status(24); tui_present(79, 24);
        int key = tui_key(1);
        if (key == 'q' || key == 27) return 0;
        if (key == TUI_UP) { info[0] = info[0] + 5 > 100 ? 100 : info[0] + 5; syscall(KOS_SYS_SOUND_VOLUME, info[0], 0, 0, 0); }
        if (key == TUI_DOWN) { info[0] = info[0] < 5 ? 0 : info[0] - 5; syscall(KOS_SYS_SOUND_VOLUME, info[0], 0, 0, 0); }
        if (key == 't') { tone(440, 250, 0); sleep_ms(270); }
        if (key >= '1' && key <= '4') { tone(440, 300, (unsigned)(key - '1')); sleep_ms(320); }
        if (key == 'm') {
            static const kms_note sample[] = {
                {294, 220, 0}, {330, 220, 0}, {392, 220, 1}, {0, 220, 0},
                {440, 220, 0}, {392, 220, 1}, {330, 220, 0}, {262, 240, 3}
            };
            play(sample, 8, 220);
        }
    }
}
#endif
