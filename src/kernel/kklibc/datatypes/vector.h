#ifndef DATATYPE_VECTOR_H
#define DATATYPE_VECTOR_H

#include "../ctypes.h"

typedef struct vector {
    void** data;
    u32 size;
    u32 capacity;
} vector_t;

vector_t* vector_create(u32 initial_capacity);
void vector_push_back(vector_t* vec, void* item);

#endif    // DATATYPE_VECTOR_H
