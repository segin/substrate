# Linux Compatibility Personality Architecture

This document outlines the design of the Linux binary compatibility layer (Personality) for the Substrate kernel. It details how the kernel mimics the Linux Application Binary Interface (ABI) to run unmodified Linux ELF executables.

## 1. Personality Subsystem

The Substrate kernel defines execution environments as "Personalities". The Linux personality (`PERS_LINUX`) provides:
- **Syscall Dispatch**: Routing interrupt 0x80 calls to Linux-compatible handlers.
- **Signal Emulation**: Translating native kernel signals to Linux signal numbers.
- **ABI Translation**: Marshalling data structures (stat, termios, sockets) between Linux userspace layouts and Substrate native kernel layouts.

## 2. Signal Handling

Linux and Substrate use different numeric values for signals. The compatibility layer performs bidirectional translation.

### 2.1 Signal Number Mapping
Two internal tables govern signal delivery:
- **`native_to_linux_signo`**: Maps internal Substrate signal numbers to Linux ABI values (for delivery to valid Linux processes).
- **`linux_to_native_signo`**: Maps Linux ABI signal numbers to internal Substrate signals (for system calls like `kill`).

**Divergences:**
- **Real-Time Signals**: Linux defines signals 32-64 as real-time. Substrate maps these to internal events or emulates them.
- **Unmapped Signals**: Signals like `SIGPWR` (Linux) have no direct native equivalent and are treated as no-ops or mapped to generic catch-alls.

### 2.2 Signal Sets & Masks
Linux employs a fixed-size 64-bit signal mask (`l_sigset_t`).
- **Input**: Userspace masks are converted to native `sigset_t` before native system calls (`sigprocmask`).
- **Output**: Native masks are converted to `l_sigset_t` before returning to userspace.

### 2.3 Stack Frames
The kernel constructs a "trampoline" on the user stack for signal handlers.
- **`struct linux_sigframe`**: Contains the register state and "retcode" stub for returning from the signal handler.
- **`struct linux_rt_sigframe`**: Extended frame for `RT` signals, containing a `ucontext` structure.

## 3. Networking ABI

The networking compatibility layer bridges the differences in socket address structures and constants.

### 3.1 Domain (Address Family) Translation
Substrate uses BSD-style constants (`AF_INET`, `AF_LOCAL`). Linux uses its own set (`LINUX_AF_...`).
- Translation functions: `linux_domain_to_native()`, `native_domain_to_linux()`.

### 3.2 Socket Address Translation
Primary structural difference:
- **Linux**: `struct sockaddr` { `family` (2), `data` (14) }
- **Substrate**: `struct sockaddr` { `len` (1), `family` (1), `data` (14) }

**Mechanism**:
- **On Entry**: The compatibility layer allocates a native `sockaddr`, sets the `sa_len` field derived from the family, and translates the family constant.
- **On Exit**: The layer copies the data to userspace, discarding `sa_len` and re-translating the family.

## 4. Terminal I/O (TTY)

Linux uses specific `ioctl` commands (0x54XX) and structure layouts.

### 4.1 Termios
The `termios` structure bitmasks differ significantly.
- **Bit Mapping**: Each flag (IFLAG, OFLAG, etc.) is individually mapped between the native and Linux bit representations.
- **Control Characters**: The `c_cc` array has different indices for control characters (e.g., `VTIME`, `VMIN`).

## 5. File System ABI

### 5.1 Stat Structures
Linux supports multiple versions of `stat`:
- **Legacy**: 16-bit fields.
- **Standard**: 32-bit fields.
- **Large File (64)**: 64-bit fields.

The native kernel uses a unified 64-bit `stat`. Wrapper functions populate the appropriate Linux userspace structure, truncating values or returning `EOVERFLOW` if necessary (e.g., 64-bit file size in 32-bit struct).

### 5.2 File Flags
`open()` and `fcntl()` flags (e.g., `O_NONBLOCK`, `O_CREAT`) are remapped to native equivalents.
