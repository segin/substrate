# TASKS.md

## TestUnix Implementation Roadmap

This document tracks the progress and remaining tasks for the TestUnix operating system.

### 1. Kernel Core (`sys/core`, `sys/kern`)
- [ ] **Memory Management:**
    - [ ] **Physical Memory Manager (PMM):**
        - [x] Basic Bitmap Allocator (`pmm.c`).
        - [ ] Parse Multiboot Memory Map (mmap) to support non-contiguous RAM.
        - [ ] Implement `pmm_alloc_contiguous` for specific DMA drivers.
    - [ ] **Virtual Memory Manager (VMM):**
        - [ ] **Paging Init:** Bootstrap kernel page directory and identity map kernel (`vmm.c`).
        - [ ] **Mapping:** Implement `vmm_map_page` and `vmm_unmap_page`.
        - [ ] **Page Fault Handler:** Handle copy-on-write, demand paging, and invalid access.
        - [ ] **Address Space:** Per-process Page Directory switching (`cr3` management).
    - [ ] **Kernel Heap (kmalloc):**
        - [ ] **Early Allocator:** Simple placement malloc for initialization.
        - [ ] **Main Allocator:** Slab or Bucket allocator for efficient kernel objects.
    - [ ] **User Memory:**
        - [ ] Implement `mmap`, `munmap`, `brk` system calls.
- [ ] **Scheduling:**
    - [ ] Implement Preemptive Multitasking (timer interrupt).
    - [ ] Implement Thread Priorities and Scheduling Classes.
    - [x] Implement Context Switching (save/restore registers properly).
    - [ ] Implement `wait`, `sleep`, `wakeup` mechanisms.
- [ ] **Synchronization:**
    - [ ] Implement Spinlocks, Mutexes, and Semaphores.
    - [ ] Implement Futex support for user-space synchronization (Linux personality).
- [ ] **Signals:**
    - [ ] Implement Signal delivery mechanism (trampoline, context saving).
    - [ ] Implement `kill`, `sigaction`, `sigprocmask`.

### 2. Architecture (`sys/arch`)
- [ ] **i386:**
    - [x] Complete GDT/TSS setup for user-mode switching.
    - [x] Implement Exception Handling (Page Fault, GPF, etc.).
- [ ] **x86_64:**
    - [ ] Implement Long Mode bootstrap (`boot.S`).
    - [ ] Implement 64-bit Paging (PML4).
    - [ ] Implement `syscall`/`sysret` entry point.

### 3. Drivers (`sys/drivers`)
- [ ] **Storage:**
    - [ ] **ATA/IDE:** Implement DMA transfers (Bus Mastering).
    - [ ] **AHCI:** Implement command list and FIS construction.
    - [ ] **NVMe:** Implement Admin Queue and I/O Queue submission/completion.
- [ ] **Input:**
    - [ ] **Keyboard:** Implement Scancode Set 2 decoding and modifiers (Shift, Ctrl).
    - [ ] **Mouse:** Implement PS/2 Mouse driver.
- [ ] **Video:**
    - [ ] Implement VESA Linear Framebuffer (LFB) support via Multiboot.
    - [ ] Implement a console over framebuffer.

### 4. Filesystem (`sys/fs`, `sys/vfs`)
- [ ] **VFS:**
    - [ ] Implement Mount points and filesystem registration.
    - [ ] Implement File Descriptor reference counting and management.
    - [ ] Implement Permissions checking (`access`).
- [ ] **EXT2:**
    - [ ] Implement Inode and Block allocation/freeing.
    - [ ] Implement Directory entry creation/deletion.
- [ ] **FAT16/32:**
    - [ ] Implement File Allocation Table parsing and chain following.
    - [ ] Implement Long File Name (LFN) support.
- [ ] **Pseudo-FS:**
    - [ ] **DevFS (`/dev`):** Device node registration.
    - [ ] **ProcFS (`/proc`):** Process information export.
    - [ ] **SysFS (`/sys`):** Kernel object hierarchy export.
    - [ ] **FUSE (`/dev/fuse`):** Userspace filesystem bridge.

### 5. System Calls & Personalities
- [ ] **Mechanisms:**
    - [ ] Implement `sys_ioctl` framework.
    - [ ] Implement `sys_pipe` and `sys_dup2`.
    - [ ] Implement `sys_time` and RTC reading.
    - [ ] **Emulation Path Lookup:** Check `/emul/<perso>/` before root for foreign personalities.
- [ ] **Personalities:**
    - [ ] **Xenix (286 & 386):**
        - [ ] **Binary Loader (`x.out`):**
            - [ ] Define `struct xexec` header (magic numbers 0x206 for 286, 0x20C for 386).
            - [ ] Implement LDT management for 16-bit Segmented execution (286).
            - [ ] Implement 32-bit segment loading for Xenix 386.
        - [ ] **System Call Interface:**
            - [ ] **Mechanism:** Setup GDT Call Gate 7 (`lcall 7,0`) for syscall entry (used by both).
            - [ ] **Dispatcher:** Implement translation layer for SVR3/Xenix syscall numbers.
            - [ ] **ABI:** Handle argument passing (Stack-based for Xenix/SVR3).
            - [ ] **Extensions:** Implement `cxenix` multiplexer and `rdchk`/`nap`.
    - [ ] **Linux:**
        - [ ] Improve `sys_clone` compatibility (flags).

### 6. C Library (`lib/c`)
- [ ] **Stdio:**
    - [ ] Implement Buffered I/O logic (`fflush`, buffer management).
    - [ ] Complete `printf` family (all format specifiers).
- [ ] **String/Mem:**
    - [ ] Optimize `memcpy`, `memset`, `memmove`.
- [ ] **Math (`libm`):**
    - [ ] Implement basic floating point functions (`sin`, `cos`, `pow`, `sqrt`).
- [ ] **Dynamic Linker (`ld.so`):**
    - [ ] Implement ELF relocation processing (PLT/GOT).
    - [ ] Implement `dlopen`, `dlsym`.

### 7. Userland Binaries (`bin/`)
- [ ] **Shell (`sh`):**
    - [ ] Implement environment variable handling.
    - [ ] Implement pipelines (`|`) and redirection (`>`, `<`).
    - [ ] Implement job control (`&`, `bg`, `fg`, `jobs`).
    - [ ] Implement scripting support (`if`, `for`, `while`).
- [ ] **Core Utils:**
    - [ ] **`ls`:** Implement flags (`-l`, `-a`, `-h`).
    - [ ] **`cp`:** Implement recursive copy (`-r`).
    - [ ] **`rm`:** Implement recursive delete (`-r`).
    - [ ] **`mkdir`:** Implement parents (`-p`).
- [ ] **System Utils:**
    - [ ] **`init`:** Implement runlevels and service management.
    - [ ] **`login`:** Implement PAM-like authentication or shadow file reading.
    - [ ] **`ps`:** Read from `/proc`.
    - [ ] **`top`:** Real-time process monitoring.

### 8. Networking (Future)
- [ ] **Layer 1: Network Interface Drivers (Kernel)**
    - [ ] **Loopback:** Virtual interface implementation.
    - [ ] **RTL8139:** Basic send/receive, interrupt handling.
    - [ ] **E1000:** Intel Gigabit Ethernet support.
    - [ ] **VirtIO Net:** Optimized network driver for QEMU/KVM.
- [ ] **Layer 2: Packet Interface Layer**
    - [ ] Define abstract packet structure (like `sk_buff` or `mbuf`).
    - [ ] Implement packet queuing and dispatching to protocol drivers.
    - [ ] Implement `ifconfig` style interface management (up/down/flags).
- [ ] **Layer 3: Protocol Drivers (Kernel Stack)**
    - [ ] **Ethernet (L2):** Frame parsing and encapsulation.
    - [ ] **ARP (L2.5):** Resolution, caching, and timeout logic.
    - [ ] **IP (L3):** IPv4 packet processing, routing table, fragmentation/reassembly.
    - [ ] **ICMP (L3):** Echo request/reply (ping) and error handling.
    - [ ] **UDP (L4):** Datagram sending/receiving.
    - [ ] **TCP (L4):** Connection state machine (SYN/ACK/FIN), sliding window, retransmission, congestion control.
- [ ] **Layer 4: Socket API**
    - [ ] **VFS Integration:** Map sockets to file descriptors.
    - [ ] **Syscalls:** Implement `socket`, `bind`, `connect`, `listen`, `accept`.
    - [ ] **I/O:** Implement `send`, `recv`, `sendto`, `recvfrom`.
    - [ ] **Multiplexing:** Implement `select`/`poll` for network sockets.
- [ ] **Supplemental: Userspace & Extensibility**
    - [ ] **NetUSE API:** Interface for running NIC drivers in userspace.
    - [ ] **Userspace Stacks:** API to allow userspace protocol drivers to attach to raw packets or sockets (TUN/TAP style).
- [ ] **Userland Tools**
    - [ ] Port `ping`.
    - [ ] Basic `netcat` implementation for testing.
    - [ ] `ifconfig` / `ip` utility.



### 9. Milestones

- [ ] **Bootable:** Kernel boots and reaches a functional user-space shell.
