#!/usr/bin/env python3
"""Headless integration test using a disposable FAT12 disk."""

import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import signal


def deadline(signum, frame):
    raise TimeoutError("Smoke test exceeded its 240 second deadline")


signal.signal(signal.SIGALRM, deadline)
signal.alarm(240)
started = time.monotonic()

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
    if "--light-theme" in sys.argv:
        subprocess.run(
            [
                "mcopy",
                "-o",
                "-i",
                str(disk),
                "system/themes/LIGHT.THM",
                "::/ETC/TUI.THM",
            ],
            check=True,
        )
    full_disk = "--full-disk" in sys.argv
    if full_disk:
        original = tmp / "SAFE.TXT"
        original.write_bytes(b"original-data\n")
        subprocess.run(
            ["mcopy", "-i", str(disk), str(original), "::/SAFE.TXT"], check=True
        )
        # Consume every available cluster while leaving directory slots free.
        boot = disk.read_bytes()[:512]
        cluster_bytes = boot[13] * 512
        reserve = tmp / "FILL.BIN"
        # Determine exact free space without first attempting an oversized copy.
        raw = disk.read_bytes()
        fat_offset = int.from_bytes(boot[14:16], "little") * 512
        fat_sectors = int.from_bytes(boot[22:24], "little")
        data_start = (
            int.from_bytes(boot[14:16], "little")
            + boot[16] * fat_sectors
            + (int.from_bytes(boot[17:19], "little") * 32 + 511) // 512
        )
        clusters = (len(raw) // 512 - data_start) // boot[13]
        free = 0
        for cluster in range(2, min(clusters + 2, 0xFF0)):
            offset = fat_offset + cluster + cluster // 2
            value = int.from_bytes(raw[offset : offset + 2], "little")
            value = value >> 4 if cluster & 1 else value & 0xFFF
            free += value == 0
        reserve.write_bytes(bytes(free * cluster_bytes))
        subprocess.run(
            ["mcopy", "-o", "-i", str(disk), str(reserve), "::/FILL.BIN"], check=True
        )
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
    private_floppy = tmp / "private-boot.img"
    private_floppy.write_bytes(floppy.read_bytes())
    floppy = private_floppy
    boot = (
        ["-cdrom", "diskimgs/KintsugiOS.iso", "-boot", "order=d"]
        if iso
        else [
            "-drive",
            f"file={floppy},format=raw,if=floppy,snapshot=on",
            "-boot",
            "order=a",
        ]
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
            until = time.monotonic() + 10
            while time.monotonic() < until:
                raw = stream.readline()
                if not raw:
                    raise RuntimeError(f"QEMU disconnected during {execute}")
                result = json.loads(raw)
                if "error" in result:
                    raise RuntimeError(result)
                if "return" in result:
                    return result["return"]
            raise TimeoutError(f"QMP timeout: {execute}")

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
            print(f"[smoke] {text}", flush=True)
            for ch in text:
                key = {
                    " ": "spc",
                    ".": "dot",
                    "-": "minus",
                    "/": "slash",
                    '"': "shift-apostrophe",
                    "'": "apostrophe",
                    "\\": "backslash",
                    "|": "shift-backslash",
                    ">": "shift-dot",
                    "&": "shift-7",
                    ":": "shift-semicolon",
                }.get(ch, ch)
                # Explicit transitions avoid deferred HMP release timers.
                keys = key.split("-")
                if ch.isupper():
                    keys = ["shift", ch.lower()]
                for down, codes in ((True, keys), (False, list(reversed(keys)))):
                    qmp(
                        "input-send-event",
                        {
                            "events": [
                                {
                                    "type": "key",
                                    "data": {
                                        "down": down,
                                        "key": {"type": "qcode", "data": code},
                                    },
                                }
                                for code in codes
                            ]
                        },
                    )
                    time.sleep(0.01)
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
        wait_for("[INIT] userspace init ready")
        wait_for("Keramika Shell - userspace")
        command("exit")
        command("echo shell-restarted-ok")
        wait_for("shell-restarted-ok")
        command("/bin/shell.elf")
        wait_for("Keramika Shell - userspace")
        command("help")
        wait_for("Keramika Shell Help")
        command("echo nested-shell-ok")
        wait_for("nested-shell-ok")
        command("exit")
        command("echo parent-shell-ok")
        wait_for("parent-shell-ok")
        # Backspace is cursor motion, never a printable VGA glyph.
        monitor("sendkey x 10")
        time.sleep(0.1)
        monitor("sendkey backspace 10")
        time.sleep(0.1)
        assert "\x08" not in screen(), "Backspace leaked into VGA text cells"
        command("echo background-ok | cat > /bg.txt &")
        wait_for("[job ")
        command("wait")
        command("cat /bg.txt")
        wait_for("background-ok")
        command("jobs")
        wait_for("PID GROUP STATE STATUS")
        command("echo 'literal&value'")
        wait_for("literal&value")
        command("missingcmd || echo fallback-ok")
        wait_for("fallback-ok")
        command("fdcheck")
        wait_for("FD checks: PASS")
        if full_disk:
            command("kuned /safe.txt")
            wait_for("Kintsugi Universal Editor")
            monitor("sendkey x 10")
            monitor("sendkey ctrl-s 10")
            wait_for("Save failed")
            monitor("sendkey ctrl-q 10")
            time.sleep(0.1)
            monitor("sendkey y 10")
            time.sleep(0.2)
            command("cat /safe.txt")
            wait_for("original-data")
            qmp("quit")
            proc.wait(timeout=10)
            assert (
                subprocess.check_output(["mtype", "-i", str(disk), "::/SAFE.TXT"])
                == b"original-data\n"
            )
            subprocess.run(["fsck.fat", "-n", str(disk)], check=True)
            print("PASS: ENOSPC preserves existing file and FAT consistency")
            sys.exit(0)
        command("echo pipeline-ok | cat > /pipe.txt")
        command("echo appended >> /pipe.txt")
        command("cat /pipe.txt")
        wait_for("pipeline-ok")
        wait_for("appended")
        command("cat /readme.txt | wc")
        wait_for("301")
        for _ in range(3):
            command("spin")
            wait_for("spin: Ctrl+C to stop")
            monitor("sendkey ctrl-c 10")
            time.sleep(0.2)
            command("status")
            wait_for("status=130")
        command("/readme.txt")
        wait_for("exec:")
        command("echo survived-invalid-elf")
        wait_for("survived-invalid-elf")
        command("help bin")
        wait_for("KDISKMAN.ELF")
        before_scroll = screen()
        monitor("sendkey x 10")
        time.sleep(0.1)
        assert screen()[: 24 * 80] == before_scroll[: 24 * 80], (
            "Typing moved shell output rows"
        )
        monitor("sendkey backspace 10")
        time.sleep(0.1)
        monitor("sendkey pgup 10")
        time.sleep(0.15)
        assert screen() != before_scroll, "Shell PageUp did not scroll output"
        monitor("sendkey pgdn 10")
        time.sleep(0.15)
        command("kfsck")
        wait_for("directories OK")
        command("proc")
        wait_for("PID PPID STATE TICKS")
        command("klog")
        wait_for("[BOOT] BIOS loader")
        command("grep boundary /readme.txt")
        wait_for("cluster boundary data")
        command("wc /readme.txt")
        command("tail /readme.txt")
        wait_for("cluster boundary data")
        for app, title in (
            ("kprocm", "Kintsugi Process Manager"),
            ("kdiskman", "Kintsugi Disk Manager"),
            ("ktui", "Kintsugi application launcher"),
        ):
            command(app)
            wait_for(title)
            if "--light-theme" in sys.argv:
                raw = (tmp / "vga.bin").read_bytes()
                assert raw[1] == 0x1F, "Shared light theme did not recolor title bar"
            if app == "kprocm":
                wait_for("KPROCM.ELF")
                monitor("sendkey pgdn 10")
                time.sleep(0.1)
                monitor("sendkey ret 10")
                wait_for("Process details")
                monitor("sendkey spc 10")
                time.sleep(0.1)
            if app == "ktui":
                monitor("sendkey ret 10")
                wait_for("Launch application")
                monitor("sendkey esc 10")
                time.sleep(0.1)
                monitor("sendkey 4 10")
                wait_for("Kintsugi Process Manager")
                monitor("sendkey q 10")
                wait_for("Kintsugi application launcher")
            monitor("sendkey q 10")
            time.sleep(0.2)
        command("kuned /edit.txt")
        wait_for("Kintsugi Universal Editor")
        command("editor-content")
        monitor("sendkey pgup 10")
        monitor("sendkey pgdn 10")
        time.sleep(0.1)
        monitor("sendkey ctrl-z 10")
        wait_for("Undo")
        monitor("sendkey ret 10")
        time.sleep(0.1)
        monitor("sendkey ctrl-f 10")
        wait_for("Find literal text")
        command("editor")
        wait_for("Found")
        monitor("sendkey ctrl-s 10")
        time.sleep(0.2)
        wait_for("Saved")
        monitor("sendkey ctrl-q 10")
        time.sleep(0.2)
        command("kuned /edit.txt --readonly")
        wait_for("Read-only viewer")
        monitor("sendkey x 10")
        monitor("sendkey ctrl-s 10")
        monitor("sendkey ctrl-q 10")
        time.sleep(0.2)
        command("vik /vik.txt")
        wait_for("-- NORMAL --")
        monitor("sendkey i 10")
        time.sleep(0.1)
        command("vik-content")
        monitor("sendkey esc 10")
        time.sleep(0.1)
        for key in ("g", "g", "y", "y", "p", "u", "ctrl-r", "u", "ctrl-q"):
            monitor(f"sendkey {key} 10")
            time.sleep(0.15)
        wait_for("-- NORMAL --")
        command(":wq")
        command("cat /vik.txt")
        wait_for("vik-content")
        command("ktte")
        wait_for("Kintsugi TUI Theme Editor")
        wait_for("Simple mode")
        monitor("sendkey ret 10")
        wait_for("Choose background color")
        wait_for("Bright green")
        before_choice = screen()
        marker = next(
            i for i, ch in enumerate(before_choice) if ch == ">" and i % 80 == 10
        )
        monitor("sendkey down 10")
        time.sleep(0.1)
        after_choice = screen()
        assert after_choice[marker] == " " and after_choice[marker + 80] == ">", (
            "Color selection did not move down"
        )
        monitor("sendkey up 10")
        time.sleep(0.1)
        assert screen()[marker] == ">", "Color selection did not move up"
        monitor("sendkey down 10")
        monitor("sendkey ret 10")
        time.sleep(0.1)
        monitor("sendkey s 10")
        wait_for("Save theme:")
        command("pick")
        wait_for("Theme saved")
        monitor("sendkey h 10")
        wait_for("VGA color guide")
        monitor("sendkey ret 10")
        time.sleep(0.1)
        monitor("sendkey n 10")
        time.sleep(0.1)
        monitor("sendkey m 10")
        monitor("sendkey right 10")
        monitor("sendkey s 10")
        wait_for("Save theme:")
        command("test")
        wait_for("Theme saved")
        monitor("sendkey ctrl-t 10")
        wait_for("Choose shared TUI theme")
        wait_for("TEST.THM")
        monitor("sendkey end 10")
        monitor("sendkey ret 10")
        time.sleep(0.1)
        monitor("sendkey q 10")
        time.sleep(0.2)
        command("kbsm")
        wait_for("Kintsugi Basic System Monitor")
        monitor("sendkey ctrl-t 10")
        wait_for("Choose shared TUI theme")
        monitor("sendkey ret 10")
        time.sleep(0.2)
        monitor("sendkey q 10")
        time.sleep(0.2)
        command("kc")
        wait_for("Kintsugi Commander")
        monitor("sendkey h 10")
        wait_for("Commander help")
        monitor("sendkey spc 10")
        time.sleep(0.1)
        monitor("sendkey q 10")
        time.sleep(0.2)
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
        assert (
            subprocess.check_output(["mtype", "-i", str(disk), "::/EDIT.TXT"])
            == b"editor-content\n"
        )
        assert actual == b"multi word text", actual
        assert (
            subprocess.check_output(["mtype", "-i", str(disk), "::/VIK.TXT"])
            == b"vik-content\n"
        )
        theme = subprocess.check_output(
            ["mtype", "-i", str(disk), "::/ETC/THEMES/TEST.THM"]
        )
        picked = subprocess.check_output(
            ["mtype", "-i", str(disk), "::/ETC/THEMES/PICK.THM"]
        )
        expected_background = b"72=80\n" if "--light-theme" in sys.argv else b"72=27\n"
        assert expected_background in picked, (
            "Enter did not accept the selected background color"
        )
        assert theme.startswith(b"00=01\n01=01\n") and len(theme) == 1536
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
            f"({time.monotonic() - started:.1f}s)",
        )
    finally:
        if sys.exc_info()[0] not in (None, SystemExit):
            print(
                "Serial diagnostics:\n" + (tmp / "serial.log").read_text()[-8000:],
                file=sys.stderr,
            )
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=10)
