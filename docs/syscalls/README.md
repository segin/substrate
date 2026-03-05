# Syscall Documentation (Substrate i386)

Last audited: March 5, 2026.

This directory documents the currently implemented Substrate syscall surface, with emphasis on:
- Native personality syscall dispatch (`sys/exec/perso/perso_native.c`)
- `lib/sys/` wrappers
- Process introspection (`sys_proc_*`)
- VM/system introspection (`sys_vm_*`)

## Documents

- `abi-and-dispatch.md`: calling convention, return semantics, personality ABI split.
- `native-syscall-catalog.md`: comprehensive catalog of syscalls wired into native dispatch, plus numbered constants not yet wired.
- `libsys-wrapper-audit.md`: `lib/sys` wrapper coverage and gaps.
- `sys-proc-family.md`: detailed status/behavior for all `sys_proc_*` calls.
- `sys-vm-family.md`: detailed status/behavior for all `sys_vm_*` calls.

## Key Findings Snapshot

- Native personality dispatch currently reads syscall arguments from user stack for `substrate`, while `lib/sys/syscall.S` issues Linux-style register argument passing.
- `lib/c` uses dedicated `_syscallN` stack-based stubs and is ABI-compatible with native dispatch.
- `sys_proc_info`, `sys_proc_list`, `sys_proc_count`, and `sys_vm_stats` are implemented.
- `sys_proc_threads`, `sys_proc_fds`, `sys_proc_maps`, `sys_proc_cwd`, `sys_proc_exe`, `sys_proc_cmdline`, and `sys_proc_environ` are currently stubs.
- `lib/sys` exports additional helpers (`sys_vm_info`, `sys_vm_swap`, `sys_vm_buffers`, `sys_vm_slabs`, etc.) that currently return `ENOSYS` from userspace stubs.
