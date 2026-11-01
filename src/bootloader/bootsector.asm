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
    call disk_load
    call memory_map
    call switch_to_pm
    jmp $
%include "src/bootloader/puts_chars.asm"
%include "src/bootloader/puts_hex.asm"
%include "src/bootloader/diskload.asm"
%include "src/bootloader/switch_to32.asm"
%include "src/bootloader/gdt.asm"
; Fixed boot handoff: count at 0x5000, up to 64 24-byte E820 entries.
[bits 16]
memory_map:
    xor ax, ax
    mov es, ax
    mov di, 0x5004
    mov dword [0x5000], 0
    xor ebx, ebx
.next:
    mov dword [es:di+20], 1
    mov eax, 0xe820
    mov edx, 0x534d4150
    mov ecx, 24
    int 0x15
    jc .done
    cmp eax, 0x534d4150
    jne .done
    cmp ecx, 20
    jb .done
    inc dword [0x5000]
    add di, 24
    cmp dword [0x5000], 64
    jae .done
    test ebx, ebx
    jnz .next
.done:
    ret
[bits 32]
BEGIN_PM:
    jmp KERNEL_OFFSET
BOOT_DRIVE: db 0
times 510-($-$$) db 0
dw 0xaa55
