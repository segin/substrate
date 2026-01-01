# TASKS.md

## TestUnix Implementation Roadmap

This document tracks the progress and remaining tasks for the TestUnix operating system.

### 1. Kernel Core (`sys/core`, `sys/kern`)
- [ ] **Memory Management:**
    - [ ] **Physical Memory Manager (PMM):**
        - [x] Basic Bitmap Allocator (`pmm.c`).
        - [ ] Parse Multiboot Memory Map (mmap) to support non-contiguous RAM.
        - [ ] Implement `pmm_alloc_contiguous` for specific DMA drivers.
    - [ ] **Memory Management (BSD/Mach Design):**
        - [ ] **Physical Memory (Machine Independent):**
            - [ ] `vm_page_t`: Core structure tracking state of every physical page.
            - [ ] **Page Queues:** Active/Inactive/Free lists for page replacement logic.
        - [ ] **PMAP Layer (Machine Dependent - i386):**
            - [x] `pmap_init`: Bootstrap hardware paging structures.
            - [ ] `pmap_enter`/`pmap_remove`: Low-level PTE manipulation.
            - [ ] `pmap_activate`: Context switch hook (CR3 loading).
            - [ ] **Recursive Paging:** Efficient Page Table mapping.
        - [ ] **VM Subsystem (Machine Independent):**
            - [ ] **VM Map:** `vm_map` structure representing an address space.
            - [ ] **VM Entries:** `vm_map_entry` representing regions (text, data, stack).
            - [ ] **VM Objects:** `vm_object` abstracting backing store (Anonymous, VNode/File).
            - [ ] **Fault Handler:** High-level `vm_fault` resolving faults against VM Objects.
    - [ ] **Kernel Allocator (UMA/Zone):**
        - [ ] **Zone Allocator:** Fixed-size object caching (equivalent to Slab, but BSD-style).
        - [ ] **Kmem:** General purpose variable-size allocator (power-of-two free lists).
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
    - [ ] **Keyboard (PS/2):**
        - [ ] **Controller:** Initialize PS/2 Controller (i8042), disable ports, perform self-test.
        - [ ] **Interrupts:** Handle IRQ1, read status/data ports.
        - [ ] **Scancodes:** Implement state machine for Set 1 (or 2) decoding.
        - [ ] **Keymap:** Map scancodes to ASCII/Unicode characters (US Layout).
        - [ ] **Buffer:** Implement a circular buffer for raw keystrokes.
    - [ ] **Mouse (PS/2):**
        - [ ] **Initialization:** Enable auxiliary device (IRQ12), set sample rate/resolution.
        - [ ] **Packet Parsing:** Decode 3-byte (or 4-byte) movement/button packets.
        - [ ] **Event Queue:** Push mouse events (dx, dy, buttons) to a system queue.
    - [ ] **Input Subsystem:**
        - [ ] Abstract `input_event` structure (type, code, value).
        - [ ] `/dev/input` interface for userspace access.
- [ ] **Video:**
    - [ ] **VESA/UEFI Framebuffer:**
        - [ ] Parse Multiboot2 Framebuffer tag or UEFI GOP.
        - [ ] Map framebuffer memory (requires VMM).
    - [ ] **Framebuffer Console:**
        - [ ] Import a bitmap font (e.g., PSF or raw bitmap).
        - [ ] Implement `fb_putc` with blitting capability.
        - [ ] Implement scrolling (hardware panning or software copy).
        - [ ] Hook into `vga_write` or create generic `console_write`.

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
    - [ ] **DevFS (`/dev`):**
        - [ ] **Device Registry:** Mechanism for drivers to register Character/Block devices.
        - [ ] **VFS Glue:** Auto-generate VFS nodes when registering devices.
        - [ ] **Nodes:** Support standard nodes (`null`, `zero`, `full`, `random`, `tty`).
    - [ ] **ProcFS (`/proc`):**
        - [ ] **Process Info:** Expose `cmdline`, `maps`, `status`, `fd` per PID.
        - [ ] **System Info:** Expose `cpuinfo`, `meminfo`, `uptime`.
        - [ ] **Dynamic generation:** Generate content on `read()` (virtual files).
    - [ ] **SysFS (`/sys`):**
        - [ ] **KObject Hierarchy:** Represent kernel objects (drivers, buses, devices).
        - [ ] **Attributes:** Map kernel variables to readable/writable files.
    - [ ] **FUSE (`/dev/fuse`):**
        - [ ] **Device Interface:** Implement `/dev/fuse` char device for control.
        - [ ] **Protocol:** Implement FUSE opcodes (`INIT`, `LOOKUP`, `READ`, `WRITE`).
        - [ ] **VFS Bridge:** Forward VFS calls to the FUSE device queue.

### 5. System Calls & Personalities
- [ ] **Mechanisms:**
    - [ ] **PTY Subsystem (Unix98):**
        - [ ] **Multiplexor:** Implement `/dev/ptmx` cloning device.
        - [ ] **DevPTS:** Implement `devpts` virtual filesystem for `/dev/pts`.
        - [ ] **API Support:** Support `grantpt`, `unlockpt`, `ptsname` (via ioctls).
        - [ ] **Line Discipline:** Implement termios processing (canonical mode, echo, signals).
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
