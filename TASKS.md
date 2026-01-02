# TASKS.md

## TestUnix Implementation Roadmap

This document tracks the progress and remaining tasks for the TestUnix operating system.

### 1. Kernel Core (`sys/kern`)
- [ ] **Console Subsystem:**
    - [ ] **Abstraction:** Implement `console_write`, `console_read` and backend registration.
    - [ ] **VGA Backend:** Adapt `vga.c` to console interface.
    - [ ] **Serial Backend:** Adapt `uart.c` to console interface.
    - [ ] **Framebuffer Backend:** For high-res graphics modes.
- [ ] **Memory Management:**
    - [x] **Physical Memory Manager (PMM):**
        - [x] Basic Bitmap Allocator.
        - [x] Multiboot mmap parsing.
        - [x] Kernel Memory Reservation (`_kernel_end`).
    - [x] **Command Line Parsing:**
        - [x] `cmdline_init`, `cmdline_get`, `cmdline_has`.
        - [x] Print command line at boot.
    - [ ] **Virtual Memory (VM):**
        - [x] `vm_page_t`, `vm_map`, `vm_object` structures.
        - [x] Page Fault Handler.
        - [x] Copy-on-Write (CoW).
        - [x] Swap Subsystem (Pager/Policy).
        - [x] `kmalloc`/`kfree` (Slab/Zone allocator).
- [ ] **Scheduling & Sync:**
    - [x] Preemptive Multitasking.
    - [x] Round-Robin Scheduler.
    - [x] `kthread` and `process` management.
    - [x] Mutexes, Spinlocks, Semaphores.
    - [x] Signals logic.

- [x] **Process Manager (`sys/pm`):**
    - [x] Basic `sys/pm` structure and `pm.o`.
    - [x] Move process creation/management out of arch-specific sched.
    - [/] `fork`, `exec`, `exit` implementation. (`fork` implemented)
    - [ ] Process Groups / Sessions.
    - [x] **Generic Scheduler:** Migrate `sched.c` logic to `sys/pm/sched.c` (generic).

### 2. Architecture (`sys/arch`)
- [ ] **i386:**
    - [x] GDT/IDT/TSS.
    - [x] Interrupt/Exception handling.
    - [x] EFI Boot Support (`kernel.efi`).
    - [ ] VM86 Mode.
- [ ] **x86_64:**
    - [ ] Long Mode Bootstrap.
    - [ ] 64-bit Paging (PML4).

### 3. Drivers (`sys/drivers`)
- [ ] **Storage:**
    - [ ] **RAM Disk:**
        - [ ] Multiboot Module loading (Initrd).
        - [ ] Block Driver (`/dev/ram0`).
    - [ ] **ATA/IDE:**
        - [x] PIO Mode.
        - [x] DMA Mode.
        - [x] Drive Enumeration (4 Buses, `/dev/storage/ideX` naming).
    - [ ] **AHCI/NVMe:** Initial stub drivers.
- [ ] **Input:**
    - [x] PS/2 Keyboard.
    - [x] PS/2 Mouse.
    - [x] Input Event Subsystem.

### 4. Filesystem (`sys/fs`, `sys/vfs`)
- [ ] **VFS:**
    - [x] Mount/Unmount logic.
    - [x] File Descriptor management.
    - [x] File Descriptor management.
    - [x] `open`, `read`, `write`, `close`, `stat`.
    - [x] `readlink`, `symlink` support.
    - [ ] **Root Mount:** Support `root=` kernel argument.
- [ ] **Filesystems:**
    - [x] EXT2 (Read/Write).
    - [x] FAT16/32.
    - [ ] **DevFS (`/dev`):**
        - [x] Device Registry.
        - [ ] Generic Block/Char device nodes.
- [ ] **Pseudo-FS:**
    - [x] ProcFS (`/proc`).
    - [x] SysFS (`/sys`).

### 5. Userland & Binaries
- [ ] **Init System:**
    - [ ] Parse `/etc/inittab` or `/etc/rc`.
    - [ ] Launch shell.
- [ ] **Shell:**
    - [ ] Basic command execution.
    - [ ] Environment variables.
- [ ] **Tools:**
    - [ ] `ls`, `cat`, `cp`, `mv`.
    - [x] `brandelf` (ELF branding tool).
    - [ ] `vi` clone.

### 6. Exec/Loader
- [x] ELF Loader.
- [x] Shebang support.
- [x] Linux ELF Detection (ABI Tag + `PT_INTERP` heuristic).
- [ ] `a.out` Loader (optional).

### 7. Verification & Testing
- [ ] **Automated Tests:**
    - [x] Kernel Unit Tests.
    - [x] Boot Crash Fix (SSE disabled).
    - [ ] Integration Tests (Boot logic).
