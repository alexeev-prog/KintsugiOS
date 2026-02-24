; 64-bit entry point for Limine
bits 64
section .text

global _start
extern kmain

_start:
    ; Limine calls us here in 64-bit long mode
    ; The stack is already set up
    
    ; Call our kernel's main function
    call kmain
    
    ; If kmain returns, halt
.hang:
    cli
    hlt
    jmp .hang
