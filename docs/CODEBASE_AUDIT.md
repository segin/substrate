# Substrate Codebase Audit

This document provides a comprehensive audit of the Substrate operating system codebase, covering architecture, patterns, and implementation status.

**Last Updated:** 2026-04-12  
**Scope:** Kernel, drivers, libraries, userland utilities

---

## Table of Contents

1. [Header Architecture](#1-header-architecture)
2. [Kernel Architecture](#2-kernel-architecture)
3. [Driver Architecture](#3-driver-architecture)
4. [VFS and Filesystems](#4-vfs-and-filesystems)
5. [Userland Libraries](#5-userland-libraries)
6. [Binary Utilities](#6-binary-utilities)
7. [Toolchain](#7-toolchain)
8. [Build System](#8-build-system)
9. [Issues and Recommendations](#9-issues-and-recommendations)

---

## 1. Header Architecture

### 1.1 Directory Structure

| Directory | Files | Purpose |
|-----------|-------|---------|
| `include/` | ~50 | Userland C library headers |
| `include/sys/` | ~15 | System call headers |
| `sys/include/` | ~10 | Kernel-public interfaces |

### 1.2 Include Guard Conventions

Three different conventions are used:

| Convention | Example | Files |
|------------|---------|-------|
| BSD style | `_HEADER_H` | `include/stdint.h` |
| Substrate style | `_SUBSTRATE_HEADER_H_` | `include/stdio.h` |
| Simple | `HEADER_H` | `sys/include/proc.h` |

### 1.3 Duplicate Headers

**Critical:** These headers exist in both `include/` and `include/sys/`:

- `sys/types.h`
- `sys/time.h`

This creates ambiguity when including headers. The `include/sys/` versions should be preferred for system calls.

### 1.4 Missing Headers

| Header | Status |
|--------|--------|
| `pthread.h` | Partial - stub only |
| `dlfcn.h` | Missing - required for dynamic loading |
| Network headers | Missing (`netinet/*`, `arpa/*`, etc.) |

---

## 2. Kernel Architecture

### 2.1 Target Architecture

- **Primary:** x86 32-bit (`i386`)
- **Planned:** x86_64 (stubbed)
- **Memory Model:** Higher-half kernel (0xC0000000+)
- **ABI:** Standard Intel C ABI

### 2.2 Memory Management

**PMM (Physical Memory Manager):**

- Returns **VIRTUAL addresses** (kernel direct-mapped)
- Virtual base: 0xC0000000
- Physical = Virtual - 0xC0000000

```c
// CORRECT usage:
void *page_virt = pmm_alloc_block();           // 0xC0000000+
pmap_enter(pmap, va, (uint32_t)page_virt - 0xC0000000, flags);  // Physical
pmm_free_block(page_virt);                     // Virtual
```

**UMA (Universal Memory Allocator):**

- Backed by PMM
- Provides `kmalloc`/`kfree` for kernel allocations
- Zones for different allocation sizes

**Per-Process PMAP:**

- `pmap_create()`, `pmap_destroy()`, `pmap_fork()`
- COW (Copy-On-Write) support
- 3GB user / 1GB kernel split
- **Known Issue:** 32 identity-mapped kernel PDEs consume 128KB+ per process

### 2.3 Scheduler

**Type:** MLFQ (Multilevel Feedback Queue)

- Realtime queue (priority 0-7)
- Timeshare queues (priority 8-63)
- Idle class for idle threads

**SMP Support:**

- Per-CPU runqueues
- Work stealing load balancing
- CPU affinity
- IPI-based preemption

### 2.4 Synchronization

**Primitives:**

| Primitive | Implementation |
|-----------|----------------|
| Mutex | `sleepq`-backed with priority inheritance |
| Semaphore | `sleepq`-backed |
| lockmgr | BSD-style (LK_SHARED, LK_EXCLUSIVE, LK_UPGRADE, LK_DRAIN) |
| Turnstiles | Priority inheritance |

**Sleep Queues:**

- Hashed sleep queues for O(1) lookup
- `sleepq_wait()`, `sleepq_wake()` API

### 2.5 Process Model

- **PID 0:** Swapper (idle process)
- **PID 1:** Init process
- Process groups and sessions
- Per-process controlling terminal (ctty)

### 2.6 System Call Interface

**Entry:** `int $0x80`

**Personalities:**

| Personality | ID | Syscalls |
|-------------|-----|----------|
| Native | 0 | ~60 |
| Linux | 1000 | 1000+ |
| FreeBSD | 2000 | 458+ |

**Key Syscalls:**

```c
// Native (sys/arch/i386/syscall.h)
SYS_EXIT=1, SYS_FORK=2, SYS_READ=3, SYS_WRITE=4, SYS_OPEN=5,
SYS_CLOSE=6, SYS_WAIT4=7, SYS_CREAT=18, SYS_LINK=19, SYS_UNLINK=20,
SYS_MMAP=90, SYS_MUNMAP=91, SYS_MPROTECT=92, SYS_BRK=45, ...
```

**Critical Bug:** `SYS_SELECT=85` and `SYS_READLINK=85` have duplicate syscall numbers.

---

## 3. Driver Architecture

### 3.1 Storage Stack

```
┌─────────────────────────────────────┐
│         Filesystem Layer            │
├─────────────────────────────────────┤
│   bio (buffer cache)                │
├─────────────────────────────────────┤
│   Partition/GPT support             │
├─────────────────────────────────────┤
│   Bus Layers (AHCI, IDE, SCSI)      │
├─────────────────────────────────────┤
│   Device Drivers                    │
└─────────────────────────────────────┘
```

**Device Naming:** `/dev/storage/`

| Pattern | Example | Type |
|---------|---------|------|
| `type`+`instance` | `ide0`, `sata0`, `scsi0` | Storage |

**VirtIO:** Block, SCSI, 9P (GPU stubbed)

### 3.2 Input Stack

```
┌─────────────────────────────────────┐
│   Event Queue                       │
├─────────────────────────────────────┤
│   HID Layer (PS/2, USB)             │
├─────────────────────────────────────┤
│   Device Drivers                    │
└─────────────────────────────────────┘
```

**PS/2:** Dual-channel support (keyboard + mouse)

**USB HID:** Via `usb_hid.c` driver

### 3.3 Video/Display

**Framebuffers:**

- Linear framebuffer driver
- BGA (Bochs Graphics Adapter) support
- `/dev/fb0`..`/dev/fb7` device registry

**Fonts:**

- PSF1, PSF2 (console fonts)
- BDF, PCF (bitmap fonts)
- Glyph cache with FNV-1a hash

**Rendering:**

- `fb_fillrect()` - ROP_COPY, ROP_XOR
- `fb_copyarea()` - overlap-safe
- `fb_imageblit()` - mono/color images
- Attributes: bold, italic, underline, strikethrough, reverse

### 3.4 USB Stack

**Controllers:** UHCI

**Class Drivers:** MSC (Mass Storage Class), HID

**Status:** HCD infrastructure exists; **usbdevfs character device driver MISSING**

---

## 4. VFS and Filesystems

### 4.1 VFS Layer

**Features:**

- vnode abstraction
- Name cache
- Mount points
- Lock manager integration

**Operations:**

- `link`/`unlink` with proper ctime updates
- `readdir` with atime updates
- `chmod`/`chown` with ctime updates

### 4.2 Filesystem Implementations

| Filesystem | Status | Notes |
|------------|--------|-------|
| ext2 | Complete | Read-write with timestamps |
| UDF | Complete | ECMA-167/OSTA spec |
| FAT | Present | Read-only likely |
| Minix | Present | - |
| procfs | Kernel-managed | Auto-mounted |
| sysfs | Kernel-managed | Auto-mounted |
| devfs | Kernel-managed | Auto-mounted |

### 4.3 Buffer Cache (bio)

- Hash-based lookup
- Queue management (LRU)
- Delayed write (syncer kthread)
- Atomic buffer operations

---

## 5. Userland Libraries

### 5.1 lib/c - C Standard Library

**Architecture:**

```
lib/c/
├── src/              # stdlib.c, string.c, sys.c, ctype.c, ...
├── stdio/            # printf, scanf, FILE streams
├── time/             # time.c, strftime.c, etc.
└── arch/i386/        # crt0.S, syscall.S, setjmp.S
```

**Features:**

- `printf`/`scanf` implementations
- `FILE` streams with BUFSIZ=1024
- Full time.h with timezone support
- POSIX timestamp compliance (64-bit time_t)

**Syscalls:** Via `int $0x80` wrapper

### 5.2 lib/m - Math Library

- C99/POSIX math implementation
- x87 FPU via inline assembly
- `fenv.c` for floating-point environment (x87 only, SSE stubbed)
- `fpclassify.c` for IEEE 754 classification

### 5.3 lib/sys - Syscall Wrappers

**Status:** Partial (~19 source files)

| Category | Coverage |
|----------|----------|
| Process | getpid, getuid, setuid, etc. |
| Memory | mlock, munlock, getpagesize |
| Time | stime, times, sysinfo |
| I/O | ioctl, select (broken), sysctl |
| Missing | read, write, open, close, fork, execve, mmap |

**Critical Issues:**

1. Duplicate `SYS_SELECT`/`SYS_READLINK` (85)
2. Hardcoded syscall number in `select.c:55` (209 for SYS_POLL)
3. `SYS_vm86` missing from kernel header
4. Inconsistent errno handling
5. No `fstat` wrapper

### 5.4 lib/edit - EditLine

**Size:** ~4,900 lines

**Features:**

- BSD libedit-compatible
- Emacs mode (default)
- Vi mode (full implementation)
- UTF-8 support
- History management
- Completion (filename + tilde expansion)
- Readline compatibility shim (`rl_compat.c`)

**Key Files:**

| File | Purpose |
|------|---------|
| `readline.c` | Main implementation (~2,784 lines) |
| `terminal.c` | Raw mode, termcap, ANSI fallback |
| `history.c` | History list management |
| `keymap.c` | Key bindings and dispatch |
| `utf8.c` | UTF-8 decode/encode/width |

### 5.5 lib/dl - Dynamic Loading

**Status:** EMPTY STUB

- Only Makefile exists
- No `dlfcn.h` header
- No `dlopen`/`dlsym`/`dlclose`/`dlerror`
- Design doc at `docs/design/ld.so-design.md` (not implemented)

### 5.6 lib/dbm - Database Library

**Status:** INCOMPLETE (57%)

| Function | Status |
|----------|--------|
| `dbm_open()` | Implemented |
| `dbm_close()` | Implemented |
| `dbm_store()` | **MISSING** |
| `dbm_fetch()` | **MISSING** |
| `dbm_delete()` | Stub (returns -1) |
| `dbm_firstkey()` | Implemented |
| `dbm_nextkey()` | Implemented |

**Storage:** Linear search in single file - O(n) performance

### 5.7 lib/usb - USB Library

**API:** libusb 1.0 compatibility

**Interface:** `/dev/usb/*` via `ioctl()` (usbdevfs ABI)

**Status:** Work-in-progress

**Critical:** Requires kernel usbdevfs driver (missing - `/dev/usb/*` nodes not created)

### 5.8 usr.lib/* - Additional Libraries

#### bc (Bignum Library)

- `bc_num` base-100 bignum implementation
- Arithmetic: add, sub, mul, div, mod, pow, sqrt
- **Issue:** Math functions (sin, cos, arctan, log, exp, bessel) are stubs

#### demangle (~5,400 lines)

- **Schemes Supported:** Itanium C++, Rust (v0 + legacy), D Language
- **Source Files:** demangle.c (182), buffer.c (148), itanium.c (1,893), rust.c (1,769), dlang.c (1,407)
- **Auto-Detection:** Prefix-based (`_Z` → Itanium, `_R` → Rust, `_D` → D)
- **Test Coverage:** Comprehensive test suite with fuzzing
- **Issues:** Missing MSVC/Objective-C/Swift schemes, no specific error codes

#### elfobj (~10,000+ lines)

- **Status:** Production Ready
- **Architectures:** 15 (i386, x86_64, ARM, AArch64, MIPS, RISC-V, LoongArch, m68k, VAX, PPC, PPC64, Alpha, IA-64)
- **Source Files:** 13 in `src/` (elf_read.c, elf_write.c, elf_reloc.c, elf_link.c, etc.)
- **API:** 114 exported symbols
- **Features:** Section/symbol/relocation handling, linking, validation, DWARF classification
- **Issues:** Missing ARM relocation backend, public header in `../../include/` (non-standard location)

#### exvi

- Ex/vi editing engine (686+ lines core)
- Full ex commands and visual mode
- Linked list buffer with `line_t` structures
- Relies on `<regex.h>`

#### regex

- Multiple backends: safe, posix, pcre2, re2
- Streaming/iterator API
- Complete implementation

#### libuu (Uuencode/Decode)

- `uu_decode_line()` - decode one line
- `uu_parse_header()` - parse "begin" header
- **Issue:** No encode function

#### join

- Text join utility library
- Single-file implementation
- Complete

---

## 6. Binary Utilities

### 6.1 ex/vi

**Architecture:** Thin wrappers over exvi

| Binary | Lines | Entry Point |
|--------|-------|-------------|
| `bin/ex/` | 83 | `exvi_main(argc, argv, EXVI_FRONTEND_EX)` |
| `bin/vi/` | 7 | `exvi_main(argc, argv, EXVI_FRONTEND_VI)` |

### 6.2 Shell

- `bin/sh/` - Bourne shell implementation
- `bin/esh/` - Enhanced shell (if present)

### 6.3 Core Utilities

| Category | Utilities |
|----------|-----------|
| File | ls, cat, cp, mv, rm, mkdir, rmdir, ln |
| Text | head, tail, wc, tr, sort, uniq, cut, paste |
| Search | grep, find (if present) |
| System | ps, kill, pwd, cd, echo |

---

## 7. Toolchain

The Substrate toolchain consists of a native C compiler, assembler, and linker built from scratch.

### 7.1 usr.bin/cc - C Compiler

**Status:** Phase 9 Implementation

**Architecture:** Frontend → Middle → Backend pipeline

| Component | Files | Purpose |
|-----------|-------|---------|
| `cmd/` | cc.c, pipeline.c | Driver and pipeline orchestration |
| `frontend/` | preproc.c, lexer.c, parser.c, sema.c, builtin.c | C frontend |
| `middle/` | ast2ir.c, legalize.c, passes/opt.c, ssa/*.c | IR lowering, optimization |
| `backend/` | emit_s.c, select.c, regalloc.c, frame.c | Code generation |

**IR System:**

- **Text IR** (`ir.h`): Human-readable SSA dump for debugging
- **SSA IR** (`cc_ssa.h`): Internal optimization representation
- **MIR** (`cc_mir.h`): Machine-specific lowered representation

**Standards Support:**

- C99 through C23 (complete)
- GNU extensions (statement expressions, labels-as-values, etc.)
- Clang compatibility features

**Targets:** x86-64 (SysV AMD64 ABI), i386 (cdecl)

**Issues:**

- Limited optimizer (only constant folding, dead temp elimination)
- No mem2reg pass
- Parser is monolithic (~5000 lines)
- Kernel header compatibility incomplete

### 7.2 usr.bin/as - Assembler

**Status:** Feature Complete (~43,000 lines, 91 files)

**Architecture:** Core + Architecture-specific encoders

| Component | Files | Purpose |
|-----------|-------|---------|
| Core | as.c, as_lexer.c, as_parser.c, as_elf_emit.c, as_sections.c, as_symtab.c, as_relax.c | Main pipeline |
| x86 | as_x86_*.c (23 files) | x86/x86_64 encoding |
| ARM | as_arm_*.c (8 files) | ARM32 encoding |
| AArch64 | as_a64_*.c (8 files) | ARM64 encoding |

**Features:**

- Dual syntax: AT&T and Intel
- VEX/EVEX encoding for AVX/AVX-512
- Branch relaxation (short vs near)
- Full ELF object emission with relocations
- ISA level gating (v1-v4 for x86_64)

**Supported Relocations:**

- i386: R_386_32, R_386_PC32, GOT/PLT/TLS variants
- x86_64: R_X86_64_64, R_X86_64_PC32, GOTPCREL, TLS variants
- ARM: ABS32, PC24, THUMB, GOT, TLS
- AArch64: ABS64, page-relative, load/store, TLS

**Issues:**

- `as_elf_emit.c` is 11,462 lines (very large)
- ARM/AArch64 parity with x86 incomplete

### 7.3 usr.bin/ld - Linker

**Status:** Feature Complete for x86/i386 (~11,500 lines, single file)

**Architecture:** Single-file `ld.c` using libelfobj

**Features:**

| Feature | Status |
|---------|--------|
| Section combining | Complete |
| Symbol resolution | Complete (strong/weak/common, versioning) |
| Archives | Complete (GNU/thin, fixpoint resolution) |
| GC sections (`--gc-sections`) | Complete |
| ICF (`--icf=safe/all`) | Complete |
| Dynamic linking | Complete (GOT/PLT/TLS) |
| Linker scripts | Complete (SECTIONS, MEMORY, PROVIDE, KEEP) |
| Symbol versioning | Complete (.gnu.version*) |
| Determinism | Complete |

**Targets:** x86_64, i386 (other archs stubbed)

**Missing:**

- Parallel linking (`--threads`)
- Arena allocators for large workloads
- AArch64/ARM backends (stubbed)

### 7.4 usr.lib/elfobj - ELF Library

**Status:** Production Ready

Used by the assembler and linker for all ELF operations.

**Key APIs Used by ld:**

- `elf_open()`, `elf_close()` - File handling
- `elf_create()`, `elf_write_file()` - Output generation
- `elf_link()` - Object merging
- `elf_add_section()`, `elf_find_section()` - Section ops
- `elf_add_symbol()`, `elf_find_symbol()` - Symbol ops
- `elf_apply_relocation_value()` - Relocation application
- `elf_hash_sysv()`, `elf_hash_gnu()` - Hash tables

---

## 8. Build System

### 8.1 Architecture

- Recursive Makefiles
- `make -C sys` - Kernel build
- `make -C lib/c` - LibC build
- `make -C bin` - Userland build

### 8.2 Toolchain Flags

**Kernel/Userland (target):**

```
-m32 -nostdlib -fno-builtin -fno-pie
```

**Host build (testing):**

```
NATIVE_BUILD=1
```

### 8.3 Root Filesystem

- `dist/` directory for root filesystem staging
- Standard Unix hierarchy (`/bin`, `/sbin`, `/usr`, `/etc`, etc.)

---

## 9. Issues and Recommendations

### 9.1 Critical Issues

| Issue | Location | Impact |
|-------|----------|--------|
| Duplicate syscall 85 | `sys/arch/i386/syscall.h` | `select`/`readlink` conflict |
| Missing `dbm_store`/`dbm_fetch` | `lib/dbm/` | DBM library non-functional |
| Missing kernel usbdevfs | `sys/drivers/` | libusb cannot work |
| Missing `dlfcn.h` | `include/` | Dynamic loading impossible |
| PMAP memory overhead | `sys/arch/i386/pmap.c` | 128KB+ per process |

### 9.2 Architectural Issues

| Issue | Recommendation |
|-------|----------------|
| Three include guard styles | Standardize on one convention |
| lib/sys incomplete | Add missing wrappers, fix errno handling |
| No network stack | Implement socket layer if needed |
| Limited test coverage | Expand `tests/sys/` framework |
| as_elf_emit.c size | Extract instruction-specific logic |
| cc parser monolith | Consider splitting into modules |

### 9.3 Missing Components

| Component | Priority | Notes |
|-----------|----------|-------|
| `lib/dl/` implementation | High | Blocked by libsys mmap |
| `lib/dbm/` store/fetch | High | Cannot store data |
| Kernel usbdevfs | Medium | libusb requires this |
| `fstat` wrapper | Medium | Common syscall missing |
| `pthread.h` | Low | Threading infrastructure partial |
| Network headers | Low | Not required for current scope |

### 9.4 Recommendations

1. **Fix duplicate syscall numbers** - Ensure unique syscall numbers across all personalities
2. **Complete lib/dbm** - Implement `dbm_store()` and `dbm_fetch()`
3. **Implement lib/dl** - Follow `docs/design/ld.so-design.md`
4. **Add kernel usbdevfs** - Create `/dev/usb/*` device nodes
5. **Standardize include guards** - Pick one convention and apply project-wide
6. **Refactor PMAP** - Dynamically allocate page tables to reduce per-process overhead
7. **Expand lib/sys** - Add missing common syscalls with consistent errno handling
8. **Improve cc optimizer** - Implement mem2reg and common optimization passes
9. **Complete ARM/AArch64 parity** - Bring encoders to same level as x86

---

## Appendix: Directory Structure

```
sys/                    # Kernel source
├── arch/i386/          # CPU (GDT, IDT, PMAP, syscall)
├── core/               # kmain entry
├── kern/               # Scheduler, VFS, syscalls
├── drivers/            # Hardware drivers
│   ├── storage/        # IDE, AHCI, VirtIO block
│   ├── video/          # Framebuffer, fonts
│   ├── input/          # PS/2, USB HID
│   └── devices/        # null, tty, etc.
├── fs/                 # Filesystems (ext2, UDF, FAT)
├── exec/               # ELF loader, personalities
├── vm/                 # Virtual memory
└── pm/                 # Power management

lib/
├── c/                  # LibC (stdio, time, stdlib)
├── m/                  # Math library (x87)
├── sys/                # Syscall wrappers
├── edit/               # EditLine (readline compat)
├── dl/                 # Dynamic loading (stub)
├── dbm/                 # DBM library (incomplete)
├── usb/                 # libusb 1.0 compat
└── pthread/            # Threading (partial)

usr.lib/
├── bc/                 # Bignum
├── demangle/           # Symbol demangling
├── elfobj/             # ELF object library
├── exvi/               # Ex/vi editing engine
├── join/               # Text join utility
├── libuu/              # Uuencode/decode
└── regex/              # Regex engine

usr.bin/
├── cc/                 # C compiler (Phase 9)
│   ├── cmd/            # Driver, pipeline
│   ├── frontend/       # Lexer, parser, sema
│   ├── middle/         # AST→SSA, optimization
│   └── backend/        # Code generation
├── as/                 # Assembler (all archs)
├── ld/                 # Linker (x86/x86_64)
├── ex/                 # Ex editor (exvi wrapper)
├── vi/                 # Vi editor (exvi wrapper)
├── sh/                 # Shell
└── [utilities]         # Core utilities

include/                # Userland headers
include/sys/            # System headers
sys/include/            # Kernel-public interfaces
```

---

*End of Audit*

---

## Memory Safety Issues Found (2026-04-12)

### 9P Filesystem (sys/fs/9p.c)

**Issue 1: Line 108 - Fixed-Size Stack Buffer**
- Severity: HIGH
- Code: `uint32_t msize = 4 + 1 + 2 + 4 + 8 + 4;` followed by `uint8_t msg[msize];`
- Issue: Fixed stack buffer (23 bytes) used for 9P TREAD message. While the request doesn't include variable data count, the response parsing could overflow if `count > 0`.
- Fix: Use `kmalloc` instead of stack array for the message.

**Issue 2: Line 176-177 - Static fs_node_t Reuse**
- Severity: CRITICAL
- Code: `static fs_node_t p9_root;` then `memset(&p9_root, 0, sizeof(fs_node_t));`
- Issue: Single static node reused for all 9P mounts. Race condition if multiple 9P filesystems mounted simultaneously.
- Fix: Allocate new `fs_node_t` via `kmalloc` for each mount.

### sysfs (sys/fs/sysfs.c)

**Issue 1: Lines 25-31 - Missing Null Termination**
- Severity: MEDIUM
- Code: `strncpy(dest, source, sizeof(dest))` should be `sizeof(dest) - 1`
- Issue: strncpy does not guarantee null termination when source length equals buffer size
- Fix: Use `strncpy(dest, source, sizeof(dest) - 1)` and explicitly null-terminate

**Issue 2: Lines 40-51 - Static fs_node_t Reuse**
- Severity: HIGH
- Issue: Static `fs_node_t` reused across multiple sysfs calls - race condition
- Fix: Allocate new node per mount or use dynamic allocation

### devfs (sys/fs/devfs.c)

**Issue: Line 321 - Potential Uninitialized Pointer**
- Severity: MEDIUM
- Issue: If `entry->node` not allocated via `kmalloc`, freeing it could corrupt memory
- Fix: Verify `entry->node` was allocated with `kmalloc` before freeing

### kmem (sys/drivers/devices/kmem.c)

**Issue: Line 188 - strcpy Instead of strncpy**
- Severity: HIGH
- Code: Uses `strcpy` without bounds checking
- Issue: Buffer overflow if source string exceeds destination size
- Fix: Replace with `strncpy(dest, source, sizeof(dest) - 1)` and null-terminate

---

## Safe Implementations (Verified)

- **sys/kern/syscall.c**: Proper error handling with kmalloc/kfree pairs
- **sys/drivers/storage/blkdev.c**: Excellent stack/heap allocation patterns with conditional kmalloc
- **sys/drivers/storage/ide/ide.c**: Uses static data only - safe
- **sys/drivers/storage/nvme/nvme.c**: Proper DMA API (dma_free_coherent)
- **lib/c/src/stdlib.c**: Custom allocator with proper magic checks
- **lib/c/src/sys.c**: Correct varargs handling with va_end

