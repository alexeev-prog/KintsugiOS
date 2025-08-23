#ifndef FRAME_ALLOC_H
#define FRAME_ALLOC_H

#include "../ctypes.h"

#define FRAME_SIZE 4096
#define FRAME_BITMAP_SIZE 32768 // 32768 * 32 бита = 1,048,576 бит -> 4GB RAM (1M фраймиков)

void init_frame_allocator();
void set_frame_used(u32 frame_addr);
void set_frame_free(u32 frame_addr);
u32 test_frame(u32 frame_addr);
u32 alloc_frame(); // возвращает **ФИЗИЧЕСКИЙ** адрес свободного фрейма
void free_frame(u32 frame_addr);

#endif
