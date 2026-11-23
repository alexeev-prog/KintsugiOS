#!/usr/bin/env python3
import subprocess, sys
from pathlib import Path
image = Path(sys.argv[1])
programs = subprocess.check_output(["make", "--no-print-directory", "list-user-programs"], text=True).split()
subprocess.run([sys.executable, "tools/mkfs_kfs2.py", str(image), *programs], check=True)
print("KFS2 system image created")
