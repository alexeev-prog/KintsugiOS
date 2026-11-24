#!/usr/bin/env python3
"""Build a KFS2 v2 image. Long mixed-case names, timestamps, flags."""
import struct, sys, time
from pathlib import Path

SECTOR, INODES, INODE_SECTORS, BITMAP_SECTORS = 512, 512, 128, 8
BITMAP_START, DATA_START, NAME, MAGIC, VERSION = 1 + INODE_SECTORS, 1 + INODE_SECTORS + BITMAP_SECTORS, 96, 0x3246534B, 2
SUPER = struct.Struct("<IIIIIIIII")          # magic, sectors, inode_sec, bitmap_sec, data, ver, inode_size, free_inodes, free_sectors
INODE = struct.Struct("<BBBBIIIIII96s")      # used, dir, flags, reserved, parent, size, first, sectors, ctime, mtime, name
image = Path(sys.argv[1])
files = [Path(x) for x in sys.argv[2:]]
sectors = 32768
data = bytearray(sectors * SECTOR)
now = int(time.time())
struct.pack_into("<IIIII", data, 0, MAGIC, sectors, INODE_SECTORS, BITMAP_SECTORS, DATA_START)
struct.pack_into("<IIII", data, 20, VERSION, INODE.size, INODES, sectors - DATA_START)

def mark(first, count):
    for n in range(first, first + count):
        data[(BITMAP_START + n // 4096) * SECTOR + (n % 4096) // 8] |= 1 << (n & 7)

def inode(i, used, directory, parent, size, first, count, name, flags=0):
    INODE.pack_into(data, SECTOR + i * INODE.size, used, directory, flags, 0,
                    parent, size, first, count, now, now,
                    name.encode()[:NAME - 1].ljust(NAME, b"\0"))

# Root and standard directories (Unix layout with Windows-style well-known folders).
inode(0, 1, 1, 0, 0, 0, 0, "/")
dirs = {"/": 0}
next_inode = 1
for name in ("bin", "boot", "etc", "home", "tmp", "man", "usr", "var", "dev", "proc", "root"):
    inode(next_inode, 1, 1, 0, 0, 0, 0, name)
    dirs["/" + name] = next_inode
    next_inode += 1
# KFS3 default account home directory (POSIX /home maps to /HOME).
inode(next_inode, 1, 1, dirs["/home"], 0, 0, 0, "user")
next_inode += 1
inode(next_inode, 1, 1, dirs["/etc"], 0, 0, 0, "themes")
dirs["/etc/themes"] = next_inode
next_inode += 1
inode(next_inode, 1, 1, dirs["/etc"], 0, 0, 0, "music")
dirs["/etc/music"] = next_inode
next_inode += 1
inode(next_inode, 1, 1, dirs["/var"], 0, 0, 0, "log")
dirs["/var/log"] = next_inode
next_inode += 1

def kfs3_hash(password, salt):
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

root_salt = "a1b2c3d4"
passwd = ("root:0:0:/root:/bin/shell:1\n"
          "user:1000:1000:/home/user:/bin/shell:0\n")
shadow = f"root:{root_salt}:{kfs3_hash('root', root_salt)}\nuser:c0ffee00:\n"
next_sector = DATA_START
for source in files:
    if not source.is_file():
        continue
    raw = source.read_bytes()
    count = max(1, (len(raw) + SECTOR - 1) // SECTOR)
    mark(next_sector, count)
    data[next_sector * SECTOR:(next_sector + count) * SECTOR] = raw.ljust(count * SECTOR, b"\0")
    inode(next_inode, 1, 0, dirs["/bin"], len(raw), next_sector, count, source.name.lower())
    next_sector += count
    next_inode += 1
# System metadata files in /etc and /var/log.
metadata = (
    ("/etc/motd", b"Welcome to KintsugiOS\n"),
    ("/etc/profile", b"# KintsugiOS shell profile\n# The shell exports PATH, HOME, USER and TERM itself.\n"),
    ("/etc/passwd", passwd.encode()),
    ("/etc/shadow", shadow.encode()),
    ("/etc/lastuser", b"user\n"),
    ("/etc/volume.cfg", b"70\n"),
    ("/etc/fsmeta", b""),
    ("/etc/services", Path("system/SERVICES").read_bytes()),
    ("/var/log/boot.log", b"KFS2/KFS3 image initialized\n"),
)
for music in sorted(Path("system/music").glob("*.kms")):
    metadata += (("/etc/music/" + music.name, music.read_bytes()),)
for path, content in metadata:
    parent = dirs[path.rsplit("/", 1)[0]]
    name = path.rsplit("/", 1)[-1]
    raw = content
    count = max(1, (len(raw) + SECTOR - 1) // SECTOR)
    mark(next_sector, count)
    data[next_sector * SECTOR:(next_sector + count) * SECTOR] = raw.ljust(count * SECTOR, b"\0")
    inode(next_inode, 1, 0, parent, len(raw), next_sector, count, name)
    next_sector += count
    next_inode += 1
image.write_bytes(data)
print(f"KFS2 v{VERSION}: {next_inode} inodes, {next_sector - DATA_START} data sectors")
