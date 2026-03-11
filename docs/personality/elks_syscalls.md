# ELKS Syscall Mapping

## 1. Scope

This document defines the ELKS syscall-number contract for the Substrate
`PERS_ELKS` personality.

The mapping below is based on:

- the in-tree `sys/exec/perso/elks_syscall_table.h`
- the current ELKS personality table in `sys/exec/perso/perso_elks.c`
- upstream `elksemu` behavior and ELKS small-model userspace expectations

Status values:

- `Direct`: intended to map directly to an existing native kernel service
- `Translate`: requires ELKS-specific marshaling or structure translation
- `Partial`: some native service exists but the ELKS ABI contract is not yet
  complete
- `Unsupported`: no ELKS contract yet; personality shall return `-ENOSYS`

Undefined slots not listed in the active table are reserved and shall return
`-ENOSYS`.

## 2. Active ELKS syscall numbers

| No. | ELKS name | Native target | Status | Notes |
|-----|-----------|---------------|--------|-------|
| 1 | `exit` | `sys_exit` | Direct | Exit code is returned through normal process-exit handling. |
| 2 | `fork` | `sys_fork` | Partial | Requires correct LDT duplication and 16-bit child ABI state. |
| 3 | `read` | `sys_read` | Translate | ELKS user pointers must be translated through the ELKS address model. |
| 4 | `write` | `sys_write` | Translate | Same pointer translation rules as `read`. |
| 5 | `open` | `sys_open` | Translate | Requires ELKS flag marshaling and 16-bit path-pointer handling. |
| 6 | `close` | `sys_close` | Direct | File descriptor semantics match directly. |
| 7 | `waitpid` | `sys_waitpid` | Translate | Wait status width and user-pointer marshaling are ELKS-specific. |
| 8 | `creat` | `sys_creat` | Translate | Equivalent to ELKS `creat` semantics at the ABI edge. |
| 9 | `link` | `sys_link` | Translate | Two ELKS path pointers. |
| 10 | `unlink` | `sys_unlink` | Translate | ELKS path pointer translation required. |
| 11 | `execve` | `sys_execve` | Partial | Requires ELKS process-entry image construction and LDT replacement. |
| 12 | `chdir` | `sys_chdir` | Translate | ELKS path pointer translation required. |
| 13 | `time` | `sys_time` | Translate | ELKS-visible time result width must match personality ABI. |
| 14 | `mknod` | `sys_mknod` | Translate | Device and path argument marshaling required. |
| 15 | `chmod` | `sys_chmod` | Translate | ELKS mode width and path-pointer handling required. |
| 16 | `chown` | `sys_lchown` | Translate | Credential width translation required. |
| 18 | `stat` | `sys_stat` | Translate | Must populate ELKS/Minix-shaped 16-bit `stat` layout. |
| 19 | `lseek` | `sys_lseek` | Translate | Offset/result handling must match ELKS ABI width. |
| 20 | `getpid` | `sys_getpid` | Direct | PID returned in ELKS-visible integer width. |
| 21 | `mount` | `sys_mount` | Partial | ELKS mount ABI and structure conventions are not finalized. |
| 22 | `umount` | `sys_umount` | Partial | ELKS ABI edge not finalized. |
| 23 | `setuid` | `sys_setuid` | Translate | UID width translation required. |
| 24 | `getuid` | `sys_getuid` | Translate | UID width translation required. |
| 25 | `stime` | `sys_stime` | Partial | Native support exists; ELKS ABI contract is not finished. |
| 27 | `alarm` | `sys_alarm` | Translate | ELKS interval width and return semantics must match ABI. |
| 28 | `fstat` | `sys_fstat` | Translate | Must emit ELKS/Minix-shaped 16-bit `stat` layout. |
| 29 | `pause` | `sys_pause` | Direct | Signal-restart behavior remains personality-specific. |
| 33 | `access` | `sys_access` | Translate | ELKS path pointer translation required. |
| 36 | `sync` | `sys_sync` | Direct | No ELKS-specific payload marshaling. |
| 37 | `kill` | `sys_kill` | Translate | ELKS smallsig numbers are translated at the personality edge before entering native signal routing. |
| 38 | `rename` | none yet in table | Unsupported | Reserved in numbering; personality shall return `-ENOSYS` until explicitly wired. |
| 39 | `mkdir` | `sys_mkdir` | Translate | ELKS path-pointer and mode marshaling required. |
| 40 | `rmdir` | `sys_rmdir` | Translate | ELKS path pointer translation required. |
| 41 | `dup` | `sys_dup` | Direct | Descriptor semantics match directly. |
| 42 | `pipe` | `sys_pipe` | Translate | Pipe result array marshaling must follow ELKS ABI width. |
| 43 | `times` | `sys_times` | Translate | Must populate ELKS-visible times structure. |
| 45 | `brk` | `sys_brk` | Partial | Must be bounded to the ELKS data/heap contract. |
| 46 | `setgid` | `sys_setgid` | Translate | GID width translation required. |
| 47 | `getgid` | `sys_getgid` | Translate | GID width translation required. |
| 48 | `signal` | `kern_sigaction` | Translate | ELKS smallsig numbers and default/ignore/custom handler conventions are translated at the personality edge; 16-bit handler entry/return remains a separate delivery contract. |
| 54 | `ioctl` | `sys_ioctl` | Partial | Requires ELKS tty ioctl-number and structure translation. |
| 55 | `fcntl` | `sys_fcntl` | Partial | Requires ELKS flag/cmd translation. |
| 60 | `umask` | `sys_umask` | Translate | ELKS mode-width handling required. |
| 63 | `dup2` | `sys_dup2` | Direct | Descriptor semantics match directly. |
| 64 | `getppid` | `sys_getppid` | Direct | PID width remains ELKS-visible integer width. |
| 65 | `getpgrp` | `sys_getpgrp` | Direct | Process-group ID width remains ELKS-visible integer width. |
| 69 | `sbrk` | `sys_brk` | Translate | Returns the prior 16-bit break through an ELKS data-segment pointer while bounding growth to the ELKS data-segment limit. |
| 70 | `ustatfs` | none yet in table | Unsupported | ELKS `struct statfs` translation and device-to-mount resolution are not wired yet. |
| 74 | `uname` | `kern_uname` | Translate | Populates the ELKS five-field `struct utsname`, truncating native Substrate identity strings to ELKS field widths. |

## 3. Reserved or currently unsupported slots

The following numbered slots are not part of the active in-tree ELKS table and
shall currently return `-ENOSYS`:

- `17`
- `26`
- `30`
- `31`
- `32`
- `34`
- `35`
- `44`
- `49`
- `50`
- `51`
- `52`
- `53`
- `56`
- `57`
- `58`
- `59`
- `61`
- `62`
- `66`
- `67`
- `68`
- `71`
- `72`
- `73`
- `75` and above unless later assigned

## 4. Non-syscall incompatibility path

`INT 0x20` is not a supported ELKS syscall vector in Substrate. It is treated as
a Minix-86 syscall attempt and is handled outside this syscall table by logging
the incompatibility and raising `SIGSYS`.
