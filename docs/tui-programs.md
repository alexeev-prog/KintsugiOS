# TUI and command-line applications

Build with `make`; `make run` installs the ELF programs into `/BIN` without
formatting an existing filesystem. Use `help bin` for the installed program
list. Launch with a lowercase command name or an explicit `/BIN/NAME.ELF` path.

## Shared TUI library

Shared file themes, PageUp/PageDown, the Vim-like `vik` editor and the extended
process snapshot ABI are documented in [THEMES_RU.md](THEMES_RU.md).

`src/user/ktuilib.h` implements an 80x25 cell buffer, clipped text, filled regions,
boxes, modal prompts/confirmation/messages, gauges and small settings files.
Rendering is submitted through one presentation syscall. Application settings
are separate fixed-size files under `/ETC`; missing files retain defaults.
Paths and names follow the current FAT12 8.3 constraints. These are minimal
ASCII interfaces, not Unicode terminals. The window layer provides framed,
focused panels, client-area clipping, shadows, four nested saved-background
modals, keyboard menus, scrollbars and shared list navigation. This is not an
interprocess compositor and does not support mouse dragging or resizing.

## Applications

### KC — Kintsugi Commander

Two independent panels, inspired by Midnight Commander. Each panel has up to
four tabs. Each directory view is bounded to 256 entries, sorted by name, with
optional directories-first sorting. A full view may omit later entries.

| Key | Action |
| --- | --- |
| Tab | Switch panel |
| T / X | Create / close current tab |
| [ / ] | Previous / next tab |
| Arrows | Select entry |
| Enter | Open directory, or open file in KUNED |
| Backspace | Parent directory |
| B | Switch to previous directory (per tab) |
| / | Open an absolute directory path |
| V / R / I | Read-only preview / rename / attributes |
| Home / End | First / last entry |
| C / M | Copy / move to the other panel, with confirmation |
| D / N | Delete / create directory |
| O | Settings menu: sizes, directories first, save |
| H | Help |
| Q / Esc | Quit |

Settings are stored in `/ETC/KC.CFG`. Copy does not recursively copy directories
The synthetic `..` entry is pinned above sorted entries outside root and can
only be opened, never deleted, copied or moved. Enter and Backspace both navigate
to the parent; B switches between the two most recently visited directories.
and does not overwrite existing targets. Directory deletion requires emptiness.
Tabs are session-local. Key commands above use lowercase letters.

### KUNED — editor

`kuned [file]` edits files up to 64 KiB. Line numbers count logical lines;
wrapped continuations have no new line number. Ctrl+S saves, Ctrl+Q quits with
unsaved-change confirmation, Ctrl+F searches literal text after the cursor.
Ctrl+O opens an arrow-key settings menu: line numbers, theme, save to
`/ETC/KUNED.CFG`. Reopen the settings dialog for each action. Arrow/Home/End,
Backspace and Delete edit the buffer. Search wraps; Ctrl+N repeats the search.
Ctrl+Z restores one of eight edit snapshots; Ctrl+R replaces one match with
nonempty text; Ctrl+G goes to a logical line; Ctrl+A saves as (only changing the
path after success). Tab inserts four spaces. Ctrl+E opens help. Basic lexical
colors distinguish strings, numbers, brackets and semicolon comments, not full
language grammars. `kuned FILE --readonly` disables editing and saving.

### Monitors and launcher

- `kbsm`: live physical memory/heap counters, allocation gauge, process count.
- `kprocm`: read-only live PID, parent, state, tick and heap-allocation table.
  It currently does not kill processes or report CPU load percentages.
- `kdiskman`: read-only information for the mounted primary ATA FAT12 volume,
  allocation gauge, C to run the filesystem checker. No partition editor,
  formatting, mount management or multi-device discovery is provided.
- `ktui`: standalone TUI launcher. Keys 1–5 launch the above applications through
  spawn/wait; Enter opens an arrow-key menu; H shows a shared-library dialog.
  Q/Esc exits all monitors. Space pauses refresh; D toggles memory accounting
  details in kbsm. Kprocm supports arrow/Home/End row selection.

## Utilities

| Command | Semantics |
| --- | --- |
| `cat FILE` | Existing streaming file reader |
| `tree [DIR]` | Recursive indented listing, default `/` |
| `find [DIR] [SUBSTRING]` | Recursive, case-sensitive filename substring search |
| `grep LITERAL FILE` | Case-sensitive literal line matching, no regex |
| `tail FILE` | Last ten lines |
| `wc FILE` | Newlines, whitespace-delimited words, bytes |
| `sort FILE` | Bytewise ascending line sort |
| `proc` | Read-only process table |
| `klog` | Current RAM journal |
| `kfsck` | Read-only allocation/directory consistency check |

`grep`, `tail`, `wc` and `sort` accept one file up to 64 KiB. Grep/sort have a
4096-line limit. Tree/find bound recursion to 32 directory levels and reject
overlong paths. There are no pipes, stdin filters, regex or POSIX option sets.

## Diagnostics and safety

Shared fixed-width structures are in `src/user/sysabi.h`. Syscalls 16–19 expose
process snapshots, bounded journal reads, FAT12 checking and volume statistics.
All user buffers are checked by the kernel.

The kernel journal is a 16 KiB RAM ring, also written to COM1. It is lost on
reboot. The BIOS loader hands off a success marker; the kernel records completion
of loading/E820/protected-mode setup. Failures before kernel entry are not
recoverable through klog. This is not a filesystem transaction journal.

Kfsck reuses the driver's allocation checker: chain lengths, cluster ownership,
cross-links, orphan allocations, directory relationships and file-size checks.
It operates on the mounted volume's in-memory FAT and reads directory sectors;
it does not independently reread/compare on-disk FAT copies, repair corruption,
or inspect a filesystem rejected during mount. A failure returns a nonzero
status and does not modify the filesystem.

## Validation

`make all diskimg` builds kernel and applications. `python3 tools/smoke_test.py`
uses disposable disks and QEMU to exercise help discovery, diagnostic utilities,
new TUI launch/exit, nested launcher spawn/wait, editor save, filesystem operations,
reboot persistence and host-side `fsck.fat`. Tab interactions, settings variants
and all utility edge cases still require additional targeted tests.
`make test-tui` checks clipping, modal stack restoration, list navigation and
scrollbars on the host. Smoke tests print each command and enforce QMP and
whole-scenario deadlines, with serial diagnostics on failure. The full target
runs nine scenarios and normally takes several minutes.
