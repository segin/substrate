# x86_64 Core Architecture

## Scope

This document covers the 64-bit kernel port: how it is built from the
shared source tree, how it boots, the long-mode CPU tables (GDT, TSS,
IDT), its internal memory layout, the compat32 layer through which it runs
the existing 32-bit userland, and the roadmap.  The binary interface it
offers programs is specified separately: [`abi-amd64.md`](abi-amd64.md)
for native 64-bit programs, and its section 10 for the 32-bit userland,
which is what the port runs today.

The amd64 kernel is added alongside the i386 one, not substituted for it.
Both build from the same tree, and the i386 kernel remains the primary
target.

## Building

```
make -C sys                 # i386 kernel (unchanged): sys/kernel.bin, ...
make -C sys ARCH=x86_64     # 64-bit kernel
make -C sys ARCH=x86_64 clean
```

`sys/Makefile` hands `ARCH=x86_64` to `sys/arch/x86_64/Makefile`.  That
Makefile compiles the machine-dependent sources (`ARCH_SRCS`) itself and
runs each machine-independent subsystem's own Makefile (`kern`, `pm`, `vm`,
`vfs`, `fs`, `net`, `exec`, the driver directories) with `ARCH=x86_64`.
`sys/Makefile.inc` then sets the object directory `$(O)` to
`sys/arch/x86_64/obj/<subdir>/` and the 64-bit compiler flags, which
define `SUBSTRATE_ARCH_X86_64`.  The i386 build compiles in place, so the
two never share an object file and either can be built at any time without
cleaning the other.

The kernel links in two passes, as on i386: the first link's symbols
become the kernel's own symbol table (`kern/ksyms.c`, for stack traces),
linked into the second.  Outputs:

| File | Format | Loaded by |
| :--- | :----- | :-------- |
| `sys/kernel-x86_64.elf` | ELF64, with symbols | GRUB `multiboot2`; gdb; `addr2line` |
| `sys/kernel-x86_64.bin` | flat image with a multiboot 1 a.out-kludge header | QEMU `-kernel`; GRUB `multiboot` |

The ELF file is installed on the root filesystem as `/vmunix64`, next to
the i386 `/vmunix`, and GRUB carries x86_64 copies of the first two boot
entries, which load it with `multiboot2` on BIOS and UEFI alike.  Its
multiboot 2 header names the physical entry point (the ELF entry is the
higher-half link address) and requests the same information tags and
framebuffer as the i386 kernel.  The flat image exists for QEMU's
`-kernel`, which refuses an ELF64.

Code generation: `-m64 -mcmodel=kernel -mno-red-zone -mgeneral-regs-only`.
The kernel is linked in the top 2 GiB (`kernel` code model); the red zone
is unusable because interrupts arrive on the current stack in ring 0; and
the compiler never uses an SSE register, so the lazy FPU switch only has
user state to manage.

### Machine headers

Shared code reaches machine-dependent definitions through
`<machine/X.h>` (`sys/include/machine/`), which selects the arch header by
`SUBSTRATE_ARCH_X86_64`:

* per-arch: `pmm`, `pmap`, `idt`, `gdt`, `percpu`, `smp`, `early_boot`,
  `signal_arch`, `efi`, `vmparam`;
* shared, in `sys/arch/x86-common/`: `cpu`, `intr`, `pci`, `fpu`;
* always the i386 one: `syscall` -- the user system-call ABI is i386's on
  both kernels.

`vmparam.h` is the arch's address-space contract for shared code:
`KERN_BASE`, `KERNEL_VA_START`, `USER32_VA_END`, `P2V()`/`V2P()`, the
`IOREMAP_BASE`..`IOREMAP_LIMIT` window and `MMIO_KVA()`.

`sys/arch/x86-common/` holds what both kernels compile unchanged: the trap
dispatcher (`trap.c`), CPU identification, the PCI and PIC code, the
physical memory manager, per-CPU data, the FPU, the local and I/O APICs
and the RTC.  The i386 user-ABI sources -- `arch/i386/{syscall,signal,
sysarch,sig_trampoline,ldt}.c` -- are compiled into the 64-bit kernel too:
they are what a 32-bit process talks to (see [Compat32](#compat32)).

## Booting

`sys/arch/x86_64/boot/boot.S` carries both a multiboot 1 and a multiboot 2
header.  Multiboot 1 loaders refuse an ELF64 file, so the multiboot 1
header uses the a.out kludge to describe the flat image by physical
address (load 1 MiB, `load_end_addr` the end of `.data`, `bss_end_addr`
the end of `.bss`).  Multiboot 2 loaders load the ELF file directly.

Under QEMU (the root on a virtio disk, as `run-networking.sh --virtio`
uses):

```
qemu-system-x86_64 -accel kvm -m 512M -kernel sys/kernel-x86_64.bin \
    -serial stdio -display none \
    -drive file=rootfs.img,format=raw,if=virtio \
    -append "serial_debug root=LABEL=sub-root"
```

The loader enters `_start` in 32-bit protected mode with paging off.
`_start`:

1. keeps the loader's magic and information pointer on the boot stack,
   since the setup needs every register;
2. checks for long mode (and notes NX support);
3. builds the boot page tables in `.bss.boot` (2 MiB pages):
   * an identity map of the first 1 GiB, needed only across the jump to
     the higher half;
   * the kernel window, `KERNEL_VMA` + 1 GiB, onto physical 0 .. 1 GiB;
   * the direct map, `DMAP_BASE` + 4 GiB, onto physical 0 .. 4 GiB;
4. enables `CR4.PAE|PGE` and `EFER.LME` (`|NXE` when supported), then
   paging with `CR0.WP` -- so a kernel store into a read-only user page
   faults into the copy-on-write path rather than writing the shared
   frame -- and far-jumps into 64-bit code;
5. jumps to the link address (`higher_half`), switches to the boot stack's
   virtual address and calls `kmain64(magic, info)`.

`kmain64()` (`kmain.c`) brings up the early serial console, drops the
identity window and calls the machine-independent `kmain()` with the
loader's information pointer translated into the direct map.  From there
the boot is the i386 one: memory, devices, the root mount, then
`/sbin/init`.

## Memory Layout

`sys/arch/x86_64/layout.h` and `vmparam.h` hold the constants;
`linker.ld` must agree with `KERNEL_VMA`.

| Region | Address | Notes |
| :----- | :------ | :---- |
| user space | `0` .. `0xBFFFFFFF` | `USER32_VA_END`: the i386 kernel's user layout, so no 32-bit binary sees a difference |
| direct map | `0xFFFFF80000000000` + 4 GiB | `DMAP_BASE` = `KERN_BASE`; `P2V()` lands here.  2 MiB pages |
| kernel image | `0xFFFFFFFF80000000` + 1 MiB | `KERNEL_VMA`; loaded at physical 1 MiB.  `V2P()` accepts either view |
| MMIO window | `0xFFFFFFFFC0000000` .. `0xFFFFFFFFC1000000` | `IOREMAP_BASE`..`IOREMAP_LIMIT` |

The direct map is placed where FreeBSD/amd64 puts it.  Physical memory is
managed below `PMM_PHYS_RAM_CAP` (3 GiB), as on i386, so `phys_addr_t`
stays 32 bits and every managed frame is inside the direct map.  The
`pmm_alloc_block()` contract of the project notes holds on both kernels:
it returns the frame's `P2V()` address.

The pmap (`pmap.c`) is four-level.  It reaches every page-table page
through the direct map rather than a recursive slot, so it can edit any
address space, current or not.  The kernel's upper half is shared by every
pmap; the page holding the i386 signal trampoline (`0xFE000000`) is
installed in each user pmap and skipped when one is forked or destroyed.

None of this is visible to programs.

## GDT/TSS Contract

| Selector | Descriptor |
| :------- | :--------- |
| `0x08` | `SEL_KCODE`: kernel code, 64-bit (L=1) |
| `0x10` | `SEL_KDATA`: kernel data |
| `0x18` | `SEL_UCODE32`: user code, 32-bit (D=1, L=0): compatibility mode |
| `0x20` | `SEL_UDATA`: user data, both bitnesses |
| `0x28` | `SEL_UCODE`: user code, 64-bit (L=1) |
| `0x30` .. `0x40` | `SEL_TLS`: the three i386 TLS slots (`%gs` = `0x33`) |
| `0x48` | `SEL_TSS`: 16-byte TSS descriptor (two slots) |
| `0x58` | `SEL_LDT`: 16-byte LDT descriptor (two slots) |

The order up to `SEL_UCODE` is fixed by `SYSCALL`/`SYSRET`: `MSR_STAR`
holds `SEL_KCODE` in bits 47:32 and `SEL_UCODE32 | 3` in bits 63:48, so a
64-bit `SYSRET` loads CS = `SEL_UCODE32 + 16` (`SEL_UCODE`) and SS =
`SEL_UCODE32 + 8` (`SEL_UDATA`), the arrangement FreeBSD/amd64 uses.  The
TLS slots sit at the same indices as on i386, so `sysarch(I386_SET_GSBASE)`
and `set_thread_area` hand a 32-bit process the selectors it expects.

The TSS provides `rsp0` (set on every switch, `arch_set_kernel_stack()`)
and IST stacks for NMI (`ist1`), double fault (`ist2`) and machine check
(`ist3`).  No I/O bitmap is exposed.

## IDT Contract

256 long-mode gates: the CPU exceptions (`0..31`), the PIC/APIC interrupt
vectors, and `int $0x80`, user-callable, as the system-call gate for
32-bit processes.  NMI, double fault and machine check run on their IST
stacks.

The stubs in `isr.S` build a `registers_t` frame whose slots are 64-bit,
with the i386 register names available as unions over their low halves,
so the shared trap, signal and system-call code reads `regs->eax`,
`regs->eip`, … on either kernel (`TF_PC()` / `TF_SET_PC()` for the
instruction pointer).  The data-segment selectors are saved and restored
only on a return to user mode.  `isr_handler()` (`x86-common/trap.c`)
dispatches exactly as on i386.

## Compat32

A process is a 32-bit i386 one on both kernels: the 64-bit kernel loads
`ELFCLASS32`/`EM_386` images and runs them on `SEL_UCODE32`.  Everything
the process exchanges with the kernel therefore keeps its i386 form.  Three
mechanisms make that hold without a separate compat system-call table:

1. **Value structures in the i386 layout.**  Structures with no pointers
   -- `struct stat`, `timespec`, `rusage`, `statfs`, the personalities'
   `stat`/`dirent`/`rusage` variants, … -- declare their fields with the
   `<sys/abi32.h>` types: `abi_long_t`/`abi_ulong_t` (32 bits),
   `abi_size_t`, and `abi_int64_t`/`abi_uint64_t` (64 bits, 4-byte
   aligned).  On the i386 kernel these are the native types, so nothing
   changes there; on the x86_64 kernel they reproduce the i386 layout.
   Each such structure is pinned with `ABI32_ASSERT_SIZE(type, i386_size)`.

2. **User twins for pointer-carrying structures.**  Where the kernel keeps
   real pointers -- `sigaction`, `stack_t`, `siginfo_t`, `sigevent`,
   `thr_param`, `iovec`, `msghdr`, the robust futex list, and the ioctl
   structures whose declarations are shared with userland (`ifreq`,
   `ifconf`, `fb_fix_screeninfo`, `video_mode_query`, `input_event`,
   `usbdevfs_ctrltransfer`) -- `<sys/compat32.h>` declares the process's
   layout (`uptr32_t` for a pointer) and `kern/compat32.c` converts at the
   copyin/copyout.  The i386 signal frame (`ucontext_t`, `siginfo_frame`)
   is built from the same twins.

3. **System-call arguments in the process's types.**  The dispatcher
   (`arch/i386/syscall.c`) passes every handler eight argument words,
   zero-extended to pointer width.  A handler parameter that is the
   process's `long`, `size_t *` or pointer slot is declared `abi_long_t`,
   `abi_size_t *` or `uptr32_t *`; a 64-bit value is taken as its two
   argument words (`off_lo`, `off_hi`), as i386 cdecl lays it out.

Personalities (Linux, FreeBSD, NetBSD, …) are i386 ABIs and go through the
same layer.

## Roadmap

| Milestone | Content | State |
| :-------- | :------ | :---- |
| M0 | Same-tree build; multiboot 1/2 boot to the higher half; direct map; GDT with compat segments, TSS, IDT; exception round trip | done |
| M1 | Machine-independent kernel built LP64-clean into the per-arch object directory | done |
| M2 | Physical memory manager and 4-level pmap over the direct map; kernel heap | done |
| M3 | Compat32: the existing i386 userland up to a shell | done: init, `rc.d` and the login services run; the torture suite matches the i386 kernel's results |
| M4 | Native amd64 userland per abi-amd64.md | started: static 64-bit programs load and run (see below) |

Open items on the 64-bit kernel:

* one CPU only (`smp.c` brings up the BSP);
* no EFI boot path (`efi_runtime.c` is stubbed) and no vm86
  (`vm86.c` returns `ENOSYS`);

## Native 64-bit processes (M4, first stage)

The 64-bit libraries build beside the 32-bit ones (`make -C lib
ARCH=x86_64`, installed in `/lib64` and `/usr/lib64`), and the kernel runs
a statically linked 64-bit program:

* **Loading.** `exec/formats/elf.c` reads an `ELFCLASS64`/`EM_X86_64`
  image into the 32-bit ELF structures the loader works on -- user space
  ends below 4 GiB, so everything fits, and what does not is refused --
  and marks the process `BITNESS_64`.  The initial stack is laid out as
  for i386 and its word area (argc, argv, envp, auxv) widened to 8-byte
  words.  Entry is `jump_to_userspace64()`: `SEL_UCODE`, `%rsp` and `%rdi`
  the stack.  A 64-bit image with a `PT_INTERP` gets its interpreter --
  `/sbin/ld64.so`, itself a 64-bit `ET_DYN` -- mapped at `0x40000000` as
  a 32-bit image gets `/sbin/ld.so`, with `AT_BASE`, `AT_PHDR`,
  `AT_PHENT`, `AT_PHNUM` and `AT_ENTRY` in the auxiliary vector, and is
  entered at the interpreter's entry point.
* **Dynamic linking.** `/sbin/ld64.so` is the 32-bit linker's source
  built for amd64 (`make -C sbin/ld.so ARCH=x86_64`;
  `docs/design/ld.so-design.md`, section 22): RELA relocations, `%fs`
  thread pointer, libraries from `/lib64` and `/usr/lib64`.  Programs
  whose Makefile sets `DYNAMIC = 1` are linked against the 64-bit shared
  objects when built with `ARCH=x86_64`; `tests/sbin/ld64/` exercises
  libc, libm, libpthread (per-thread TLS from the linker) and `dlopen`.
* **System calls.** `syscall_msr_init()` enables `SYSCALL`
  (`EFER.SCE`, `STAR`, `LSTAR`, `FMASK`).  `syscall_entry64` (isr.S)
  switches to the thread's kernel stack, builds the frame an interrupt
  gate would have pushed and joins the `int $0x80` path, returning by
  `IRETQ`.  The dispatcher takes a 64-bit frame's arguments from
  `%rdi %rsi %rdx %r10 %r8 %r9` (and two from the stack) and converts the
  result to the carry-flag convention.  `execve` reads a 64-bit caller's
  argv and envp as 8-byte pointers.

* **Structures.**  A 64-bit process uses the LP64 layouts of
  abi-amd64.md, while the kernel's own structures keep the i386 layout
  (see Compat32).  `<sys/amd64_abi.h>` declares the 64-bit side, pinned
  with `ABI64_ASSERT_SIZE`, and the conversion happens in one of two
  places:
  * *value structures* -- `stat`, `timespec`, `rusage`, the `getdents`
    record, the 16-byte `sigset_t`, `statfs`, `statvfs`, `sysinfo`,
    `sys_procinfo_t`, `rlimit`, `itimerspec`, `mq_attr`, `shmid_ds`,
    `semid_ds` -- in a wrapper the dispatcher picks for 64-bit frames
    (`exec/perso/perso_native64.c`, `native_amd64_syscall()`), which runs
    the kernel-internal form of the call and converts at the boundary.
    The wrappers for `thr_exit` and `sigqueue` exist for another reason:
    their argument is pointer-sized data rather than an address, so they
    take all 64 bits of it from the saved frame instead of the 32-bit
    argument word;
  * *pointer-carrying structures* -- `iovec`, `sigaction`, `stack_t`,
    `siginfo_t`, `sigevent`, `msghdr` -- in the converters the 32-bit
    path already uses (`kern/compat32.c`), which read the calling
    process's layout (`proc_abi_is_amd64()`);
  * *what a handler copies in the middle of its work* -- a `size_t` or
    pointer result (`sysctl`'s `oldlenp`, the counts of the `sys_proc_*`
    and `sys_vm_*` listings, `thr_join`'s status), a `struct timespec`
    timeout (`sigtimedwait`, `thr_suspend`, `ksem_timedwait`,
    `mq_timedsend`/`mq_timedreceive`, `futex`, `sched_rr_get_interval`),
    `struct sched_param`, and the elements of a `sys_map_t` or
    `sys_swapinfo_t` array -- in helpers of the same file
    (`usize_copyin()`, `uptr_copyout()`, `timespec_copyin()`, ...), which
    the handlers call instead of a bare `copyin`/`copyout`.

  Three things follow the process without a structure of their own.
  Control messages (`sys/net/af_unix.c`): the process's `CMSG_ALIGN`
  rounds to its `size_t`, so for a 64-bit process `cmsg_len` counts a
  16-byte header and records advance in steps of 8, while the data still
  starts 12 bytes in, where its `CMSG_DATA` looks.  Robust futex lists
  (`kern/futex.c`): the head is 24 bytes with 64-bit links, and the
  length registered by `set_robust_list` selects the walk.  Device
  records: `SIOCGIFCONF`, `FBIOGET_FSCREENINFO`, `FBIOGET_VIDEO_MODES`,
  `USBDEVFS_CONTROL` and the events read from `/dev/input/event0` are
  produced in the caller's layout by the drivers themselves.
* **Signals.**  `sendsig()` hands a 64-bit frame to `sendsig_amd64()`
  (`arch/x86_64/signal64.c`): `struct sigframe` below the red zone, the
  FreeBSD `mcontext` with the FXSAVE image, and a 64-bit trampoline at
  `0xFE000060` that calls the handler and then `sigreturn(&sf_uc)`.

What a 64-bit process does not have yet:

* `ptrace`: its requests exchange the i386 register set and 32-bit words
  only;
* **threads and TLS**: `%fs` base handling, `thr_new`;
* in the dynamic linker: `R_X86_64_IRELATIVE` (indirect functions) and
  lazy binding -- as in the 32-bit linker, everything is bound at load
  time;
* a 64-bit `libgcc_s.so.1` and C++ runtime: the 64-bit libraries are
  built with the host compiler, and `libm.so.0` links what it needs from
  `libgcc.a`.

`make -C bin/sh sh64` builds the in-tree shell as a static 64-bit program;
`make -C bin/sh ARCH=x86_64` builds it dynamically linked.

## Verification

* Build: `make -C sys ARCH=x86_64` with `-Werror`, and the i386 build,
  after every shared change; CI builds both (`.github/workflows/ci.yml`,
  job `kernel-x86_64`) and checks that the assembled image carries
  `/vmunix64`.
* Boot: the QEMU command above reaches `/etc/rc.d` and its services with
  the unchanged image.
* Torture suite (`tests/lib/**/torture_*`): run under both kernels on
  private copies of `rootfs.img`; the outputs must match line for line.
* Layout: `pahole -s` over `sys/kernel.bin` and `sys/kernel-x86_64.elf`
  lists every structure whose size differs; each one that reaches a
  process must be pinned and made equal.

Host tests (`tests/sys`): `host_test_x86_64_asm` (the arch assembly
assembles under `cc -m64` and exports its entry symbols),
`host_test_x86_64_gdt`, `host_test_x86_64_idt` and
`host_test_x86_64_syscall` (the GDT/TSS, IDT and syscall-MSR contracts).
