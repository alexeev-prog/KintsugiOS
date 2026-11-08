# Reliability contract (current i386 kernel)

The current source supports one foreground ring-3 program, not spawn/wait or a
multi-process scheduler. Do not install leftover ELF files from other revisions:
the installer obtains the supported list from `make list-user-programs`.

## Isolation and shutdown

User pages are separate from supervisor mappings. Syscalls validate ranges and
write permissions before dereferencing user buffers. ELF loading checks headers,
file/virtual bounds, segment alignment, executable entry, aggregate memory limit,
and rejects overlapping load pages and dynamic executables.

User exceptions terminate the program with `128 + vector` (page fault: 142).
NMI, double fault and machine check remain fatal kernel events. The fault report
includes vector, error, EIP, CS and CR2 in the journal. Ordinary user faults do
not enter the kernel panic path.

Ctrl+C is captured by the keyboard IRQ and sets a cancellation flag. Return from
a user IRQ, syscall entry and blocking keyboard waits check it. A program doing
no syscalls can therefore be stopped (status 130). Cancellation is deferred
inside filesystem/kernel work, avoiding abandonment of in-progress mutations.
This cannot recover from a kernel infinite loop with interrupts disabled.

Normal exit, fault and cancellation return through the same kernel continuation:
switch to the kernel page directory, restore/free the terminal snapshot, destroy
user page tables/pages and free the ELF image. User heap pages are owned by that
address space. This is not yet a proof against every resource leak under OOM.

## Filesystem safety

Mount checks BPB geometry, FAT bounds/copies, chains, ownership, directory
relationships and allocation consistency. Invalid/unavailable volumes are not
mounted; the shell remains usable. Replacement reserves a new chain before
publishing it, keeping the old chain intact when allocation returns ENOSPC.
This requires enough space for both versions, even when replacing a large file
with a smaller one.

There is NO power-loss transaction guarantee or write-ahead journal. An ATA I/O
failure during FAT/directory publication can leave an inconsistent volume.
Do not interpret the ENOSPC guarantee as atomicity against power loss, controller
failure or arbitrary external disk modification. Automatic repair is not done.

## Logs and errors

`klog` reads a 16 KiB RAM ring (oldest bytes overwritten); COM1 receives the same
messages. Logs do not persist across reboot. Kernel panic prints registers to
COM1/VGA and halts. User reports record exception details and exit status.
Negative filesystem errors and their messages are centralized in `vfs_error`:

| Value | Meaning |
| --- | --- |
| -1 | Not found |
| -2 | I/O error |
| -3 | Invalid argument/path/ELF |
| -4 | No space |
| -5 | Already exists |
| -6 / -7 | Not a directory / Is a directory |
| -8 | Directory not empty |
| -9 | Read only |
| -10 | Corrupt filesystem |
| -11 | Out of memory |
| -12 | Busy |

## ABI

ELF32 little-endian i386 ET_EXEC, static linking, entry in executable PT_LOAD.
User address range: `[0x40000000, 0x80000000)`. Heap starts at `0x60000000`,
limited to 16 MiB. Stack: eight pages with a guard page below it. Initial stack
contains 32-bit argc and argv pointer; argv is NUL-terminated. At most 32 arguments.

`int 0x80`: EAX number/result; EBX, ECX, EDX, ESI arguments in that order.
Other registers are restored. Negative results are errors unless stated otherwise.

| Number | Arguments | Result |
| --- | --- | --- |
| 0 | status | exit, low 8 bits |
| 1 | buffer, length | bytes written, max 4096 |
| 2 | path, offset, buffer, capacity | bytes read, max 4096 |
| 3 | path, cursor pointer, dirent pointer | 1 entry / 0 end |
| 4 | buffer, capacity | getcwd, 0 success |
| 5 | bytes | grow heap, pointer or 0 on failure |
| 6 | buffer, capacity | canonical input, max 4096, 0 EOF |
| 7 | 2000-cell buffer, cursor x, cursor y | present TUI |
| 8 | mode: 0 poll / 1 wait / 2 one-tick wait | keyboard event |
| 9 | stats pointer | memory snapshot |
| 10 | path, data, size | replace file, max 65536 |
| 11 | path, operation, optional stat pointer | 0 mkdir, 1 unlink, 2 rmdir, 3 chdir, 4 stat |
| 12 | source, destination, move flag | copy/rename |
| 17 | offset, buffer, capacity | journal bytes, max 4096 |

13–16 are not implemented. Shared filesystem layouts are defined once in
`src/user/fsabi.h`; TUI cells/stats in `src/user/tuiabi.h`. Syscall and allocator
mechanics live in `runtime.c`, not in applications. ABI is experimental, without
binary version negotiation; rebuild applications with the kernel.

## Automated verification

`make test` uses disposable images, never persistent user disks. Tests cover
floppy/ISO/large boot reads, repeated cancellation of syscall-free loops,
invalid ELF rejection, user faults and subsequent shell use, syscall range
checks, allocator checks, invalid BPB, cyclic FAT, absent disk, kernel panic
reports, full-disk replacement preserving original bytes, reboot persistence and
host-side `fsck.fat -n`. They are regression tests, not exhaustive fuzzing,
I/O-failure injection or a verified security boundary.
