# Kernel Test Framework

The Substrate kernel includes a built-in test framework for running unit tests, property tests, and regression tests directly within the kernel environment. Tests typically run during boot before the scheduler fully activates or init is spawned.

## Running Tests

To run kernel tests, append the `test` parameter to the kernel command line.

### Examples

**Run all tests:**
```
test=all
```
or
```
test=1
```

**Run specific test suite:**
```
test=pmap
test=mmap
```

**Halt system after testing:**
```
test=all test_halt=1
```

## adding a New Test

1.  **create Source File:**
    Create a new `.c` file in `sys/tests/` (e.g., `test_myfeature.c`).

    ```c
    #include "../kern/console.h"

    void run_myfeature_tests(void) {
        kprint("Running MyFeature Tests...\n");
        // ... assertions ...
    }
    ```

2.  **Register in Runner:**
    Edit `sys/tests/test_runner.c`:
    - Add forward declaration: `void run_myfeature_tests(void);`
    - Add to `run_kernel_tests()` logic.

3.  **Update Makefile:**
    Add `test_myfeature.c` to `SRCS` in `sys/tests/Makefile`.

## Test Suites

-   **PMAP (`test=pmap`):**
    -   `test_pmap.c`: Unit tests for lifecycle and protection.
    -   `property_pmap_protect.c`: Property tests for Copy-on-Write and permission logic.
-   **MMAP (`test=mmap`):**
    -   Basic mmap/munmap functionality (WIP).
-   **IDE (`test=ide`):**
    -   `test_ide_perf.c`: Benchmarks IDE PIO data transfer performance (`inw` loop vs `rep insw`).

## Build System Integration

Tests are built as a static object (`tests.o`) and linked into `kernel.bin`. The entry point `run_kernel_tests()` is called from `kmain` in `sys/kern/main.c`.
