{ pkgs ? import <nixpkgs> {} }:

let
  customToolchainPath = "toolchain/i386-elf-15.1.0-Linux-x86_64/bin";
in
pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    fasm
    nasm
    qemu
    bochs
    gdb
    grub2
    xorriso
    mtools
    (python3.withPackages (ps: with ps; [ pyelftools ]))
  ];

  shellHook = ''
    export PATH="${customToolchainPath}:$PATH"
    echo "Environment ready for KintsugiOS development!"
    echo "Using cross-compiler from: ${customToolchainPath}"
    echo "Type 'make run' to build and launch in QEMU (if your Makefile supports it)"
  '';
}
