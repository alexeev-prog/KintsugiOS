; Single-sector reads avoid track and 64 KiB DMA boundaries.
[bits 16]
disk_load:
    mov ah, 8
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc .error
    and cl, 0x3f
    mov [spt], cl
    inc dh
    mov [heads], dh
    xor ax, ax
    mov es, ax
    mov bx, KERNEL_OFFSET
    mov si, KERNEL_SECTORS
.sector:
    mov byte [retries], 3
.retry:
    mov ax, [cylinder]
    mov ch, al
    mov cl, [sector]
    shl ah, 6
    or cl, ah
    mov dh, [head]
    mov dl, [BOOT_DRIVE]
    mov ax, 0x0201
    push bx
    push si
    push es
    int 0x13
    pop es
    pop si
    pop bx
    jnc .next
    mov [status], ah
    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13
    dec byte [retries]
    jnz .retry
    mov ah, [status]
    jmp .error
.next:
    add bx, 512
    jnc .address_ok
    mov ax, es
    add ax, 0x1000
    mov es, ax
.address_ok:
    dec si
    jz .done
    inc byte [sector]
    mov al, [sector]
    cmp al, [spt]
    jbe .sector
    mov byte [sector], 1
    inc byte [head]
    mov al, [head]
    cmp al, [heads]
    jb .sector
    mov byte [head], 0
    inc word [cylinder]
    jmp .sector
.error:
    movzx dx, ah
    mov bx, disk_error_msg
    call puts_chars
    call puts_hex
    cli
.halt:
    hlt
    jmp .halt
.done:
    ret
spt: db 18
heads: db 2
sector: db 2
head: db 0
cylinder: dw 0
retries: db 0
status: db 0
disk_error_msg: db "BIOS disk status:", 0
