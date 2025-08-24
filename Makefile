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
PAGING_OBJ = $(BIN_DIR)/kernel/paging_asm.o

C_SOURCES = $(shell find $(SRC_DIR) -name '*.c')
C_OBJS = $(C_SOURCES:$(SRC_DIR)/%.c=$(BIN_DIR)/%.o)

OBJS = $(KERNEL_ENTRY) $(INTERRUPT_OBJ) $(C_OBJS)

RED=\033[0;31m
GREEN=\033[0;32m
YELLOW=\033[0;33m
BLUE=\033[0;34m
CYAN=\033[0;36m
RESET=\033[0m

all: $(BIN_DIR)/kintsugios.bin

$(BIN_DIR)/kintsugios.bin: $(BIN_DIR)/bootsector.bin $(BIN_DIR)/kernel.bin
	@printf "$(BLUE)[CAT]  Cat    %s %-42s -> %s$(RESET)\n" "bootsector" "kernel" "$(BIN_DIR)/kintsugios.bin"
	@cat $^ > $@

$(BIN_DIR)/bootsector.bin: $(SRC_DIR)/bootloader/bootsector.asm
	@printf "$(CYAN)[ASM]  Compiling %-50s -> %s$(RESET)\n" "bootsector.asm" "bootsector.bin"
	@mkdir -p $(BIN_DIR)
	@$(ASM) $(ASMFLAGS_BIN) $< -o $@

$(BIN_DIR)/kernel.bin: $(OBJS)
	@printf "$(BLUE)[LD]   Linking   %-50s -> %s$(RESET)\n" "$^" "$@"
	@$(LD) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/%.o: $(SRC_DIR)/%.asm
	@printf "$(CYAN)[ASM]  Compiling %-50s -> %s$(RESET)\n" "$<" "$@"
	@mkdir -p $(dir $@)
	@$(ASM) $(ASMFLAGS_ELF) $< -o $@

$(BIN_DIR)/%.o: $(SRC_DIR)/%.c
	@printf "$(CYAN)[CC]   Compiling %-50s -> %s$(RESET)\n" "$<" "$@"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(DISKIMG_DIR)/$(DISKIMG_NAME): $(BIN_DIR)/kintsugios.bin
	@printf "$(BLUE)[DD]   Make IMG  %-50s -> %s$(RESET)\n" "$(BIN_DIR)/kintsugios.bin" "$@"
	@mkdir -p $(DISKIMG_DIR)
	@dd if=/dev/zero of=$@.tmp bs=1024 count=1440
	@dd if=$< of=$@.tmp conv=notrunc
	@mv $@.tmp $@

diskimg: $(DISKIMG_DIR)/$(DISKIMG_NAME)

run_bin: $(BIN_DIR)/kintsugios.bin
	@printf "$(GREEN)[QEMU] Run bin   %-50s$(RESET)\n" "$(BIN_DIR)/kintsugios.bin"
	@qemu-system-i386 -fda $(BIN_DIR)/kintsugios.bin

run: $(DISKIMG_DIR)/$(DISKIMG_NAME)
	@printf "$(GREEN)[QEMU] Run img   %-50s$(RESET)\n" "$<"
	@qemu-system-i386 -fda $< -boot a -m 16

debug: $(DISKIMG_DIR)/$(DISKIMG_NAME)
	@printf "$(GREEN)[QEMU] Debug img %-50s$(RESET)\n" "$<"
	@qemu-system-i386 -fda $< -boot a -s -S -m 16

clean:
	@printf "$(RED)[RM]   Clean $(BIN_DIR) and $(DISKIMG_DIR)$(RESET)\n"
	@rm -rf $(BIN_DIR)/* $(DISKIMG_DIR)/*

.PHONY: all diskimg run run_bin debug clean clean_all
