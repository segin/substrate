# `kmain()` Initialization Flow

## Scope

This document records the current high-level initialization order in `sys/kern/main.c`.
It is intentionally implementation-oriented so kernel work can reason about ordering constraints during early boot.

## Boot Entry Sequence

`kmain(magic, addr)` currently executes the following major phases:

1. early GDT/IDT setup for fault capture
2. PM bootstrap state (`pm_init()`, swapper process seed)
3. Multiboot / FreeBSD-loader handoff decoding
4. kernel command-line parser initialization
5. early SMP scaffold (`smp_init()`)
6. memory stack initialization through `init_memory()`
7. console and UART backend selection
8. per-CPU/GDT/IDT/FPU/RTC architecture bring-up
9. PMAP bootstrap and signal trampoline mapping
10. LAPIC and full SMP discovery/bootstrap
11. partition detection and scheduler initialization
12. input, framebuffer, RNG, CRC, and misc subsystem bring-up
13. storage discovery and boot ramdisk registration
14. VFS initialization and root filesystem mount
15. init process creation
16. VM background worker startup
17. reclamation of early boot memory
18. idle scheduler loop

## Ordering Constraints

### Early Fault Visibility

`early_gdt_init()` and `early_idt_init()` happen first so faults before the normal console stack still produce diagnosable output.

### Command Line Before Console Policy

The kernel parses `console=`, `debug=`, and related boot flags before runtime
console registration so serial-console selection and early debug-channel policy
are available during console bring-up.

### SMP Before UMA, Then Again After Full Mapping

The boot path performs SMP work in two stages:

- `smp_init()` sets up early discovery scaffolding
- `smp_discover_cores()` runs once before UMA startup and again after `pmap_bootstrap()` installs the full higher-half direct map

This gives UMA the right CPU count while avoiding dependence on low bootstrap mappings after full paging setup.

### Storage Before Root Mount

Block providers and partition scanners are initialized before `init_root_fs()`:

- GEOM core
- GPT / MBR / BSD scanners
- PCI and storage drivers
- boot ramdisk registration from Multiboot modules

That ordering allows root selection from raw disks, partitions, or ramdisks.

### Root Filesystem Selection Contract

`init_root_fs()` does not assume `ext2` unconditionally anymore.

- if `rootfstype=` is omitted, the kernel probes the supported block-backed root filesystems in kernel order
- if `rootfstype=auto` is specified, the same ordered probe runs
- if `rootfstype=` contains a comma-separated list, the kernel tries each named filesystem in the listed order
- if mounting the requested `root=` fails, the same filesystem selection policy is retried against `/dev/storage/ram0`

The current built-in probe order is:

- `ext2`
- `fat`
- `minix`
- `udf`

`exfat` is intentionally not part of the root probe because the driver is initialized but does not yet register a mountable filesystem implementation.

### VFS Before Pseudo-Filesystem Mounts

`vfs_init()` runs before root mount and pseudo-fs mountpoint handling.
After the real root is mounted, the kernel ensures `/dev`, `/proc`, and `/sys` exist and mounts:

- `devfs`
- `procfs`
- `sysfs`

## Init and Background Workers

The boot path intentionally creates init before starting VM background workers:

1. `sched_spawn_kernel_process(init_task, cmdline)`
2. `vm_page_late_init()`

This preserves `PID 1` for init instead of letting background kernel workers consume it during bring-up.

## Final Boot Loop

After early reclaim of setup and Multiboot bookkeeping pages, the BSP enters the scheduler-driven idle loop:

- `sched_yield()`
- `sti; hlt`

At that point all further forward progress is driven by runnable threads and interrupts.
