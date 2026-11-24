#!/usr/bin/env python3
"""Build a bootable ext2 image for the KFS3 POSIX layer.

Usage:
  mkfs_ext2.py IMAGE                  build from the current source tree
  mkfs_ext2.py IMAGE --from DIR       convert an already exported tree (migration)

The default mode populates a staging tree with the current ELF programs, system
files and music; `tools/migrate_kfs2.py` can produce such a tree from an old
KFS2 image for migration. The image is checked with e2fsck afterwards.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

IMAGE = Path(sys.argv[1])
EXTERNAL = Path(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[2] == "--from" else None
ROOT = Path.cwd()
SECTORS = 32768          # 16 MiB at 512 bytes/sector
BLOCKS = SECTORS // 2    # 1 KiB ext2 blocks


def kfs3_hash(password: str, salt: str) -> str:
    h = 2166136261
    for ch in salt.encode():
        h ^= ch
        h = (h * 16777619) & 0xFFFFFFFF
    for rnd in range(512):
        for ch in password.encode():
            h ^= ch
            h = (h * 16777619) & 0xFFFFFFFF
            h ^= h >> 13
        h ^= rnd & 255
        h = (h * 16777619) & 0xFFFFFFFF
    return f"{h:08x}"


def build_stage(stage: Path) -> None:
    for directory in ("bin", "boot", "etc", "etc/music", "etc/themes", "home",
                      "home/user", "tmp", "man", "usr", "var", "var/log", "dev",
                      "proc", "root"):
        (stage / directory).mkdir(parents=True, exist_ok=True)
    (stage / "home/user").chmod(0o700)
    (stage / "tmp").chmod(0o777)

    programs = subprocess.check_output(
        ["make", "--no-print-directory", "list-user-programs"], text=True
    ).split()
    for program in programs:
        source = Path(program)
        if source.is_file():
            shutil.copy2(source, stage / "bin" / source.name.lower())
    (stage / "boot/kernel.bin").write_bytes((ROOT / "bin/kernel.bin").read_bytes())

    salt = "a1b2c3d4"
    files = {
        "etc/passwd": "root:0:0:/root:/bin/shell:1\nuser:1000:1000:/home/user:/bin/shell:0\n",
        "etc/shadow": f"root:{salt}:{kfs3_hash('root', salt)}\nuser:c0ffee00:\n",
        "etc/lastuser": "user\n",
        "etc/volume.cfg": "70\n",
        "etc/fsmeta": "",
        "etc/motd": "Welcome to KintsugiOS\n",
        "etc/profile": "# KintsugiOS shell profile\n# The shell exports PATH, HOME, USER and TERM itself.\n",
        "etc/services": (ROOT / "system/SERVICES").read_text(),
        "var/log/boot.log": "ext2 filesystem initialized\n",
    }
    for path, content in files.items():
        (stage / path).write_text(content)
    (stage / "etc/shadow").chmod(0o600)
    for music in sorted((ROOT / "system/music").glob("*.kms")):
        shutil.copy2(music, stage / "etc/music" / music.name)
    for theme in sorted((ROOT / "system/themes").glob("*")):
        if theme.is_file():
            shutil.copy2(theme, stage / "etc/themes" / theme.name)
    if (stage / "etc/themes/DARK.THM").exists():
        shutil.copy2(stage / "etc/themes/DARK.THM", stage / "etc/TUI.THM")


if EXTERNAL is None:
    with tempfile.TemporaryDirectory(prefix="kintsugi-ext2-", dir="/tmp/opencode") as tmp:
        stage = Path(tmp) / "root"
        build_stage(stage)
        IMAGE.parent.mkdir(parents=True, exist_ok=True)
        if IMAGE.exists():
            IMAGE.unlink()
        subprocess.run([
            "mke2fs", "-q", "-F", "-t", "ext2", "-b", "1024", "-I", "128",
            "-O", "dir_index,^ext_attr,^resize_inode,^metadata_csum,"
                  "^huge_file,^flex_bg,^uninit_bg,^large_file",
            "-d", str(stage), str(IMAGE), str(BLOCKS),
        ], check=True)
        subprocess.run(["e2fsck", "-fy", str(IMAGE)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
else:
    IMAGE.parent.mkdir(parents=True, exist_ok=True)
    if IMAGE.exists():
        IMAGE.unlink()
    subprocess.run([
        "mke2fs", "-q", "-F", "-t", "ext2", "-b", "1024", "-I", "128",
        "-O", "dir_index,^ext_attr,^resize_inode,^metadata_csum,"
              "^huge_file,^flex_bg,^uninit_bg,^large_file",
        "-d", str(EXTERNAL), str(IMAGE), str(BLOCKS),
    ], check=True)
    subprocess.run(["e2fsck", "-fy", str(IMAGE)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

print(f"ext2 image: {IMAGE} ({BLOCKS} blocks of 1024 bytes)")
