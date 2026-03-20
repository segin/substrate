# ELKS Personality Specification

## Scope
Provides a 16-bit Linux-like execution environment for Substrate, allowing it to run ELKS (Embeddable Linux Kernel Subset) binaries.

## Personality Contract
- **Binary Format:** ELKS binaries are recognized as Minix-style 16-bit `a.out` images.
- **Memory Model:** Loaded through a private per-process LDT. LDT state is serialized by a dedicated process lock.
- **Execution Mode:** Runs as a 16-bit protected-mode personality (`BITNESS_16`), not VM86.
- **Syscalls:** Uses `INT 0x80` with the ELKS register argument order: `BX, CX, DX, DI, SI`.
- **Signals:** `signal()` and `kill()` are translated at the personality edge. Signal delivery uses the ELKS libc callback convention: the kernel pushes a far-return frame for `_signal_cbhandler(sig)` on the ELKS user stack and resumes via `lret $2`.
- **`/dev/kmem` Compatibility:** ELKS processes see an ELKS-shaped synthetic task snapshot through intercepted operations on native `/dev/kmem`. This enables upstream ELKS `ps` and `meminfo` to run.
- **Minix Traps:** `INT 0x20` is treated as a Minix-86 trap attempt, logged, and converted into `SIGSYS`.

## Implementation Details
- Full-size LDT tables are backed by a dedicated UMA zone.
- Synthetic `/dev/kmem` image exports task, `_seg_all`, and `_heap_all` rings plus `MEM_GETUSAGE` accounting.
- Task-table view reserves slot 0 for swapper/idle.
