#!/usr/bin/env bash

echo "KintsugiOS Development Environment Check"
echo "========================================"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

total_checks=0
passed_checks=0
warnings=0

# Function to check a tool
check_tool() {
    local tool_name=$1
    local check_cmd=$2
    local required=${3:-true}
    
    ((total_checks++))
    
    if eval "$check_cmd" >/dev/null 2>&1; then
        echo -e "${GREEN}[PASS]${NC} $tool_name"
        ((passed_checks++))
        return 0
    else
        if [ "$required" = true ]; then
            echo -e "${RED}[FAIL]${NC} $tool_name (required)"
            return 0
        else
            echo -e "${YELLOW}[WARN]${NC} $tool_name (optional)"
            ((warnings++))
            return 0
        fi
    fi
}

# Special check for Bochs on NixOS
check_bochs() {
    ((total_checks++))
    if command -v bochs >/dev/null 2>&1; then
        echo -e "${GREEN}[PASS]${NC} Bochs"
        ((passed_checks++))
        return 0
    else
        echo -e "${YELLOW}[WARN]${NC} Bochs (optional)"
        ((warnings++))
        return 0
    fi
}

# Check cross compiler tools
echo
echo "Cross Compiler Tools:"
check_tool "i386-elf-gcc" "i386-elf-gcc --version" || exit 1
check_tool "i386-elf-ld" "i386-elf-ld --version" || exit 1
check_tool "i386-elf-objcopy" "i386-elf-objcopy --version" || exit 1

# Check assemblers
echo
echo "Assemblers:"
check_tool "FASM" "fasm | head -1" || exit 1
check_tool "NASM" "nasm --version"

# Check emulators
echo
echo "Emulators:"
check_tool "QEMU i386" "qemu-system-i386 --version" || exit 1
check_bochs

# Check utilities
echo
echo "Utilities:"
check_tool "GDB" "gdb --version"
check_tool "MTools" "mtools --version"
check_tool "Xorriso" "xorriso --version"

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
    echo -e "${GREEN}Environment is fully ready for KintsugiOS development!${NC}"
    exit 0
elif [ $passed_checks -ge $((total_checks - warnings)) ]; then
    echo -e "${YELLOW}Environment is minimally ready (some optional tools missing).${NC}"
    exit 0
else
    echo -e "${RED}Environment is not ready. Please install missing required tools.${NC}"
    exit 1
fi
