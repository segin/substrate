# ELKS Syscall Mapping

## 1. Scope

This document defines the ELKS syscall-number contract for the Substrate
`PERS_ELKS` personality.

The mapping below is grounded in:

- upstream `elks/arch/i86/kernel/syscall.dat`
- upstream libc wrapper headers under `/home/segin/elks/libc/include`
- the in-tree `sys/exec/perso/elks_syscall_table.h`
- the current Substrate implementation in `sys/exec/perso/perso_elks.c`

Status values:

- `Direct`: direct native dispatch is intended
- `Translate`: implemented through ELKS-specific marshaling or structure translation
- `Partial`: implemented in some form, but ABI/runtime correctness is still incomplete
- `Unsupported`: not wired in the current Substrate ELKS personality; returns `-ENOSYS`

## 2. Upstream ELKS syscall surface

| No. | ELKS name | Native target | Status | Notes |
|-----|-----------|---------------|--------|-------|
| 1 | `exit` | `sys_exit` | Direct | Exit status follows the normal process-exit path. |
| 2 | `fork` | `sys_fork` | Partial | Numeric slot is wired, but full ELKS shell child-exec behavior is not yet stable. |
| 3 | `read` | `sys_read` | Translate | DS-based pointer translation required. |
| 4 | `write` | `sys_write` | Translate | DS-based pointer translation required. |
| 5 | `open` | `sys_open` | Translate | ELKS pathname and flag marshaling required. |
| 6 | `close` | `sys_close` | Direct | File descriptor semantics match directly. |
| 7 | `wait4` / `waitpid` | `kern_waitpid` | Partial | Status-pointer translation exists, but full shell-mediated process lifecycle remains incomplete. |
| 8 | `creat` | `sys_creat` | Translate | ELKS pathname and mode marshaling required. |
| 9 | `link` | `sys_link` | Translate | Two ELKS path pointers. |
| 10 | `unlink` | `sys_unlink` | Translate | ELKS path-pointer translation required. |
| 11 | `execve` | `kern_execve` | Partial | Packed ELKS startup-stack decoding exists, but complex shell-launched exec paths are still unstable. |
| 12 | `chdir` | `sys_chdir` | Translate | ELKS path-pointer translation required. |
| 13 | `time` | personality time wrapper | Translate | ELKS-visible result width must match the ELKS ABI. |
| 14 | `mknod` | `sys_mknod` | Translate | Device and path marshaling required. |
| 15 | `chmod` | `sys_chmod` | Translate | ELKS mode-width translation required. |
| 16 | `chown` | `sys_lchown` | Translate | ELKS credential-width translation required. |
| 17 | `brk` | `sys_brk` | Partial | Bounded to the ELKS data/heap contract. |
| 18 | `stat` | `kern_stat` | Translate | Must emit ELKS-width `struct stat`. |
| 19 | `lseek` | `sys_lseek` | Translate | ELKS offset/result semantics differ from native. |
| 20 | `getpid` | personality wrapper | Translate | Upstream ELKS libc expects this slot to provide both PID and PPID information through its ABI. |
| 21 | `mount` | `sys_mount` | Partial | Integer ELKS filesystem types and ELKS mount flags are translated at the personality edge; native options/data semantics remain incomplete. |
| 22 | `umount` | `sys_umount` | Partial | ELKS ABI edge not finalized. |
| 23 | `setuid` | `sys_setuid` | Translate | ELKS credential-width translation required. |
| 24 | `getuid` | personality wrapper | Translate | Upstream ELKS libc expects this slot to provide both UID and EUID information through its ABI. |
| 25 | `stime` | personality time wrapper | Partial | Native support exists; ELKS ABI edge is still incomplete. |
| 27 | `alarm` | `sys_alarm` | Translate | ELKS interval width and return semantics must match ABI. |
| 28 | `fstat` | `kern_fstat` | Translate | Must emit ELKS-width `struct stat`. |
| 29 | `pause` | `sys_pause` | Direct | Signal-restart behavior remains personality-specific. |
| 30 | `utime` | none yet in table | Unsupported | Upstream ELKS syscall slot exists; not yet wired in Substrate. |
| 31 | `chroot` | none yet in table | Unsupported | Upstream ELKS syscall slot exists; not yet wired in Substrate. |
| 32 | `vfork` | `sys_vfork` | Partial | Numeric slot is wired, but real ELKS shell spawn stability is not yet proven. |
| 33 | `access` | `sys_access` | Translate | ELKS path-pointer translation required. |
| 36 | `sync` | `sys_sync` | Direct | No ELKS-specific payload marshaling. |
| 37 | `kill` | `sys_kill` | Translate | ELKS smallsig numbers are translated at the personality edge. |
| 38 | `rename` | none yet in table | Unsupported | Upstream ELKS syscall slot exists; not yet wired in Substrate. |
| 39 | `mkdir` | `sys_mkdir` | Translate | ELKS pathname and mode marshaling required. |
| 40 | `rmdir` | `sys_rmdir` | Translate | ELKS path-pointer translation required. |
| 41 | `dup` | `sys_dup` | Direct | Descriptor semantics match directly. |
| 42 | `pipe` | `kern_pipe` | Translate | Pipe result array must be written back in ELKS width. |
| 43 | `times` | `sys_times` | Translate | Must emit ELKS-visible `struct tms`. |
| 45 | `dup2` | `sys_dup2` | Direct | Descriptor semantics match directly. |
| 46 | `setgid` | `sys_setgid` | Translate | ELKS credential-width translation required. |
| 47 | `getgid` | personality wrapper | Translate | Upstream ELKS libc expects this slot to provide both GID and EGID information through its ABI. |
| 48 | `signal` | `kern_sigaction` | Translate | ELKS smallsig numbers and callback conventions are translated at the personality edge. |
| 50 | `fcntl` | `sys_fcntl` | Partial | Variadic ELKS `fcntl` exists upstream; command and argument translation are incomplete. |
| 54 | `ioctl` | `sys_ioctl` | Partial | ELKS tty and `/dev/kmem` compatibility translation exists; broader ioctl coverage does not. |
| 55 | `reboot` | none yet in table | Unsupported | Upstream ELKS syscall slot exists; not yet wired in Substrate. |
| 57 | `lstat` | `kern_lstat` | Translate | Must emit ELKS-width `struct stat` without following final symlinks. |
| 58 | `symlink` | none yet in table | Unsupported | Upstream ELKS syscall slot exists; not yet wired in Substrate. |
| 59 | `readlink` | `kern_readlink` | Translate | ELKS pathname and output-buffer translation required. |
| 60 | `umask` | `sys_umask` | Translate | ELKS mode-width handling required. |
| 61 | `settimeofday` | personality time wrapper | Partial | Current Substrate wrapper applies the seconds field; full ELKS `timeval`/`timezone` semantics are not yet complete. |
| 62 | `gettimeofday` | personality time wrapper | Translate | ELKS `timeval` and optional `timezone` buffers are personality-specific. |
| 63 | `select` | personality poll wrapper | Translate | Implemented by translating ELKS bitmasks and timeout through `kern_poll()`. |
| 64 | `readdir` | personality dirent wrapper | Translate | Must emit ELKS `struct dirent` records. |
| 66 | `fchown` | none yet in table | Unsupported | Upstream ELKS syscall slot exists; not yet wired in Substrate. |
| 68 | `setsid` | none yet in table | Unsupported | Upstream ELKS syscall slot exists; not yet wired in Substrate. |
| 69 | `sbrk` | `sys_brk` | Translate | Returns the prior 16-bit break through an ELKS data-segment pointer. |
| 70 | `ustatfs` | personality mount-list wrapper | Partial | Mounted-filesystem index translation works, but flat ELKS `/dev/hd*` aliases are still absent. |
| 71 | `setitimer` | none yet in table | Unsupported | Upstream ELKS syscall slot exists; not yet wired in Substrate. |
| 72 | `sysctl` | none yet in table | Unsupported | Upstream ELKS syscall slot exists; not yet wired in Substrate. |
| 74 | `uname` | `kern_uname` | Translate | Populates the ELKS five-field `struct utsname` with ELKS field widths. |

## 3. Current unsupported slots

The following syscall numbers are present in upstream ELKS but are not part of
the current Substrate ELKS personality implementation:

- `30` `utime`
- `31` `chroot`
- `38` `rename`
- `55` `reboot`
- `58` `symlink`
- `66` `fchown`
- `68` `setsid`
- `71` `setitimer`
- `72` `sysctl`

Undefined slots outside the upstream ELKS numbering contract, or upstream slots
not yet wired above, shall currently return `-ENOSYS`.

## 4. Non-syscall incompatibility path

`INT 0x20` is not a supported ELKS syscall vector in Substrate. It is treated
as a Minix-86 syscall attempt and is handled outside this syscall table by
logging the incompatibility and raising `SIGSYS`.
