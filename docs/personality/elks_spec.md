# ELKS (Embeddable Linux Kernel Subset) Personality Specification

## 1. Introduction
This document defines the Substrate operating system's execution personality for ELKS (Embeddable Linux Kernel Subset) 16-bit binaries. It covers the identification of ELKS binary formats, ABI semantics, the signal model, and expected memory models.

## 2. Binary Format Recognition
ELKS executables primarily utilize a variation of the MINIX a.out format tailored for the 16-bit 8086 architecture, often identifiable by specific magic numbers (e.g., `0x0410` or `0x0413` in the header).
The Substrate ELKS personality module (`sys/exec/perso/elks`) must register binary handlers to intercept `execve()` for ELKS binaries, verifying the magic number and architecture flags before loading the executable into a 16-bit compatible memory environment.

## 3. ABI Semantics
The ELKS ABI closely mirrors an early 16-bit Linux ABI implementation.
- **Syscall Calling Convention:** System calls are typically invoked via `int 0x80`, similar to standard 32-bit Linux, or specialized software interrupts common to 16-bit Linux-8086.
- **Register Usage:** Arguments are usually passed in 16-bit general-purpose registers (`BX`, `CX`, `DX`, `SI`, `DI`, `BP`), with the syscall number loaded into `AX`.
- **Return Values:** Results are returned in `AX`, with errors indicated by negative values (from `-1` to `-125`) or by manipulating the carry flag (`CF`), mirroring Linux semantics.

## 4. Signal Model and POSIX Mapping
The ELKS signal model adheres to early POSIX standards, with signal numbers aligning with standard x86 Linux signals.

| ELKS Signal | Substrate (POSIX) Equivalent | Description |
|---|---|---|
| `SIGHUP` (1) | `SIGHUP` | Hangup |
| `SIGINT` (2) | `SIGINT` | Interrupt |
| `SIGQUIT` (3) | `SIGQUIT` | Quit |
| `SIGILL` (4) | `SIGILL` | Illegal Instruction |
| `SIGTRAP` (5) | `SIGTRAP` | Trace/Breakpoint |
| `SIGABRT` (6) | `SIGABRT` | Abort |
| `SIGFPE` (8) | `SIGFPE` | Floating-Point Exception |
| `SIGKILL` (9) | `SIGKILL` | Kill |
| `SIGSEGV` (11) | `SIGSEGV` | Segmentation Violation |
| `SIGPIPE` (13) | `SIGPIPE` | Broken Pipe |
| `SIGALRM` (14) | `SIGALRM` | Alarm Clock |
| `SIGTERM` (15) | `SIGTERM` | Termination |
| `SIGUSR1` (10) | `SIGUSR1` | User-defined 1 (Linux mapping) |
| `SIGUSR2` (12) | `SIGUSR2` | User-defined 2 |

Signal delivery requires pushing a 16-bit signal frame onto the user stack. The ELKS personality must emulate the 16-bit signal trampoline and restore original register contexts correctly upon completion (`sigreturn`).

## 5. Memory Models
Since ELKS targets the Intel 8086 architecture, it inherits constraints associated with segmented memory. Substrate must provide proper execution environments mapping to these classical 16-bit memory models (tiny/small/medium/compact/large):

- **Tiny Model:** 
  - Code and Data share a single 64KB segment (`CS = DS = ES = SS`).
  - Represents `.COM` style executables. Pointers are 16-bit "near".
- **Small Model:** 
  - Separate 64KB segment for Code (`CS`), and a shared 64KB segment for Data, Stack, and Extra (`DS = SS = ES`).
  - Pointers are 16-bit "near" pointers.
- **Medium Model:** 
  - Multiple Code segments (far calls), but a single 64KB Data segment.
  - Code pointers are 32-bit (16:16 "far"), Data pointers are 16-bit "near".
- **Compact Model:** 
  - Single 64KB Code segment, but multiple Data segments (far data).
  - Code pointers are 16-bit "near", Data pointers are 32-bit "far".
- **Large Model:** 
  - Multiple Code segments and multiple Data segments.
  - All pointers are 32-bit "far" pointers.

The ELKS personality loader must configure the appropriate Local Descriptor Table (LDT) entries in Substrate to respect these segments, creating separate 16-bit descriptors with appropriate base addresses and 64KB limits to accurately emulate the environment.
