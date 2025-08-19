org 0x7C00
use16

start:
	cli

	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7C00
	sti

	mov [boot_drive], dl

	mov si, msg_loading
	call bios_print

	mov bx, 0x1000
	mov es, bx
	xor bx, bx
	mov dh, 20
	mov dl, [boot_drive]
	call bios_disk_load

	call enter_pm

bios_disk_load:
	mov ah, 0x02
	mov al, dh
	mov ch, 0x00
	mov dh, 0x00
	mov cl, 0x02
	int 0x13
	ret

bios_print:
	lodsb
	or al, al
	jz .done
	mov ah, 0x0E
	int 0x10
	jmp bios_print
.done:
	ret

enter_pm:
	cli

	lgdt [gdt_descriptor]
	mov eax, cr0
	or eax, 0x1
	mov cr0, eax

	jmp 0x08:init_pm

use32
init_pm:
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000
    mov esp, ebp

    jmp 0x10000

boot_drive db 0
msg_loading db "Loading KintsugiOS...", 0xD, 0xA, 0

gdt:
    dq 0

    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00

    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00

gdt_descriptor:
    dw $ - gdt - 1
    dd gdt

times 510 - ($ - $$) db 0
dw 0xAA55
