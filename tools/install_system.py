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


for directory in ("BIN", "BOOT", "ETC", "ETC/THEMES", "HOME", "TMP"):
    print(f"Checking directory: {directory}", flush=True)
    if not run("mdir", "-i", image, f"::/{directory}/", probe=True):
        run("mmd", "-i", image, f"::/{directory}")
programs = subprocess.check_output(
    ["make", "--no-print-directory", "list-user-programs"], text=True
).split()
for filename in programs:
    program = Path(filename)
    run("mcopy", "-o", "-i", image, str(program), f"::/BIN/{program.name}")
    print(f"Installed program: {program.name}")
run("mcopy", "-o", "-i", image, "bin/kernel.bin", "::/BOOT/KERNEL.BIN")
for theme in sorted(Path("system/themes").glob("*.THM")):
    run("mcopy", "-o", "-i", image, str(theme), f"::/ETC/THEMES/{theme.name}")
if not run("mdir", "-i", image, "::/ETC/TUI.THM", probe=True):
    run("mcopy", "-i", image, "system/themes/DARK.THM", "::/ETC/TUI.THM")
print("System installation completed.")
