# ELKS Syscall Table Mapping

This document outlines the mapping of ELKS (Embeddable Linux Kernel Subset) system calls to Substrate's native system calls. ELKS syscall numbers heavily overlap with early Linux 16-bit syscall numbers, streamlining the mapping process.

| ELKS Syscall # | ELKS Name | Substrate Equivalent | Support Status | Notes |
|---|---|---|---|---|
| 1 | `exit` | `sys_exit` | Supported | |
| 2 | `fork` | `sys_fork` | Supported | |
| 3 | `read` | `sys_read` | Supported | |
| 4 | `write` | `sys_write` | Supported | |
| 5 | `open` | `sys_open` | Supported | Requires 16-bit flag translation |
| 6 | `close` | `sys_close` | Supported | |
| 7 | `waitpid` | `sys_wait4` | Supported | Returns 16-bit wait status |
| 8 | `creat` | `sys_open` | Supported | Emulated via `open(path, O_CREAT\|O_TRUNC|O_WRONLY)` |
| 9 | `link` | `sys_link` | Supported | |
| 10 | `unlink` | `sys_unlink` | Supported | |
| 11 | `execve` | `sys_execve` | Supported | Triggers personality switch if target is not ELKS |
| 12 | `chdir` | `sys_chdir` | Supported | |
| 13 | `time` | `sys_time` | Supported | Returns 32-bit time truncated to ELKS expectations |
| 14 | `mknod` | `sys_mknod` | Supported | |
| 15 | `chmod` | `sys_chmod` | Supported | |
| 16 | `chown` | `sys_chown` | Supported | |
| 18 | `stat` | `sys_stat` | Supported | Requires translation to 16-bit `stat` struct |
| 19 | `lseek` | `sys_lseek` | Supported | |
| 20 | `getpid` | `sys_getpid` | Supported | |
| 23 | `setuid` | `sys_setuid` | Supported | |
| 24 | `getuid` | `sys_getuid` | Supported | |
| 27 | `alarm` | `sys_alarm` | Supported | |
| 28 | `fstat` | `sys_fstat` | Supported | Requires translation to 16-bit `stat` struct |
| 29 | `pause` | `sys_pause` | Supported | |
| 33 | `access` | `sys_access` | Supported | |
| 36 | `sync` | `sys_sync` | Supported | |
| 37 | `kill` | `sys_kill` | Supported | Translates POSIX signals to ELKS signals |
| 39 | `mkdir` | `sys_mkdir` | Supported | |
| 40 | `rmdir` | `sys_rmdir` | Supported | |
| 41 | `dup` | `sys_dup` | Supported | |
| 42 | `pipe` | `sys_pipe` | Supported | |
| 43 | `times` | `sys_times` | Supported | |
| 45 | `brk` | `sys_brk` | Supported | Validates against 64KB segment boundaries |
| 46 | `setgid` | `sys_setgid` | Supported | |
| 47 | `getgid` | `sys_getgid` | Supported | |
| 48 | `signal` | `sys_signal` | Supported | Requires signal trampoline thunking |
| 54 | `ioctl` | `sys_ioctl` | Partial | Terminal ioctls need extensive emulation depending on TTY |
| 55 | `fcntl` | `sys_fcntl` | Supported | Requires 16-bit `F_GETFL`/`F_SETFL` translation |
| 63 | `dup2` | `sys_dup2` | Supported | |
| 64 | `getppid` | `sys_getppid` | Supported | |
| 65 | `getpgrp` | `sys_getpgrp` | Supported | |

## Unsupported/Substrate-Specific Call Handling
Calls related to 32-bit memory management (`mmap`, `mprotect`) are unsupported or emulated strictly within the 64KB constraints of the assigned ELKS personality segment. Any unmapped ELKS syscall will return `-ENOSYS`.
