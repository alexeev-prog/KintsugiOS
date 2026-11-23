#!/usr/bin/env python3
import struct, sys
from pathlib import Path

SECTOR, INODES, INODE_SECTORS, BITMAP_SECTORS = 512, 256, 64, 8
BITMAP_START, DATA_START, NAME, MAGIC = 1 + INODE_SECTORS, 1 + INODE_SECTORS + BITMAP_SECTORS, 96, 0x3246534B
INODE = struct.Struct("<BBHIIII96s")
image = Path(sys.argv[1])
files = [Path(x) for x in sys.argv[2:]]
sectors = 32768
data = bytearray(sectors * SECTOR)
struct.pack_into("<IIIII", data, 0, MAGIC, sectors, INODE_SECTORS, BITMAP_SECTORS, DATA_START)

def mark(first, count):
    for n in range(first, first + count):
        data[(BITMAP_START + n // 4096) * SECTOR + (n % 4096) // 8] |= 1 << (n & 7)

def inode(i, used, directory, parent, size, first, count, name):
    INODE.pack_into(data, SECTOR + i * INODE.size, used, directory, 0, parent, size, first, count,
                    name.encode()[:NAME - 1].ljust(NAME, b"\0"))

inode(0, 1, 1, 0, 0, 0, 0, "/")
dirs, next_inode = {"/": 0}, 1
for name in ("BIN", "BOOT", "ETC", "HOME", "TMP", "MAN"):
    inode(next_inode, 1, 1, 0, 0, 0, 0, name); dirs["/" + name] = next_inode; next_inode += 1
inode(next_inode, 1, 1, dirs["/ETC"], 0, 0, 0, "THEMES"); next_inode += 1
next_sector = DATA_START
for source in files:
    if not source.is_file(): continue
    raw = source.read_bytes(); count = max(1, (len(raw) + SECTOR - 1) // SECTOR)
    mark(next_sector, count)
    data[next_sector * SECTOR:(next_sector + count) * SECTOR] = raw.ljust(count * SECTOR, b"\0")
    inode(next_inode, 1, 0, dirs["/BIN"], len(raw), next_sector, count, source.name)
    next_sector += count; next_inode += 1
image.write_bytes(data)
