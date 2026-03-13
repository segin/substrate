# /dev/tty Specification

## Overview
`/dev/tty` is a pseudo-device that represents the controlling terminal of the current process. In Substrate, it currently maps to the combined input of the PS/2 keyboard and the output of the VGA/Framebuffer console.

## Implementation
- **Read:** Uses the kernel TTY core to provide canonical and raw input processing with blocking reads.
- **Write:** Routes output through the TTY line discipline and then into the console backend stack.
- **Line Discipline:** Supports `termios` input/output flags (canonical mode, echo, signal generation, and flow control).
- **Output State:** Tracks the current output column so tab expansion and CR/LF post-processing derive from stream state, not `winsize`.
- **Signal Semantics:** Interrupted foreground/background TTY operations surface `-EINTR` to callers instead of leaking internal sentinel values.

## TTY Driver Interface
Console and future TTY devices are implemented via the `tty_driver` callbacks:
- `install` / `remove`: Allocate or release per-TTY private data.
- `open` / `close`: Hardware initialization and shutdown.
- `write` / `put_char`: Output paths (bulk vs single character).
- `flush_chars`: Trigger hardware transmission.
- `write_room` / `chars_in_buffer`: Report output buffer availability and pending bytes.
- `ioctl`: Driver-specific controls.
- `throttle` / `unthrottle`: Hardware flow-control hooks.

## VFS Integration
- Registered as a character device (`FS_CHARDEVICE`) in DevFS.

## Constraints
- TTY control structures are allocated from `kmalloc`, not raw PMM pages, so they follow normal kernel object lifetime and never depend on low physical memory reuse.
- The core validates `struct tty` magic and driver callback targets before indirect calls, so corruption degrades to `-EIO` instead of executing low or invalid addresses.
- Input is currently shared across all processes (no per-session TTYs).
- The active hardware text VT permanently owns its TTY object. `/dev/console` and keyboard delivery resolve through the active VT first and fall back to the legacy console pointer only when no VT-backed console exists.
