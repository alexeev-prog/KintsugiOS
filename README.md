# KintsugiOS
Minimalistic and rich x86 operation system in (F)ASM and C

## Requirements
This project use [i386 ELF GCC cross compiler](http://newos.org/toolchains/i386-elf-15.1.0-Linux-x86_64.tar.xz) and FASM 1.73, NASM 2.16 and GDB, MTools, Xorriso.

Please, download cross compiler toolchain and copy him to `toolchain/` directory. And you can change `shell.nix`:

```nix
let
  customToolchainPath = "toolchain/i386-elf-15.1.0-Linux-x86_64/bin"; # put here your path to toolchain bin directory
in
```

Before development, run `check-env.sh`:

```bash
 $ ./check-env.sh

KintsugiOS Development Environment Check
========================================

Cross Compiler Tools:
[PASS] i386-elf-gcc
[PASS] i386-elf-ld
[PASS] i386-elf-objcopy

Assemblers:
[PASS] FASM
[PASS] NASM

Emulators:
[PASS] QEMU i386
[PASS] Bochs

Utilities:
[PASS] GDB
[PASS] MTools
[PASS] Xorriso

Environment Check Summary:
==========================
Total checks: 10
Passed: 10
Environment is fully ready for KintsugiOS development!
```
