# Init and background commands

The kernel boots `/BIN/INIT.ELF` as PID 1. The installer includes this
executable automatically. If it cannot be loaded, or exits, the kernel
falls back to the rescue shell. A missing filesystem also uses the rescue
shell directly.

Init runs in ring 3, spawns `/BIN/SHELL.ELF`, waits for it and restarts it
after exit. The normal command interpreter, parser, history, line editor and
filesystem completion run in userspace. The kernel shell is retained only as
a rescue fallback. This is not yet a configurable service manager.

Syscall 32 performs session cleanup; 33 accepts a bounded pipeline launch
description (all user strings are copied and checked); 34 exposes legacy
builtin operations and job queries to the registered shell. Legacy diagnostic
and filesystem builtins still execute in the kernel. Init adopts orphaned
children and the session reaps completed children automatically.

## Background execution

Terminate an external command or pipeline with `&`:

```text
echo hello > /hello.txt &
echo pipeline | cat > /pipe.txt &
jobs
wait
cat /pipe.txt
```

The shell returns immediately after a successful background launch and prints
the process group ID. Each pipeline has its own process group. Foreground
Ctrl+C does not interrupt other groups. Background terminal input and fullscreen
presentation are rejected; redirect output to avoid interleaving it with the
prompt.

`jobs` lists children still present in the process table. Completed jobs are
automatically removed, so this is not a persistent completion history. `wait`
waits for all currently unreaped children and returns the status of the last
one visited (or zero when there are none). It does not move jobs to the
foreground.

Quoted or escaped ampersands are literal text. Only a trailing `&` on a
single command or pipeline is supported; command lists such as `a & b`,
`&&`, and `a || b &` are rejected. Kernel-only builtins cannot run in the
background. There is no `fg`, `bg`, stopped-job handling, or signal command yet.
