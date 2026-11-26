#include "kms.h"

static unsigned read_number(const char** cursor) {
    const char* p = *cursor;
    while (*p == ' ' || *p == '\t' || *p == ',' || *p == ';') {
        ++p;
    }
    if (*p < '0' || *p > '9') {
        *cursor = p;
        return 0xffffffffu;
    }
    unsigned value = 0;
    while (*p >= '0' && *p <= '9') {
        unsigned digit = (unsigned)(*p++ - '0');
        if (value > (0xffffffu - digit) / 10u) {
            return 0xffffffffu;
        }
        value = value * 10 + digit;
    }
    *cursor = p;
    return value;
}

int kms_load(const char* path, kms_track* track) {
    char data[4096];
    int size = syscall(2, (u32)path, 0, (u32)data, sizeof(data) - 1);
    track->count = 0;
    track->default_duration = 220;
    track->title[0] = 0;
    if (size <= 0) {
        return 0;
    }
    data[size] = 0;
    char* line = data;
    while (*line && track->count < KMS_MAX_NOTES) {
        char* end = line;
        while (*end && *end != '\n' && *end != '\r') {
            ++end;
        }
        char saved = *end;
        *end = 0;
        char* p = line;
        while (*p == ' ' || *p == '\t' || *p == '#') {
            ++p;
        }
        if (*p) {
            if (p[0] == 'n' && p[1] == 'a' && p[2] == 'm' && p[3] == 'e' && p[4] == ' ') {
                unsigned n = 0;
                const char* text = p + 5;
                while (*text && n + 1 < sizeof(track->title)) {
                    track->title[n++] = *text++;
                }
                track->title[n] = 0;
            } else if (p[0] == 't' && p[1] == 'e' && p[2] == 'm' && p[3] == 'p' && p[4] == 'o') {
                const char* value = p + 5;
                while (*value == ' ' || *value == '=') {
                    ++value;
                }
                unsigned parsed = read_number(&value);
                if (parsed != 0xffffffffu && parsed >= 40 && parsed <= 2000) {
                    track->default_duration = parsed;
                }
            } else {
                unsigned frequency = read_number((const char**)&p);
                unsigned duration = read_number((const char**)&p);
                unsigned instrument = read_number((const char**)&p);
                if (frequency != 0xffffffffu && duration != 0xffffffffu) {
                    kms_note* note = &track->notes[track->count++];
                    note->frequency = frequency > 20000 ? 0 : frequency;
                    note->duration = duration < 40 || duration > 2000 ? track->default_duration : duration;
                    note->instrument = instrument > 3 ? 0 : instrument;
                }
            }
        }
        if (!saved) {
            break;
        }
        line = end + 1;
    }
    if (!track->title[0]) {
        const char* slash = path;
        for (const char* p = path; *p; ++p) {
            if (*p == '/') {
                slash = p + 1;
            }
        }
        unsigned n = 0;
        while (*slash && n + 1 < sizeof(track->title) && *slash != '.') {
            track->title[n++] = *slash++;
        }
        track->title[n] = 0;
    }
    return track->count > 0;
}

void kms_step(const kms_track* track, unsigned* position) {
    if (!track->count) {
        return;
    }
    const kms_note* note = &track->notes[*position];
    u32 duration = note->duration;
    if (note->frequency) {
        u32 sounded = duration > 30 ? duration - 30 : duration;
        syscall(KOS_SYS_SOUND_TONE, note->frequency, sounded, note->instrument, 0);
    }
    *position = (*position + 1) % track->count;
}

const char* kms_instrument_name(unsigned instrument) {
    static const char* names[] = { "koto", "flute", "bell", "bass" };
    return instrument < 4 ? names[instrument] : "koto";
}
