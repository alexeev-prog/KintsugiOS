#ifndef KOS_MOUSE_H
#define KOS_MOUSE_H
#include "../../user/desktopabi.h"
void mouse_init(void);
void mouse_byte(unsigned char byte);
const kos_mouse* mouse_state(void);
#endif
