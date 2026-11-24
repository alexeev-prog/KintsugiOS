#ifndef USER_KMS_H
#define USER_KMS_H

/* Kintsugi Music: a tiny plain-text note format.
 * Lines are "frequency duration_ms instrument"; 0 frequency is a rest.
 * "name <title>" and "tempo <ms>" headers are optional. */
#include "runtime.h"
#include "desktopabi.h"

#define KMS_MAX_NOTES 128

typedef struct {
    unsigned frequency;
    u32 duration;
    unsigned instrument;
} kms_note;

typedef struct {
    kms_note notes[KMS_MAX_NOTES];
    unsigned count;
    u32 default_duration;
    char title[32];
} kms_track;

/* Returns 1 when at least one note was parsed. */
int kms_load(const char* path, kms_track* track);
/* Plays one note and advances the cursor. Caller handles pacing and input. */
void kms_step(const kms_track* track, unsigned* position);
const char* kms_instrument_name(unsigned instrument);

#endif
