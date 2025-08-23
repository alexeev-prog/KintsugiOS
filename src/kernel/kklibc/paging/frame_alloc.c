#include "frame_alloc.h"
#include "../../drivers/screen.h"
#include "../stdlib.h"
#include "paging.h"

u32 frame_bitmap[FRAME_BITMAP_SIZE]; // битмапа фреймов

void set_frame_used(u32 frame_addr) {
    u32 frame_num = frame_addr / FRAME_SIZE;
    u32 index = frame_num / 32;
    u32 offset = frame_num % 32;
    frame_bitmap[index] |= (1 << offset);
}

void set_frame_free(u32 frame_addr) {
    u32 frame_num = frame_addr / FRAME_SIZE;
    u32 index = frame_num / 32;
    u32 offset = frame_num % 32;
    frame_bitmap[index] &= ~(1 << offset);
}

u32 test_frame(u32 frame_addr) {
    u32 frame_num = frame_addr / FRAME_SIZE;
    u32 index = frame_num / 32;
    u32 offset = frame_num % 32;
    return (frame_bitmap[index] & (1 << offset));
}

u32 alloc_frame() {
    // Ищем первый свободный кадр в битовой карте
    for (u32 i = 0; i < FRAME_BITMAP_SIZE; i++) {
        if (frame_bitmap[i] != 0xFFFFFFFF) { // Если не все биты установлены
            for (u32 j = 0; j < 32; j++) {
                if (!(frame_bitmap[i] & (1 << j))) {
                    u32 frame_num = i * 32 + j;
                    u32 frame_addr = frame_num * FRAME_SIZE;
                    set_frame_used(frame_addr);
                    return frame_addr;
                }
            }
        }
    }
    kprint("ERROR: Out of physical memory!\n");
    return 0; // No free frames
}

void free_frame(u32 frame_addr) {
    set_frame_free(frame_addr);
}
