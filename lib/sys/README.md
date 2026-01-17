# lib/sys - System Call Wrapper Library

Provides libc-style wrappers for Substrate system calls.

## Purpose
Move direct `int $0x80` syscall invocations out of application code
into a proper library, matching the BSD/Linux convention of libc
wrapping raw syscalls.

## Structure
```
lib/sys/
├── Makefile
├── include/
│   └── sys/
│       └── syscall.h    # SYS_* constants and syscall() prototype
├── syscall.S            # Assembly syscall entry point
└── vm86.c               # vm86() wrapper
```

## Usage
```c
#include <sys/syscall.h>
#include <sys/vm86.h>

// Instead of inline asm:
int ret = syscall(SYS_vm86, &info);
// Or with typed wrapper:
int ret = vm86(&info);
```
