#!/usr/bin/env python3
"""Headless integration test using a disposable FAT12 disk."""

import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

Path("/tmp/opencode").mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="kintsugi-", dir="/tmp/opencode") as directory:
    tmp = Path(directory)
    disk = tmp / "fat.img"
    subprocess.run(["truncate", "-s", "8M", str(disk)], check=True)
    subprocess.run(["mkfs.fat", "-F", "12", str(disk)], check=True)
    payload = b"KINTSUGI_READ_OK\n" + b"cluster boundary data\n" * 300
    sample = tmp / "README.TXT"
    sample.write_bytes(payload)
    subprocess.run(["mcopy", "-i", str(disk), str(sample), "::/README.TXT"], check=True)
    subprocess.run([sys.executable, "tools/install_system.py", str(disk)], check=True)
    invalid = "--invalid-fs" in sys.argv
    corrupt = "--corrupt-fat" in sys.argv
    if corrupt:
        with disk.open("r+b") as image:
            boot = image.read(512)
            reserved = int.from_bytes(boot[14:16], "little")
            fat_sectors = int.from_bytes(boot[22:24], "little")
            # Cluster 2 points to itself in every FAT copy.
            for copy in range(boot[16]):
                image.seek((reserved + copy * fat_sectors) * 512 + 3)
                pair = int.from_bytes(image.read(2), "little")
                image.seek((reserved + copy * fat_sectors) * 512 + 3)
                image.write(((pair & 0xF000) | 2).to_bytes(2, "little"))
    no_disk = "--no-disk" in sys.argv
    if invalid:
        with disk.open("r+b") as image:
            image.seek(13)  # illegal zero sectors-per-cluster
            image.write(b"\0")
    iso = "--iso" in sys.argv
    floppy = Path("diskimgs/kintsugi_floppy_i386.img")
    if "--large-kernel" in sys.argv:
        # Force reads across tracks, cylinders and the 64 KiB DMA boundary.
        (tmp / "kernel_size.inc").write_text("%define KERNEL_SECTORS 200\n")
        subprocess.run(
            [
                "nasm",
                "-I" + str(tmp) + "/",
                "-f",
                "bin",
                "src/bootloader/bootsector.asm",
                "-o",
                str(tmp / "boot.bin"),
            ],
            check=True,
        )
        contents = (tmp / "boot.bin").read_bytes() + Path("bin/kernel.bin").read_bytes()
        floppy = tmp / "boot.img"
        floppy.write_bytes(contents + bytes(1474560 - len(contents)))
    boot = (
        ["-cdrom", "diskimgs/KintsugiOS.iso", "-boot", "order=d"]
        if iso
        else ["-drive", f"file={floppy},format=raw,if=floppy", "-boot", "order=a"]
    )
    drives = [] if no_disk else ["-drive", f"file={disk},format=raw,if=ide,index=0"]
    memory = next(
        (x.split("=", 1)[1] for x in sys.argv if x.startswith("--memory=")), "64"
    )
    proc = subprocess.Popen(
        [
            "qemu-system-i386",
            "-m",
            memory,
            "-display",
            "none",
            "-serial",
            f"file:{tmp}/serial.log",
            "-qmp",
            f"unix:{tmp}/qmp,server=on,wait=off",
            *drives,
            *boot,
        ]
    )
    try:
        sock = socket.socket(socket.AF_UNIX)
        for _ in range(100):
            try:
                sock.connect(str(tmp / "qmp"))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                time.sleep(0.05)
        sock.settimeout(10)
        stream = sock.makefile("rwb", buffering=0)
        json.loads(stream.readline())

        def qmp(execute, arguments=None):
            stream.write(
                (
                    json.dumps({"execute": execute, "arguments": arguments or {}})
                    + "\n"
                ).encode()
            )
            while True:
                result = json.loads(stream.readline())
                if "error" in result:
                    raise RuntimeError(result)
                if "return" in result:
                    return result["return"]

        qmp("qmp_capabilities")

        def monitor(command):
            return qmp("human-monitor-command", {"command-line": command})

        def screen():
            path = tmp / "vga.bin"
            monitor(f'pmemsave 0xb8000 4000 "{path}"')
            return path.read_bytes()[::2].decode("ascii", errors="replace")

        def wait_for(text):
            for _ in range(100):
                if text in screen():
                    return
                time.sleep(0.1)
            raise AssertionError(f"Missing {text!r}; VGA:\n{screen()}")

        def command(text):
            for ch in text:
                key = {
                    " ": "spc",
                    ".": "dot",
                    "-": "minus",
                    "/": "slash",
                    '"': "shift-apostrophe",
                    "'": "apostrophe",
                    "\\": "backslash",
                }.get(ch, ch)
                monitor(f"sendkey {key} 10")
                time.sleep(0.025)
            monitor("sendkey ret 10")
            time.sleep(0.3)

        wait_for("Keramika Shell")
        for _ in range(100):
            if "[INFO] shell ready" in (tmp / "serial.log").read_text():
                break
            time.sleep(0.01)
        assert "[INFO] shell ready" in (tmp / "serial.log").read_text()
        assert "[INFO] E820 PMM VMM paging ready" in (tmp / "serial.log").read_text()
        command("memtest")
        wait_for("Memory self-test: PASS")
        command("meminfo")
        wait_for("Paging: enabled")
        if "--panic" in sys.argv or "--fault" in sys.argv:
            fault = "--fault" in sys.argv
            command("fault confirm" if fault else "panic confirm")
            wait_for("PANIC:")
            serial = (tmp / "serial.log").read_text()
            assert "PANIC:" in serial
            if fault:
                assert (
                    "Invalid Opcode" in serial and "eip=" in serial and "eax=" in serial
                )
            qmp("quit")
            proc.wait(timeout=10)
            print(
                "PASS: unified panic",
                "exception registers" if fault else "explicit request",
            )
            sys.exit(0)
        if invalid or no_disk or corrupt:
            command("ls")
            wait_for("filesystem unavailable")
            command("help")
            wait_for("Keramika Shell Help")
            qmp("quit")
            proc.wait(timeout=10)
            print(
                "PASS:",
                "missing disk"
                if no_disk
                else "corrupt FAT"
                if corrupt
                else "invalid BPB",
                "handled without crash",
            )
            sys.exit(0)
        command("fat12info")
        command("/bin/check.elf")
        wait_for("User syscall checks: PASS")
        wait_for("User malloc checks: PASS")
        command("./BIN/ECHO.ELF path-exec-ok")
        wait_for("path-exec-ok")
        command("/bin/input.elf")
        wait_for("stdin> ")
        command("hello-input")
        wait_for("input: hello-input")
        command("/bin/fault.elf")
        wait_for("User fault: vector=14")
        command("status")
        wait_for("status=142")
        command("echo survived-user-fault")
        wait_for("survived-user-fault")
        command("fstest confirm")
        wait_for("Filesystem self-test: PASS")
        command("ls")
        command("cat readme.txt")
        wait_for("cluster boundary data")
        command("ls")
        wait_for(str(len(payload)))
        command("write check.txt persistent-data")
        command("cat check.txt")
        wait_for("persistent-data")
        command("write check.txt shorter")
        command("cat check.txt")
        wait_for("shorter")
        command("write check.txt multi word text")
        command("cat check.txt")
        wait_for("multi word text")
        command('write quote.txt "quoted text" escaped\\ space')
        command("cat /quote.txt")
        wait_for("quoted text escaped space")
        command("help fs")
        wait_for("Replace file contents")
        command("not-a-command")
        command("status")
        wait_for("status=127")
        command('echo "unfinished')
        command("status")
        wait_for("status=2")
        # Clear old output so a previous cat cannot hide a completion failure.
        command("clear")
        # Command and root-path completion, editing, history and Delete.
        for key in ["c", "a", "tab", "spc", "slash", "q", "u", "tab", "ret"]:
            monitor(f"sendkey {key} 10")
            time.sleep(0.04)
        wait_for("quoted text escaped space")
        command("echo history-marker")
        monitor("sendkey up 10")
        time.sleep(0.1)
        monitor("sendkey ret 10")
        time.sleep(0.1)
        wait_for("history-marker")
        command("echo edit-marker")
        for key in [
            "up",
            "home",
            "right",
            "right",
            "right",
            "right",
            "right",
            "delete",
            "end",
            "ret",
        ]:
            monitor(f"sendkey {key} 10")
            time.sleep(0.04)
        wait_for("dit-marker")
        command("create empty.txt")
        command("del empty.txt")
        command("mkdir docs")
        command("mkdir docs/sub")
        command("cd docs/sub")
        command("write note.txt nested-data")
        command("append note.txt -tail")
        command("cp note.txt copy.txt")
        command("mv copy.txt ../moved.txt")
        command("truncate note.txt 6")
        command("cd /")
        command("clear")
        command("cat /docs/moved.txt")
        wait_for("nested-data-tail")
        command("rmdir docs")
        wait_for("Directory not empty")
        command("mv docs docs/sub/loop")
        wait_for("Invalid path")
        command("mkdir gone")
        command("rmdir gone")
        # A full machine reset must not lose acknowledged writes.
        ready_count = (tmp / "serial.log").read_text().count("[INFO] shell ready")
        qmp("system_reset")
        for _ in range(150):
            if (tmp / "serial.log").read_text().count(
                "[INFO] shell ready"
            ) > ready_count:
                break
            time.sleep(0.1)
        else:
            raise AssertionError("Kernel did not restart")
        command("cat /check.txt")
        wait_for("multi word text")
        qmp("quit")
        proc.wait(timeout=10)
        actual = subprocess.check_output(["mtype", "-i", str(disk), "::/CHECK.TXT"])
        assert actual == b"multi word text", actual
        assert (
            subprocess.check_output(["mtype", "-i", str(disk), "::/QUOTE.TXT"])
            == b"quoted text escaped space"
        )
        listing = subprocess.check_output(["mdir", "-i", str(disk), "::/"])
        assert b"EMPTY" not in listing
        assert (
            subprocess.check_output(["mtype", "-i", str(disk), "::/README.TXT"])
            == payload
        )
        assert (
            subprocess.check_output(["mtype", "-i", str(disk), "::/DOCS/MOVED.TXT"])
            == b"nested-data-tail"
        )
        assert (
            subprocess.check_output(["mtype", "-i", str(disk), "::/DOCS/SUB/NOTE.TXT"])
            == b"nested"
        )
        subprocess.run(["fsck.fat", "-n", str(disk)], check=True)
        print(
            "PASS:",
            "ISO" if iso else "floppy",
            "boot, editor/parser/completion, FAT12, reboot persistence, fsck",
        )
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=10)
