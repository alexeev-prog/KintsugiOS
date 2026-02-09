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
HDDIMG_NAME = kintsugi_hdd_i386.img
FAT12_HDD_NAME = kintsugi_fat12_hdd.img
ISO_NAME = KintsugiOS.iso

FAT12_TEST_FILES = test_files/README.TXT
TEST_FILES_DIR = test_files

MKISOFS = genisoimage
XORRISO = xorriso

KERNEL_OFFSET = 0x007e00

CFLAGS = -g -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -ffreestanding -I$(SRC_DIR)/kernel/include
ASMFLAGS_BIN = -f bin
ASMFLAGS_ELF = -f elf
LDFLAGS = -Ttext $(KERNEL_OFFSET) --oformat binary

KERNEL_ENTRY = $(BIN_DIR)/bootloader/kernel_entry.o
INTERRUPT_OBJ = $(BIN_DIR)/kernel/cpu/interrupt.o

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
	@python getversion.py __update_version 2>/dev/null || true
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

# ==============================================
# DISK IMAGES
# ==============================================

$(DISKIMG_DIR)/$(DISKIMG_NAME): $(BIN_DIR)/kintsugios.bin
	@printf "$(BLUE)[IMG]  Make Floppy IMG  %-50s -> %s$(RESET)\n" "$(BIN_DIR)/kintsugios.bin" "$@"
	@mkdir -p $(DISKIMG_DIR)
	@dd if=/dev/zero of=$@.tmp bs=1024 count=1440 2>/dev/null
	@dd if=$< of=$@.tmp conv=notrunc 2>/dev/null
	@mv $@.tmp $@

$(DISKIMG_DIR)/$(HDDIMG_NAME):
	@printf "$(BLUE)[IMG]  Make Empty HDD IMG  %-50s -> %s$(RESET)\n" "empty disk" "$@"
	@mkdir -p $(DISKIMG_DIR)
	@dd if=/dev/zero of=$@ bs=512 count=32768 2>/dev/null  # 16MB disk

$(DISKIMG_DIR)/$(FAT12_HDD_NAME): $(BIN_DIR)/kintsugios.bin $(FAT12_TEST_FILES)
	@printf "$(BLUE)[FAT12] Creating FAT12 HDD image (ATA compatible)$(RESET)\n"
	@mkdir -p $(DISKIMG_DIR) $(TEST_FILES_DIR)

	@dd if=/dev/zero of=$@.tmp bs=512 count=16384 2>/dev/null

	@printf "$(YELLOW)[FAT12] Formatting as FAT12...$(RESET)\n"
	@if command -v mkfs.fat >/dev/null 2>&1; then \
		mkfs.fat -F12 $@.tmp 2>/dev/null; \
	elif command -v mkdosfs >/dev/null 2>&1; then \
		mkdosfs -F12 $@.tmp 2>/dev/null; \
	else \
		printf "$(RED)[ERROR] Need mkfs.fat or mkdosfs! Install dosfstools.$(RESET)\n"; \
		exit 1; \
	fi

	@printf "$(YELLOW)[FAT12] Copying test files...$(RESET)\n"
	@if command -v mcopy >/dev/null 2>&1; then \
		for file in $(FAT12_TEST_FILES); do \
			mcopy -i $@.tmp $$file ::/ 2>/dev/null && \
			printf "$(GREEN)[OK]   Copied $$file$(RESET)\n" || \
			printf "$(YELLOW)[WARN] Failed to copy $$file$(RESET)\n"; \
		done; \
	else \
		printf "$(YELLOW)[WARN] mcopy not found, creating empty FAT12$(RESET)\n"; \
		printf "$(YELLOW)[WARN] Install mtools for file copying$(RESET)\n"; \
	fi

	@mv $@.tmp $@
	@printf "$(GREEN)[FAT12] Created HDD with FAT12: $@ $(RESET)\n"
	@printf "$(YELLOW)[NOTE] This is an HDD image, not floppy! Use with -hda$(RESET)\n"

# ==============================================
# TEST FILES CREATION
# ==============================================

$(TEST_FILES_DIR)/TEST.TXT:
	@mkdir -p $(TEST_FILES_DIR)
	@echo "Hello from KintsugiOS FAT12 Filesystem!" > $@
	@echo "This file was read from the HDD using ATA PIO driver." >> $@
	@echo "If you see this, your FAT12 implementation works!" >> $@
	@printf "$(GREEN)[FILE] Created $@$(RESET)\n"

$(TEST_FILES_DIR)/README.TXT:
	@mkdir -p $(TEST_FILES_DIR)
	@echo "KintsugiOS" > $@
	@echo "====================" >> $@
	@echo "" >> $@
	@echo "This is a test file on the FAT12 HDD." >> $@
	@echo "The HDD is accessed via ATA PIO driver." >> $@
	@echo "The floppy contains the OS kernel." >> $@
	@echo "The HDD contains the filesystem." >> $@
	@printf "$(GREEN)[FILE] Created $@$(RESET)\n"

$(TEST_FILES_DIR)/HELLO.TXT:
	@mkdir -p $(TEST_FILES_DIR)
	@echo "Hello World from FAT12!" > $@
	@printf "$(GREEN)[FILE] Created $@$(RESET)\n"

# ==============================================
# ISO CREATION
# ==============================================

check-iso-tools:
	@if ! command -v $(MKISOFS) >/dev/null 2>&1 && ! command -v $(XORRISO) >/dev/null 2>&1; then \
		echo "Error: Neither genisoimage nor xorriso found. Install one of them to create ISO."; \
		exit 1; \
	fi

$(DISKIMG_DIR)/$(ISO_NAME): $(DISKIMG_DIR)/$(DISKIMG_NAME) check-iso-tools
	@printf "$(BLUE)[ISO]  Creating ISO image %-50s -> %s$(RESET)\n" "$(DISKIMG_DIR)/$(DISKIMG_NAME)" "$@"
	@if command -v $(MKISOFS) >/dev/null 2>&1; then \
		$(MKISOFS) -quiet -V 'KINTSUGI_OS' -input-charset iso8859-1 -o $@ -b $(DISKIMG_NAME) -hide boot.catalog -boot-load-size 4 -no-emul-boot $(DISKIMG_DIR); \
	elif command -v $(XORRISO) >/dev/null 2>&1; then \
		$(XORRISO) -as mkisofs -b $(DISKIMG_NAME) -o $@ $(DISKIMG_DIR); \
	fi

# ==============================================
# PHONY TARGETS
# ==============================================

diskimg: $(DISKIMG_DIR)/$(DISKIMG_NAME)

hddimg: $(DISKIMG_DIR)/$(HDDIMG_NAME)

fat12hdd: $(DISKIMG_DIR)/$(FAT12_HDD_NAME)

iso: $(DISKIMG_DIR)/$(ISO_NAME)

testfiles: $(FAT12_TEST_FILES)

clean:
	@printf "$(RED)[CLEAN] Cleaning $(BIN_DIR) and $(DISKIMG_DIR)$(RESET)\n"
	@rm -rf $(BIN_DIR)/* $(DISKIMG_DIR)/*

clean_all: clean
	@printf "$(RED)[CLEAN] Cleaning test files$(RESET)\n"
	@rm -rf $(TEST_FILES_DIR)

# ==============================================
# RUN TARGETS (QEMU)
# ==============================================

run_bin: $(BIN_DIR)/kintsugios.bin
	@printf "$(GREEN)[QEMU] Run from binary %-50s$(RESET)\n" "$(BIN_DIR)/kintsugios.bin"
	@qemu-system-i386 -fda $(BIN_DIR)/kintsugios.bin -m 16

run_fda: $(DISKIMG_DIR)/$(DISKIMG_NAME)
	@printf "$(GREEN)[QEMU] Run from floppy %-50s$(RESET)\n" "$<"
	@qemu-system-i386 -fda $< -boot a -m 16

run_hdd: $(DISKIMG_DIR)/$(DISKIMG_NAME) $(DISKIMG_DIR)/$(HDDIMG_NAME)
	@printf "$(GREEN)[QEMU] Run with HDD   %-50s$(RESET)\n" "$<"
	@qemu-system-i386 -fda $< -hda $(DISKIMG_DIR)/$(HDDIMG_NAME) -boot a -m 16

run_fat12: $(DISKIMG_DIR)/$(DISKIMG_NAME) $(DISKIMG_DIR)/$(FAT12_HDD_NAME)
	@printf "$(GREEN)[QEMU] Running with FAT12 HDD (MAIN TEST)$(RESET)\n"
	@printf "$(YELLOW)[CONFIG] Floppy: boot OS, HDD: FAT12 filesystem$(RESET)\n"
	@qemu-system-i386 \
		-fda $(DISKIMG_DIR)/$(DISKIMG_NAME) \
		-hda $(DISKIMG_DIR)/$(FAT12_HDD_NAME) \
		-boot a \
		-m 16 \
		-display sdl \
		-name "KintsugiOS"

run_iso: $(DISKIMG_DIR)/$(ISO_NAME) $(DISKIMG_DIR)/$(HDDIMG_NAME)
	@printf "$(GREEN)[QEMU] Run ISO   %-50s$(RESET)\n" "$<"
	@qemu-system-i386 -hda ${DISKIMG_DIR}/$(HDDIMG_NAME) -cdrom $< -boot d -m 16

# ==============================================
# DEBUG TARGETS
# ==============================================

debug_fda: $(DISKIMG_DIR)/$(DISKIMG_NAME)
	@printf "$(GREEN)[QEMU] Debug floppy %-50s$(RESET)\n" "$<"
	@qemu-system-i386 -fda $< -boot a -s -S -m 16

debug_hdd: $(DISKIMG_DIR)/$(DISKIMG_NAME) $(DISKIMG_DIR)/$(HDDIMG_NAME)
	@printf "$(GREEN)[QEMU] Debug with HDD %-50s$(RESET)\n" "$<"
	@qemu-system-i386 -fda $< -hda $(DISKIMG_DIR)/$(HDDIMG_NAME) -boot a -s -S -m 16

# DEBUG with FAT12 HDD
debug_fat12: $(DISKIMG_DIR)/$(DISKIMG_NAME) $(DISKIMG_DIR)/$(FAT12_HDD_NAME)
	@printf "$(GREEN)[QEMU] Debug with FAT12 HDD %-50s$(RESET)\n"
	@printf "$(YELLOW)[DEBUG] Start QEMU, then connect with: gdb -ex 'target remote localhost:1234'$(RESET)\n"
	@qemu-system-i386 \
		-fda $(DISKIMG_DIR)/$(DISKIMG_NAME) \
		-hda $(DISKIMG_DIR)/$(FAT12_HDD_NAME) \
		-boot a \
		-s -S \
		-m 16

debug_iso: $(DISKIMG_DIR)/$(ISO_NAME)
	@printf "$(GREEN)[QEMU] Debug ISO %-50s$(RESET)\n" "$<"
	@qemu-system-i386 -cdrom $< -boot d -s -S -m 16

# ==============================================
# QUICK COMMANDS
# ==============================================

quick: clean all testfiles fat12hdd run_fat12

re: clean all run_fat12

info:
	@echo "=== KintsugiOS Disk Images ==="
	@echo "Floppy (boot):    $(DISKIMG_DIR)/$(DISKIMG_NAME)"
	@echo "Empty HDD:        $(DISKIMG_DIR)/$(HDDIMG_NAME)"
	@echo "FAT12 HDD:        $(DISKIMG_DIR)/$(FAT12_HDD_NAME)"
	@echo "ISO:              $(DISKIMG_DIR)/$(ISO_NAME)"
	@echo ""
	@echo "=== Useful Commands ==="
	@echo "make run_fat12    - Run with FAT12 HDD (main test)"
	@echo "make quick        - Clean, build, create FAT12, run"
	@echo "make debug_fat12  - Debug with FAT12 HDD"
	@echo "make testfiles    - Create test files only"
	@echo "make fat12hdd     - Create FAT12 HDD only"

.PHONY: all diskimg hddimg fat12hdd iso testfiles \
        clean clean_all \
        run_bin run_fda run_hdd run_fat12 run_iso \
        debug_fda debug_hdd debug_fat12 debug_iso \
        check-iso-tools quick re info
