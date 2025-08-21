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

KERNEL_ENTRY = $(BIN_DIR)/bootloader/kernel_entry.o
INTERRUPT_OBJ = $(BIN_DIR)/kernel/cpu/interrupt.o

C_SOURCES = $(shell find $(SRC_DIR) -name '*.c')
C_OBJS = $(C_SOURCES:$(SRC_DIR)/%.c=$(BIN_DIR)/%.o)

OBJS = $(KERNEL_ENTRY) $(INTERRUPT_OBJ) $(C_OBJS)

all: $(BIN_DIR)/kintsugios.bin

$(BIN_DIR)/kintsugios.bin: $(BIN_DIR)/bootsector.bin $(BIN_DIR)/kernel.bin
	cat $^ > $@

$(BIN_DIR)/bootsector.bin: $(SRC_DIR)/bootloader/bootsector.asm
	mkdir -p $(BIN_DIR)
	$(ASM) $(ASMFLAGS_BIN) $< -o $@

$(BIN_DIR)/kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/%.o: $(SRC_DIR)/%.asm
	mkdir -p $(dir $@)
	$(ASM) $(ASMFLAGS_ELF) $< -o $@

$(BIN_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

diskimg: $(BIN_DIR)/kintsugios.bin
	mkdir -p $(DISKIMG_DIR)
	dd if=/dev/zero of=$(DISKIMG_DIR)/$(DISKIMG_NAME) bs=1024 count=1440
	dd if=$(BIN_DIR)/kintsugios.bin of=$(DISKIMG_DIR)/$(DISKIMG_NAME) conv=notrunc

run_bin: $(BIN_DIR)/kintsugios.bin
	qemu-system-i386 -fda $(BIN_DIR)/kintsugios.bin

run: diskimg
	qemu-system-i386 -fda $(DISKIMG_DIR)/$(DISKIMG_NAME) -boot a

debug: diskimg
	qemu-system-i386 -fda $(DISKIMG_DIR)/$(DISKIMG_NAME) -boot a -s -S

clean:
	rm -rf $(BIN_DIR)/*

clean_all:
	rm -rf $(BIN_DIR)/* $(DISKIMG_DIR)/*

.PHONY: all diskimg run run_bin clean clean_all
