# `lib/sys` Wrapper Audit

Sources:
- `lib/sys/*.c`
- `lib/sys/syscall.S`
- `include/sys/sysinfo.h`
- `sys/exec/perso/perso_native.c`

## Raw Entry Point

- `syscall()` in `lib/sys/syscall.S` uses Linux-style register argument passing (`ebx..ebp`).
- Native `substrate` syscall dispatch currently expects stack arguments for this personality.
- Result: the raw `lib/sys` calling convention is ABI-divergent from current native dispatch policy.

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
- `sys_vm_stats()` -> `SYS_VM_STATS`

### Wrappers or APIs with known gaps

- `vm86()` uses hardcoded `SYS_vm86=113`; this number is not in `sys/arch/i386/syscall.h` and not wired in native table.
- `sys_proc_pers_name()` uses hardcoded `SYS_PROC_PERS_NAME=360`; not wired in native table.
- `sys_vm_info()`, `sys_vm_swap()`, `sys_vm_buffers()`, `sys_vm_slabs()` are userspace stubs that return `ENOSYS`.
- `sys_cpu_count()` returns constant `1` in userspace instead of issuing `SYS_CPU_COUNT`.
- `sys_cpu_info()`, `sys_cpu_times()`, `sys_cpu_loadavg()` are userspace stubs returning `ENOSYS`.
- `sys_uptime()`, `sys_boottime()`, `sys_domainname()` are userspace stubs returning `ENOSYS`.
- `sys_net_interfaces()`, `sys_net_addrs()`, `sys_net_stats()`, `sys_net_routes()` are userspace stubs returning `ENOSYS`.
- `sys_hostname()` in `lib/sys/sysinfo.c` currently calls `gethostname()` fallback rather than direct `SYS_HOSTNAME`.

## Error-Handling Consistency Notes

- `lib/sys` wrappers generally return raw syscall values directly.
- There is no uniform `-1`/`errno` normalization in `lib/sys`.
- Kernel syscall handlers currently use mixed return conventions (`-errno`, plain `-1`, and positive errno in `sys_sysctl`).

## Recommended Follow-up

- Align `lib/sys/syscall.S` with native stack ABI, or move native personality to register ABI.
- Add missing syscall numbers to public headers only when kernel dispatch wiring exists.
- Convert userspace stubs (`sys_vm_*`, `sys_cpu_*`, `sys_net_*`) to either real syscalls or remove from exported API until implemented.
- Normalize syscall error conventions kernel-wide and enforce it in wrappers/tests.
