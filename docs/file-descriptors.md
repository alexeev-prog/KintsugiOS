# File descriptors

Implemented on branch `feature/process-io-init`. This describes the current code,
not a future roadmap. The previous requirements-plan file has been removed.

Each process has 16 descriptors. Root programs start with terminal input at 0
and terminal output at 1 and 2. Spawn inherits all open descriptors. Dup2 and
inheritance reference the same open description, sharing regular-file offsets.
Descriptors close at process exit (before wait); session cleanup also closes
descriptors for surviving descendants. Reference counts own file descriptions
and pipe endpoints/buffers.

## ABI and runtime

`src/user/ioabi.h` defines syscall numbers/flags; `runtime.h` exposes wrappers.
Existing syscall numbers and path-based interfaces remain unchanged.

| Number | Operation | Arguments |
| --- | --- | --- |
| 20 | open | path, flags |
| 21 | close | fd |
| 22 | read | fd, buffer, size |
| 23 | write | fd, buffer, size |
| 24 | dup2 | source fd, destination fd |
| 25 | pipe | writable pointer to two ints |
| 26 | seek | fd, absolute offset |

Read/write are bounded to 4096 bytes per call and may return partial counts.
Read returns 0 for EOF. Open accepts READ=1, WRITE=2, APPEND=4; it opens existing
files only, without create/truncate. Append uses the existing VFS append operation.
Zero-length valid reads/writes return immediately. Seek on terminal/pipe fails.

Additional errors: -13 bad descriptor/access mode, -14 descriptor table full,
-15 broken pipe, -16 non-seekable object. -17 is an internal retry condition.
The current syscall dispatcher retries empty/full pipes and terminal input at
explicit scheduling points; dedicated event wait queues are not implemented.

Pipes have a 4096-byte bounded ring. Empty reads wait while writers exist; closing
the final writer produces EOF after buffered bytes. Closing the final reader
makes writes fail with broken pipe. Closing a duplicate only releases its own
reference. Full writes wait; other writes may be partial. No POSIX PIPE_BUF
atomicity contract is provided yet.

## Validation and current limitations

`fdcheck` verifies pipe transfer, EOF, broken pipe, bad descriptors and shared
file offsets. The QEMU smoke suite runs it before existing process/TUI/FAT tests.
The default smoke suite passed after these changes.

This is the FD foundation, not completion of the full process/I/O/init request:
the kernel shell does not yet parse pipelines/redirections, init has not moved
to userspace, configuration and terminal process-group ownership are not added.
New terminal FD reads currently deliver individual byte events, not canonical
line editing; legacy input/TUI syscalls retain their existing semantics. Existing
applications still use legacy console operations, so dup2 does not redirect those
operations. Cwd remains global. File handles remain path-based, without Unix
open-unlink/rename identity semantics. Do not rely on concurrent rename/replacement
of an open file. These constraints must not be presented as POSIX behavior.
