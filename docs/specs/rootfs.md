# Root Filesystem Staging Specification

## Overview
This document defines the staging and deployment rules for the Substrate root filesystem.

## Staging Rules
- `dist/` is reserved for the contents of a real Substrate target root filesystem.
- Boot media artifacts (e.g., `sys/kernel.flp`, ad hoc boot disks, scratch bring-up images) are **not** architecture-level rootfs staging and must not be stored under `dist/` unless they are themselves installed files inside `/`.
- Target filesystem image generation is managed by `build-rootfs.sh`.

## Layout
The staging directory follows the standard Unix hierarchy:
- `/bin`: Essential user binaries.
- `/sbin`: System administration binaries.
- `/etc`: Configuration files.
- `/usr/bin`: Extended user tools.
- `/usr/lib`: Shared libraries.
- `/usr/man`: Manual pages.
- `/proc`, `/sys`, `/dev`: Mount points for pseudo-filesystems.
