#include "keyboard.h"

#include "../cpu/isr.h"
#include "../kklibc/function.h"
#include "lowlevel_io.h"
#include "../user/process.h"

#define EVENT_COUNT 128
static int events[EVENT_COUNT];
static volatile u32 head, tail;
static u8 shifts, caps, control, extended;

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
    u8 code = port_byte_in(0x60);
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
        return;
    }
    if (code == 0x1d) {
        control = !release;
        return;
    }
    if (release) {
        return;
    }
    if (!ext && code == 0x3a) {
        caps ^= 1;
        return;
    }
    if (ext) {
        switch (code) {
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
    if (code == 0x1c) {
        enqueue('\n');
    } else if (code == 1) {
        enqueue(27);
    } else if (code == 0x0e) {
        enqueue('\b');
    } else if (code == 0x0f) {
        enqueue('\t');
    } else if (code < sizeof(lower) - 1) {
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
            if (c == 's' || c == 'S') {
                enqueue(19);
                return;
            }
            if (c == 'q' || c == 'Q') {
                enqueue(17);
                return;
            }
            if (c == 'c' || c == 'C') {
                if (!process_interrupt()) enqueue(3);
            } else if (c == 'd' || c == 'D') {
                enqueue(4);
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
