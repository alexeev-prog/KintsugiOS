#ifndef KINTSUGI_TEXT_H
#define KINTSUGI_TEXT_H

/* UTF-8 at file/API boundaries, CP866 in the compact VGA-compatible cell ABI.
 * Unicode key events live above the legacy special-key range (256..). */
#define TEXT_UNICODE_KEY 0x10000

static inline unsigned text_glyph(unsigned cp) {
    if (cp < 128) {
        return cp;
    }
    if (cp >= 0x410 && cp <= 0x43f) {
        return cp - 0x410 + 0x80;
    }
    if (cp >= 0x440 && cp <= 0x44f) {
        return cp - 0x440 + 0xe0;
    }
    if (cp == 0x401) {
        return 0xf0;
    }
    if (cp == 0x451) {
        return 0xf1;
    }
    return '?';
}

/* Invalid/truncated sequences consume one byte, preserving legacy box glyphs. */
static inline unsigned text_decode(const char** text) {
    const unsigned char* p = (const unsigned char*)*text;
    unsigned cp = *p++, n = 0, minimum = 0;
    if (cp >= 0xc2 && cp <= 0xdf) {
        cp &= 31;
        n = 1;
        minimum = 0x80;
    } else if (cp >= 0xe0 && cp <= 0xef) {
        cp &= 15;
        n = 2;
        minimum = 0x800;
    } else if (cp >= 0xf0 && cp <= 0xf4) {
        cp &= 7;
        n = 3;
        minimum = 0x10000;
    } else {
        ++*text;
        return cp;
    }
    for (unsigned i = 0; i < n; ++i) {
        if ((p[i] & 0xc0) != 0x80) {
            return (unsigned char)*(*text)++;
        }
        cp = (cp << 6) | (p[i] & 63);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        return (unsigned char)*(*text)++;
    }
    *text = (const char*)(p + n);
    return text_glyph(cp);
}

static inline unsigned text_prev(const char* s, unsigned p) {
    if (p) {
        --p;
        while (p && ((unsigned char)s[p] & 0xc0) == 0x80) {
            --p;
        }
    }
    return p;
}

static inline unsigned text_next(const char* s, unsigned p) {
    if (s[p]) {
        ++p;
        while (((unsigned char)s[p] & 0xc0) == 0x80) {
            ++p;
        }
    }
    return p;
}

static inline unsigned text_columns(const char* s, unsigned bytes) {
    unsigned n = 0;
    for (unsigned i = 0; i < bytes && s[i]; i = text_next(s, i)) {
        ++n;
    }
    return n;
}

static inline unsigned text_key(int key, char out[4]) {
    if (key >= 32 && key < 127) {
        out[0] = key;
        return 1;
    }
    if (!(key & TEXT_UNICODE_KEY)) {
        return 0;
    }
    unsigned cp = key & 0xffff;
    if (cp < 0x80 || cp > 0x7ff) {
        return 0;
    }
    out[0] = 0xc0 | (cp >> 6);
    out[1] = 0x80 | (cp & 63);
    return 2;
}
#endif
