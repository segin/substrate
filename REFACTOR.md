# Refactor Checklist for /dev/kmem

This checklist outlines steps to audit and improve existing /dev/kmem implementations
to meet security and robustness requirements.

## 1. Privilege & Lockdown
- [ ] Verify that `open()`, `read()`, `write()`, and `ioctl()` explicitly check for privileged access (euid == 0).
- [ ] Verify that `securelevel` or kernel lockdown mode is checked. If `securelevel > 0`, access should be denied or restricted to read-only.
- [ ] Ensure that `mmap()` support is removed or explicitly returns `EPERM`/`EINVAL`.

## 2. Address Interpretation
- [ ] Confirm file offsets are treated as **Kernel Virtual Addresses (KVA)**, not physical addresses.
- [ ] For physical memory access, use `/dev/mem`, not `/dev/kmem`.

## 3. Safe Copy Primitives
- [ ] Replace direct pointer dereferences (e.g., `*ptr = val`) with safe kernel copy primitives (`copyin()`, `copyout()`, `uiomove()`).
- [ ] Ensure fault handlers are active during copy operations to catch page faults (invalid addresses) gracefully.
- [ ] Verify that faults return appropriate error codes (`EFAULT`) and do not panic the kernel.

## 4. Policy Enforcement
- [ ] Implement sysctl controls for enabling/disabling read and write access (`kern.kmem.allow_read`, `kern.kmem.allow_write`).
- [ ] Ensure default policy is conservative (Write Disabled).
- [ ] Consider implementing allow/deny lists for sensitive kernel regions (text, page tables, etc.).

## 5. Concurrency & Reentrancy
- [ ] Ensure the driver handles concurrent access correctly.
- [ ] Verify that per-file offsets are maintained correctly (use `struct file` or `uio` offset, not global static offset).

## 6. Testing Strategy
- [ ] Create a safe kernel test helper module that exports a specific buffer address.
- [ ] Verify regression tests run against this safe buffer only.
- [ ] Ensure tests cover:
    - Privilege escalation attempts (non-root access).
    - Policy enforcement (write when disabled).
    - Fault handling (read/write invalid addresses).
    - `mmap` rejection.

## 7. Audit & Logging
- [ ] Log privileged access attempts to system audit logs.
- [ ] Log policy changes.

## 8. Documentation
- [ ] Update `man(4)` page to reflect security policies and limitations.
- [ ] Document the risk of using `/dev/kmem` on production systems.
