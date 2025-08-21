# ==============================================
# Makefile for KintsugiOS
# ==============================================

ASM = nasm
CC = i386-elf-gcc
LD = i386-elf-ld
SRC_DIR = src
BIN_DIR = bin
DISKIMG_DIR = diskimgs
DISKIMG_NAME = kintsugi_floppy_i386.img

CFLAGS = -g -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -ffreestanding -I$(SRC_DIR)/kernel/include
ASMFLAGS_BIN = -f bin
ASMFLAGS_ELF = -f elf
LDFLAGS = -Ttext 0x1000 --oformat binary

KERNEL_ENTRY = $(BIN_DIR)/kernel_entry.o
INTERRUPT_OBJ = $(BIN_DIR)/interrupt.o
KERNEL_OBJ = $(BIN_DIR)/kernel.o
UTILS_OBJ = $(BIN_DIR)/utils.o
KEYBOARD_OBJ = $(BIN_DIR)/keyboard.o
SCREEN_OBJ = $(BIN_DIR)/screen.o
IDT_OBJ = $(BIN_DIR)/idt.o
ISR_OBJ = $(BIN_DIR)/isr.o
PORTS_OBJ = $(BIN_DIR)/ports.o
TIMER_OBJ = $(BIN_DIR)/timer.o
MEM_OBJ = $(BIN_DIR)/mem.o
STRING_OBJ = $(BIN_DIR)/string.o

OBJS = $(KERNEL_ENTRY) $(KERNEL_OBJ) $(INTERRUPT_OBJ) $(KEYBOARD_OBJ) $(SCREEN_OBJ) $(IDT_OBJ) $(ISR_OBJ) $(PORTS_OBJ) $(TIMER_OBJ) $(MEM_OBJ) $(STRING_OBJ) $(UTILS_OBJ)

all: $(BIN_DIR)/kintsugios.bin

$(BIN_DIR)/kintsugios.bin: $(BIN_DIR)/bootsector.bin $(BIN_DIR)/kernel.bin
	cat $^ > $@

$(BIN_DIR)/bootsector.bin: $(SRC_DIR)/bootloader/bootsector.asm
	mkdir -p $(BIN_DIR)
	$(ASM) $(ASMFLAGS_BIN) $< -o $@

$(BIN_DIR)/kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

$(KERNEL_ENTRY): $(SRC_DIR)/bootloader/kernel_entry.asm
	mkdir -p $(BIN_DIR)
	$(ASM) $(ASMFLAGS_ELF) $< -o $@

$(INTERRUPT_OBJ): $(SRC_DIR)/kernel/cpu/interrupt.asm
	mkdir -p $(BIN_DIR)
	$(ASM) $(ASMFLAGS_ELF) $< -o $@

$(KERNEL_OBJ): $(SRC_DIR)/kernel/kernel/kernel.c
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(UTILS_OBJ): $(SRC_DIR)/kernel/kernel/utils.c
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KEYBOARD_OBJ): $(SRC_DIR)/kernel/drivers/keyboard.c
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SCREEN_OBJ): $(SRC_DIR)/kernel/drivers/screen.c
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(IDT_OBJ): $(SRC_DIR)/kernel/cpu/idt.c
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(ISR_OBJ): $(SRC_DIR)/kernel/cpu/isr.c
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(PORTS_OBJ): $(SRC_DIR)/kernel/cpu/ports.c
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TIMER_OBJ): $(SRC_DIR)/kernel/cpu/timer.c
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(MEM_OBJ): $(SRC_DIR)/kernel/libc/mem.c
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(STRING_OBJ): $(SRC_DIR)/kernel/libc/string.c
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

diskimg: $(BIN_DIR)/kintsugios.bin
	mkdir -p $(DISKIMG_DIR)
	dd if=/dev/zero of=$(DISKIMG_DIR)/$(DISKIMG_NAME) bs=1024 count=1440
	dd if=$(BIN_DIR)/kintsugios.bin of=$(DISKIMG_DIR)/$(DISKIMG_NAME) conv=notrunc

run_bin: $(BIN_DIR)/kintsugios.bin
	qemu-system-i386 -fda $(BIN_DIR)/kintsugios.bin

run: diskimg
	qemu-system-i386 -fda $(DISKIMG_DIR)/$(DISKIMG_NAME) -boot a

clean:
	rm -f $(BIN_DIR)/*.o $(BIN_DIR)/*.bin

clean_all:
	rm -rf $(BIN_DIR)/* $(DISKIMG_DIR)/*

.PHONY: all diskimg run run_bin clean clean_all
