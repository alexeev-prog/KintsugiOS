; -----------------------------------------------------------------------------
;  KintsugiOS Bootloader source code
;  File: bootloader/bootsector.asm
; Description:
;   Загрузочный сектор Kintsugi OS
; -----------------------------------------------------------------------------

[org 0x7c00]

KERNEL_OFFSET equ 0x007e00	; Смещение в памяти, из которого мы загрузим ядро

	mov [BOOT_DRIVE], dl	; BIOS хранит наш загрузочный диск в формате DL, поэтому
							; лучше запомнить это на будущее. (Помните об этом
							; BIOS задает нам загрузочный диск в формате "dl" при загрузке)
	mov bp, 0xA0000			; Устанавливаем стек
	mov sp, bp

	mov bx, MSG_REAL_MODE	; Печатаем сообщение
	call puts_chars

	call load_kernel		; Загружаем ядро
	call switch_to_pm		; Переключаемся в Защищенный Режим
	jmp $

%include "src/bootloader/puts_chars.asm"		; Вывод строки
%include "src/bootloader/puts_hex.asm"			; ф. печати 16-ричного числа
%include "src/bootloader/diskload.asm"			; ф. чтения диска
%include "src/bootloader/puts_chars32.asm"		; Вывод строки в 32 PM
%include "src/bootloader/switch_to32.asm"		; Переключиться на 32 PM
%include "src/bootloader/gdt.asm"				; GDT

[bits 16]

load_kernel:
	mov bx, MSG_LOAD_KERNEL
	call puts_chars			; Печатаем сообщение о том, то мы загружаем ядро
							; Устанавливаем параметры для функции disk_load:
	mov bx, KERNEL_OFFSET	; Загрузим данные в место памяти
							; смещению KERNEL_OFFSET
	mov dh, 55				; Загрузим много секторов для ядра.
	mov dl, [BOOT_DRIVE]	; Загрузим данные из BOOT_DRIVE (Возвращаем BOOT_DRIVE)
	call disk_load			; Вызываем функцию disk_load
	ret

[bits 32]					; Сюда мы попадем после переключения в 32PM

BEGIN_PM:
	mov ebx, MSG_PROT_MODE
	call puts_chars_pm		; Печатаем сообщение об успешной загрузке в 32PM
	call KERNEL_OFFSET		; Переходим в адрес, по которому загрузился код ядра
	jmp $


BOOT_DRIVE:			db 0
MSG_REAL_MODE:		db "Start RMx16", 0 							; [Старт] 16 битный реальный режим
MSG_PROT_MODE:		db "Start PMx32", 0        					; [Успех] 32 битный защищенный режим
MSG_LOAD_KERNEL:	db "Start Kernel", 0 					; [Загрузка] ядра с видео памятью

times 510-($-$$) db 0
dw 0xaa55
