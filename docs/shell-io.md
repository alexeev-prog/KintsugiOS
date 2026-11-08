# Shell streams and process groups

The kernel shell supports external-command pipelines with up to eight stages,
final stdout replacement (`>`) and append (`>>`), and conditional fallback (`||`).
Quotes and backslash escapes protect operator characters. Builtins are not run
inside pipelines. Examples:

```
echo hello | cat > /hello.txt
echo again >> /hello.txt
cat /hello.txt | grep hello | wc
missing-command || echo failed
```

All pipeline ELFs are loaded before any stage executes. Pipe ends unused by each
child are closed; the shell closes its temporary ends. Stages execute concurrently,
including transfers exceeding pipe capacity. The last stage determines status.
Redirecting with `>` intentionally truncates the target after executable loading;
output is not transactional and can remain partial after execution fails.

Pipeline members share a foreground group. Ctrl+C marks every group member,
including blocked readers/writers. Terminal FD reads and raw TUI presentation/input
require foreground membership. Spawn inherits its parent's group.

Pipe waits are keyed by pipe object: reading, writing and endpoint closure wake
matching blocked processes. Terminal FD waits currently wake on timer interrupts.
This is not POSIX job control or a complete terminal line discipline.

`write_all` centralizes partial writes. Cat reads stdin with no filename. Grep,
tail, wc and sort accept optional filenames, otherwise stdin (64 KiB input limit).
Their ordinary output uses stdout descriptors; legacy direct console calls remain.

Regression tests exercise `echo | cat > file`, append, and `cat large-file | wc`
with input exceeding the 4096-byte pipe buffer, in addition to FD lifetime tests.

Not implemented: background `&`, userspace PID 1/init configuration, userspace
shell migration, per-process cwd, complete terminal modes, input/stderr redirection.
Unsupported background/input operators return syntax errors rather than silently
executing a different command. These omissions mean the broader init/job-control
request is not complete.
