# ELKS Syscall Reference

## 1. Scope

This file is the implementation-status reference for the in-tree ELKS
personality. It complements `elks_spec.md` and `elks_syscalls.md` by recording
what is wired today and what remains partial or unsupported.

Status meanings:

- `Working`: implemented and backed by current host tests or direct QEMU smoke
- `Partial`: implemented at the personality edge, but known ABI or runtime
  gaps remain
- `Unsupported`: not wired in the current ELKS personality; returns `-ENOSYS`

## 2. Active translated syscalls

| No. | ELKS name | Status | Notes |
|-----|-----------|--------|-------|
| 1 | `exit` | Working | Exits cleanly through the normal process-exit path. |
| 2 | `fork` | Partial | LDT cloning exists, but shell-driven child execution is still unstable. |
| 3 | `read` | Working | DS pointer translation is implemented and host-validated. |
| 4 | `write` | Working | DS pointer translation is implemented and host-validated. |
| 5 | `open` | Working | 16-bit pathname translation is implemented and smoke-tested on current roots. |
| 6 | `close` | Working | Directly wired. |
| 7 | `wait4` / `waitpid` | Partial | Status-pointer translation exists, but process lifecycle is not yet proven through the unstable ELKS shell child path. |
| 8 | `creat` | Working | Routed through the ELKS pathname/mode wrapper. |
| 9 | `link` | Partial | Two-path ELKS pointer translation is implemented; no dedicated upstream smoke yet. |
| 10 | `unlink` | Working | Pathname translation is implemented and smoke-tested. |
| 11 | `execve` | Partial | Packed ELKS startup-stack decoding and LDT replacement exist, but native-shell to ELKS-shell and ELKS shell child-exec flows still fault. |
| 12 | `chdir` | Partial | Translation exists, but the shell path exercising it is not currently a stable end-to-end proof point. |
| 13 | `time` | Working | Translated safely at the personality edge and exercised by upstream `date`. |
| 14 | `mknod` | Partial | Marshaling exists; no upstream smoke yet. |
| 15 | `chmod` | Partial | Marshaling exists; no upstream smoke yet. |
| 16 | `chown` | Partial | Marshaling exists; no upstream smoke yet. |
| 17 | `brk` | Working | 16-bit bounded `brk` translation is implemented. |
| 18 | `stat` | Working | ELKS-width `struct stat` translation exists. |
| 19 | `lseek` | Partial | Wrapper exists, but full ELKS width semantics are not yet broadly validated. |
| 20 | `getpid` | Working | Personality wrapper is wired. |
| 21 | `mount` | Partial | Translation exists, but full upstream ELKS mount ABI is not validated. |
| 22 | `umount` | Partial | Translation exists, but upstream smoke is incomplete. |
| 23 | `setuid` | Partial | Direct/native path exists; ELKS credential-width validation is incomplete. |
| 24 | `getuid` | Working | Personality wrapper is wired. |
| 25 | `stime` | Partial | Personality wrapper exists, but the full upstream ABI is not yet complete. |
| 27 | `alarm` | Working | Exercised with ELKS signal delivery. |
| 28 | `fstat` | Working | ELKS-width `struct stat` translation exists. |
| 29 | `pause` | Working | Exercised with alarm/signal behavior. |
| 30 | `utime` | Unsupported | Not wired in current personality. |
| 31 | `chroot` | Unsupported | Not wired in current personality. |
| 32 | `vfork` | Partial | Numeric slot is wired, but ELKS shell spawn still fails in real runtime tests. |
| 33 | `access` | Partial | Path translation exists; upstream smoke is incomplete. |
| 36 | `sync` | Partial | Direct mapping exists; no ELKS-specific smoke yet. |
| 37 | `kill` | Working | ELKS smallsig translation is implemented. |
| 38 | `rename` | Unsupported | Not wired in current personality. |
| 39 | `mkdir` | Partial | Translation exists; no dedicated upstream smoke yet. |
| 40 | `rmdir` | Partial | Translation exists; no dedicated upstream smoke yet. |
| 41 | `dup` | Partial | Direct mapping exists; no ELKS-specific smoke yet. |
| 42 | `pipe` | Partial | 16-bit fd-array marshaling exists; no upstream pipeline smoke yet. |
| 43 | `times` | Partial | ELKS `struct tms` translation exists; no upstream smoke yet. |
| 45 | `dup2` | Partial | Direct mapping exists; no ELKS-specific smoke yet. |
| 46 | `setgid` | Partial | Direct/native path exists; ELKS width validation is incomplete. |
| 47 | `getgid` | Working | Personality wrapper is wired. |
| 48 | `signal` | Working | ELKS signal-number translation and callback-frame delivery are implemented. |
| 50 | `fcntl` | Partial | Currently routed to native `sys_fcntl` without full ELKS command/argument translation. |
| 54 | `ioctl` | Partial | `/dev/kmem` compatibility and tty `termios` translation exist; wider ioctl coverage does not. |
| 55 | `reboot` | Unsupported | Not wired in current personality. |
| 57 | `lstat` | Working | ELKS-width `struct stat` translation exists for non-following path lookup. |
| 58 | `symlink` | Unsupported | Not wired in current personality. |
| 59 | `readlink` | Working | ELKS pathname/output-buffer translation is implemented. |
| 60 | `umask` | Partial | Direct/native path exists; ELKS validation is incomplete. |
| 61 | `settimeofday` | Partial | Current wrapper only meaningfully applies the seconds field. |
| 62 | `gettimeofday` | Working | ELKS `timeval`/`timezone` translation is implemented and host-validated. |
| 63 | `select` | Working | ELKS bitmask and timeout translation through `kern_poll()` is host-validated. |
| 64 | `readdir` | Working | ELKS `struct dirent` emission is implemented. |
| 66 | `fchown` | Unsupported | Not wired in current personality. |
| 68 | `setsid` | Unsupported | Not wired in current personality. |
| 69 | `sbrk` | Working | Old-break copyout and bounded growth are implemented. |
| 70 | `ustatfs` | Partial | Mounted-filesystem index translation works; ELKS flat device-name compatibility does not. |
| 71 | `setitimer` | Unsupported | Not wired in current personality. |
| 72 | `sysctl` | Unsupported | Not wired in current personality. |
| 74 | `uname` | Working | The ELKS five-field `utsname` is translated and smoke-tested with upstream `uname`. |

## 3. Smoke-tested binaries

The current ELKS QEMU smoke suite validates these stable paths:

- `hello_elks`
- `sleep_elks`
- `fileio_elks`
- `fork_elks`
- upstream `ls`
- upstream `pwd`
- upstream `date`
- upstream `tty`
- upstream `stty`
- upstream `uname`
- upstream `df`
- upstream `ps`
- upstream `meminfo`
- upstream `sh` prompt

The following paths are still considered unstable and are not claimed as
working smoke coverage:

- upstream `sh` executing `ls`
- upstream `sh` executing `cd /perso; pwd`
- native `/bin/sh` handing off to upstream ELKS `sh`

These are driven by `tests/elks/run_tests.sh`.

## 4. Known open runtime issues

- Persistent core-file emission is not implemented; the kernel only captures
  in-memory crash state for ELKS faults today.
- Shell-mediated child process execution remains unstable.
- Many structure-bearing syscalls still need ELKS-specific layout validation.
- `ustatfs(70)` now works for mount enumeration, but `df` still lacks ELKS-style
  block-device names because Substrate does not currently expose flat `/dev/hd*`
  aliases.
