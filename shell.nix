{ pkgs ? import <nixpkgs> {} }:

let
  customToolchainPath = "toolchain/x86_64-elf-14.2.0-Linux-x86_64/bin";
in
pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    fasm
    nasm
    qemu
    bochs
    gdb
    gcc
    grub2
    limine
    xorriso
    mtools
    binutils
    (python3.withPackages (ps: with ps; [ pyelftools ]))
  ];

  shellHook = ''
    export PATH="${customToolchainPath}:$PATH"
    echo "Environment ready for KintsugiOS development!"
    echo "Using cross-compiler from: ${customToolchainPath}"
    echo "Type 'make quick' to build and launch in QEMU"
  '';
}
