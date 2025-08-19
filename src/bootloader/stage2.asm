org 0x10000
use32

kernel_entry:
    jmp 0x10200

times 512 - ($ - $$) db 0
