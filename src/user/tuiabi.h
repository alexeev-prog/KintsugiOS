#ifndef TUI_ABI_H
#define TUI_ABI_H

/* Shared wire types only; no kernel internals or pointers. */
typedef struct {
    unsigned char character, color;
} ktui_cell;

typedef struct {
    unsigned int ram_kib, free_pages, allocated_pages, heap_used, heap_free, heap_committed;
} ktui_stats;
#endif
