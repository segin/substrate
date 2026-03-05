# `sys_vm_*` Family Reference

Prototypes are exported in `include/sys/sysinfo.h`.
Kernel implementation for `sys_vm_stats` is in `sys/kern/sysinfo.c`.

## Implemented syscall

### `int sys_vm_stats(sys_vmstat_t *stats)`

- Native syscall number: `255` (`SYS_VM_STATS`)
- Wired in native dispatch: yes
- Wrapper in `lib/sys/sysinfo.c`: yes

Current kernel behavior:
- Validates pointer is non-NULL and in user range.
- Fills `sys_vmstat_t` from PMM and VM page/swap stats.
- Populates: `total`, `free`, `available`, `buffers`, `cached`, `swap_total`, `swap_free`, `swap_cached`.
- Returns `0` on success.
- Returns `-14` on pointer faults.

## Exported APIs currently implemented as userspace stubs

These functions are declared in public headers and implemented in `lib/sys/sysinfo.c`, but currently return `ENOSYS` (with zeroed output where applicable):

- `int sys_vm_info(sys_vminfo_t *info)`
- `int sys_vm_swap(sys_swapinfo_t *swap, size_t *count)`
- `int sys_vm_buffers(sys_bufinfo_t *buf)`
- `int sys_vm_slabs(sys_slabinfo_t *slabs, size_t *count)`

## Related VM/system wrappers with gaps

- `int sys_cpu_count(void)` currently returns constant `1` in userspace, while kernel provides `SYS_CPU_COUNT`.
- `int sys_cpu_info(...)`, `int sys_cpu_times(...)`, and `int sys_cpu_loadavg(...)` are userspace stubs.
- `int sys_uptime(...)`, `int sys_boottime(...)` are userspace stubs.

## `mlock` / `munlock` note

`mlock()` and `munlock()` syscall numbers are wired (`150`, `151`), but current kernel handlers are explicit no-op stubs returning success.
