# Requirements to Test Matrix

| Req ID | Description | Test Case | Expected Outcome |
| :--- | :--- | :--- | :--- |
| **U1** | Privilege & Policy Check | `test_mem_open_unprivileged` | `open("/dev/mem")` returns -1, `errno == EPERM` for non-root user. |
| **U1** | Securelevel Enforcement | `test_mem_securelevel` | If `kern.securelevel > 0`, `open` (write) fails or `write` fails. |
| **U2** | Physical Semantics | `test_mem_read_phys` | `read` from test helper PA returns expected pattern. |
| **U2** | Seek Behavior | `test_mem_seek` | `lseek` sets offset correctly; subsequent read returns correct data. |
| **U3** | Exact Byte Semantics | `test_mem_partial_io` | `read`/`write` of arbitrary size (non-aligned) succeeds. |
| **U4** | Safe Error Handling | `test_mem_fault` | `read` from invalid PA (if mockable) or user buffer fault returns `EFAULT`. |
| **U5** | fstat/stat | `test_mem_fstat` | `fstat` returns `S_IFCHR`. |
| **O1** | mmap Support | `test_mem_mmap` | `mmap` of test helper PA succeeds; writes are visible via `read`. |
| **C2** | Safe Test Harness | `test_mem_integrity` | Tests only access `debug.mem_test_addr`; no arbitrary memory access. |
