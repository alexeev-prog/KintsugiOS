[bits 32]
section .text.entry
global _start
extern kmain
extern __bss_start
extern __bss_end
_start:
    cld
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb
    call kmain
.idle:
    hlt
    jmp .idle
section .note.GNU-stack noalloc noexec nowrite progbits
