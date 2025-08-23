[bits 32]
[global load_page_dir]
[global enable_paging]

load_page_dir:
    mov eax, [esp+4]  ; аргумент из стека
    mov cr3, eax      ; и суем в CR3
    ret

enable_paging:
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    ret
