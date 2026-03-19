# Console and UART Bring-Up

## Scope

This document describes the current console stack for early boot, text/graphics console output, and serial console routing in `sys/kern/main.c`, `sys/drivers/console/console.c`, and `sys/drivers/console/uart/`.

## Console Layers

Substrate currently has three distinct console phases:

1. early boot UART printing
2. registered kernel console backends
3. tty-backed `/dev` exposure for userspace stdio

These phases overlap during bring-up but serve different purposes.

## Early Boot Output

Before the main console framework is initialized, the kernel uses `early_uart_print()` for crash-safe bring-up messages such as:

- `KMAIN START`
- PMM / VM milestone tracing
- early exception diagnostics

This path exists so faults before full GDT/IDT/console initialization still produce output.

## Runtime Console Selection

The kernel recognizes a `console=` command-line parameter with the following current values:

- `console0`: keep the default console stack
- `serial0`
- `serial1`
- `serial2`
- `serial3`

`serial0..serial3` map to:

- `serial0` -> COM1
- `serial1` -> COM2
- `serial2` -> COM3
- `serial3` -> COM4

When a serial console is selected, `uart_select_port()` chooses the COM port before `uart_init()`.

## Serial Debug Mirroring

Serial output is enabled when either of these conditions is true:

- `serial_debug` is present on the kernel command line
- `console=serialN` is selected

When enabled, the UART console backend is registered with the console framework and kernel `kprint()` output is mirrored there.

## Console Backend Order

The current `kmain()` flow is:

1. `console_init()`
2. `hw_text_init()` for VGA text registration
3. optional `uart_select_port()`
4. `uart_init()`
5. optional `console_register(uart_get_console())`
6. later `fb_init()` once paging and Multiboot framebuffer data are ready

This means VGA/hw-text is available as the default screen console, while serial is an optional mirrored or selected backend.

## Userspace Console Exposure

After VFS comes up, the kernel registers the console in devfs and attaches stdio for init/userspace through the tty layer.

Relevant surfaces:

- `/dev/tty`
- console stdio for init
- `/dev/comm/serial0` .. `/dev/comm/serial3` for ports that actually probe present

The console tty also carries job-control foreground-group state so interactive shells see consistent `getpgrp()` / `tcgetpgrp()` behavior.

## UART Device Namespace

UART character devices identify themselves under the communication subsystem:

- `/dev/comm/serial0`
- `/dev/comm/serial1`
- `/dev/comm/serial2`
- `/dev/comm/serial3`

This keeps serial devices aligned with the broader `/dev/comm/*` namespace used for communications hardware.

Substrate only initializes and publishes UART device nodes for ports that can
be distinguished as present. Legacy ISA probe results are preferred once the
bus model is online; early console bring-up falls back to direct UART scratch
register probing so `console=serialN` can degrade cleanly on systems where the
selected COM port does not exist.

## Modem Status

The UART devfs nodes expose modem-control and modem-status signals through
`TIOCMGET`, `TIOCMSET`, `TIOCMBIS`, and `TIOCMBIC`.

Current `TIOCMGET` reporting includes:

- `TIOCM_CTS`
- `TIOCM_DSR`
- `TIOCM_CD`
- `TIOCM_RI`
- `TIOCM_DTR`
- `TIOCM_RTS`

The implementation reads MCR and MSR directly from the selected UART port and
maps the line state into the Substrate termios modem-bit API.
