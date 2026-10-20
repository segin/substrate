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
-   **IDE QEMU Integration:**
    -   `test=ide_qemu_pio`: PIO read/write round-trip against a scratch IDE disk.
    -   `test=ide_qemu_dma`: DMA read/write round-trip against a scratch IDE disk.
    -   `test=ide_qemu_atapi`: ATAPI READ CAPACITY / READ TOC / sector read against a scratch ISO.
    -   `test=ide_qemu_extra`: tertiary/quaternary PCI IDE channel detection.
    -   `run_ide_qemu.sh`: disposable QEMU harness for the four IDE integration cases using `kernel.zimage` plus `root=/dev/storage/ram0`.

## Build System Integration

Tests are built as a static object (`tests.o`) and linked into `kernel.bin`. The entry point `run_kernel_tests()` is called from `kmain` in `sys/kern/main.c`.

## Guest torture tests

`torture_*.c` are ordinary user programs that exercise one kernel fix
end to end through the system calls.  `make torture` cross-builds them
(`TORTURE_CROSS`, default `/opt/substrate/bin/i386-unknown-substrate-`);
`run-torture.sh <binary> [file ...]` boots one as init on a throwaway copy
of `rootfs.img` and exits 0 when it prints a passing `Result:` line.
`KERNEL`, `IMAGE`, `TIMEOUT` and `QEMU_ARGS` override what it boots with.

Most need only the root disk.  The rest, and what they were run with:

| test | needs |
|---|---|
| `torture_halt` | its own harness, `run-torture-halt.sh` |
| `torture_setuid` | `run-torture.sh torture_setuid setuid_helper setuid_preload.so` |
| `torture_srcaddr` | a NIC at 10.0.2.15: `-netdev user,id=n0 -device e1000,netdev=n0` |
| `torture_audio_*` | `-audiodev wav,id=a0,path=out.wav -device intel-hda -device hda-output,audiodev=a0`; the capture is judged by ear or analysis, as each header says |
| `torture_usbkbd` | `-device qemu-xhci,id=x -device usb-kbd,bus=x.0` and keys sent with the monitor's `sendkey` |
| `torture_ext2_htree` | an indexed ext2 volume as `sata0` (recipe in its header), then `e2fsck -fn` |
| `torture_minix` | a `mkfs.minix` volume as `sata0`, then `fsck.minix -f` |
| `torture_fat_nodecache` | a FAT volume as `sata0` (recipe in its header) |
| `torture_fat_persist` | a FAT volume, and its device path in a file passed as `/fat-persist-dev` |
| `torture_fsync` | runs bare; with an ext2 volume behind QEMU `blkdebug` failing `flush_to_disk` and its path in `/fsync-flush-dev`, checks `EIO` |

An extra disk becomes `sata0` with
`-drive file=X,format=raw,if=none,id=d1 -device ahci,id=ahci -device ide-hd,drive=d1,bus=ahci.0`.
