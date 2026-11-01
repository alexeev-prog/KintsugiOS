[bits 32]
section .text.start
global _start
extern main
_start:
    mov eax, [esp]
    mov edx, [esp+4]
    push edx
    push eax
    call main
    mov ebx, eax
    xor eax, eax
    int 0x80
    ud2
section .note.GNU-stack noalloc noexec nowrite progbits
