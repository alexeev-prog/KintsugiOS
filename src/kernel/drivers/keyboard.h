#ifndef KEYBOARD_H
#define KEYBOARD_H

enum {
    KEY_UP = 256,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,
    KEY_SCROLL_UP,
    KEY_SCROLL_DOWN,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_INSERT,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10
};

void init_keyboard(void);
int keyboard_read_event(void);

#endif
