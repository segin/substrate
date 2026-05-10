# `lib/sys` Wrapper Audit

Sources:
- `lib/sys/*.c`
- `lib/sys/syscall.S`
- `include/sys/sysinfo.h`
- `sys/exec/perso/perso_native.c`

## Raw Entry Point

- `syscall()` in `lib/sys/syscall.S` uses the **stack argument
  ABI**: it pops the syscall number off the stack into `%eax`,
  re-pushes the caller's return address, and fires `int $0x80`
  with args laid out at `useresp[1..8]` — exactly what native /
  FreeBSD / NetBSD / OpenBSD / SVR4 dispatch reads.
- Verified end-to-end by `tests/lib/sys/host_test_syscall_abi.c`
  (0..6 args).

## Wrapper Coverage

### Direct wrappers to mapped native syscalls

- `getpid()`, `getppid()` -> `SYS_GETPID`, `SYS_GETPPID`
- `getuid()`, `getgid()`, `geteuid()`, `getegid()`, `setuid()`, `setgid()` -> UID/GID syscalls
- `getpgid()`, `setpgid()`, `getpgrp()`, `setsid()`, `getsid()` -> process-group/session syscalls
- `getrusage()` -> `SYS_GETRUSAGE`
- `times()` -> `SYS_TIMES`
- `ioctl()` -> `SYS_IOCTL`
- `stime()` -> `SYS_STIME`
- `reboot()` -> `SYS_REBOOT`
- `sysctl()` -> `SYS_SYSCTL`
- `sysinfo()` -> `SYS_SYSINFO`
- `sys_proc_info()`, `sys_proc_list()`, `sys_proc_threads()`, `sys_proc_fds()`, `sys_proc_maps()`, `sys_proc_cwd()`, `sys_proc_exe()`, `sys_proc_cmdline()`, `sys_proc_environ()` -> `SYS_PROC_*`
- `sys_proc_pers_name()` -> `SYS_PROC_PERS_NAME` (256)
- `sys_vm_stats()` -> `SYS_VM_STATS`
- `vm86()` -> `SYS_VM86` (113)

`getpriority`, `setpriority`, `clock_gettime`, `waitpid`, and
`munmap` already have typed wrappers in `lib/c/src/sys.c`; they do
NOT live in `lib/sys` to avoid duplicate-symbol link errors when a
binary links both archives.  Adding them here was attempted in an
earlier commit and reverted once the conflict surfaced.

### Wrappers or APIs with known gaps

- `sys_vm_info()`, `sys_vm_swap()`, `sys_vm_buffers()`, `sys_vm_slabs()` are userspace stubs that return `ENOSYS`.
- `brk()` and `futex()` have no native wrapper.  Rationale:
  `brk` is conventionally an internal implementation detail of
  libc malloc and is not wired into native dispatch on Substrate;
  `futex` is a Linux-shaped primitive and the native ABI prefers
  the typed pthread/turnstile path over a generic futex syscall.
  Both kernel handlers (`sys_brk`, `sys_futex`) exist but are
  reachable only through the Linux personality.
- `sys_cpu_count()` returns constant `1` in userspace instead of issuing `SYS_CPU_COUNT`.
- `sys_cpu_info()`, `sys_cpu_times()`, `sys_cpu_loadavg()` are userspace stubs returning `ENOSYS`.
- `sys_uptime()`, `sys_boottime()`, `sys_domainname()` are userspace stubs returning `ENOSYS`.
- `sys_net_interfaces()`, `sys_net_addrs()`, `sys_net_stats()`, `sys_net_routes()` are userspace stubs returning `ENOSYS`.
- `sys_hostname()` in `lib/sys/sysinfo.c` currently calls `gethostname()` fallback rather than direct `SYS_HOSTNAME`.

## Error-Handling Consistency Notes

The Substrate-wide error contract lives in
[`docs/syscalls/error-contract.md`](error-contract.md): the kernel
returns negated errno (`>= 0` success, `-1..-255` failure), and the
public lib/sys / lib/c wrappers normalize that to POSIX `-1`/`errno`.

Known stragglers still in the legacy mixed-return regime (these
will be migrated next time they're touched):

- `sys_sysctl` returns positive errno values from some sub-paths.
- A few internal-only `sys_*` helpers return plain `-1`.
- Pre-contract typed wrappers in `lib/sys` that return raw kernel
  values without the `errno = -ret; return -1;` normalization.

## Recommended Follow-up

- Align `lib/sys/syscall.S` with native stack ABI, or move native personality to register ABI.
- Add missing syscall numbers to public headers only when kernel dispatch wiring exists.
- Convert userspace stubs (`sys_vm_*`, `sys_cpu_*`, `sys_net_*`) to either real syscalls or remove from exported API until implemented.
- Normalize syscall error conventions kernel-wide and enforce it in wrappers/tests.
