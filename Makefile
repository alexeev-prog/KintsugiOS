# ==============================================
# Makefile for KintsugiOS
# ==============================================

ASM=fasm
CC=i386-elf-gcc
LD=i386-elf-ld
DD=dd
RM=rm

SRC_DIR = src
BOOTLOADER_DIR = $(SRC_DIR)/bootloader
KERNEL_DIR = $(SRC_DIR)/kernel
BIN_DIR = bin
TARGET = $(BIN_DIR)/KintsugiOS.bin

all: $(TARGET)

$(TARGET): $(BIN_DIR)/stage1.bin $(BIN_DIR)/stage2.bin $(BIN_DIR)/kernel.bin
	cat $(BIN_DIR)/stage1.bin $(BIN_DIR)/stage2.bin $(BIN_DIR)/kernel.bin > $(TARGET)
	truncate -s 1440k $(TARGET) # Создаем образ дискеты

$(BIN_DIR)/stage1.bin: $(BOOTLOADER_DIR)/stage1.asm
	mkdir -p $(BIN_DIR)
	$(ASM) $< $@

$(BIN_DIR)/stage2.bin: $(BOOTLOADER_DIR)/stage2.asm
	mkdir -p $(BIN_DIR)
	$(ASM) $< $@

$(BIN_DIR)/kernel.bin: $(KERNEL_DIR)/kernel.c
	mkdir -p $(BIN_DIR)
	$(CC) -ffreestanding -nostdlib -c $< -o $(BIN_DIR)/kernel.o
	$(LD) -o $@ -Ttext 0x10200 --oformat binary $(BIN_DIR)/kernel.o

run: $(TARGET)
	qemu-system-i386 -drive format=raw,file=$(TARGET)

debug: $(TARGET)
	qemu-system-i386 -drive format=raw,file=$(TARGET) -s -S &

clean:
	$(RM) -rf $(BIN_DIR)
