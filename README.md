# KintsugiOS
Минималистичная x86 операционная система на (F)ASM и С.

## Requirements
Этот проект использует [кросс компилятор i386 ELF GCC](http://newos.org/toolchains/i386-elf-15.1.0-Linux-x86_64.tar.xz), FASM 1.73, NASM 2.16 и GDB, MTools, Xorriso.

Пожалуйста, скачайте тулчейн компилятора и переместите его в директорию `toolchain/`. Также вы можете изменить `shell.nix`:

```nix
let
  customToolchainPath = "toolchain/i386-elf-15.1.0-Linux-x86_64/bin"; # ваш путь до бинарников компилятора
in
```

Перел разработкой, проверьте вывод `check-env.sh`:

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
