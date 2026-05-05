# Substrate libc Architecture

## Overview

The Substrate libc is a minimal, freestanding C library designed for the x86 32-bit architecture (with x86_64 stub support). It implements standard C library functionality (libc) and system call wrappers (libsys) as separate static libraries, providing POSIX-compliant APIs for the Substrate operating system.

## Architecture

### Build Structure

```
lib/c/
├── Makefile                    # Build configuration (recursive Make)
├── arch/                       # Architecture-specific code
│   └── i386/
│       ├── crt0.S              # Program entry point (startup code)
│       ├── syscall.S           # Syscall wrappers (int $0x80)
│       └── setjmp.S            # setjmp/longjmp implementation
├── src/                        # Library source files
│   ├── stdlib.c                # Memory allocation, atexit, qsort, random
│   ├── string.c                # String manipulation, memcpy/memset
│   ├── sys.c                   # System call wrappers
│   ├── sysctl_helpers.c        # sysctl helpers
│   ├── ctype.c                 # Character classification
│   ├── assert.c                # Assertion macros
│   ├── dirent.c                # Directory enumeration
│   ├── getopt.c                # Command-line argument parsing
│   ├── pwd.c                   # Password database
│   ├── grp.c                   # Group database
│   ├── libgen.c                # Library generation utilities
│   ├── div64.c                 # 64-bit division routines
│   ├── time/time.c             # Time/date functions
│   ├── wchar.c                 # Wide character functions (stub)
│   └── fnmatch.c               # Pattern matching
├── stdio/                      # Standard I/O implementation
│   ├── printf.c                # Printf/vprintf/vsprintf/vsnprintf
│   ├── scanf.c                 # Scanf/fscanf/sscanf/fmprint
│   ├── stdio_core.c            # FILE stream I/O, buffering, popen
│   └── stdstreams.c            # stdin/stdout/stderr initialization
├── sys_generic.c               # Generic syscall wrappers (fallback)
├── include/
│   └── sys_local.h             # Local sys header
└── tests/
    └── bench_malloc.c          # Memory allocator benchmark
```

### Header Files (include/)

Headers are located in the project root `include/` directory, organized into categories:

```
include/
├── stdlib.h                    # Memory, conversions, random, qsort
├── string.h                    # String operations, memory functions
├── unistd.h                    # POSIX calls (fork, exec, file I/O)
├── stdio.h                     # File I/O (printf, scanf, stdio)
├── ctype.h                     # Character classification
├── errno.h                     # Error codes
├── fcntl.h                     # File control flags
├── fenv.h                      # Floating-point environment (stub)
├── math.h                      # Math functions (stub)
├── signal.h                    # Signal handling
├── time.h                      # Time/date functions
├── stdarg.h                    # Variable arguments macros
├── stddef.h                    # Standard types (NULL, offsetof)
├── stdint.h                    # Fixed-width integer types
├── stdbool.h                   # Boolean type and macros
├── stdatomic.h                 # Atomic operations
├── sys/
│   ├── types.h                 # System types (pid_t, uid_t, etc.)
│   ├── stat.h                  # File statistics (stat, lstat)
│   ├── mman.h                  # Memory mapping (mmap, munmap)
│   ├── sysctl.h                # System control
│   ├── syscall.h               # Syscall numbers (redirect to arch/i386)
│   ├── time.h                  # Time syscalls
│   ├── wait.h                  # Process wait
│   ├── select.h                # File descriptor sets
│   ├── poll.h                  # Poll system call
│   ├── resource.h              # Resource limits
│   ├── statfs.h, statvfs.h     # Filesystem statistics
│   ├── ldt.h                   # LDT (Local Descriptor Table)
│   ├── ioct.h                  # IOctls
│   ├── floopy.h                # Floppy constants
│   ├── futex.h                 # Futex syscalls
│   ├── utsname.h               # System information
│   ├── sysinfo.h               # System information
│   ├── cdefs.h                 # Common definitions/macros
│   └── ...                     # Other sys headers
└── arch/i386/
    └── syscall.h               # i386-specific syscall numbers
```

## Core Components

### 1. crt0.S - Program Entry Point

**Purpose:** Provides the `_start` symbol for program entry, handling:
- Argument parsing (`argc`, `argv`, `envp`)
- Stack setup (initial stack pointer, initial stack)
- Environment pointer (`environ` variable)
- Calling `main(argc, argv, envp)` as entry point
- Cleanup and program exit handling

**ABI:** i386 stack-based calling convention (arguments on stack, right-to-left)

### 2. syscall.S - Syscall Wrappers

**Purpose:** Architecture-specific wrapper functions for system calls using `int $0x80`:
- `_syscall0` through `_syscall6` (0-6 argument variants)
- Uses stack-based argument passing (matching i386 ABI)
- Registers saved/restored via C wrapper functions

**Example:**
```assembly
.globl _syscall1
_syscall1:
    push %ebp
    mov %esp, %ebp
    push %ebx          # arg1
    push %eax          # syscall number
    push %ebp
    mov $0x80, %eax    # syscall number
    int $0x80
    ...
```

### 3. setjmp.S - Longjmp/Setjmp

**Purpose:** Implements `setjmp`/`longjmp` for non-local jumps:
- **jmp_buf layout:** 6 integers (ebx, esi, edi, ebp, esp, eip)
- Captures/restores CPU registers for context switching
- Uses `exception_handler` for `longjmp` with `exception_type`

**Design:** Stack-based register saving (matches i386 ABI)

### 4. stdlib.c - Memory Allocator

**Purpose:** Custom heap allocator with:
- Linked-list block management
- 16-byte alignment
- mmap-based allocation (4096-byte page size)
- Coalescing and splitting strategies
- Process-wide entropy for `arc4random()`

**APIs:** `malloc`, `free`, `realloc`, `qsort`, `rand`, `srand`, `atexit`

### 5. string.c - String Operations

**Purpose:** Optimized string/memory functions:
- `memcpy`, `memmove`, `memset` (optimized)
- `strcat`, `strncpy`, `strcmp`, `strncmp`
- `strcpy`, `strdup`, `strchr`, `strstr`
- Uses inline assembly for performance-critical functions

### 6. sys.c - System Call Wrappers

**Purpose:** POSIX syscall wrappers:
- File I/O (`open`, `read`, `write`, `close`, `stat`, `unlink`)
- Process control (`fork`, `exec`, `exit`, `wait`, `getpid`)
- File control (`chown`, `chmod`, `access`, `link`, `symlink`)
- Resource management (`mmap`, `munmap`, `brk`)

### 7. stdio/ - Standard I/O

**Purpose:** Full printf/scanf implementation:
- **printf.c:** `printf`, `vprintf`, `sprintf`, `vsprintf`, `snprintf`, `vsnprintf`
- **scanf.c:** `scanf`, `fscanf`, `sscanf`, `vscanf`, `vsscanf`
- **stdio_core.c:** FILE stream I/O, buffering (BUFSIZ = 1024), `fdopen`, `fopen`, `popen`, `pclose`
- **stdstreams.c:** stdin/stdout/stderr initialization

**Design:** Stream-based I/O with buffering, popen support

### 8. time/time.c - Time Functions

**Purpose:** Comprehensive time/date handling:
- `gmtime_r`, `localtime_r`, `mktime` (POSIX time)
- Full timezone parsing (TZ environment variable)
- DST calculation (US rules: M3.2.0, M11.1.0)
- `strftime`, `strptime` (date formatting/parsing)
- `clock`, `difftime` (clock ticks)
- `asctime_r`, `ctime_r` (human-readable time)

**Design:** Supports TZ environment variable, full DST calculation

### 9. div64.c - 64-bit Division

**Purpose:** 64-bit division routines (no undefined behavior):
- `__udivdi3` (unsigned 64-bit division)
- `__umoddi3` (unsigned 64-bit modulo)
- `__divdi3` (signed 64-bit division with overflow handling)
- `__moddi3` (signed 64-bit modulo)

**Implementation:** Software bit-by-bit division (no hardware dependency)

### 10. sysctl_helpers.c - System Control

**Purpose:** sysctl helper functions:
- `sysctl`, `sysctlbyname`, `sysctlnametomib`
- Typed wrappers: `sysctl_int`, `sysctl_uint`, `sysctl_quad`, `sysctl_string`
- `sysctl_get_buf`, `sysctlbyname_get_buf` (buffer management)
- Automatic buffer growth with `realloc`

**Design:** MIB-based name translation, automatic buffer management

## Design Patterns

### 1. Minimal, Freestanding Design

The libc follows a minimal, freestanding approach:
- No locale support (returns "C" locale)
- Simple error handling (global `errno` variable)
- Minimal memory allocator (not optimized)
- Chacha20-based `arc4random()` for randomness
- No threading primitives yet

### 2. i386-Specific Architecture

The library is designed for i386 architecture:
- Stack-based calling convention (right-to-left argument passing)
- `int $0x80` for system calls
- No register-based ABI (unlike x86_64)
- GDT/TLS support for thread-local storage

### 3. Build System

Uses recursive Makefile pattern:
- `make -C lib/c` builds the entire library
- Single static library `libc.a` containing all compiled objects
- Architecture-specific code in `lib/c/arch/i386/`
- No shared libraries (freestanding only)

### 4. Header Organization

Headers follow standard POSIX organization:
- Library headers in `include/` (stdlib, string, stdio, etc.)
- System headers in `include/sys/` (types, stat, mman, etc.)
- Architecture-specific headers in `include/arch/i386/` (syscall)

### 5. System Call Wrappers

Two-tier syscall abstraction:
1. **Architecture-specific:** `_syscall0` through `_syscall6` (syscall.S)
2. **Library wrappers:** Typed wrappers (sys.c, sysctl_helpers.c)

### 6. Buffer Management

Common patterns for buffer management:
- Fixed-size buffers (BUFSIZ = 1024 for stdio)
- Dynamic allocation (sysctl_get_buf with realloc)
- Static buffers (asctime_r, ctime_r)

## Header File Summary

### Library Headers

- **stdlib.h:** Memory allocation, random, conversions (malloc, free, rand, qsort, atexit)
- **string.h:** String operations, memory functions (memcpy, strcmp, strcpy)
- **unistd.h:** POSIX calls (fork, exec, file I/O)
- **stdio.h:** File I/O (printf, scanf, fread, fwrite)
- **ctype.h:** Character classification (isdigit, isalpha, etc.)
- **errno.h:** Error codes (errno, strerror)
- **signal.h:** Signal handling (signal, kill, sigaction)
- **time.h:** Time/date functions (time, strftime, gmtime)

### System Headers

- **sys/types.h:** System types (pid_t, uid_t, off_t, time_t, etc.)
- **sys/stat.h:** File statistics (stat_t, st_mode, st_nlink)
- **sys/mman.h:** Memory mapping (mmap, munmap, PROT_* flags)
- **sys/sysctl.h:** System control (CTL_* constants, sysctl)
- **sys/syscall.h:** Syscall numbers (redirects to arch/i386/syscall.h)
- **sys/unistd.h:** Syscall constants (SYS_* numbers)

### Architecture Headers

- **arch/i386/syscall.h:** i386 syscall numbers (SYS_* constants)
- **arch/i386/syscall.S:** Syscall assembly wrappers
- **arch/i386/crt0.S:** Program entry point

## Implementation Notes

### 1. Memory Allocation (stdlib.c)

- Uses mmap(2) for large allocations (4096-page alignment)
- Linked-list block management with coalescing
- 16-byte alignment (fits most data types)
- Process-wide arc4random() with chacha20

### 2. Standard I/O (stdio/)

- Stream buffering with BUFSIZ = 1024 bytes
- Full printf format specifiers (%d, %x, %s, %p, %ld, etc.)
- Pop open/close with process spawning
- fdopen/fopen/fdopen for file streams

### 3. Time Functions (time/time.c)

- Full timezone parsing (TZ environment variable)
- DST calculation (US rules: M3.2.0, M11.1.0)
- strftime/strptime (date formatting/parsing)
- Local time conversion with timezone support

### 4. Syscall Wrappers (syscall.S)

- 0-6 argument variants (_syscall0 to _syscall6)
- Stack-based argument passing (i386 ABI)
- int $0x80 for system calls
- Registers preserved across syscall

### 5. 64-bit Division (div64.c)

- No hardware division instructions (portable)
- Software bit-by-bit division
- Overflow handling for signed division
- __builtin_trap for divide-by-zero

## Build Patterns

### Makefile Structure

```makefile
# Top-level Makefile
all: lib/c sys bin sbin
  # - lib/c: libc library
  # - sys: kernel library
  # - bin: userland binaries
  # - sbin: system utilities

# lib/c/Makefile
OBJ := stdlib.o string.o sys.o sysctl_helpers.o ...
ARCH_OBJS := arch/i386/crt0.S arch/i386/syscall.S ...
STDIO_OBJS := stdio/printf.o stdio/scanf.o ...
all: libc.a
libc.a: $(OBJ) $(ARCH_OBJS) $(STDIO_OBJS)
```

### Compiler Flags

- `-m32` for i386 compilation
- `-nostdlib` for freestanding environment
- `-fno-builtin` to avoid built-in functions
- `-O2` for optimization

### Libraries

- **libc.a:** Static library (all library code)
- **libsys.a:** System call wrappers (syscall wrappers)
- No shared libraries (freestanding only)

## Future Work

- **x86_64 Support:** Add x86_64-specific syscall wrappers
- **Threading:** Implement pthread primitives
- **Localization:** Add locale support
- **Math Library:** Implement math.h functions
- **Character Sets:** Add wchar.h support

## References

- [C Standard Library](https://en.wikipedia.org/wiki/C_standard_library)
- [POSIX System Calls](https://pubs.opengroup.org/onlinepubs/9699995519/)
- [i386 Architecture](https://www.intel.com/content/www/us/en/developer/articles/technical/64-bit-intel-architecture-programming.html)
- [GNU C Library](https://www.gnu.org/software/libc/)
- [BSD libc](https://www.freebsd.org/cgi/man.cgi?query=libc)
