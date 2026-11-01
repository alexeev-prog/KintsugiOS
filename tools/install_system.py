#!/usr/bin/env python3
"""Install system files into an existing FAT image without formatting user data."""

import subprocess
import sys
from pathlib import Path

image = sys.argv[1]


def run(*arguments, probe=False):
    try:
        result = subprocess.run(
            arguments,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            timeout=15,
        )
    except subprocess.TimeoutExpired:
        sys.exit(
            f"Timed out: {' '.join(arguments)}. Stop QEMU and check the image with fsck.fat -n."
        )
    if result.returncode:
        detail = (result.stderr + result.stdout).strip()
        if probe:
            return False
        sys.exit(f"Failed: {' '.join(arguments)}\n{detail}")
    return True


for directory in ("BIN", "BOOT", "ETC", "HOME", "TMP"):
    print(f"Checking directory: {directory}", flush=True)
    if not run("mdir", "-i", image, f"::/{directory}/", probe=True):
        run("mmd", "-i", image, f"::/{directory}")
for program in sorted(Path("bin/user").glob("*.ELF")):
    run("mcopy", "-o", "-i", image, str(program), f"::/BIN/{program.name}")
    print(f"Installed program: {program.name}")
run("mcopy", "-o", "-i", image, "bin/kernel.bin", "::/BOOT/KERNEL.BIN")
print("System installation completed.")
