#include "ktuilib.h"

/* Shared pentatonic material: D minor pentatonic-ish "night garden" scale.
 * 0 is a rest. All playback is paced with KOS_SYS_SLEEP so notes do not
 * overwrite each other in the single-voice PC speaker. */
static void sleep_ms(u32 ms) { syscall(KOS_SYS_SLEEP, ms, 0, 0, 0); }
static void tone(unsigned n, u32 ms) { syscall(KOS_SYS_SOUND_TONE, n, ms, 0, 0); }
#if SOUND_APP != 1
static const unsigned garden[] = {
    294, 330, 392, 0, 440, 392, 330, 0,
    294, 330, 392, 440, 523, 0, 440, 392,
    330, 294, 262, 0, 294, 330, 294, 0
};
#endif
#if SOUND_APP == 2
static const unsigned sakura[] = {
    440, 440, 494, 0, 440, 440, 494, 0,
    440, 494, 523, 494, 440, 494, 440, 392,
    330, 330, 392, 440, 494, 440, 392, 330
};
#endif
#if SOUND_APP != 2
static void play(const unsigned* notes, unsigned count, u32 tempo) {
    for (unsigned i = 0; i < count; ++i) {
        if (notes[i]) tone(notes[i], tempo - 30);
        sleep_ms(tempo);
    }
}
#endif

#if SOUND_APP == 1
/* KMusa - procedural motif generator. Enter picks a new variation from a
 * deterministic seed, P replays the current one. */
static unsigned seed = 0x9e3779b9u;
static unsigned rnd(unsigned n) { seed = seed * 1664525u + 1013904223u; return n ? (seed >> 16) % n : 0; }
static unsigned motif[16];
static void generate(void) {
    static const unsigned scale[] = {262, 294, 330, 392, 440, 523, 587, 659};
    for (int i = 0; i < 16; ++i) motif[i] = rnd(6) == 0 ? 0 : scale[rnd(8)];
    motif[15] = 262; /* resolve to the tonic */
}
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    generate();
    for (;;) {
        tui_clear(TUI_DESKTOP);
        tui_bar(0, "KMusa - Japanese motif generator");
        tui_box(6, 3, 68, 16, "Moonlit garden", TUI_BORDER);
        tui_text(10, 6, "Enter: new motif   P: play   Q: quit", TUI_STATUS);
        tui_text(10, 8, "Current motif:", TUI_KEYWORD);
        for (int i = 0; i < 16; ++i) {
            if (motif[i]) tui_number(10 + (i % 8) * 8, 10 + (i / 8) * 2, motif[i], TUI_NUMBER);
            else tui_text(10 + (i % 8) * 8, 10 + (i / 8) * 2, "-", TUI_COMMENT);
        }
        tui_text(10, 15, "Single-voice PC speaker; volume: volumectl / soundctl", TUI_COMMENT);
        tui_status(24); tui_present(79, 24);
        int key = tui_key(1);
        if (key == 'q' || key == 27) return 0;
        if (key == '\n') generate();
        if (key == 'p' || key == '\n') play(motif, 16, 220);
    }
}
#elif SOUND_APP == 2
/* KListen - TUI player with a real playlist, progress and pause. */
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    const char* tracks[] = { "Moonlit garden", "Sakura drift" };
    const unsigned* music[] = { garden, sakura };
    u32 selected = 0, note = 0;
    int playing = 0;
    for (;;) {
        tui_clear(TUI_DESKTOP);
        tui_bar(0, "KListen - TUI audio player");
        tui_box(6, 3, 68, 16, "Playlist", TUI_BORDER);
        tui_list_draw(10, 6, 60, 2, tracks, 2, selected, 0, 1);
        tui_text(10, 10, playing ? "Playing" : "Stopped", playing ? TUI_SUCCESS : TUI_STATUS);
        tui_gauge(10, 12, 60, note, 24);
        tui_text(10, 14, "Enter play/pause | Up/Down track | Q quit", TUI_STATUS);
        tui_status(24); tui_present(79, 24);
        if (playing) {
            unsigned n = music[selected][note];
            if (n) tone(n, 200);
            note = (note + 1) % 24;
            sleep_ms(230);
            int key = tui_key(0);
            if (key == ' ' || key == '\n') playing = 0;
            else if (key == 'q' || key == 27) return 0;
            else if (key == TUI_UP && selected) { --selected; note = 0; }
            else if (key == TUI_DOWN && selected < 1) { ++selected; note = 0; }
        } else {
            int key = tui_key(1);
            if (key == 'q' || key == 27) return 0;
            if (key == '\n' || key == ' ') playing = 1;
            if (key == TUI_UP && selected) --selected;
            if (key == TUI_DOWN && selected < 1) ++selected;
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
        if (key == 't') { syscall(KOS_SYS_SOUND_TONE, 440, 250, 0, 0); sleep_ms(270); }
        if (key >= '1' && key <= '4') { syscall(KOS_SYS_SOUND_TONE, 440, 300, key - '1', 0); sleep_ms(320); }
        if (key == 'm') play(garden, 24, 200);
    }
}
#endif