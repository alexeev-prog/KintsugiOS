; paging.asm
section .text
global load_page_directory
global enable_paging

; Загрузка каталога страниц в CR3
load_page_directory:
    push ebp
    mov ebp, esp
    mov eax, [ebp+8]    ; Получаем адрес каталога страниц
    mov cr3, eax        ; Загружаем в CR3
    pop ebp
    ret

; Включение paging
enable_paging:
    push ebp
    mov ebp, esp
    mov eax, cr0        ; Получаем текущее значение CR0
    or eax, 0x80000000  ; Устанавливаем бит paging (PG)
    mov cr0, eax        ; Загружаем обратно в CR0
    pop ebp
    ret
