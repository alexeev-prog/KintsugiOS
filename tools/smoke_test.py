#!/usr/bin/env python3
"""Headless integration test using a disposable FAT12 disk."""
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

with tempfile.TemporaryDirectory(prefix='kintsugi-', dir='/tmp/opencode') as directory:
    tmp = Path(directory)
    disk = tmp / 'fat.img'
    subprocess.run(['truncate', '-s', '8M', str(disk)], check=True)
    subprocess.run(['mkfs.fat', '-F', '12', str(disk)], check=True)
    payload = b'KINTSUGI_READ_OK\n' + b'cluster boundary data\n' * 300
    sample = tmp / 'README.TXT'
    sample.write_bytes(payload)
    subprocess.run(['mcopy', '-i', str(disk), str(sample), '::/README.TXT'], check=True)
    invalid = '--invalid-fs' in sys.argv
    no_disk = '--no-disk' in sys.argv
    if invalid:
        with disk.open('r+b') as image:
            image.seek(13)  # illegal zero sectors-per-cluster
            image.write(b'\0')
    iso = '--iso' in sys.argv
    floppy = Path('diskimgs/kintsugi_floppy_i386.img')
    if '--large-kernel' in sys.argv:
        # Force reads across tracks, cylinders and the 64 KiB DMA boundary.
        (tmp / 'kernel_size.inc').write_text('%define KERNEL_SECTORS 200\n')
        subprocess.run(['nasm', '-I'+str(tmp)+'/', '-f', 'bin',
                        'src/bootloader/bootsector.asm', '-o', str(tmp / 'boot.bin')], check=True)
        contents = (tmp / 'boot.bin').read_bytes() + Path('bin/kernel.bin').read_bytes()
        floppy = tmp / 'boot.img'
        floppy.write_bytes(contents + bytes(1474560-len(contents)))
    boot = ['-cdrom', 'diskimgs/KintsugiOS.iso', '-boot', 'order=d'] if iso else [
        '-drive', f'file={floppy},format=raw,if=floppy', '-boot', 'order=a']
    drives = [] if no_disk else ['-drive', f'file={disk},format=raw,if=ide,index=0']
    proc = subprocess.Popen(['qemu-system-i386', '-m', '64', '-display', 'none',
                             '-qmp', f'unix:{tmp}/qmp,server=on,wait=off',
                             *drives, '-no-reboot', *boot])
    try:
        sock = socket.socket(socket.AF_UNIX)
        for _ in range(100):
            try:
                sock.connect(str(tmp / 'qmp'))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                time.sleep(.05)
        sock.settimeout(10)
        stream = sock.makefile('rwb', buffering=0)
        json.loads(stream.readline())

        def qmp(execute, arguments=None):
            stream.write((json.dumps({'execute': execute, 'arguments': arguments or {}})+'\n').encode())
            while True:
                result = json.loads(stream.readline())
                if 'error' in result:
                    raise RuntimeError(result)
                if 'return' in result:
                    return result['return']

        qmp('qmp_capabilities')

        def monitor(command):
            return qmp('human-monitor-command', {'command-line': command})

        def screen():
            path = tmp / 'vga.bin'
            monitor(f'pmemsave 0xb8000 4000 "{path}"')
            return path.read_bytes()[::2].decode('ascii', errors='replace')

        def wait_for(text):
            for _ in range(100):
                if text in screen():
                    return
                time.sleep(.1)
            raise AssertionError(f'Missing {text!r}; VGA:\n{screen()}')

        def command(text):
            for ch in text:
                key = {' ': 'spc', '.': 'dot', '-': 'minus'}.get(ch, ch)
                monitor(f'sendkey {key} 10')
                time.sleep(.025)
            monitor('sendkey ret 10')
            time.sleep(.3)

        wait_for('Keramika Shell')
        if invalid or no_disk:
            command('ls')
            wait_for('FAT12 not mounted')
            command('help')
            wait_for('Keramika Shell Help')
            qmp('quit')
            proc.wait(timeout=10)
            print('PASS:', 'missing disk' if no_disk else 'invalid BPB', 'handled without crash')
            sys.exit(0)
        command('fat12info')
        command('ls')
        command('cat readme.txt')
        wait_for('cluster boundary data')
        command('ls')
        wait_for(str(len(payload)))
        command('write check.txt persistent-data')
        command('cat check.txt')
        wait_for('persistent-data')
        command('write check.txt shorter')
        command('cat check.txt')
        wait_for('shorter')
        command('write check.txt multi word text')
        command('cat check.txt')
        wait_for('multi word text')
        command('create empty.txt')
        command('del empty.txt')
        qmp('quit')
        proc.wait(timeout=10)
        actual = subprocess.check_output(['mtype', '-i', str(disk), '::/CHECK.TXT'])
        assert actual == b'multi word text', actual
        listing = subprocess.check_output(['mdir', '-i', str(disk), '::/'])
        assert b'EMPTY' not in listing
        assert subprocess.check_output(['mtype', '-i', str(disk), '::/README.TXT']) == payload
        subprocess.run(['fsck.fat', '-n', str(disk)], check=True)
        print('PASS:', 'ISO' if iso else 'floppy', 'boot, shell, FAT12 read/write/overwrite/delete, fsck')
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=10)
