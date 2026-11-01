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
    KEY_SCROLL_DOWN
};

void init_keyboard(void);
int keyboard_read_event(void);

#endif
