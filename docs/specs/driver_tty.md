# /dev/tty Specification

## Overview
`/dev/tty` is a pseudo-device that represents the controlling terminal of the current process. In Substrate, it currently maps to the combined input of the PS/2 keyboard and the output of the VGA/Framebuffer console.

## Implementation
- **Initialization:** `tty_init()` clears the global TTY slot table before device allocation begins.
- **Raw Input Queue:** Incoming hardware bytes land in a circular `raw_buf`, so IRQ-side producers can wrap and continue feeding the line discipline without shifting storage.
- **Output Queue:** Driver-facing output is staged in a `write_buf` FIFO and drained in-order through the active `tty_driver`, so bursty writers do not have to synchronize directly with hardware pacing.
- **Canonical Buffer:** In `ICANON` mode, `canon()` cooks one delimited raw line into `read_buf`, where readers consume post-edited bytes rather than peeking directly at IRQ input state.
- **IXOFF Watermarks:** With `IXOFF` enabled, the raw queue asserts flow control at the high-water mark with `VSTOP`, then releases it at the low-water mark with `VSTART` after canonical draining makes room again.
- **Input Mode State:** `c_iflag` defaults to `ICRNL|IXON`, and `TCSETS`/`TCGETS` preserve the configured input-mode bitmask, including `IGNBRK`, `ISTRIP`, `INLCR`, `IGNCR`, `ICRNL`, and `IXON`.
- **Output Mode State:** `c_oflag` defaults to `OPOST|ONLCR`, and callers can round-trip `OPOST`, `ONLCR`, and `OXTABS` through the ioctl surface without losing flag state.
- **Control Mode State:** `c_cflag` defaults to `CREAD|CS8|HUPCL`, and the TTY core preserves configured `CSIZE`, `PARENB`, `CSTOPB`, and `CRTSCTS` bits across `TCSETS`/`TCGETS`.
- **Local Mode State:** `c_lflag` defaults to canonical signal-generating echo mode, and callers can round-trip `ICANON`, `ECHO`, `ECHOE`, `ECHOK`, `ISIG`, and `TOSTOP` through the ioctl surface.
- **Control Character Table:** `c_cc` ships with standard default bindings for interrupt, quit, erase, kill, EOF, start, stop, and word erase, and the full control-character table round-trips through `TCSETS`/`TCGETS`, including `VMIN` and `VTIME`.
- **Parity Status Handling:** The receive path accepts out-of-band input status, strips high bits under `ISTRIP`, and drops parity-failed bytes when `INPCK|IGNPAR` is active instead of queueing bad input into the line discipline.
- **Read:** Uses the kernel TTY core to provide canonical and raw input processing with blocking reads.
- **Write:** Routes output through the TTY line discipline and then into the console backend stack.
- **Line Discipline:** Supports `termios` input/output flags (canonical mode, echo, signal generation, and flow control).
- **Canonical Erase Echo:** `VERASE` in canonical mode emits the standard visual erase sequence `\b`, space, `\b` when `ECHOE` is active, so shells and full-screen programs clear deleted characters instead of leaving stale glyphs on screen.
- **Raw Echo:** In non-canonical mode with `ECHO` enabled, incoming bytes are echoed unchanged rather than being line-edited first.
- **Control-Character Echo:** With `ECHOCTL` enabled, signal-generating control bytes are echoed in caret form such as `^C` before the corresponding foreground-group signal is raised.
- **Canonical Erase Semantics:** In canonical mode, `VERASE` removes the most recent character from the pending cooked line before that line is copied into the read buffer.
- **Canonical Kill Semantics:** In canonical mode, `VKILL` clears the pending cooked line, so subsequent reads see only input typed after the kill operation.
- **Canonical Word Erase:** In canonical mode, `VWERASE` trims trailing spaces at the cursor and then deletes the preceding non-space run, leaving any earlier word separator intact.
- **Canonical EOF Semantics:** In canonical mode, `VEOF` terminates the current cooked line without inserting a byte into the stream. If data is pending, a read returns that data immediately; if the line is empty, the read returns `0`.
- **Output Newline Expansion:** With `OPOST|ONLCR` enabled, output newlines are expanded to carriage-return/newline on the driver side.
- **Output Tab Expansion:** With `OXTABS` enabled, tabs are expanded into spaces based on the current output column rather than being passed through as raw tab bytes.
- **Input Newline Translation:** With `ICRNL` enabled, carriage return received from the hardware path is converted to newline before canonical processing.
- **Software Flow Control:** With `IXON` enabled, received `VSTOP` pauses output emission and received `VSTART` resumes it.
- **Signal Generation Gate:** With `ISIG` enabled, `VINTR`, `VQUIT`, and `VSUSP` generate foreground-group signals; when `ISIG` is cleared, those bytes are delivered as ordinary input instead.
- **Termios Round-Trip:** `TCSETS*` updates the live line-discipline state and `TCGETS` returns the current `termios` image back unchanged.
- **Window Size Signaling:** `TIOCSWINSZ` updates the stored terminal geometry, raises `SIGWINCH` for the foreground process group, and `TIOCGWINSZ` returns the same `winsize` structure back to callers.
- **DevFS Publication:** `tty_register_device()` publishes each TTY as a `0666` character device node with direct read, write, open, close, and ioctl callbacks bound back to the TTY core.
- **Output State:** Tracks the current output column so tab expansion and CR/LF post-processing derive from stream state, not `winsize`.
- **Signal Semantics:** Interrupted foreground/background TTY operations surface `-EINTR` to callers instead of leaking internal sentinel values.

## TTY Driver Interface
Console and future TTY devices are implemented via the `tty_driver` callbacks:
- `install` / `remove`: Allocate or release per-TTY private data. `tty_alloc()` invokes `install` before publishing the slot, and `tty_free()` invokes `remove` during teardown.
- `open` / `close`: Hardware initialization and shutdown. The core calls `open` exactly once on the first active reference and `close` exactly once on the final release.
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
