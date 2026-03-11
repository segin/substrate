# ELKS Syscall Reference

## 1. Scope

This file is the implementation-status reference for the in-tree ELKS
personality. It complements `elks_spec.md` and `elks_syscalls.md` by recording
what is wired today and what remains partial or unsupported.

Status meanings:

- `Working`: implemented and covered by current unit or QEMU smoke tests
- `Partial`: implemented at the personality edge, but with known ABI or runtime
  gaps
- `Unsupported`: not wired in the ELKS personality; returns `-ENOSYS`

## 2. Active translated syscalls

| No. | ELKS name | Status | Notes |
|-----|-----------|--------|-------|
| 1 | `exit` | Working | Exits cleanly through the normal process exit path. |
| 2 | `fork` | Working | LDT is cloned and smoke-tested with ELKS parent/child execution. |
| 3 | `read` | Working | DS pointer translation is implemented and smoke-tested. |
| 4 | `write` | Working | DS pointer translation is implemented and smoke-tested. |
| 5 | `open` | Working | 16-bit pathname translation is implemented; smoke-tested on ext2 root. |
| 6 | `close` | Working | Directly wired. |
| 7 | `waitpid` | Working | Status pointer translation is implemented and smoke-tested. |
| 8 | `creat` | Working | Smoke-tested through ELKS file-I/O binary on ext2 root. |
| 9 | `link` | Partial | Two-path ELKS pointer translation is implemented and host-validated; no dedicated upstream smoke yet. |
| 10 | `unlink` | Working | 16-bit pathname translation is implemented and smoke-tested. |
| 11 | `execve` | Working | ELKS packed startup stack decoding and LDT replacement are implemented. |
| 12 | `chdir` | Partial | ELKS pathname translation is implemented and host-validated; no dedicated ELKS smoke yet. |
| 13 | `time` | Partial | Numbering is reserved in the design, but no ELKS runtime validation yet. |
| 14 | `mknod` | Partial | ELKS pathname/device marshaling is implemented and host-validated; no upstream smoke yet. |
| 15 | `chmod` | Partial | ELKS pathname/mode marshaling is implemented and host-validated; no upstream smoke yet. |
| 16 | `chown` | Partial | ELKS pathname/credential marshaling is implemented and host-validated; no upstream smoke yet. |
| 18 | `stat` | Partial | Requires ELKS/Minix-shaped `stat` structure translation. |
| 19 | `lseek` | Partial | Native syscall exists; ELKS width semantics are not fully validated. |
| 20 | `getpid` | Partial | Expected to work through native return path; not smoke-tested yet. |
| 21 | `mount` | Partial | ELKS string/data-pointer translation is implemented and host-validated, but no upstream mount smoke covers full ELKS mount-option ABI yet. |
| 22 | `umount` | Partial | ELKS target-path translation is implemented and host-validated; no upstream smoke yet. |
| 23 | `setuid` | Partial | Native syscall exists; ELKS credential-width validation not complete. |
| 24 | `getuid` | Partial | Native syscall exists; ELKS credential-width validation not complete. |
| 25 | `stime` | Unsupported | No completed ELKS ABI contract yet. |
| 27 | `alarm` | Working | Smoke-tested with ELKS signal delivery. |
| 28 | `fstat` | Partial | Requires ELKS/Minix-shaped `stat` structure translation. |
| 29 | `pause` | Working | Smoke-tested with `alarm` and signal delivery. |
| 33 | `access` | Partial | ELKS pathname translation is implemented and host-validated; no dedicated upstream smoke yet. |
| 36 | `sync` | Partial | Expected direct mapping; no ELKS smoke coverage yet. |
| 37 | `kill` | Working | ELKS smallsig translation is implemented. |
| 39 | `mkdir` | Partial | ELKS pathname/mode translation is implemented and host-validated; no dedicated upstream smoke yet. |
| 40 | `rmdir` | Partial | Native path exists; ELKS smoke coverage not complete. |
| 41 | `dup` | Partial | Direct mapping exists; no ELKS smoke coverage yet. |
| 42 | `pipe` | Partial | ELKS 16-bit pipe-fd array marshaling is implemented and host-validated; no upstream pipeline smoke yet. |
| 43 | `times` | Partial | ELKS `struct tms` translation is implemented and host-validated; no upstream `times` smoke yet. |
| 45 | `brk` | Working | 16-bit bounded `brk` translation is implemented. |
| 46 | `setgid` | Partial | Native syscall exists; ELKS width validation not complete. |
| 47 | `getgid` | Partial | Native syscall exists; ELKS width validation not complete. |
| 48 | `signal` | Working | ELKS handler translation and callback-frame delivery are implemented. |
| 54 | `ioctl` | Unsupported | ELKS ioctl numbering and structure translation are not finished. |
| 55 | `fcntl` | Unsupported | ELKS command/flag translation is not finished. |
| 60 | `umask` | Partial | Direct/native path exists; ELKS validation not complete. |
| 63 | `dup2` | Partial | Direct mapping exists; no ELKS smoke coverage yet. |
| 64 | `getppid` | Partial | Direct mapping exists; no ELKS smoke coverage yet. |
| 65 | `getpgrp` | Partial | Direct mapping exists; no ELKS smoke coverage yet. |
| 69 | `sbrk` | Working | 16-bit heap growth with old-break copyout is implemented and unit-tested. |
| 70 | `ustatfs` | Partial | Mounted-filesystem index translation works and upstream `df` now enumerates mount points, but ELKS-style block-device names remain blank under Substrate's nested `/dev/storage` layout. |
| 74 | `uname` | Working | The ELKS five-field `utsname` is translated from native Substrate identity strings and smoke-tested with upstream `uname`. |

## 3. Smoke-tested binaries

The current ELKS QEMU smoke suite validates:

- `hello_elks`
- `sleep_elks`
- `fileio_elks`
- `fork_elks`

These are driven by `tests/elks/run_tests.sh`.

## 4. Known open runtime issues

- LDT bounds-violation validation is not complete; current fault testing still
  exposes an unresolved fault-handling bug.
- Core dump support for ELKS processes is not implemented.
- Many structure-bearing syscalls still need ELKS-specific layout translation.
- `ustatfs(70)` now works for mount enumeration, but `df` still lacks ELKS-style block-device names because Substrate does not currently expose flat `/dev/hd*` aliases.
