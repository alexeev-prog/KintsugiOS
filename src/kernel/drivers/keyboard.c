#include "keyboard.h"

#include "../../shared/text.h"
#include "../cpu/isr.h"
#include "../kklibc/function.h"
#include "../user/process.h"
#include "lowlevel_io.h"
#include "mouse.h"
#include "terminal.h"

#define EVENT_COUNT 128
static int events[EVENT_COUNT];
static volatile u32 head, tail;
static u8 shifts, caps, control, extended;
static u8 alt, russian;
static u8 layout_latched;

int keyboard_russian(void) {
    return russian;
}

void keyboard_set_layout(int value) {
    russian = !!value;
    terminal_get_state()->dirty = 1;
}

static const u16 ru[58] = { [0x10] = 0x439, [0x11] = 0x446, [0x12] = 0x443, [0x13] = 0x43a, [0x14] = 0x435,
                            [0x15] = 0x43d, [0x16] = 0x433, [0x17] = 0x448, [0x18] = 0x449, [0x19] = 0x437,
                            [0x1a] = 0x445, [0x1b] = 0x44a, [0x1e] = 0x444, [0x1f] = 0x44b, [0x20] = 0x432,
                            [0x21] = 0x430, [0x22] = 0x43f, [0x23] = 0x440, [0x24] = 0x43e, [0x25] = 0x43b,
                            [0x26] = 0x434, [0x27] = 0x436, [0x28] = 0x44d, [0x29] = 0x451, [0x2c] = 0x44f,
                            [0x2d] = 0x447, [0x2e] = 0x441, [0x2f] = 0x43c, [0x30] = 0x438, [0x31] = 0x442,
                            [0x32] = 0x44c, [0x33] = 0x431, [0x34] = 0x44e };

static void toggle_layout(void) {
    keyboard_set_layout(!russian);
}

static const char lower[] = "??1234567890-=??qwertyuiop[]??asdfghjkl;'`?\\zxcvbnm,./??? ";
static const char upper[] = "?" "?!@#$%^&*()_+??QWERTYUIOP{}??ASDFGHJKL:\"~?|ZXCVBNM<>???? ";

static void enqueue(int key) {
    u32 next = (head + 1) % EVENT_COUNT;
    if (next != tail) {
        events[head] = key;
        __asm__ volatile("" : : : "memory");
        head = next;
    }
}

static void keyboard_callback(registers_t registers) {
    UNUSED(registers);
    u8 status = port_byte_in(0x64);
    if (!(status & 1)) {
        return;
    }
    u8 code = port_byte_in(0x60);
    if (status & 0x20) {
        mouse_byte(code);
        return;
    }
    if (code == 0xe0) {
        extended = 1;
        return;
    }
    if (code == 0xe1) {
        extended = 0;
        return;
    }
    u8 release = code & 0x80;
    code &= 0x7f;
    u8 ext = extended;
    extended = 0;
    if (!ext && (code == 0x2a || code == 0x36)) {
        u8 mask = code == 0x2a ? 1 : 2;
        if (release) {
            shifts &= ~mask;
        } else {
            shifts |= mask;
        }
        if (!layout_latched && shifts && (alt || control)) {
            toggle_layout();
            layout_latched = 1;
        }
        if (!shifts) {
            layout_latched = 0;
        }
        return;
    }
    if (code == 0x1d) {
        control = !release;
        if (!layout_latched && control && shifts) {
            toggle_layout();
            layout_latched = 1;
        }
        if (!control && !alt) {
            layout_latched = 0;
        }
        return;
    }
    if (code == 0x38) {
        alt = !release;
        if (!layout_latched && alt && shifts) {
            toggle_layout();
            layout_latched = 1;
        }
        if (!alt && !control) {
            layout_latched = 0;
        }
        return;
    }
    if (release) {
        return;
    }
    if (code == 0x58) {
        toggle_layout();
        return;
    } /* F12: not intercepted by host layout switchers. */
    if (!ext && code == 0x3a) {
        caps ^= 1;
        return;
    }
    if (ext) {
        switch (code) {
            case 0x52:
                enqueue(KEY_INSERT);
                break;
            case 0x49:
                enqueue(KEY_PAGE_UP);
                break;
            case 0x51:
                enqueue(KEY_PAGE_DOWN);
                break;
            case 0x48:
                enqueue(shifts ? KEY_SCROLL_UP : KEY_UP);
                break;
            case 0x50:
                enqueue(shifts ? KEY_SCROLL_DOWN : KEY_DOWN);
                break;
            case 0x4b:
                enqueue(KEY_LEFT);
                break;
            case 0x4d:
                enqueue(KEY_RIGHT);
                break;
            case 0x47:
                enqueue(KEY_HOME);
                break;
            case 0x4f:
                enqueue(KEY_END);
                break;
            case 0x53:
                enqueue(KEY_DELETE);
                break;
        }
        return;
    }
    if (code >= 0x3b && code <= 0x44) {
        enqueue(KEY_F1 + code - 0x3b);
    } else if (code == 0x1c) {
        enqueue('\n');
    } else if (code == 1) {
        enqueue(27);
    } else if (code == 0x0e) {
        enqueue('\b');
    } else if (code == 0x0f) {
        enqueue('\t');
    } else if (code < sizeof(lower) - 1) {
        if (russian && !control && code < 58 && ru[code]) {
            u32 cp = ru[code];
            if ((shifts != 0) ^ caps) {
                cp = cp == 0x451 ? 0x401 : cp - 32;
            }
            enqueue(TEXT_UNICODE_KEY | cp);
            return;
        }
        char c = lower[code];
        if (c == '?') {
            return;
        }
        int uppercase = shifts != 0;
        if (c >= 'a' && c <= 'z') {
            uppercase ^= caps;
        }
        c = uppercase ? upper[code] : c;
        if (control) {
            if (c != 'c' && c != 'C') {
                if (c >= 'a' && c <= 'z') {
                    enqueue(c - 'a' + 1);
                } else if (c >= 'A' && c <= 'Z') {
                    enqueue(c - 'A' + 1);
                }
                return;
            }
            if (c == 'c' || c == 'C') {
                if (!process_interrupt()) {
                    enqueue(3);
                }
            }
        } else {
            enqueue(c);
        }
    }
}

int keyboard_read_event(void) {
    if (tail == head) {
        return 0;
    }
    int key = events[tail];
    __asm__ volatile("" : : : "memory");
    tail = (tail + 1) % EVENT_COUNT;
    return key;
}

void init_keyboard(void) {
    register_interrupt_handler(IRQ1, keyboard_callback);
}
