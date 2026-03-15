# ELKS Personality Specification

## 1. Scope

This document defines the Substrate kernel contract for running ELKS
(Embeddable Linux Kernel Subset) user programs under the `PERS_ELKS`
personality on i386.

The contract is intentionally split into:

- on-disk executable recognition
- process and memory model
- syscall ABI
- signal ABI
- incompatibility behavior

This is a personality contract. It does not redefine the native Substrate
signal or process ABI. Translation happens at the ELKS personality boundary.

## 1.1 Current implementation note

This document is the target contract grounded in the upstream ELKS source tree
under `/home/segin/elks`.

Current Substrate runtime coverage is narrower than the full target:

- direct upstream ELKS programs such as `ls`, `pwd`, `date`, `tty`, `stty`,
  `uname`, `df`, `ps`, and `meminfo` are the current compatibility baseline
- reaching the upstream ELKS shell prompt is part of that baseline
- shell-mediated child process execution remains unstable and is not claimed as
  complete by this document

## 2. Binary Format Contract

Substrate ELKS execution targets the Minix-style 16-bit `a.out` format used by
ELKS and `elksemu`.

### 2.1 Recognized primary header contract

The executable header contract Substrate targets is the packed
`minix_exec_hdr` / `elksemu` shape documented in:

- `/home/segin/elks/Documentation/text/binformat.txt`
- `/home/segin/elks/elksemu/elks.h`

The first 32 bits of the executable header are interpreted as a little-endian
`type` field encoding magic, flags, and CPU type. The currently supported ELKS
image identifiers are:

- `0x04100301` (`ELKS_COMBID`)
- `0x04200301` (`ELKS_SPLITID`)
- `0x04300301` (`ELKS_SPLITID_AHISTORICAL`)

Operationally, this means the first four bytes are:

- byte 0: `0x01`
- byte 1: `0x03`
- byte 2: `0x10`, `0x20`, or `0x30`
- byte 3: `0x04`

Substrate shall not identify ELKS binaries by a standalone `0x0301` halfword
test. The discriminator is the full Minix-style 32-bit `type` field together
with the rest of the packed ELKS executable header.

### 2.2 Header layout

The base executable header is the packed Minix-style header:

- `type`
- `hlen`
- `version`
- `tseg`
- `dseg`
- `bseg`
- `entry`
- `chmem`
- `minstack`
- `syms`

The loader shall also recognize the ELKS supplemental-header variants:

- plain Minix header only
- relocation supplemental header
- far-text supplemental header

If supplemental headers are present, relocation sizes shall be integral
multiples of the relocation record size.

### 2.3 Program image classes

The ELKS loader contract distinguishes:

- combined I&D images
- split I&D images
- split images with far-text supplemental data

The memory model supported at runtime determines which of these can actually be
executed, but format recognition shall identify all three image classes.

## 3. Process Contract

When Substrate successfully executes an ELKS program, it shall:

- set `current_process->perso_id = PERS_ELKS`
- set `current_process->bitness = BITNESS_16`
- execute the process through a private per-process LDT
- enter userspace with 16-bit ring-3 code/data selectors

ELKS execution is a 16-bit protected-mode personality, not VM86. The kernel
remains 32-bit protected mode throughout.

## 4. LDT and Addressing Contract

### 4.1 Baseline segment model

The minimum Substrate ELKS contract is a private process LDT containing:

- one ring-3 16-bit code descriptor for `CS`
- one ring-3 16-bit data descriptor for `DS`
- `ES` and `SS` aliased to the data descriptor unless a wider model requires
  otherwise

Descriptor rules:

- DPL shall be `3`
- descriptors shall be ordinary code/data segments
- conforming code segments shall not be accepted for ELKS user mappings
- call gates are not part of the ELKS user ABI contract
- descriptor limits are byte granular for 16-bit ELKS segments

### 4.2 Pointer model

The ELKS personality shall support:

- near pointers: 16-bit offsets within the active segment
- far pointers: 16:16 selector-or-segment plus offset values where the memory
  model requires them

For protected-mode ELKS execution inside Substrate, effective linear address
resolution is:

`linear = LDT_base(selected_segment) + (offset & 0xFFFF)`

Substrate shall preserve 16-bit wrap semantics on the offset portion of ELKS
user pointers.

### 4.3 Memory-model scope

Upstream ELKS toolchain and libc support multiple 16-bit models:

- tiny
- small
- medium
- compact
- large

However, upstream ELKS user documentation currently describes the practical
load baseline as `-mcmodel=small`: up to `64KB` of code plus up to `64KB` of
data+stack in separate segments.

Substrate's first-class execution target shall therefore be:

- small-model ELKS binaries

The compatibility plan for broader models is:

- tiny: compatible subset of small-model execution
- medium: requires far-text entry and callback correctness
- compact: requires far-data correctness across syscall pointer marshaling
- large: requires both far-text and far-data correctness

Substrate shall not claim medium/compact/large runtime compatibility merely
because the loader recognizes their header variants.

## 5. Syscall ABI Contract

### 5.1 Supported syscall vector

The supported ELKS syscall entry vector is `INT 0x80`.

Substrate shall treat `INT 0x80` from an ELKS process as the personality
syscall gateway.

### 5.2 Register convention

The ELKS syscall ABI contract is:

- `AX`: syscall number
- `BX`, `CX`, `DX`, `SI`, `DI`, `BP`: syscall arguments in 16-bit form
- `AX`: return value

Negative return values in `AX` represent `-errno` in ELKS-visible form.

Arguments that represent user pointers are 16-bit ELKS addresses and must be
translated through the ELKS process address model before being passed to native
kernel services.

For compact and large ELKS programs, upstream libc explicitly treats some user
arguments as far pointers and uses `sys_setseg()` before issuing `INT 0x80`.
Substrate shall treat the ELKS syscall ABI as a 16-bit ABI whose pointer
interpretation depends on the ELKS program's memory model and wrapper
convention, not as a flat near-pointer-only ABI.

### 5.3 `argv` / `envp` process-entry contract

At ELKS program entry, the user stack shall present:

- 16-bit `argc`
- a 16-bit `argv[]` pointer vector terminated by a null 16-bit pointer
- a 16-bit `envp[]` pointer vector terminated by a null 16-bit pointer
- NUL-terminated argument and environment strings

The stack image shall be word aligned.

The personality is responsible for building a 16-bit entry image; it shall not
reuse the native 32-bit ELF process-entry contract.

### 5.4 Heap contract

`brk` and `sbrk`-style growth are constrained by the ELKS data/heap/stack
segment layout. The ELKS personality shall reject heap growth beyond the
personality-visible data-space contract rather than silently exposing native
32-bit process address-space behavior.

### 5.5 `/dev/kmem` compatibility contract

ELKS process-inspection tools expect `/dev/kmem` to expose ELKS kernel-internal
structures through `MEM_GET*` ioctls plus offset-based `lseek`/`read`
operations.

Substrate shall keep native `/dev/kmem` semantics unchanged for native and
other personalities. For ELKS processes only, the ELKS personality may
intercept `/dev/kmem` file-descriptor operations and present a synthetic
ELKS-shaped kernel snapshot instead.

The minimum supported compatibility contract is:

- `MEM_GETDS` returning a personality-defined synthetic data-segment base
- `MEM_GETHEAP` returning the synthetic `_heap_all` list-head offset
- `MEM_GETSEGALL` returning the synthetic `_seg_all` list-head offset
- `MEM_GETTASK` returning the offset of the exported task table inside that
  synthetic image
- `MEM_GETMAXTASKS` returning the number of exported task slots
- `MEM_GETUSAGE` returning a bounded 16-bit main-memory summary that stays
  coherent for old ELKS tools on large-memory Substrate systems
- offset-based `read` access over the synthetic image for task, segment, stack,
  heap, and related inspection data consumed by upstream ELKS `ps` and
  `meminfo`

For compatibility with older installed ELKS `ps` binaries that begin scanning
at task slot 1, the exported synthetic task table reserves slot 0 for the
swapper/idle view and places the current userspace ELKS process in a later slot.

Ioctls that require direct far-pointer dereference into real kernel memory are
not part of the minimum supported compatibility surface unless explicitly
emulated.

## 6. Signal ABI Contract

Substrate's native signal semantics remain the native kernel contract. ELKS
processes observe an ELKS-facing signal ABI translated at the personality edge.

### 6.1 ELKS small-signal numbering

The ELKS userspace ABI that Substrate shall target is the 16-bit "smallsig"
layout used by ELKS:

| ELKS | Meaning  | Native/Substrate signal |
|------|----------|-------------------------|
| 1    | `SIGHUP` | `SIGHUP` |
| 2    | `SIGINT` | `SIGINT` |
| 3    | `SIGQUIT` | `SIGQUIT` |
| 4    | `SIGWINCH` | `SIGWINCH` |
| 5    | `SIGSTOP` | `SIGSTOP` |
| 6    | `SIGABRT` | `SIGABRT` |
| 7    | `SIGTSTP` | `SIGTSTP` |
| 8    | `SIGCONT` | `SIGCONT` |
| 9    | `SIGKILL` | `SIGKILL` |
| 10   | `SIGUSR1` | `SIGUSR1` |
| 11   | `SIGSEGV` | `SIGSEGV` |
| 12   | `SIGCHLD` | `SIGCHLD` |
| 13   | `SIGPIPE` | `SIGPIPE` |
| 14   | `SIGALRM` | `SIGALRM` |
| 15   | `SIGTERM` | `SIGTERM` |
| 16   | `SIGURG` | `SIGURG` |

ELKS `SIGTTIN` and `SIGUSR2` are not part of the smallsig numbered ABI in this
mode and shall not be exposed as valid ELKS signal numbers.

### 6.2 Signal frame contract

ELKS signal delivery requires a 16-bit user-visible signal frame and a matching
16-bit signal-return path. The ELKS personality shall not reuse the native
i386, Linux, or BSD signal-frame layouts verbatim.

For the ELKS libc contract Substrate targets, the kernel enters the installed
callback handler as if it had been reached by a far call. The ELKS user stack
shall contain, in order:

- return IP
- return CS
- 16-bit signal number

This matches the upstream ELKS libc `_signal_cbhandler(sig)` callback stubs and
the ELKS kernel-side `arch_setup_sighandler_stack()` contract. For the current
ELKS libc trees in `/home/segin/elks`, the callback completes delivery with a
far return that discards the 16-bit signal number (`lret $2` / `retf 2`).

Substrate shall not expose a separate ELKS-visible `sigreturn` syscall for this
path. Return from the signal callback is part of the ELKS far-return frame
contract, not a native i386 `sigreturn` ABI clone.

### 6.3 Unsupported native-only signals

Signals with no ELKS-visible number in the smallsig ABI are personality-local
translation problems. They may be handled internally by the kernel, but they
are not part of the stable ELKS user ABI until explicit mapping rules are
defined.

## 7. Incompatibility Contract

### 7.1 Minix-86 syscall trap

`INT 0x20` is not part of the supported ELKS syscall ABI in Substrate.

If an ELKS process issues `INT 0x20`, Substrate shall treat it as a Minix-86
syscall attempt and shall:

- log that a Minix-86 syscall trap was attempted
- preserve enough trap context for diagnosis
- raise `SIGSYS`

This behavior is an incompatibility trap, not a supported ELKS execution path.

### 7.2 Personality separation

The ELKS personality shall remain distinct from:

- native Substrate ABI
- Linux i386 ELF personality
- Minix compatibility work

Shared kernel services may be reused internally, but the ELKS user-visible ABI
shall be documented and validated independently.

## 8. Memory Models

Substrate shall document the standard 16-bit C memory models as follows.

| Model | Code | Data | Stack | Pointer rules | ELKS/Substrate contract |
|-------|------|------|-------|---------------|-------------------------|
| Tiny | one segment | same segment | same segment | code/data near | valid conceptual model; `.COM`-style environment, not the baseline `a.out` target |
| Small | one code segment | one data segment | same as data | code near, data near | minimum supported ELKS `a.out` execution model |
| Medium | multiple code segments | one data segment | same as data | code far, data near | requires far-text support and an additional code selector |
| Compact | one code segment | multiple data segments | model-specific | code near, data far | not part of the initial Substrate ELKS runtime contract |
| Large | multiple code segments | multiple data segments | model-specific | code far, data far | not part of the initial Substrate ELKS runtime contract |

### 8.1 Combined vs split images

Combined and split ELKS images are file-layout properties. They are related to,
but not identical with, the C memory model:

- combined I&D images typically align with the baseline small-model loader path
- split I&D images require distinct code and data placement
- far-text images are the first required step toward medium-model execution

## 9. Current Kernel Deltas

The current in-tree ELKS code already establishes the architectural direction:

- `PERS_ELKS`
- `BITNESS_16`
- LDT-backed process execution
- 16-bit user entry via `jump_to_elks`
- explicit Minix trap handling for `INT 0x20`

However, the final ELKS loader and syscall personality must follow this
documented contract rather than a simplified ad hoc header model.
