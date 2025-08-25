#include "vector.h"
#include "../mem.h"

vector_t* vector_create(u32 initial_capacity) {
    vector_t* vec = (vector_t*)kmalloc(sizeof(vector_t));
    vec->data = (void**)kmalloc(initial_capacity * sizeof(void*));
    vec->size = 0;
    vec->capacity = initial_capacity;
    return vec;
}

void vector_push_back(vector_t* vec, void* item) {
    if (vec->size >= vec->capacity) {
        vec->capacity *= 2;
        vec->data = (void**)krealloc(vec->data, vec->capacity * sizeof(void*));
    }
    vec->data[vec->size++] = item;
}
