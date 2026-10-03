# x86_64 Core Architecture

## Scope

This document covers the 64-bit kernel port: how it is built from the
shared source tree, how it boots, the core long-mode CPU tables (GDT, TSS,
IDT, syscall MSRs), its internal memory layout, and the bring-up roadmap.
The binary interface it offers programs is specified separately:
[`abi-amd64.md`](abi-amd64.md) for native 64-bit programs, and its section
10 for the existing 32-bit userland, which is what the port runs first.

The amd64 kernel is added alongside the i386 one, not substituted for it.
Both build from the same tree, and the i386 kernel remains the primary
target until the 64-bit one runs the userland.

## Building

```
make -C sys                 # i386 kernel (unchanged): sys/kernel.bin, ...
make -C sys ARCH=x86_64     # 64-bit kernel
make -C sys ARCH=x86_64 clean
```

`sys/Makefile` hands `ARCH=x86_64` to `sys/arch/x86_64/Makefile`, which
compiles every object, the shared machine-independent sources included,
into `sys/arch/x86_64/obj/`.  The i386 build compiles in place, so the two
never share an object file and either can be built at any time without
cleaning the other.  Outputs:

| File | Format | Loaded by |
| :--- | :----- | :-------- |
| `sys/kernel-x86_64.elf` | ELF64, with symbols | GRUB `multiboot2`; gdb |
| `sys/kernel-x86_64.bin` | flat image with a multiboot 1 a.out-kludge header | QEMU `-kernel`; GRUB `multiboot` |

The flat image is what gets installed on the root filesystem, as
`/vmunix64`, next to the i386 `/vmunix`.

Code generation: `-m64 -mcmodel=kernel -mno-red-zone -mgeneral-regs-only`.
The kernel is linked in the top 2 GiB (`kernel` code model); the red zone
is unusable because interrupts arrive on the current stack in ring 0; and
no SSE register is touched until the kernel saves FPU state.  Sections are
garbage-collected (`-ffunction-sections`, `--gc-sections`), so a shared
source contributes only what the 64-bit kernel references.

Machine-independent sources join `MI_SRCS` in the arch Makefile as they
are made LP64-clean.  So far that is `sys/lib/string.c`.

## Booting

`sys/arch/x86_64/boot/boot.S` carries both a multiboot 1 and a multiboot 2
header.  Multiboot 1 loaders refuse an ELF64 file, so the multiboot 1
header uses the a.out kludge to describe the flat image by physical
address (load 1 MiB, `load_end_addr` the end of `.data`, `bss_end_addr`
the end of `.bss`).  Multiboot 2 loaders load the ELF file directly.  The
`/vmunix64` GRUB entry is therefore `multiboot /vmunix64`.

Under QEMU:

```
qemu-system-x86_64 -kernel sys/kernel-x86_64.bin -serial stdio \
    -display none -m 512
```

The early console is COM1 at 115200 8N1 (`cons.c`).

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
   paging, and far-jumps into 64-bit code;
5. jumps to the link address (`higher_half`), switches to the boot stack's
   virtual address and calls `kmain64(magic, info)`.

`kmain64()` (`kmain.c`) brings up the early console, reports the CPU and
the loader's memory map (read through the direct map, for either
multiboot protocol), installs the kernel's own GDT and TSS and the IDT,
drops the identity window, and checks the exception path with a
breakpoint.  It ends with a `Result: PASS (x86_64 milestone 0)` line.

## Memory Layout

`sys/arch/x86_64/layout.h` holds the constants; `linker.ld` must agree
with `KERNEL_VMA`.

| Region | Address | Notes |
| :----- | :------ | :---- |
| user space | `0` .. `0x00007FFFFFFFFFFF` | `VM_MAXUSER_ADDRESS`; 32-bit processes stay below 4 GiB |
| direct map | `0xFFFFF80000000000` | `DMAP_BASE`, PML4 slot 496; all physical memory (the boot tables map 4 GiB) |
| kernel image | `0xFFFFFFFF80000000` + 1 MiB | `KERNEL_VMA`; loaded at physical 1 MiB |

The direct map is placed where FreeBSD/amd64 puts it.  `phys_to_dmap()`,
`dmap_to_phys()` and `kva_to_phys()` convert between the views.  None of
this is visible to programs.

## GDT/TSS Contract

The selector order is fixed by `SYSCALL`/`SYSRET` and by the need for a
32-bit user code segment for the i386 userland:

| Selector | Descriptor |
| :------- | :--------- |
| `0x08` | `SEL_KCODE`: kernel code, 64-bit (L=1) |
| `0x10` | `SEL_KDATA`: kernel data |
| `0x18` | `SEL_UCODE32`: user code, 32-bit (D=1, L=0): compatibility mode |
| `0x20` | `SEL_UDATA`: user data, both bitnesses |
| `0x28` | `SEL_UCODE`: user code, 64-bit (L=1) |
| `0x30` | `SEL_TSS`: 16-byte TSS descriptor (two slots) |

`MSR_STAR` holds `SEL_KCODE` in bits 47:32 and `SEL_UCODE32 | 3` in bits
63:48.  `SYSCALL` loads CS = `SEL_KCODE`, SS = `SEL_KCODE + 8`.  A 64-bit
`SYSRET` loads CS = `SEL_UCODE32 + 16` (`SEL_UCODE`) and SS =
`SEL_UCODE32 + 8` (`SEL_UDATA`); a 32-bit `SYSRET` loads CS =
`SEL_UCODE32`.  This is the same arrangement FreeBSD/amd64 uses.

`gdt_init()` reloads CS with a far return and the data segments from
`SEL_KDATA`, then loads the task register.

Per-CPU state:
- one GDT per CPU
- one `tss64` per CPU
- one IST stack each for NMI, double fault, and machine check per CPU

The TSS uses:
- `rsp0` for privilege transitions into the kernel
- `ist1` for NMI
- `ist2` for double fault
- `ist3` for machine check
- `iopb_offset = sizeof(struct tss64)` (no I/O bitmap exposed yet)

## IDT Contract

The x86_64 IDT contains 256 entries using 16-byte long-mode descriptors.
Initialized vectors:
- CPU exceptions `0..31`
- legacy IRQ window `32..47`
- `int $0x80`, user-callable: the i386 system-call gate for 32-bit
  processes (abi-amd64.md, section 10)

IST usage is explicit:
- NMI uses `IST_NMI`
- double fault uses `IST_DF`
- machine check uses `IST_MC`

The common stubs in `isr.S` pass the frame in `%rdi`, align the stack to
16 bytes for the C handler, and restore it from the callee-saved `%rbx`.
`trap.c` counts breakpoints and returns from them; any other exception
prints the vector, `rip`, `cr2` and the registers and halts.

## Syscall Contract

`syscall_init_64()` programs:
- `MSR_LSTAR` with `syscall_entry`
- `MSR_FMASK` to clear `IF`
- `MSR_STAR` as above

`syscall_handler_64()` dispatches through the active personality
attached to `current_process`:
- out-of-range or missing syscall slots return `-ENOSYS`
- up to six syscall arguments are passed through unchanged
- the return value is staged in `RAX`

The kernel keeps returning `-errno` internally on both architectures; the
native amd64 return path will convert to the FreeBSD convention of
abi-amd64.md (carry set, positive errno).  `syscall.c` is not yet part of
the milestone 0 build.

## Roadmap

| Milestone | Content | State |
| :-------- | :------ | :---- |
| M0 | Same-tree build; multiboot 1/2 boot to the higher half; direct map; GDT with compat segments, TSS, IDT; exception round trip | done |
| M1 | Machine-independent kernel built LP64-clean into the per-arch object directory | |
| M2 | Physical memory manager and 4-level pmap over the direct map; kernel heap | |
| M3 | Compat32: the existing i386 userland (`int $0x80` in compatibility mode, i386 structures and signal frame) up to a shell | |
| M4 | Native amd64 userland per abi-amd64.md | |

Files in `sys/arch/x86_64/` outside the build (`pmap.c`, `pmap_asm.S`,
`switch.S`, `syscall.c`, `efi/`) predate the bring-up and are brought in,
and corrected against `layout.h`, as their milestone arrives.  `pmap.c`
still assumes the old direct-map base.

## Verification

Boot: `make -C sys ARCH=x86_64` followed by the QEMU command above must
print `Result: PASS (x86_64 milestone 0)`.

Host tests (`tests/sys`):
- `host_test_x86_64_asm`
  - `boot.S`, `isr.S`, and `switch.S` assemble under `cc -m64`
  - required bootstrap, ISR, and context-switch symbols are exported
- `host_test_x86_64_gdt`
  - selector layout and access bytes, including the 32-bit user code
    segment (L=0) and the 64-bit one (L=1)
  - per-CPU TSS initialization
  - IST population
  - `tss_set_rsp0()` / `tss_get()` behavior
- `host_test_x86_64_idt`
  - exception/IRQ/syscall gate installation
  - IST assignment for critical exceptions
  - exception-name lookup
- `host_test_x86_64_syscall`
  - `LSTAR`, `FMASK`, `STAR` programming contract
  - syscall dispatch through the active personality
  - `-ENOSYS` fallback behavior
