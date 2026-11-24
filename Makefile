.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

# Tools and build configuration
ASM ?= nasm
CROSS ?= $(shell command -v i386-elf-gcc >/dev/null 2>&1 && echo i386-elf-)
CC := $(CROSS)gcc
LD := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
PYTHON ?= python3
QEMU ?= qemu-system-i386
QEMU_FLAGS ?=
AUDIO_FLAGS ?= -audiodev driver=sdl,id=kos-audio -device adlib,audiodev=kos-audio -machine pcspk-audiodev=kos-audio
MEMORY ?= 64
VGA ?= std
BIN_DIR := bin
DISKIMG_DIR := diskimgs
DISKIMG_NAME := kintsugi_floppy_i386.img
HDDIMG_NAME := kintsugi_hdd_i386.img
FAT12_HDD_NAME := kintsugi_fat12_hdd.img
KFS2_HDD_NAME := kintsugi_kfs2_hdd.img
KFS3_HDD_NAME := kintsugi_ext2_hdd.img
ISO_NAME := KintsugiOS.iso
FLOPPY := $(DISKIMG_DIR)/$(DISKIMG_NAME)
FATDISK := $(DISKIMG_DIR)/$(FAT12_HDD_NAME)
KFS2DISK := $(DISKIMG_DIR)/$(KFS2_HDD_NAME)
KFS3DISK := $(DISKIMG_DIR)/$(KFS3_HDD_NAME)
ISO := $(DISKIMG_DIR)/$(ISO_NAME)
FAT12_TEST_FILES := $(wildcard test_files/*)
CFLAGS := -Os -g -m32 -std=gnu11 -ffreestanding -nostdinc -fno-builtin -fno-stack-protector -fno-pie -fno-pic -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -msoft-float -mpreferred-stack-boundary=2 -Wall -Wextra -MMD -MP
LDFLAGS := -m elf_i386 -T src/bootloader/kernel.ld
C_SOURCES := $(sort $(shell find src/kernel -name '*.c'))
OBJS := $(BIN_DIR)/bootloader/kernel_entry.o $(BIN_DIR)/kernel/cpu/interrupt.o $(BIN_DIR)/kernel/user/entry.o $(C_SOURCES:src/%.c=$(BIN_DIR)/%.o)

all: $(BIN_DIR)/kintsugios.bin users

USER_PROGRAMS := ECHO CAT LS PWD FAULT CHECK INPUT SPIN KLOG FDCHECK KCALC VOLUMECTL POSIXINFO KILL CHMOD CHOWN BASENAME DIRNAME PRINTF DATE UNAME SLEEP TOUCH HEAD SEQ PRINTENV DU CUT TR NL REV TAC UNIQ TEE YES EXPR FACTOR CKSUM OD STRINGS CMP REALPATH TRUNCATE MKTEMP
USER_ELFS := $(addprefix $(BIN_DIR)/user/,$(addsuffix .ELF,$(USER_PROGRAMS)))
USER_ELFS += $(BIN_DIR)/user/INIT.ELF
USER_ELFS += $(BIN_DIR)/user/SHELL.ELF
$(BIN_DIR)/user/SHELL.ELF: src/user/shell.c src/user/shellabi.h src/user/runtime.h src/user/user.ld $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/start.o
	$(CC) $(CFLAGS) -c $< -o $(BIN_DIR)/user/SHELL.o
	$(LD) -m elf_i386 -T src/user/user.ld $(BIN_DIR)/user/start.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/SHELL.o -o $@
$(BIN_DIR)/user/INIT.ELF: src/user/init.c src/user/ktuilib.h src/user/user.ld $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/ktuilib.o $(BIN_DIR)/user/start.o
	$(CC) $(CFLAGS) -c $< -o $(BIN_DIR)/user/INIT.o
	$(LD) -m elf_i386 -T src/user/user.ld $(BIN_DIR)/user/start.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/ktuilib.o $(BIN_DIR)/user/INIT.o -o $@
TUI_PROGRAMS := KUNED VIK KBSM KMEMMAN KC KPROCM KDISKMAN KTUI KTTE PAGER JOURNAL HIERO
TUI_PROGRAMS += KTERM KMAN DIR
TUI_PROGRAMS += KSET KPAINT KIWI KINFO KFETCH KRRAWL
TUI_PROGRAMS += KMUSA KLISTEN SOUNDCTL KTMUX KSERVICES KSERSYS KSDAEMON KCSV KMINES K2048 KLIFE
UTIL_PROGRAMS := TREE FIND GREP TAIL WC SORT KFSCK PROC DF
USER_ELFS += $(addprefix $(BIN_DIR)/user/,$(addsuffix .ELF,$(UTIL_PROGRAMS)))
USER_ELFS += $(addprefix $(BIN_DIR)/user/,$(addsuffix .ELF,$(TUI_PROGRAMS)))
$(BIN_DIR)/user/ktuilib.o: src/user/ktuilib.c src/user/ktuilib.h src/user/tuiabi.h src/user/runtime.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
define TUI_RULE
$(BIN_DIR)/user/$(1).ELF: src/user/$(2).c src/user/ktuilib.h src/user/user.ld $(BIN_DIR)/user/ktuilib.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/start.o
	$$(CC) $$(CFLAGS) -c $$< -o $$(BIN_DIR)/user/$(1).o
	$$(LD) -m elf_i386 -T src/user/user.ld $$(BIN_DIR)/user/start.o $$(BIN_DIR)/user/runtime.o $$(BIN_DIR)/user/ktuilib.o $$(BIN_DIR)/user/$(1).o -o $$@
endef
$(eval $(call TUI_RULE,KUNED,kuned))
$(BIN_DIR)/user/VIK.ELF: src/user/kuned.c src/user/ktuilib.h src/user/user.ld $(BIN_DIR)/user/ktuilib.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/start.o
	$(CC) $(CFLAGS) -DVIK=1 -c $< -o $(BIN_DIR)/user/VIK.o
	$(LD) -m elf_i386 -T src/user/user.ld $(BIN_DIR)/user/start.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/ktuilib.o $(BIN_DIR)/user/VIK.o -o $@
$(eval $(call TUI_RULE,KBSM,kbsm))
$(eval $(call TUI_RULE,KMEMMAN,kbsm))
$(BIN_DIR)/user/KMEMMAN.ELF: CFLAGS += -DKMEMMAN=1
$(eval $(call TUI_RULE,KC,kc))
$(eval $(call TUI_RULE,KTTE,ktte))
$(eval $(call TUI_RULE,PAGER,pager))
$(eval $(call TUI_RULE,JOURNAL,journal))
$(eval $(call TUI_RULE,HIERO,hieroglyph))
$(eval $(call TUI_RULE,KTERM,kterm))
$(eval $(call TUI_RULE,KMAN,kman))
$(eval $(call TUI_RULE,DIR,dir))
$(eval $(call TUI_RULE,KSET,ksettings))
$(eval $(call TUI_RULE,KPAINT,kpaint))
$(eval $(call TUI_RULE,KIWI,kiwi))
$(eval $(call TUI_RULE,KINFO,kinfo))
$(eval $(call TUI_RULE,KFETCH,kfetch))
$(BIN_DIR)/user/kms.o: src/user/kms.c src/user/kms.h src/user/runtime.h src/user/desktopabi.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BIN_DIR)/user/KRRAWL.ELF: src/user/krrawl.c src/user/kms.h src/user/ktuilib.h src/user/user.ld $(BIN_DIR)/user/ktuilib.o $(BIN_DIR)/user/kms.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/start.o
	$(CC) $(CFLAGS) -c $< -o $(BIN_DIR)/user/KRRAWL.o
	$(LD) -m elf_i386 -T src/user/user.ld $(BIN_DIR)/user/start.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/ktuilib.o $(BIN_DIR)/user/kms.o $(BIN_DIR)/user/KRRAWL.o -o $@
$(eval $(call TUI_RULE,KTMUX,ktmux))
$(eval $(call TUI_RULE,KSERVICES,kservices))
$(eval $(call TUI_RULE,KSERSYS,ksersys))
$(BIN_DIR)/user/KSDAEMON.ELF: src/user/ksersys.c src/user/authabi.h src/user/sysabi.h src/user/user.ld $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/start.o
	$(CC) $(CFLAGS) -DSERVICE_DAEMON=1 -c $< -o $(BIN_DIR)/user/KSDAEMON.o
	$(LD) -m elf_i386 -T src/user/user.ld $(BIN_DIR)/user/start.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/KSDAEMON.o -o $@
$(eval $(call TUI_RULE,KCSV,kcsv))
$(eval $(call TUI_RULE,KMINES,kmines))
$(eval $(call TUI_RULE,K2048,k2048))
$(eval $(call TUI_RULE,KLIFE,klife))
$(BIN_DIR)/user/KMUSA.ELF: src/user/soundapps.c src/user/kms.h src/user/ktuilib.h src/user/user.ld $(BIN_DIR)/user/ktuilib.o $(BIN_DIR)/user/kms.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/start.o
	$(CC) $(CFLAGS) -DSOUND_APP=1 -c $< -o $(BIN_DIR)/user/KMUSA.o
	$(LD) -m elf_i386 -T src/user/user.ld $(BIN_DIR)/user/start.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/ktuilib.o $(BIN_DIR)/user/kms.o $(BIN_DIR)/user/KMUSA.o -o $@
$(BIN_DIR)/user/KLISTEN.ELF: src/user/soundapps.c src/user/kms.h src/user/ktuilib.h src/user/user.ld $(BIN_DIR)/user/ktuilib.o $(BIN_DIR)/user/kms.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/start.o
	$(CC) $(CFLAGS) -DSOUND_APP=2 -c $< -o $(BIN_DIR)/user/KLISTEN.o
	$(LD) -m elf_i386 -T src/user/user.ld $(BIN_DIR)/user/start.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/ktuilib.o $(BIN_DIR)/user/kms.o $(BIN_DIR)/user/KLISTEN.o -o $@
$(BIN_DIR)/user/SOUNDCTL.ELF: src/user/soundapps.c src/user/kms.h src/user/ktuilib.h src/user/user.ld $(BIN_DIR)/user/ktuilib.o $(BIN_DIR)/user/kms.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/start.o
	$(CC) $(CFLAGS) -DSOUND_APP=3 -c $< -o $(BIN_DIR)/user/SOUNDCTL.o
	$(LD) -m elf_i386 -T src/user/user.ld $(BIN_DIR)/user/start.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/ktuilib.o $(BIN_DIR)/user/kms.o $(BIN_DIR)/user/SOUNDCTL.o -o $@
$(BIN_DIR)/user/KCALC.ELF: src/user/kcalc.c src/user/runtime.h src/user/user.ld $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/start.o
	$(CC) $(CFLAGS) -c $< -o $(BIN_DIR)/user/KCALC.o
	$(LD) -m elf_i386 -T src/user/user.ld $(BIN_DIR)/user/start.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/KCALC.o -o $@
$(BIN_DIR)/user/ktuilib.o: src/user/tuidoc.c
define DIAG_RULE
$(BIN_DIR)/user/$(1).ELF: src/user/diagnostics.c src/user/sysabi.h $(BIN_DIR)/user/ktuilib.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/start.o
	$$(CC) $$(CFLAGS) -DAPP=$(2) -c $$< -o $$(BIN_DIR)/user/$(1).o
	$$(LD) -m elf_i386 -T src/user/user.ld $$(BIN_DIR)/user/start.o $$(BIN_DIR)/user/runtime.o $$(BIN_DIR)/user/ktuilib.o $$(BIN_DIR)/user/$(1).o -o $$@
endef
$(eval $(call DIAG_RULE,KPROCM,1))
$(eval $(call DIAG_RULE,KDISKMAN,2))
$(eval $(call DIAG_RULE,KTUI,3))
define UTIL_RULE
$(BIN_DIR)/user/$(1).ELF: src/user/utils.c src/user/sysabi.h $(BIN_DIR)/user/ktuilib.o $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/start.o
	$$(CC) $$(CFLAGS) -Wno-unused-function -Wno-unused-parameter -DUTIL=$(2) -c $$< -o $$(BIN_DIR)/user/$(1).o
	$$(LD) -m elf_i386 -T src/user/user.ld $$(BIN_DIR)/user/start.o $$(BIN_DIR)/user/runtime.o $$(BIN_DIR)/user/ktuilib.o $$(BIN_DIR)/user/$(1).o -o $$@
endef
$(eval $(call UTIL_RULE,TREE,1))
$(eval $(call UTIL_RULE,FIND,2))
$(eval $(call UTIL_RULE,GREP,3))
$(eval $(call UTIL_RULE,TAIL,4))
$(eval $(call UTIL_RULE,WC,5))
$(eval $(call UTIL_RULE,SORT,6))
$(eval $(call UTIL_RULE,KFSCK,8))
$(eval $(call UTIL_RULE,PROC,9))
$(eval $(call UTIL_RULE,DF,10))
$(BIN_DIR)/user/start.o: src/user/start.asm
	@mkdir -p $(dir $@)
	$(ASM) -f elf32 $< -o $@
$(BIN_DIR)/user/runtime.o: src/user/runtime.c src/user/runtime.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
define USER_RULE
$(BIN_DIR)/user/$(1).ELF: src/user/commands.c src/user/user.ld src/user/runtime.h src/user/posix.h src/shared/signal.h $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/start.o
	$$(CC) $$(CFLAGS) -Wno-unused-function -DCOMMAND=$(2) -c $$< -o $$(BIN_DIR)/user/$(1).o
	$$(LD) -m elf_i386 -T src/user/user.ld $$(BIN_DIR)/user/start.o $$(BIN_DIR)/user/runtime.o $$(BIN_DIR)/user/$(1).o -o $$@
endef
$(eval $(call USER_RULE,ECHO,1))
$(eval $(call USER_RULE,CAT,2))
$(eval $(call USER_RULE,LS,3))
$(eval $(call USER_RULE,PWD,4))
$(eval $(call USER_RULE,FAULT,5))
$(eval $(call USER_RULE,CHECK,6))
$(eval $(call USER_RULE,INPUT,7))
$(eval $(call USER_RULE,SPIN,8))
$(eval $(call USER_RULE,FDCHECK,10))
$(eval $(call USER_RULE,KLOG,9))
$(eval $(call USER_RULE,VOLUMECTL,11))
$(eval $(call USER_RULE,POSIXINFO,12))
$(eval $(call USER_RULE,KILL,13))
$(eval $(call USER_RULE,CHMOD,14))
$(eval $(call USER_RULE,CHOWN,15))
$(eval $(call USER_RULE,BASENAME,16))
$(eval $(call USER_RULE,DIRNAME,17))
$(eval $(call USER_RULE,PRINTF,18))
$(eval $(call USER_RULE,DATE,19))
$(eval $(call USER_RULE,UNAME,20))
$(eval $(call USER_RULE,SLEEP,21))
$(eval $(call USER_RULE,TOUCH,22))
$(eval $(call USER_RULE,HEAD,23))
$(eval $(call USER_RULE,SEQ,24))
$(eval $(call USER_RULE,PRINTENV,25))
$(eval $(call USER_RULE,DU,26))
$(eval $(call USER_RULE,CUT,27))
$(eval $(call USER_RULE,TR,28))
$(eval $(call USER_RULE,NL,29))
$(eval $(call USER_RULE,REV,30))
$(eval $(call USER_RULE,TAC,31))
$(eval $(call USER_RULE,UNIQ,32))
$(eval $(call USER_RULE,TEE,33))
$(eval $(call USER_RULE,YES,34))
$(eval $(call USER_RULE,EXPR,35))
$(eval $(call USER_RULE,FACTOR,36))
$(eval $(call USER_RULE,CKSUM,37))
$(eval $(call USER_RULE,OD,38))
$(eval $(call USER_RULE,STRINGS,39))
$(eval $(call USER_RULE,CMP,40))
$(eval $(call USER_RULE,REALPATH,41))
$(eval $(call USER_RULE,TRUNCATE,42))
$(eval $(call USER_RULE,MKTEMP,43))
ADMIN_PROGRAMS := USERADD USERPSWD ROOTPSWD USERCHANGE
USER_ELFS += $(addprefix $(BIN_DIR)/user/,$(addsuffix .ELF,$(ADMIN_PROGRAMS)))
define ADMIN_RULE
$(BIN_DIR)/user/$(1).ELF: src/user/useradm.c src/user/authabi.h src/user/runtime.h src/user/user.ld $(BIN_DIR)/user/runtime.o $(BIN_DIR)/user/start.o
	$$(CC) $$(CFLAGS) -DUSER_TOOL=$(2) -c $$< -o $$(BIN_DIR)/user/$(1).o
	$$(LD) -m elf_i386 -T src/user/user.ld $$(BIN_DIR)/user/start.o $$(BIN_DIR)/user/runtime.o $$(BIN_DIR)/user/$(1).o -o $$@
endef
$(eval $(call ADMIN_RULE,USERADD,1))
$(eval $(call ADMIN_RULE,USERPSWD,2))
$(eval $(call ADMIN_RULE,ROOTPSWD,3))
$(eval $(call ADMIN_RULE,USERCHANGE,4))
users: $(USER_ELFS)
list-user-programs:
	@printf '%s\n' $(USER_ELFS)
.PHONY: list-user-programs
system-install: $(FATDISK) $(USER_ELFS) $(BIN_DIR)/kernel.bin
	$(PYTHON) tools/install_system.py $(FATDISK)
.PHONY: users system-install

# Compile and link the freestanding kernel
$(BIN_DIR)/kernel/drivers/framebuffer.o: CFLAGS += -O2
# Security, VFS and scheduler paths are size-optimized to keep the image
# below the protected-mode stack guard while remaining easy to audit.
$(BIN_DIR)/kernel/user/process.o $(BIN_DIR)/kernel/user/auth.o \
$(BIN_DIR)/kernel/fs/kfs3.o $(BIN_DIR)/kernel/fs/vfs.o \
$(BIN_DIR)/kernel/shell/commands_fs.o: CFLAGS += -Os
# Non-timing-critical storage and command code: size matters for the image.
$(BIN_DIR)/kernel/fs/kfs2.o $(BIN_DIR)/kernel/fs/fat12.o \
$(BIN_DIR)/kernel/fs/block.o $(BIN_DIR)/kernel/user/fd.o \
$(BIN_DIR)/kernel/shell/commands_diag.o $(BIN_DIR)/kernel/shell/commands_misc.o \
$(BIN_DIR)/kernel/shell/commands_system.o: CFLAGS += -Os
$(BIN_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Recompile existing objects when compiler/flags change (including old builds).
$(BIN_DIR)/build_config: FORCE
	@mkdir -p $(BIN_DIR)
	@printf '%s\n' '$(CC) $(CFLAGS)' '$(ASM) -f elf32' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(OBJS): $(BIN_DIR)/build_config
FORCE:
.PHONY: FORCE

$(BIN_DIR)/%.o: src/%.asm
	@mkdir -p $(dir $@)
	$(ASM) -f elf32 $< -o $@

$(BIN_DIR)/kernel.elf: $(OBJS) src/bootloader/kernel.ld
	$(LD) $(LDFLAGS) $(OBJS) -Map $(BIN_DIR)/kernel.map -o $@.tmp
	mv $@.tmp $@

$(BIN_DIR)/kernel.bin: $(BIN_DIR)/kernel.elf
	$(OBJCOPY) -O binary $< $@.tmp
	@test -s $@.tmp
	mv $@.tmp $@

$(BIN_DIR)/kernel_size.inc: $(BIN_DIR)/kernel.bin
	@$(PYTHON) -c 'from pathlib import Path; n=(Path("$<").stat().st_size+511)//512; assert 0<n<=(0x80000-0x7e00)//512, "Kernel too large"; Path("$@").write_text(f"%define KERNEL_SECTORS {n}\n")'

$(BIN_DIR)/bootsector.bin: $(wildcard src/bootloader/*.asm) $(BIN_DIR)/kernel_size.inc
	$(ASM) -I$(BIN_DIR)/ -f bin src/bootloader/bootsector.asm -o $@

$(BIN_DIR)/kintsugios.bin: $(BIN_DIR)/bootsector.bin $(BIN_DIR)/kernel.bin
	@$(PYTHON) -c 'from pathlib import Path; b=Path("$(BIN_DIR)/bootsector.bin").read_bytes(); k=Path("$(BIN_DIR)/kernel.bin").read_bytes(); assert len(b)==512 and b[510:]==b"\x55\xaa"; Path("$@").write_bytes(b+k+bytes((-len(k))%512))'

$(FLOPPY): $(BIN_DIR)/kintsugios.bin
	@mkdir -p $(dir $@)
	@$(PYTHON) -c 'from pathlib import Path; b=Path("$<").read_bytes(); assert len(b)<=1474560; Path("$@.tmp").write_bytes(b+bytes(1474560-len(b)))'
	mv $@.tmp $@

# Persistent disks: never depend on kernel or fixture timestamps
$(DISKIMG_DIR)/$(HDDIMG_NAME):
	@mkdir -p $(dir $@)
	truncate -s 16M $@

# Persistent user data: source/test changes never implicitly format this disk.
$(FATDISK):
	@mkdir -p $(dir $@)
	@command -v mkfs.fat >/dev/null || { echo 'Install dosfstools'; exit 1; }
	@command -v mcopy >/dev/null || { echo 'Install mtools'; exit 1; }
	truncate -s 8M $@.tmp
	mkfs.fat -F 12 -n KINTSUGIOS $@.tmp
	@set -e; for file in $(FAT12_TEST_FILES); do mcopy -o -i $@.tmp "$$file" ::/; done
	mv $@.tmp $@

$(ISO): $(FLOPPY)
	@mkdir -p $(BIN_DIR)/iso
	cp $(FLOPPY) $(BIN_DIR)/iso/$(DISKIMG_NAME)
	@if command -v genisoimage >/dev/null; then \
		genisoimage -quiet -V KINTSUGI_OS -b $(DISKIMG_NAME) -o $@.tmp $(BIN_DIR)/iso; \
	else xorriso -as mkisofs -V KINTSUGI_OS -b $(DISKIMG_NAME) -o $@.tmp $(BIN_DIR)/iso; fi
	mv $@.tmp $@

diskimg: $(FLOPPY)
hddimg: $(DISKIMG_DIR)/$(HDDIMG_NAME)
fat12hdd: $(FATDISK)
disk-create: $(FATDISK)
disk-reset:
	@test "$(CONFIRM)" = YES || { echo 'Deletes filesystem data: make disk-reset CONFIRM=YES'; exit 1; }
	$(MAKE) -B disk-create
iso: $(ISO)
testfiles: $(FAT12_TEST_FILES)

# Every normal launch has a filesystem disk; run_bin uses a full-size floppy.
FLOPPY_DRIVE = -drive file=$(FLOPPY),format=raw,if=floppy,index=0
FAT_DRIVE = -drive file=$(FATDISK),format=raw,if=ide,index=0
RUN = $(QEMU) -m $(MEMORY) -vga $(VGA) -name KintsugiOS $(AUDIO_FLAGS) $(QEMU_FLAGS)
run: run_kfs2
run_bin run_fda run_hdd run_fat12: $(FLOPPY) $(FATDISK) system-install
	$(RUN) $(FLOPPY_DRIVE) $(FAT_DRIVE) -boot order=a
$(KFS2DISK): $(USER_ELFS) $(BIN_DIR)/kernel.bin
	@mkdir -p $(dir $@)
	$(PYTHON) tools/install_kfs2.py $@
$(KFS3DISK): $(USER_ELFS) $(BIN_DIR)/kernel.bin tools/mkfs_ext2.py
	@mkdir -p $(dir $@)
	$(PYTHON) tools/mkfs_ext2.py $@
kfs2: $(KFS2DISK)
kfs3 ext2: $(KFS3DISK)
run_kfs2: $(FLOPPY) $(KFS2DISK)
	$(RUN) $(FLOPPY_DRIVE) -drive file=$(KFS2DISK),format=raw,if=ide,index=0 -boot order=a
run_kfs3: $(FLOPPY) $(KFS3DISK)
	$(RUN) $(FLOPPY_DRIVE) -drive file=$(KFS3DISK),format=raw,if=ide,index=0 -boot order=a
run_iso: $(ISO) $(FATDISK) system-install
	$(RUN) -cdrom $(ISO) $(FAT_DRIVE) -boot order=d
debug_fda debug_hdd debug_fat12: $(FLOPPY) $(FATDISK)
	$(RUN) $(FLOPPY_DRIVE) $(FAT_DRIVE) -boot order=a -s -S
debug_iso: $(ISO) $(FATDISK)
	$(RUN) -cdrom $(ISO) $(FAT_DRIVE) -boot order=d -s -S

# Do not delete user-created files or writable filesystem images on clean.
clean:
	rm -f $(USER_ELFS) $(addprefix $(BIN_DIR)/user/,$(addsuffix .o,$(USER_PROGRAMS))) $(BIN_DIR)/user/start.o
	rm -f $(OBJS) $(OBJS:.o=.d) $(BIN_DIR)/kernel.elf $(BIN_DIR)/kernel.map $(BIN_DIR)/kernel.bin $(BIN_DIR)/kernel_size.inc $(BIN_DIR)/bootsector.bin $(BIN_DIR)/kintsugios.bin $(FLOPPY) $(ISO)
clean_all: clean
	@echo 'Filesystem images and test_files preserved; remove them explicitly to recreate.'
quick re:
	$(MAKE) clean
	$(MAKE) run_fat12
help info:
	@echo 'Build: make [all|diskimg|iso]'
	@echo 'Disk: make disk-create (only if missing); make disk-reset CONFIRM=YES (ERASES data)'
	@echo 'Run: make run / run_kfs2 (KFS2), make run_kfs3 (KFS3 POSIX), make run_fat12 (legacy)'
	@echo 'Test: make test (temporary disks only); clean preserves persistent disks'
	@echo 'Debug: make debug_fat12; gdb bin/kernel.elf -ex "target remote :1234"'
	@echo 'Headless: make run QEMU_FLAGS="-display none -monitor stdio"'
	@echo 'CC=$(CC), LD=$(LD); filesystem images survive make clean.'

-include $(OBJS:.o=.d) $(wildcard $(BIN_DIR)/user/*.d)
# Headless integration tests (never use the persistent user disk)
HOSTCC ?= cc
test-tui:
	@mkdir -p $(BIN_DIR)
	$(HOSTCC) -Wall -Wextra -Werror -Wno-pointer-to-int-cast -no-pie tools/test_tui.c -o $(BIN_DIR)/test-tui
	$(BIN_DIR)/test-tui
test: $(FLOPPY) $(ISO) $(USER_ELFS) test-tui
	$(PYTHON) tools/smoke_test.py --display
	$(PYTHON) tools/smoke_test.py
	$(PYTHON) tools/smoke_test.py --iso
	$(PYTHON) tools/smoke_test.py --large-kernel
	$(PYTHON) tools/smoke_test.py --no-disk
	$(PYTHON) tools/smoke_test.py --invalid-fs
	$(PYTHON) tools/smoke_test.py --corrupt-fat
	$(PYTHON) tools/smoke_test.py --full-disk
	$(PYTHON) tools/smoke_test.py --panic
	$(PYTHON) tools/smoke_test.py --fault
.PHONY: test kfs2 kfs3 run_kfs2 run_kfs3
.PHONY: test-tui
.PHONY: help disk-create disk-reset
.PHONY: all diskimg hddimg fat12hdd iso testfiles run run_bin run_fda run_hdd run_fat12 run_iso run_kfs3 debug_fda debug_hdd debug_fat12 debug_iso clean clean_all quick re info
