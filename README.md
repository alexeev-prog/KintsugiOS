# KintsugiOS
Minimalistic and rich x86 operation system in (F)ASM and C

## Requirements
This project use [i386 ELF GCC cross compiler](http://newos.org/toolchains/i386-elf-15.1.0-Linux-x86_64.tar.xz) and FASM 1.73.

Please, download cross compiler toolchain and copy him to `toolchain/` directory. And you can change `shell.nix`:

```nix
let
  customToolchainPath = "toolchain/i386-elf-15.1.0-Linux-x86_64/bin"; # put here your path to toolchain bin directory
in
```
