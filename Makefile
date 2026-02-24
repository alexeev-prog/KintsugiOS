# ==============================================
# Makefile for KintsugiOS (Limine + 64-bit)
# ==============================================

# Tools
ASM = nasm
CC = x86_64-elf-gcc
LD = x86_64-elf-ld
OBJCOPY = x86_64-elf-objcopy

# Fallback to system gcc if cross-compiler not found
ifeq ($(shell command -v $(CC) 2>/dev/null),)
    CC = gcc -m64
    LD = ld
    OBJCOPY = objcopy
    $(warning Cross-compiler not found, using system gcc with -m64)
endif

# Directories
SRC_DIR = src
BIN_DIR = bin
DISKIMG_DIR = diskimgs
TEST_FILES_DIR = test_files

# Output files
ISO_NAME = KintsugiOS.iso
FAT12_HDD_NAME = kintsugi_fat12_hdd.img
KERNEL_ELF = $(BIN_DIR)/kernel.elf

# Test files
FAT12_TEST_FILES = $(TEST_FILES_DIR)/README.TXT \
                   $(TEST_FILES_DIR)/TEST.TXT \
                   $(TEST_FILES_DIR)/HELLO.TXT

# Compiler flags
CFLAGS = -g -m64 -ffreestanding -nostdlib -nostdinc \
         -fno-builtin -fno-stack-protector -nostartfiles \
         -nodefaultlibs -Wall -Wextra -O2 \
         -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
         -I$(SRC_DIR)/kernel/include

ASMFLAGS_ELF = -f elf64

# Find Limine paths (for NixOS and normal Linux)
LIMINE_STORE_PATH = /nix/store/rhn53fc96hqg4i41r03by6hvmq0fbdbb-limine-9.5.0
LIMINE_BIN = $(LIMINE_STORE_PATH)/bin/limine
LIMINE_SYS = $(LIMINE_STORE_PATH)/share/limine/limine-bios.sys

# Fallback to searching if the hardcoded path doesn't exist
ifeq ($(wildcard $(LIMINE_BIN)),)
    LIMINE_BIN = $(shell command -v limine 2>/dev/null)
endif

ifeq ($(wildcard $(LIMINE_SYS)),)
    LIMINE_SYS = $(firstword $(wildcard /nix/store/*-limine-*/share/limine/limine-bios.sys))
endif

# Colors for output
RED = \033[0;31m
GREEN = \033[0;32m
YELLOW = \033[1;33m
BLUE = \033[0;34m
CYAN = \033[0;36m
RESET = \033[0m

# Find all C sources
C_SOURCES = $(shell find $(SRC_DIR) -name '*.c')
C_OBJS = $(C_SOURCES:$(SRC_DIR)/%.c=$(BIN_DIR)/%.o)

# Assembly sources (only needed ones)
ENTRY_ASM = $(SRC_DIR)/kernel/limine_entry.asm
INTERRUPT_ASM = $(SRC_DIR)/kernel/cpu/interrupt.asm
ENTRY_OBJ = $(BIN_DIR)/kernel/limine_entry.o
INTERRUPT_OBJ = $(BIN_DIR)/kernel/cpu/interrupt.o

# All objects
OBJS = $(ENTRY_OBJ) $(INTERRUPT_OBJ) $(C_OBJS)

# ==============================================
# Main targets
# ==============================================

.PHONY: all clean clean_all iso run debug quick testfiles fat12hdd info check-limine

all: $(KERNEL_ELF)
	@printf "$(GREEN)[DONE] Kernel built successfully$(RESET)\n"

# Create necessary directories
$(BIN_DIR) $(DISKIMG_DIR) $(TEST_FILES_DIR):
	@mkdir -p $@

# ==============================================
# Compilation rules
# ==============================================

# Entry point assembly
$(ENTRY_OBJ): $(ENTRY_ASM) | $(BIN_DIR)
	@printf "$(CYAN)[ASM]  Compiling %-40s -> %s$(RESET)\n" "$(notdir $<)" "$@"
	@mkdir -p $(dir $@)
	@$(ASM) $(ASMFLAGS_ELF) $< -o $@

# Interrupt handlers assembly
$(INTERRUPT_OBJ): $(INTERRUPT_ASM) | $(BIN_DIR)
	@printf "$(CYAN)[ASM]  Compiling %-40s -> %s$(RESET)\n" "$(notdir $<)" "$@"
	@mkdir -p $(dir $@)
	@$(ASM) $(ASMFLAGS_ELF) $< -o $@

# C files
$(BIN_DIR)/%.o: $(SRC_DIR)/%.c | $(BIN_DIR)
	@printf "$(CYAN)[CC]   Compiling %-40s -> %s$(RESET)\n" "$(notdir $<)" "$@"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

# Link kernel
$(KERNEL_ELF): $(OBJS) linker.ld | $(BIN_DIR)
	@printf "$(BLUE)[LD]   Linking %-41s -> %s$(RESET)\n" "kernel" "$@"
	@$(LD) -T linker.ld -o $@ $(OBJS)
	@printf "$(GREEN)[LD]   Kernel linked successfully$(RESET)\n"
	@printf "      Size: %d bytes\n" $$(stat -c%s $@ 2>/dev/null || stat -f%z $@ 2>/dev/null)

# ==============================================
# ISO creation
# ==============================================

# Check if Limine files exist
check-limine:
	@printf "$(BLUE)[CHECK] Looking for Limine files...$(RESET)\n"
	@if [ -z "$(LIMINE_SYS)" ]; then \
		printf "$(RED)[ERROR] limine-bios.sys not found!$(RESET)\n"; \
		printf "Try: find /nix/store -name 'limine-bios.sys' 2>/dev/null\n"; \
		exit 1; \
	fi
	@if [ -z "$(LIMINE_BIN)" ]; then \
		printf "$(RED)[ERROR] limine binary not found!$(RESET)\n"; \
		printf "Try: find /nix/store -name 'limine' -type f 2>/dev/null\n"; \
		exit 1; \
	fi
	@printf "$(GREEN)[OK]   Found: $(LIMINE_SYS)$(RESET)\n"
	@printf "$(GREEN)[OK]   Found: $(LIMINE_BIN)$(RESET)\n"

# Create ISO root directory
$(BIN_DIR)/iso_root: | $(BIN_DIR)
	@mkdir -p $(BIN_DIR)/iso_root/boot/limine
	@mkdir -p $(BIN_DIR)/iso_root/boot

# Copy kernel to ISO
$(BIN_DIR)/iso_root/boot/kernel.elf: $(KERNEL_ELF) | $(BIN_DIR)/iso_root
	@cp $< $@
	@printf "$(BLUE)[CP]   Copying kernel to ISO$(RESET)\n"

# Copy Limine files to ISO
$(BIN_DIR)/iso_root/boot/limine/limine-bios.sys: $(LIMINE_SYS) | $(BIN_DIR)/iso_root
	@cp $< $@
	@printf "$(BLUE)[CP]   Copying limine-bios.sys$(RESET)\n"

# Create limine-cd.bin as a copy of limine-bios.sys
$(BIN_DIR)/iso_root/boot/limine/limine-cd.bin: $(BIN_DIR)/iso_root/boot/limine/limine-bios.sys
	@cp $< $@
	@printf "$(BLUE)[CP]   Using limine-bios.sys as limine-cd.bin$(RESET)\n"

# Copy Limine config
$(BIN_DIR)/iso_root/boot/limine/limine.conf: limine.conf | $(BIN_DIR)/iso_root
	@cp $< $@
	@printf "$(BLUE)[CP]   Copying limine.conf$(RESET)\n"

# Create ISO image
iso: check-limine $(BIN_DIR)/iso_root/boot/kernel.elf \
     $(BIN_DIR)/iso_root/boot/limine/limine-bios.sys \
     $(BIN_DIR)/iso_root/boot/limine/limine-cd.bin \
     $(BIN_DIR)/iso_root/boot/limine/limine.conf | $(DISKIMG_DIR)
	@printf "$(BLUE)[ISO]  Creating ISO image$(RESET)\n"
	@xorriso -as mkisofs -b boot/limine/limine-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		$(BIN_DIR)/iso_root -o $(DISKIMG_DIR)/$(ISO_NAME) 2>/dev/null
	@$(LIMINE_BIN) deploy $(DISKIMG_DIR)/$(ISO_NAME)
	@printf "$(GREEN)[ISO]  Created: $(DISKIMG_DIR)/$(ISO_NAME)$(RESET)\n"
	@printf "       Size: %d bytes\n" $$(stat -c%s $(DISKIMG_DIR)/$(ISO_NAME) 2>/dev/null || stat -f%z $(DISKIMG_DIR)/$(ISO_NAME) 2>/dev/null)

# ==============================================
# FAT12 HDD image
# ==============================================

# Create test files
$(TEST_FILES_DIR)/TEST.TXT: | $(TEST_FILES_DIR)
	@echo "Hello from KintsugiOS FAT12 Filesystem!" > $@
	@echo "This file was read from the HDD using ATA PIO driver." >> $@
	@echo "If you see this, your FAT12 implementation works!" >> $@
	@printf "$(GREEN)[FILE] Created $@$(RESET)\n"

$(TEST_FILES_DIR)/README.TXT: | $(TEST_FILES_DIR)
	@echo "KintsugiOS" > $@
	@echo "====================" >> $@
	@echo "" >> $@
	@echo "This is a test file on the FAT12 HDD." >> $@
	@echo "The HDD is accessed via ATA PIO driver." >> $@
	@echo "The ISO contains the OS kernel." >> $@
	@echo "The HDD contains the filesystem." >> $@
	@printf "$(GREEN)[FILE] Created $@$(RESET)\n"

$(TEST_FILES_DIR)/HELLO.TXT: | $(TEST_FILES_DIR)
	@echo "Hello World from FAT12!" > $@
	@printf "$(GREEN)[FILE] Created $@$(RESET)\n"

testfiles: $(FAT12_TEST_FILES)
	@printf "$(GREEN)[DONE] Test files created$(RESET)\n"

# Create FAT12 HDD image
fat12hdd: $(FAT12_TEST_FILES) | $(DISKIMG_DIR)
	@printf "$(BLUE)[FAT12] Creating FAT12 HDD image$(RESET)\n"
	@dd if=/dev/zero of=$(DISKIMG_DIR)/$(FAT12_HDD_NAME).tmp bs=512 count=16384 2>/dev/null
	@printf "$(YELLOW)[FAT12] Formatting as FAT12...$(RESET)\n"
	@mkfs.fat -F12 $(DISKIMG_DIR)/$(FAT12_HDD_NAME).tmp 2>/dev/null
	@printf "$(YELLOW)[FAT12] Copying test files...$(RESET)\n"
	@for file in $(FAT12_TEST_FILES); do \
		mcopy -i $(DISKIMG_DIR)/$(FAT12_HDD_NAME).tmp $$file ::/ 2>/dev/null && \
		printf "$(GREEN)[OK]   Copied $$file$(RESET)\n" || \
		printf "$(YELLOW)[WARN] Failed to copy $$file$(RESET)\n"; \
	done
	@mv $(DISKIMG_DIR)/$(FAT12_HDD_NAME).tmp $(DISKIMG_DIR)/$(FAT12_HDD_NAME)
	@printf "$(GREEN)[FAT12] Created: $(DISKIMG_DIR)/$(FAT12_HDD_NAME)$(RESET)\n"
	@printf "         Size: %d bytes\n" $$(stat -c%s $(DISKIMG_DIR)/$(FAT12_HDD_NAME) 2>/dev/null || stat -f%z $(DISKIMG_DIR)/$(FAT12_HDD_NAME) 2>/dev/null)

# ==============================================
# Run targets
# ==============================================

run: iso fat12hdd
	@printf "$(GREEN)[QEMU] Running KintsugiOS with Limine$(RESET)\n"
	@printf "$(YELLOW)========================================$(RESET)\n"
	@qemu-system-x86_64 \
		-cdrom $(DISKIMG_DIR)/$(ISO_NAME) \
		-hda $(DISKIMG_DIR)/$(FAT12_HDD_NAME) \
		-m 256M \
		-smp 2 \
		-display sdl \
		-name "KintsugiOS" \
		-serial mon:stdio

debug: iso fat12hdd
	@printf "$(GREEN)[QEMU] Debug mode$(RESET)\n"
	@printf "$(YELLOW)Connect with: gdb -ex 'target remote localhost:1234'$(RESET)\n"
	@printf "$(YELLOW)Or use: gdb -ex 'target remote localhost:1234' -ex 'symbol-file $(KERNEL_ELF)'$(RESET)\n"
	@qemu-system-x86_64 \
		-cdrom $(DISKIMG_DIR)/$(ISO_NAME) \
		-hda $(DISKIMG_DIR)/$(FAT12_HDD_NAME) \
		-m 256M \
		-smp 2 \
		-s -S \
		-display sdl \
		-name "KintsugiOS (Debug)"

quick: clean testfiles fat12hdd iso run

# ==============================================
# Clean targets
# ==============================================

clean:
	@printf "$(RED)[CLEAN] Cleaning build directory$(RESET)\n"
	@rm -rf $(BIN_DIR)

clean_all: clean
	@printf "$(RED)[CLEAN] Cleaning disk images and test files$(RESET)\n"
	@rm -rf $(DISKIMG_DIR)
	@rm -rf $(TEST_FILES_DIR)

# ==============================================
# Info target
# ==============================================

info:
	@echo "=== KintsugiOS Build Configuration ==="
	@echo "Compiler:       $(CC)"
	@echo "Assembler:      $(ASM)"
	@echo "Linker:         $(LD)"
	@echo ""
	@echo "=== Limine Files ==="
	@echo "limine-bios.sys: $(LIMINE_SYS)"
	@echo "limine binary:   $(LIMINE_BIN)"
	@echo ""
	@echo "=== Output Files ==="
	@echo "Kernel ELF:      $(KERNEL_ELF)"
	@echo "ISO image:       $(DISKIMG_DIR)/$(ISO_NAME)"
	@echo "FAT12 HDD:       $(DISKIMG_DIR)/$(FAT12_HDD_NAME)"
	@echo ""
	@echo "=== Useful Commands ==="
	@echo "make              - Build kernel only"
	@echo "make iso           - Create ISO image"
	@echo "make fat12hdd      - Create FAT12 HDD image"
	@echo "make run           - Run in QEMU"
	@echo "make debug         - Run in debug mode"
	@echo "make quick         - Clean, build, and run"
	@echo "make clean         - Clean build files"
	@echo "make clean_all     - Clean everything"
