# x86_64 Boot & System Call Protocol

## System Call Interface

The kernel implements a standard x86_64 system call interface using the `syscall` instruction.

### Calling Convention
*   **Instruction:** `syscall`
*   **Return Address:** `rcx` (destroyed by `syscall`)
*   **Flags:** `r11` (destroyed by `syscall`)
*   **Return Value:** `rax`
*   **Arguments:**
    1.  `rdi`
    2.  `rsi`
    3.  `rdx`
    4.  `r10` (NOT `rcx`, as `syscall` uses it)
    5.  `r8`
    6.  `r9`
*   **Kernel Stack:** Switched automatically via `RSP` in TSS (or `MSR_SYSCALL_MSR` logic).

### Interrupt 0x80
For compatibility/legacy/simplicity, `int 0x80` is also supported with the same calling convention as i386 (arguments in `ebx`, `ecx`, `edx`, etc.), but truncated to 32-bit.

## Boot Protocol

The kernel supports two entry methods:

### 1. Multiboot2 (Legacy BIOS / UEFI via GRUB)
*   **Magic:** `0xE85250D6`
*   **Architecture:** `0` (i386) or `4` (MIPS)? No, for x86_64, usually we boot into 32-bit protected mode via Multiboot2 and trampoline to Long Mode.
*   **Entry State:** 32-bit Protected Mode, Paging Disabled.

### 2. EFI Native Boot
*   The kernel file can be compiled/linked as a valid PE32+ executable.
*   **Entry Point:** `efi_main`
*   **Subsystem:** `EFI_APPLICATION` (10)
*   **State:** 64-bit Long Mode, Identity Mapped (by Firmware), UEFI Boot Services active.
*   **Responsibility:** The kernel must exit Boot Services and set up its own page tables and GDT.

## Memory Map
*   **Kernel Physical Load Address:** 2MB (`0x200000`)
*   **Kernel Virtual Base:** `0xFFFFFFFF80000000` (Higher Half)
