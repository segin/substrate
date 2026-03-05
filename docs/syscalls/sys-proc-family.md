# `sys_proc_*` Family Reference

Primary structures and prototypes are in `include/sys/sysinfo.h`.
Kernel entry points are in `sys/kern/syscall.c`.

## Implemented calls

### `int sys_proc_info(pid_t pid, sys_procinfo_t *info)`

Current behavior:
- `pid == 0` targets current process.
- Otherwise scans fixed process table for matching PID.
- Returns `0` on success.
- Returns negative error on failure (`-1` if missing/invalid, `-14` on copyout fault).

Fields currently populated by kernel:
- `pid`, `ppid`, `pgid`, `sid`, `uid`, `gid`, `state`, `bitness`, `start_time`, `name`

Fields present in struct but not fully populated in current implementation:
- `euid`, `egid`, `perso_id`, `tty`, `nice`, `user_time`, `sys_time`, `vsize`, `rss`

### `int sys_proc_list(pid_t *pids, size_t count)`

Current behavior:
- If `pids == NULL` or `count == 0`, returns total active process count.
- Clamps `count` to `1024`.
- Active process definition currently is `processes[i].pid != -1` over a fixed table.
- Returns number of PIDs copied or a negative error.

### `int sys_proc_count(void)`

Current behavior:
- Counts entries with `pid != -1` in fixed process table.
- Returns non-negative count.

## Wired but currently stubbed

The following calls are present in native dispatch but currently return `-1` stubs:

- `int sys_proc_threads(pid_t pid, tid_t *tids, size_t *count)`
- `int sys_proc_fds(pid_t pid, sys_fd_t *fds, size_t *count)`
- `int sys_proc_maps(pid_t pid, sys_map_t *maps, size_t *count)`
- `int sys_proc_cwd(pid_t pid, char *buf, size_t len)`
- `int sys_proc_exe(pid_t pid, char *buf, size_t len)`
- `int sys_proc_cmdline(pid_t pid, char **argv, size_t *argc)`
- `int sys_proc_environ(pid_t pid, char **envp, size_t *envc)`

## Wrapper-only (not wired in native dispatch)

### `int sys_proc_pers_name(int perso_id, char *buf, size_t len)`

- Exposed by `lib/sys/proc.c`.
- Uses hardcoded syscall number `360` (`SYS_PROC_PERS_NAME`).
- Not currently defined in `sys/arch/i386/syscall.h` or native syscall table.

## API design notes

- All `sys_proc_*` APIs are currently Substrate-specific introspection calls.
- Buffer-count style APIs (`*_list`, `*_threads`, `*_fds`, `*_maps`) should standardize truncation and required-size reporting semantics.
- Final ABI should define stable field-population guarantees for `sys_procinfo_t`.
