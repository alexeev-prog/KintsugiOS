#!/usr/bin/env bash

echo "KintsugiOS Development Environment Check (64-bit Limine)"
echo "========================================================="

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

total_checks=0
passed_checks=0
warnings=0

check_tool() {
    local tool_name=$1
    local check_cmd=$2
    local required=${3:-true}
    
    ((total_checks++))
    
    if eval "$check_cmd" >/dev/null 2>&1; then
        echo -e "${GREEN}[PASS]${NC} $tool_name"
        ((passed_checks++))
    else
        if [ "$required" = true ]; then
            echo -e "${RED}[FAIL]${NC} $tool_name (required)"
        else
            echo -e "${YELLOW}[WARN]${NC} $tool_name (optional)"
            ((warnings++))
        fi
    fi
}

# Проверка 64-битного компилятора
check_64bit_compiler() {
    ((total_checks++))
    
    if command -v x86_64-elf-gcc >/dev/null 2>&1; then
        echo -e "${GREEN}[PASS]${NC} x86_64-elf-gcc"
        ((passed_checks++))
    elif command -v gcc >/dev/null 2>&1; then
        # Проверяем поддержку -m64
        if gcc -m64 -dM -E - < /dev/null >/dev/null 2>&1; then
            echo -e "${GREEN}[PASS]${NC} gcc (with -m64 support)"
            ((passed_checks++))
        else
            echo -e "${RED}[FAIL]${NC} gcc (no -m64 support)"
        fi
    else
        echo -e "${RED}[FAIL]${NC} No 64-bit compiler found"
    fi
}

# Проверка Limine (теперь полноценная)
check_limine() {
    ((total_checks++))
    
    local limine_found=0
    
    # Проверяем наличие бинарников
    if command -v limine >/dev/null 2>&1; then
        echo -e "${GREEN}[PASS]${NC} limine (binary)"
        ((passed_checks++))
        limine_found=1
    fi
    
    if command -v limine-deploy >/dev/null 2>&1; then
        echo -e "${GREEN}[PASS]${NC} limine-deploy"
        ((passed_checks++))
        limine_found=1
    fi
    
    # Проверяем файлы в стандартных местах
    if [ -f "/etc/limine/limine.sys" ] || [ -f "/usr/share/limine/limine.sys" ]; then
        echo -e "${GREEN}[PASS]${NC} limine system files"
        limine_found=1
    fi
    
    if [ $limine_found -eq 0 ]; then
        echo -e "${YELLOW}[WARN]${NC} Limine not found (will use limine package)"
        ((warnings++))
    fi
}

echo
echo "Compiler (64-bit):"
check_64bit_compiler

echo
echo "Assemblers:"
check_tool "FASM" "fasm | head -1"
check_tool "NASM" "nasm --version"

echo
echo "Emulators:"
check_tool "QEMU x86_64" "qemu-system-x86_64 --version"
check_tool "Bochs" "bochs --help" false

echo
echo "Bootloader:"
check_limine

echo
echo "Utilities:"
check_tool "GDB" "gdb --version" false
check_tool "MTools" "mtools --version"
check_tool "Xorriso" "xorriso --version"
check_tool "mkfs.fat" "mkfs --version"
check_tool "Python 3" "python3 --version"
check_tool "Make" "make --version"

# Summary
echo
echo "Environment Check Summary:"
echo "=========================="
echo -e "Total checks: $total_checks"
echo -e "${GREEN}Passed: $passed_checks${NC}"
if [ $warnings -gt 0 ]; then
    echo -e "${YELLOW}Warnings: $warnings${NC}"
fi

if [ $passed_checks -eq $total_checks ]; then
    echo -e "${GREEN}Environment is fully ready for KintsugiOS 64-bit development!${NC}"
    exit 0
elif [ $passed_checks -ge $((total_checks - warnings)) ]; then
    echo -e "${YELLOW}Environment is minimally ready (some optional tools missing).${NC}"
    echo -e "${YELLOW}Run 'nix-shell' to get all dependencies.${NC}"
    exit 0
else
    echo -e "${RED}Environment is not ready. Please install missing required tools.${NC}"
    echo -e "${RED}Run 'nix-shell' to enter development environment.${NC}"
    exit 1
fi
