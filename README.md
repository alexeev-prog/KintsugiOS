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

## Литература, источники

По языку ассемблера:

 + Зубков С. В. «Assembler для DOS, Windows и Unix»
 + [Введение в машинный код](http://wasm.ru/article.php?article=1022001)
 + [Программирование на ассемблере под DOS](http://wasm.ru/article.php?article=1022003)
 + [Введение в ассемблер](https://hackware.ru/?p=8654)
 + [ASCII таблица](https://www.asciitable.com/)
 + [Погружение в ассемблер](https://xakep.ru/2017/09/11/asm-course-1/)
 + [От изучающего ассемблер до взломщика программ](https://wasm.in/attachments/skljarov-i-izuchaem-assembler-za-7-dnej-pdf.2906/)
 + [Руководство по ассемблеру MASM intel x86_64](https://metanit.com/assembler/tutorial/)

По языку Си:

 + Керниган Б., Ритчи Д. «Язык программирования C»
 + Шилдт Г. «Полный справочник по C»

По устройству операционных систем:

 + Таненбаум Э. «Современные операционные системы»
 + Таненбаум Э. «Операционные системы: Разработка и реализация»
 + Олифер В., Олифер Н. «Сетевые операционные системы»
 + [OSDEV Wiki](http://osdev.org)

По архитектуре ЭВМ:

 + Таненбаум Э. «Архитектура компьютера»
 + Гук М. «Аппаратные средства IBM PC. Энциклопедия»
 + Петцольд Ч. «Код. Тайный язык информатики»
