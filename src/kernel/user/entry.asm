[bits 32]
section .text
global user_enter, user_leave, syscall_entry, process_switch
extern syscall_dispatch
; Single foreground process. Save the kernel continuation independently of TSS stack.
user_enter:
    push ebp
    push ebx
    push esi
    push edi
    mov [saved_esp], esp
    mov ecx, [esp+20]
    mov edx, [esp+24]
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push dword 0x23
    push edx
    push dword 0x202
    push dword 0x1b
    push ecx
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    iretd
user_leave:
    cli
    mov edx, [esp+4]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov esp, [saved_esp]
    mov eax, edx
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
syscall_entry:
    cld
    push ds
    push es
    push fs
    push gs
    pusha
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call syscall_dispatch
    add esp, 4
    popa
    pop gs
    pop fs
    pop es
    pop ds
    iretd
section .bss
saved_esp: resd 1
section .text
; Switch kernel continuations; callers hold interrupts disabled.
process_switch:
    push ebp
    push ebx
    push esi
    push edi
    mov eax, [esp+20]
    mov [eax], esp
    mov esp, [esp+24]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
