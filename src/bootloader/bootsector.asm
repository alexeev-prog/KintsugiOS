[org 0x7c00]
[bits 16]
%include "kernel_size.inc"
KERNEL_OFFSET equ 0x7e00
    jmp 0:boot
boot:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    cld
    sti
    mov [BOOT_DRIVE], dl
    mov bx, MSG_LOAD_KERNEL
    call puts_chars
    call disk_load
    call switch_to_pm
    jmp $
%include "src/bootloader/puts_chars.asm"
%include "src/bootloader/puts_hex.asm"
%include "src/bootloader/diskload.asm"
%include "src/bootloader/switch_to32.asm"
%include "src/bootloader/gdt.asm"
[bits 32]
BEGIN_PM:
    jmp KERNEL_OFFSET
BOOT_DRIVE: db 0
MSG_LOAD_KERNEL: db "Loading KintsugiOS", 0
times 510-($-$$) db 0
dw 0xaa55
