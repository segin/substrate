# Substrate Syscall Error Contract

> **Status:** binding for new code, retroactively enforced where
> tests cover it.  See REQ-07-0056 in
> `docs/tasks/07-6a-system-call-wrapper-library.md`.

## The contract

Substrate uses the standard Unix syscall error convention end-to-end.
There is exactly one convention; there are no per-syscall variations.

### Kernel boundary

Every `sys_*` handler returns a `long` (or compatible signed type) where:

- `>= 0`  — success.  The value is the syscall's natural result
  (a count, an fd, a pid, a length, etc.).
- `-1 ... -255` — failure.  The negated errno (e.g. `-EFAULT = -14`,
  `-ENOENT = -2`).

Handlers MUST NOT return positive errno values, MUST NOT return a
plain `-1` "generic failure", and MUST NOT use `errno` themselves
(the kernel has no per-thread errno; it lives in libc).

The negated-errno contract is what `i386_extract_syscall_args` and
the `int $0x80` trampoline assume when stuffing the value back into
the user `%eax` register.

### Userland boundary (`lib/sys` and `lib/c`)

Typed wrappers normalize the kernel return into the POSIX convention:

```c
long ret = syscall(SYS_FOO, ...);
if (ret < 0 && ret > -256) {
    errno = (int)(-ret);
    return -1;
}
return ret;
```

For wrappers that return a pointer (e.g. `mmap`):

```c
void *p = (void *)syscall(SYS_MMAP, ...);
if ((long)p < 0 && (long)p > -256) {
    errno = (int)(-(long)p);
    return MAP_FAILED;
}
return p;
```

The `> -256` guard exists because some syscalls legitimately return
large negative values (signed offsets from `lseek`, etc.); only the
errno range `1..255` is reserved for failure.

`lib/sys` wrappers MAY return the raw kernel value if their caller
is internal kernel-aware code (e.g. tests, syscall trace helpers),
but the public-facing wrappers in `include/` MUST normalize.

## Verification

- `tests/lib/sys/host_test_libsys_error_contract.c` exercises a
  representative wrapper from each error class (POSIX `-1`/`errno`,
  pointer/`MAP_FAILED`) and asserts the normalization holds.
- `tests/lib/sys/host_test_libsys_syscall_audit.sh` keeps the
  `SYS_*` numbering honest so error-class tests don't silently
  point at the wrong dispatch slot.

## Migration notes

A handful of older handlers still return positive errnos or plain
`-1`.  These are exempted ONLY until their next bug-fix touch; new
patches in those files MUST adopt the contract above.  Known stragglers
are tracked under "Error-Handling Consistency Notes" in
`docs/syscalls/libsys-wrapper-audit.md`.
