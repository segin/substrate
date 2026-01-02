# /dev/tty Specification

## Overview
`/dev/tty` is a pseudo-device that represents the controlling terminal of the current process. In TestUnix, it currently maps to the combined input of the PS/2 keyboard and the output of the VGA/Framebuffer console.

## Implementation
- **Read:** Pops characters from the global keyboard circular buffer. Non-blocking (returns 0 if empty).
- **Write:** Forwards data to `vga_write()`, which automatically selects between VGA text mode and the graphical framebuffer.

## VFS Integration
- Registered as a character device (`FS_CHARDEVICE`) in DevFS.

## Constraints
- Does not yet support `termios` line discipline (canonical mode, echoing, etc.).
- Input is currently shared across all processes (no per-session TTYs).
- Read is non-blocking (does not sleep until data arrives).
