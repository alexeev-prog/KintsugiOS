#ifndef IDT_H
#define IDT_H

#include "type.h"

/* Селекторы сегментов */
#define KERNEL_CS 0x08

/* Как определяется каждый элемент прерывания (обработчик) */
typedef struct {
    u16 low_offset; /* Младшие 16 бит адреса функции обработчика */
    u16 sel; /* Селектор сегментов ядра */
    u8 always0;
    /*
    Бит 7 первого байта: "Присутствует прерывание"
Биты 6-5: Уровень привилегий вызывающего абонента (0=ядро..3=пользователь)
Бит 4: Устанавливается равным 0 для элементов прерывания
Биты 3-0: биты 1110 = десятичное число 14 = "32-разрядный элемент прерывания"*/
    u8 flags;
    u16 high_offset; /* Higher 16 bits of handler function address */
} __attribute__((packed)) idt_gate_t ;

/* A pointer to the array of interrupt handlers.
 * Assembly instruction 'lidt' will read it */
typedef struct {
    u16 limit;
    u32 base;
} __attribute__((packed)) idt_register_t;

#define IDT_ENTRIES 256
extern idt_gate_t idt[IDT_ENTRIES];
extern idt_register_t idt_reg;


/* Functions implemented in idt.c */
void set_idt_gate(int n, u32 handler);
void set_idt();

#endif
