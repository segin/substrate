# Substrate Native ABI — amd64 (x86-64)

This document defines the binary interface between a 64-bit Substrate
kernel and **64-bit** Substrate native programs.  It is the counterpart of
[`abi-i386.md`](abi-i386.md).

The ABI is based on FreeBSD/amd64: the same calling convention, system-call
mechanism, error reporting, signal frame and data-structure layouts, with
the differences listed in [section 11](#11-differences-from-freebsdamd64).

**Status.**  Defined, not yet implemented.  For now the Substrate userland
stays 32-bit: the 64-bit kernel runs today's i386 binaries unchanged through
the compatibility path in [section 10](#10-32-bit-programs-on-the-64-bit-kernel).
A 64-bit userland built against this document comes later.  Nothing here
changes the i386 ABI.

---

## 1. Data Model

LP64, as the System V AMD64 psABI.

| Type | Size | Notes |
| :--- | :--- | :---- |
| `char` | 1 | signed |
| `short` | 2 | |
| `int` | 4 | |
| `long`, `long long` | 8 | |
| pointer, `size_t`, `ssize_t`, `ptrdiff_t`, `intptr_t` | 8 | |
| `register_t` | 8 | |
| `long double` | 16 | x87 80-bit extended, padded |
| `wchar_t` | 4 | signed |
| `off_t`, `time_t`, `suseconds_t`, `blkcnt_t` | 8 | signed |
| `dev_t`, `ino_t`, `nlink_t` | 8 | unsigned |
| `mode_t` | 4 | unsigned (FreeBSD: 2; see section 11) |
| `pid_t`, `uid_t`, `gid_t`, `id_t` | 4 | |
| `blksize_t`, `fflags_t`, `socklen_t` | 4 | |
| `clock_t` | 4 | signed |
| `sigset_t` | 16 | `struct { uint32_t __bits[4]; }` — 128 signals |

Byte order is little-endian.  Structures follow the psABI's natural
alignment rules; 8-byte types are 8-byte aligned.

## 2. Function Calling Convention

The System V AMD64 psABI, unchanged:

* Integer/pointer arguments in `%rdi %rsi %rdx %rcx %r8 %r9`, the rest on
  the stack; floating-point arguments in `%xmm0`–`%xmm7`.
* Return in `%rax` (and `%rdx` for 128-bit values), `%xmm0`/`%xmm1` for
  floating point.
* Callee-saved: `%rbx %rbp %r12 %r13 %r14 %r15`; `%rsp` is 16-byte
  aligned at every call instruction.
* A 128-byte red zone below `%rsp` belongs to the function; the kernel
  never writes there (signal frames are placed below it).

## 3. System Calls

### Mechanism

* **Instruction:** `syscall`.
* **Number:** `%rax`.
* **Arguments:** `%rdi %rsi %rdx %r10 %r8 %r9` — `%r10` in place of
  `%rcx`, which `syscall` overwrites.  A seventh and eighth argument are
  read from the user stack at `8(%rsp)` and `16(%rsp)`; the word at
  `0(%rsp)` is ignored (it holds the libc stub's return address).
* **Clobbered:** `%rcx` (return `%rip`) and `%r11` (`%rflags`), by the
  instruction itself.  Every other register is preserved, except the
  return registers below.

### Return Value

* **Success:** carry flag (`RFLAGS.CF`) clear; the result in `%rax`, and
  for the few calls with a second result (`pipe`, `fork`'s child flag)
  in `%rdx`.
* **Error:** carry flag set; the positive `errno` value in `%rax`.

This is FreeBSD's convention, and it differs from the i386 Substrate ABI,
which returns `-errno` in `%eax`.  The flag keeps every 64-bit result
value — including addresses in the upper half of the user range —
unambiguous.  The C library wrapper tests the carry flag, stores `%rax` in
`errno` and returns `-1`.

Inside the kernel, system-call handlers keep returning `-errno`; the amd64
return path converts.

### Numbering

The 64-bit native personality uses the **Substrate native system-call
numbers**, the same table as i386 (`<sys/syscall.h>`); only the argument
and structure conventions differ.  Number 0 is the indirect `syscall(2)`:
the real number is in `%rdi` and the arguments shift one register along.

### Restart

A call interrupted by a signal that should restart (`ERESTART`) is
re-executed by backing `%rip` up over the two-byte `syscall` instruction,
with `%rax` and the argument registers restored.  `EJUSTRETURN` (used by
`sigreturn`) leaves the registers exactly as the call established them.

## 4. Address Space

| Range | Use |
| :---- | :-- |
| `0x0000000000000000` – `0x0000000000000FFF` | never mapped (null page) |
| `0x0000000000400000` | default load address of non-PIE executables |
| ... | heap, `mmap` area, shared objects |
| below `0x00007FFFFFFFF000` | main thread's stack, growing down (`USRSTACK`) |
| `0x00007FFFFFFFF000` – `0x00007FFFFFFFFFFF` | shared page: the signal trampoline (read/execute) |
| `0x0000800000000000` – `0xFFFF7FFFFFFFFFFF` | non-canonical |
| `0xFFFF800000000000` and up | kernel; never accessible from user mode |

The highest user address is `0x00007FFFFFFFFFFF` (`VM_MAXUSER_ADDRESS` is
`0x0000800000000000`).  `mmap` without `MAP_FIXED` never returns an address
at or above `USRSTACK` minus the stack limit.

## 5. Data Structures

Sizes and offsets are for amd64; field names follow `abi-i386.md` where the
structure exists there.

### `struct timespec` (16 bytes) and `struct timeval` (16 bytes)

| Offset | Field | Type | Size |
| :----- | :---- | :--- | :--- |
| 0 | `tv_sec` | `time_t` | 8 |
| 8 | `tv_nsec` / `tv_usec` | `long` / `suseconds_t` | 8 |

### `struct stat` (224 bytes)

The FreeBSD/amd64 layout.

| Offset | Field | Type | Size |
| :----- | :---- | :--- | :--- |
| 0 | `st_dev` | `dev_t` | 8 |
| 8 | `st_ino` | `ino_t` | 8 |
| 16 | `st_nlink` | `nlink_t` | 8 |
| 24 | `st_mode` | `mode_t` | 4 |
| 28 | `st_uid` | `uid_t` | 4 |
| 32 | `st_gid` | `gid_t` | 4 |
| 36 | `st_padding1` | `int32_t` | 4 |
| 40 | `st_rdev` | `dev_t` | 8 |
| 48 | `st_atim` | `struct timespec` | 16 |
| 64 | `st_mtim` | `struct timespec` | 16 |
| 80 | `st_ctim` | `struct timespec` | 16 |
| 96 | `st_birthtim` | `struct timespec` | 16 |
| 112 | `st_size` | `off_t` | 8 |
| 120 | `st_blocks` | `blkcnt_t` | 8 |
| 128 | `st_blksize` | `blksize_t` | 4 |
| 132 | `st_flags` | `fflags_t` | 4 |
| 136 | `st_gen` | `uint64_t` | 8 |
| 144 | `st_spare[10]` | `uint64_t` | 80 |

`st_mode` occupies the four bytes FreeBSD splits into a 16-bit `st_mode`
and `st_bsdflags`.  `st_birthtim` is zero where the filesystem does not
record a creation time.  `st_atime` and friends are macros for the
`tv_sec` members, as on i386.

### `struct dirent` (variable; `getdents`)

| Offset | Field | Type | Size |
| :----- | :---- | :--- | :--- |
| 0 | `d_fileno` | `ino_t` | 8 |
| 8 | `d_off` | `off_t` | 8 |
| 16 | `d_reclen` | `uint16_t` | 2 |
| 18 | `d_type` | `uint8_t` | 1 |
| 19 | `d_pad0` | `uint8_t` | 1 |
| 20 | `d_namlen` | `uint16_t` | 2 |
| 22 | `d_pad1` | `uint16_t` | 2 |
| 24 | `d_name` | `char[]` | NUL-terminated |

Records are padded to a multiple of 8 bytes; `d_reclen` includes the
padding.  `d_ino` is a macro for `d_fileno`.

### `struct pollfd` (8 bytes)

Unchanged from i386: `int fd`, `short events`, `short revents`.

### `struct iovec` (16 bytes)

`void *iov_base` (0), `size_t iov_len` (8).

### `stack_t` (24 bytes; `sigaltstack`)

| Offset | Field | Type | Size |
| :----- | :---- | :--- | :--- |
| 0 | `ss_sp` | `void *` | 8 |
| 8 | `ss_size` | `size_t` | 8 |
| 16 | `ss_flags` | `int` | 4 |
| 20 | (padding) | | 4 |

The FreeBSD member order.  The i386 ABI orders the members `ss_sp`,
`ss_flags`, `ss_size`.

### `struct sigaction` (32 bytes)

| Offset | Field | Type | Size |
| :----- | :---- | :--- | :--- |
| 0 | `sa_handler` / `sa_sigaction` | pointer | 8 |
| 8 | `sa_flags` | `int` | 4 |
| 12 | `sa_mask` | `sigset_t` | 16 |
| 28 | (padding) | | 4 |

### `siginfo_t` (80 bytes)

| Offset | Field | Type | Size |
| :----- | :---- | :--- | :--- |
| 0 | `si_signo` | `int` | 4 |
| 4 | `si_errno` | `int` | 4 |
| 8 | `si_code` | `int` | 4 |
| 12 | `si_pid` | `pid_t` | 4 |
| 16 | `si_uid` | `uid_t` | 4 |
| 20 | `si_status` | `int` | 4 |
| 24 | `si_addr` | `void *` | 8 |
| 32 | `si_value` | `union sigval` | 8 |
| 40 | `_reason` | union | 40 |

### `struct thr_param` (104 bytes; `thr_new`)

| Offset | Field | Type | Size |
| :----- | :---- | :--- | :--- |
| 0 | `start_func` | pointer | 8 |
| 8 | `arg` | pointer | 8 |
| 16 | `stack_base` | pointer | 8 |
| 24 | `stack_size` | `size_t` | 8 |
| 32 | `tls_base` | pointer | 8 |
| 40 | `tls_size` | `size_t` | 8 |
| 48 | `child_tid` | `long *` | 8 |
| 56 | `parent_tid` | `long *` | 8 |
| 64 | `flags` | `int` | 4 |
| 68 | (padding) | | 4 |
| 72 | `rtp` | `struct rtprio *` | 8 |
| 80 | `spare[3]` | pointer | 24 |

`rtp` is accepted and ignored until real-time priorities exist; `spare`
must be zero.

### `struct rusage` (144 bytes)

`ru_utime`, `ru_stime` (`struct timeval`, 16 each), then fourteen `long`
counters (`ru_maxrss` … `ru_nivcsw`), as FreeBSD.

### `struct utsname` (1536 bytes)

Unchanged from i386: six 256-byte fields.  `machine` is `"amd64"`.

### Socket addresses

Byte-identical to i386: they contain no pointers or `long`s.

### Other structures

Every other structure a system call carries has the natural LP64 layout
of its declaration in the userland headers: a `long`, `size_t`, `time_t`
or pointer member is 8 bytes and aligned to 8.  The ones whose size
differs from i386:

| Structure | Header | Size | Calls |
| :-------- | :----- | :--- | :---- |
| `struct itimerspec` | `<time.h>` | 32 | `timer_settime`, `timer_gettime` |
| `struct rlimit` | `<sys/resource.h>` | 16 | `getrlimit`, `setrlimit` (`rlim_t` is 64 bits; `RLIM_INFINITY` all ones) |
| `struct statfs` | `<sys/statfs.h>` | 120 | `statfs`, `fstatfs` |
| `struct statvfs` | `<sys/statvfs.h>` | 120 | `statvfs`, `fstatvfs` |
| `struct sysinfo` | `<sys/sysinfo.h>` | 112 | `sysinfo` |
| `sys_procinfo_t` | `<sys/sysinfo.h>` | 92 | `sys_proc_info` |
| `sys_map_t` | `<sys/sysinfo.h>` | 280 | `sys_proc_maps` |
| `sys_swapinfo_t` | `<sys/sysinfo.h>` | 280 | `sys_vm_swap` |
| `struct mq_attr` | `<mqueue.h>` | 32 | `mq_open`, `mq_getattr`, `mq_setattr` |
| `struct sched_param` | `<sched.h>` | 48 | `sched_getparam`, `sched_setparam`, `sched_setscheduler` |
| `struct shmid_ds` | `<sys/shm.h>` | 80 | `shmctl` |
| `struct semid_ds` | `<sys/sem.h>` | 56 | `semctl` |
| `struct sigevent` | `<signal.h>` | 32 | `timer_create`, `mq_notify` |
| `struct msghdr` | `<sys/socket.h>` | 48 | `sendmsg`, `recvmsg` |
| `struct robust_list_head` | `<sys/futex.h>` | 24 | `set_robust_list`, `get_robust_list` |

A `size_t *` or `void **` result — `sysctl`'s `oldlenp`, the element
counts of the `sys_proc_*` and `sys_vm_*` listings, the status of
`thr_join` — is read and written as 8 bytes.  The exit value given to
`thr_exit` and the value given to `sigqueue` are carried at their full 64
bits.

`struct cmsghdr` is 12 bytes (`socklen_t cmsg_len`, two `int`s) and
`CMSG_ALIGN` rounds to 8, so `cmsg_len` is 16 plus the length of the data
(`CMSG_LEN`), the data starts 12 bytes into the record (`CMSG_DATA`), and
the next record starts at `cmsg_len` rounded up to 8.

`struct ifreq` is 40 bytes (the union widens with `struct ifmap`), so the
array `SIOCGIFCONF` fills has 40-byte elements and `struct ifconf` is 16
bytes; `struct fb_fix_screeninfo` is 80 bytes, `struct video_mode_query`
16, `struct input_event` 24 and `struct usbdevfs_ctrltransfer` 24.

## 6. Process Initialization

At the first instruction of the entry point (the executable's, or the
dynamic linker's when there is a `PT_INTERP`):

* `%rsp` points at `argc` and is 16-byte aligned.
* `%rdi` also holds that address (the convention FreeBSD's `crt1`
  relies on).
* `%rdx` is 0 — no exit-time cleanup function is passed in it.
* All other general registers are 0; `RFLAGS` has only `IF` set; the x87
  and SSE state is initialised (`FNINIT`, `MXCSR = 0x1F80`).

The stack, from `%rsp` upward:

| Content | Size |
| :------ | :--- |
| `argc` | 8 (a `long`) |
| `argv[0]` … `argv[argc-1]`, `NULL` | 8 each |
| `envp[0]` …, `NULL` | 8 each |
| auxiliary vector: `{ long a_type; long a_val; }` pairs, ending in `AT_NULL` | 16 each |
| padding, `AT_RANDOM` bytes (16), the platform string, argument and environment strings | |

The auxiliary vector uses the Substrate native `AT_*` numbers
(`sys/exec/formats/elf.h`), the same as i386: `AT_PHDR`, `AT_PHENT`,
`AT_PHNUM`, `AT_PAGESZ` (4096), `AT_BASE`, `AT_FLAGS`, `AT_ENTRY`, the
credential entries, `AT_SECURE`, `AT_RANDOM`, `AT_EXECFN` and
`AT_PLATFORM` (`"x86_64"`).  `AT_HWCAP` carries CPUID leaf 1 `%edx`.

## 7. Signals

### Delivery

1. The frame is placed on the alternate signal stack if one is in force
   and the handler asked for it, otherwise below the interrupted `%rsp`
   **minus the 128-byte red zone**.
2. The address is rounded down to 16 bytes and `struct sigframe` is
   written there.
3. The thread resumes in the signal trampoline in the shared page with
   `%rsp` pointing at the frame, `%rdi` = signal number, `%rsi` =
   `&sf_si` for an `SA_SIGINFO` handler (otherwise the signal code),
   `%rdx` = `&sf_uc`, and for a non-`SA_SIGINFO` handler `%rcx` = the
   fault address.  The trampoline calls the handler through
   `sf_ahu` (so the handler sees the psABI's call-time alignment), then
   calls `sigreturn(&sf_uc)`.

`RFLAGS.DF` is cleared for the handler; `TF` follows `sigreturn`'s
context.

### `struct sigframe`

| Offset | Field | Type | Size |
| :----- | :---- | :--- | :--- |
| 0 | `sf_ahu` | handler pointer | 8 |
| 16 | `sf_uc` | `ucontext_t` | 880 |
| 896 | `sf_si` | `siginfo_t` | 80 |

`sf_uc` is 16-byte aligned; bytes 8–15 are padding.

### `ucontext_t` (880 bytes)

| Offset | Field | Type | Size |
| :----- | :---- | :--- | :--- |
| 0 | `uc_sigmask` | `sigset_t` | 16 |
| 16 | `uc_mcontext` | `mcontext_t` | 800 |
| 816 | `uc_link` | pointer | 8 |
| 824 | `uc_stack` | `stack_t` | 24 |
| 848 | `uc_flags` | `int` | 4 |
| 852 | `__spare__[4]` | `int` | 16 |
| 868 | (padding) | | 12 |

### `mcontext_t` (800 bytes)

The FreeBSD/amd64 layout; each register is 8 bytes unless stated.

| Offset | Field | | Offset | Field |
| :----- | :---- |-| :----- | :---- |
| 0 | `mc_onstack` | | 136 | `mc_addr` (fault address) |
| 8 | `mc_rdi` | | 144 | `mc_flags` (4) |
| 16 | `mc_rsi` | | 148 | `mc_es` (2) |
| 24 | `mc_rdx` | | 150 | `mc_ds` (2) |
| 32 | `mc_rcx` | | 152 | `mc_err` |
| 40 | `mc_r8` | | 160 | `mc_rip` |
| 48 | `mc_r9` | | 168 | `mc_cs` |
| 56 | `mc_rax` | | 176 | `mc_rflags` |
| 64 | `mc_rbx` | | 184 | `mc_rsp` |
| 72 | `mc_rbp` | | 192 | `mc_ss` |
| 80 | `mc_r10` | | 200 | `mc_len` (= 800) |
| 88 | `mc_r11` | | 208 | `mc_fpformat` |
| 96 | `mc_r12` | | 216 | `mc_ownedfp` |
| 104 | `mc_r13` | | 224 | `mc_fpstate` (512, `FXSAVE` image, 16-aligned) |
| 112 | `mc_r14` | | 736 | `mc_fsbase` |
| 120 | `mc_r15` | | 744 | `mc_gsbase` |
| 128 | `mc_trapno` (4) | | 752 | `mc_xfpustate` |
| 132 | `mc_fs` (2) | | 760 | `mc_xfpustate_len` |
| 134 | `mc_gs` (2) | | 768 | `mc_spare[4]` |

`sigreturn` validates the context: `mc_len` must be 800, `mc_cs` and
`mc_ss` must be the 64-bit user selectors, `mc_rflags` may change only the
user-modifiable bits, and `mc_rip` must be canonical.

## 8. Thread-Local Storage

Variant II, as the i386 ABI, but based on `%fs` (the i386 ABI uses `%gs`):
`%fs:0` holds the thread pointer itself, and static TLS blocks sit below
it.  The base is set with `sysarch(2)`:

| Operation | Number | Argument |
| :-------- | :----- | :------- |
| `AMD64_GET_FSBASE` | 128 | `void **` |
| `AMD64_SET_FSBASE` | 129 | `void **` |
| `AMD64_GET_GSBASE` | 130 | `void **` |
| `AMD64_SET_GSBASE` | 131 | `void **` |

`thr_new` sets the new thread's `%fs` base from `tls_base`.  The
`RDFSBASE`/`WRFSBASE` instructions are not enabled; the bases are part of
the context saved on every switch and in `mcontext_t`.

## 9. Executables and Linking

* **ELF:** `ELFCLASS64`, `ELFDATA2LSB`, `e_machine = EM_X86_64` (62),
  branded `EI_OSABI = ELFOSABI_SUBSTRATE` (64), as on i386.
* **Toolchain:** target `x86_64-unknown-substrate`, BFD output vector
  `elf64-x86-64-substrate`; non-PIE text at `0x400000`, maximum page size
  `0x200000`.
* **Libraries:** the two architectures' libraries sit side by side.  The
  32-bit ones keep `/lib` and `/usr/lib`; the 64-bit ones go in `/lib64`
  and `/usr/lib64`.  `make -C lib ARCH=x86_64` builds the 64-bit set from
  the same sources, under each library's `obj-x86_64/`, and installs it
  there (`make -C lib both` / `install-both` for the pair).  The startup
  and system-call code is per architecture (`lib/c/arch/<arch>/`,
  `lib/sys/arch/<arch>/`).
* **Dynamic linker:** `/sbin/ld.so` is the 32-bit linker and stays so.
  The 64-bit linker is `/sbin/ld64.so` (the `PT_INTERP` of a 64-bit
  dynamic executable), searching `/lib64`, `/usr/lib64` and
  `/usr/local/lib64`, then the directories of `/etc/ld.so.conf`; it
  skips any object that is not `ELFCLASS64`/`EM_X86_64`.  It is built
  from the 32-bit linker's sources (`make -C sbin/ld.so ARCH=x86_64`;
  `docs/design/ld.so-design.md`, section 22).  The kernel maps it at
  `0x40000000` and enters it with the stack of section 6; it relocates
  itself, loads and relocates the program's libraries, sets up TLS and
  jumps to `AT_ENTRY` with `%rsp` and `%rdi` as the kernel passed them
  and `%rdx` = 0.  Programs are linked dynamically (`DYNAMIC = 1` in the
  program's Makefile) or statically.
* **Relocations:** the psABI's `R_X86_64_*` set, in `DT_RELA` tables
  (`DT_PLTREL` = `DT_RELA`).  `ld64.so` supports `RELATIVE`, `GLOB_DAT`,
  `JUMP_SLOT`, `64`, `PC32`, `COPY`, `TPOFF64`, `DTPMOD64` and
  `DTPOFF64`, binding everything at load time.  `IRELATIVE` is not
  supported yet: an object carrying one is refused with a diagnostic.
* **Dynamic TLS:** the thread control block at `%fs:0` is 64 bytes:
  the thread pointer at `%fs:0`, the DTV pointer at `%fs:8`, the rest
  spare.  `DTV[m]` is the address of module `m`'s block in the thread;
  `__tls_get_addr` takes a pointer to `{ module, offset }`, two 8-byte
  words, in `%rdi`.  `ld64.so` sets the initial thread's `%fs` base with
  the native `SYS_SET_GSBASE` call (which, for a 64-bit process, sets
  `%fs`), and hands libpthread a block of the same layout for each new
  thread.

## 10. 32-bit Programs on the 64-bit Kernel

The first purpose of the 64-bit kernel is to run the existing 32-bit
Substrate userland unchanged.

* An `ELFCLASS32`, `EM_386` executable runs in IA-32e **compatibility
  mode**: a 32-bit user code segment, 32-bit data segments, and its
  address space confined below 4 GiB.  Initially the layout is exactly the
  i386 kernel's (user space below `0xC0000000`), so no existing binary sees
  a difference; it may grow to the full 4 GiB later.
* It keeps the **i386 ABI** of [`abi-i386.md`](abi-i386.md) in every
  respect: `int $0x80`, the number in `%eax`, arguments on the stack,
  `-errno` in `%eax`, the i386 structure layouts, the i386 signal frame,
  and TLS through `%gs` and `sysarch(I386_SET_GSBASE)`.
* The kernel keeps the i386 forms at the boundary, in a compat32 layer
  (detail in [`arch_x86_64_core.md`](arch_x86_64_core.md#compat32)):
  * system-call arguments arrive as 32-bit words, zero-extended; a
    handler declares the process's `long`, `size_t *` or pointer slot with
    the `<sys/abi32.h>` types, and takes a 64-bit value as its two words;
  * structures without pointers (`stat`, `dirent`, `timespec` and
    everything that embeds it, `rusage`, `statfs`, …) are declared in the
    i386 layout on both kernels, with the `<sys/abi32.h>` field types, and
    pinned to their i386 size;
  * structures with pointers (`iovec`, `msghdr`, `stack_t`, `sigaction`,
    `siginfo_t`, `sigevent`, `thr_param`, and ioctl structures such as
    `ifreq`) have an i386-layout twin in `<sys/compat32.h>` and are
    converted at the copyin/copyout.

  The signal frame for a 32-bit thread is the i386 one.
* System-call dispatch is keyed on both the personality and the process's
  bitness: (native, i386), (native, amd64), (Linux, i386), (Linux, amd64),
  (FreeBSD, i386), … .  The foreign personalities' i386 ABIs run in
  compatibility mode the same way; their amd64 ABIs come with the 64-bit
  userland or not at all.

The 64-bit kernel's own memory layout (direct map, kernel base, page
tables) is internal and not part of this ABI; see
[`arch_x86_64_core.md`](arch_x86_64_core.md).

## 11. Differences from FreeBSD/amd64

| Area | FreeBSD/amd64 | Substrate/amd64 |
| :--- | :------------ | :-------------- |
| System-call numbers | FreeBSD's table | Substrate native table (shared with i386) |
| `mode_t` | 16 bits, plus `st_bsdflags` in `struct stat` | 32 bits, filling both |
| Auxiliary vector tags | FreeBSD's `AT_*` | Substrate native `AT_*` (as i386) |
| `struct utsname` | 5 fields of 256 | 6 fields of 256 (adds `domainname`) |
| ELF branding | `ELFOSABI_FREEBSD` (9) | `ELFOSABI_SUBSTRATE` (64) |
| Dynamic linker | `/libexec/ld-elf.so.1` | `/sbin/ld.so` |
| Shared page | timekeeping and signal code | signal trampoline only (for now) |

Everything else in this document is intended to match FreeBSD/amd64 byte
for byte, so FreeBSD's amd64 headers and libc sources are the reference
where this document is silent.
