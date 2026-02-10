# /dev/mem Refactor Checklist

This checklist is intended for auditing and refactoring existing `/dev/mem` implementations to ensure security, stability, and correctness.

## 1. Privilege & Policy Checks
- [ ] **Open Check:** Does `open()` enforce `root` (UID 0) or `CAP_SYS_RAWIO`?
- [ ] **Securelevel:** Does the driver respect `securelevel > 0` or a `secure_mode` flag?
    - If secure mode is active, writes to `/dev/mem` should be forbidden.
    - If secure mode is strict, reads might also be restricted.
- [ ] **Range Policy:** Is there a mechanism to restrict access to specific physical ranges (allowlist/denylist)?
    - Ensure kernel text/data is protected if required.

## 2. Address Translation & Access
- [ ] **Physical Semantics:** Are file offsets interpreted strictly as **physical addresses**?
- [ ] **Virtual Mapping:** Does the driver assume a 1:1 mapping (e.g., `offset + 0xC0000000`)?
    - **Fix:** If so, ensure it bounds-checks against the direct map limit (e.g., 1GB).
    - **Fix:** For High Memory (>1GB), use `pmap_kenter` or temporary mappings instead of direct pointer arithmetic.
- [ ] **Fault Handling:** Does the driver handle bus faults or invalid accesses gracefully?
    - Use `copyin`/`copyout` or `on_fault` handlers when copying to/from user space.
    - Don't panic the kernel on invalid user requests.

## 3. mmap Support
- [ ] **Implementation:** Does `mmap` actually map physical pages?
    - **Fix:** If `mmap` just allocates anonymous memory and copies data (snapshot), replace it with true `pmap_enter` calls.
- [ ] **Cache Coherency:** Are page protection bits (CACHE_DISABLE, WRITE_THROUGH) set correctly for MMIO ranges?
- [ ] **Safety:** Does `mmap` enforce the same privilege and range checks as `open`/`read`?

## 4. Concurrency & Reentrancy
- [ ] **Locking:** Is shared state (if any) protected?
- [ ] **Per-File State:** Is the file offset independent for each open descriptor? (VFS usually handles this, but verify).

## 5. Documentation & Testing
- [ ] **Man Page:** Is there a `mem.4` man page describing behavior and security risks?
- [ ] **Tests:** Is there a safe test harness?
    - **Fix:** Do NOT test by reading random physical addresses. Use a controlled helper module that exports a safe physical page.
