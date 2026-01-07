# 9P (9P2000.L) Client Specification

## Overview
The 9P client enables Substrate to access remote filesystems over a network or virtualized transport (e.g., VirtIO-9P). It follows the 9P2000.L protocol extension.

## Implementation
- **VFS Integration:** Translates VFS calls into 9P messages (`TWALK`, `TOPEN`, `TREAD`, etc.).
- **Protocol:** Uses a header-based message format with 32-bit size, 8-bit type, and 16-bit tag.
- **Mounting:** Supports mounting remote exports via `vfs_mount()`.

## API
### `void p9_init(void)`
Registers the 9P filesystem type with the VFS.

## Constraints
- Transport layer (VirtIO/TCP) is currently stubbed.
- Initial prototype only supports a subset of 9P messages.
- No support for authentication (`TAUTH`).
