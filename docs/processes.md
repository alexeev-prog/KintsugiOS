# Processes

The kernel supports up to 16 user processes. Each owns an address space,
page-backed heap, user stack, kernel stack, saved execution context and optional
terminal snapshot. PIDs increase across launches. States are unused, ready,
running, waiting and zombie. Exit status remains available until wait/reaping.

Userspace API in `src/user/runtime.h`:

```c
char* args[] = { "ECHO", "hello", 0 };
int pid = spawn("/BIN/ECHO.ELF", 2, args);
if (pid > 0) {
    int status = wait(pid);
    /* status is the child's exit code, not a POSIX encoded wait status. */
}
```

Syscalls 13, 14 and 15 implement spawn, wait and getpid. Spawn copies arguments
before loading a separate ELF address space; it does not clone the parent.
Wait accepts only a direct child PID. Arguments are bounded to 32 strings, each
smaller than VFS_PATH_MAX. Errors are negative VFS error codes.

Scheduling is round robin, with a five-tick quantum (approximately 100 ms at
50 Hz). Timer preemption happens only when interrupting userspace; kernel
operations are not preempted. Keyboard input waits offer explicit scheduling
points. CPU tick accounting is currently approximate.

Ctrl+C marks the foreground process for termination with status 130. Interrupt
return from userspace, syscall entry and blocking keyboard reads check this
request. Consequently a userspace loop without syscalls remains interruptible.
Wait temporarily transfers foreground ownership to the child.

Current boundaries: shell execution remains synchronous; returning from the
root program terminates and reaps its remaining descendants. Console, keyboard
and filesystem working directory are shared, not virtualized per process.
There are no process groups, background shell jobs, file descriptor tables or
public process-list syscall yet. Terminal snapshots are restored on reaping.
This is a scheduling/process foundation, not POSIX process semantics.

Manual emulator checks for this subsystem:

1. Run a program containing `for (;;) {}` and press Ctrl+C: shell must return.
2. Spawn two CPU-bound children before waiting: both must make progress.
3. Wait for a child returning 42: wait must return 42; a second wait must fail.
4. Fault a child: parent must survive and receive `128 + exception vector`.
5. Repeat spawn/wait and verify memory is reclaimed; test missing/invalid ELF.
