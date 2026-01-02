# FreeBSD kinfo_proc Specification

## Overview
`kinfo_proc` is a core structure in FreeBSD's `libkvm` and `sysctl` interfaces, used to export process information from the kernel to userspace. This component implements the specific layout for FreeBSD 14.3 compatibility on i386.

## Design
- **ABI Stability:** The structure layout (offsets and sizes) must strictly match the FreeBSD 14.3 `sys/sys/user.h` definition.
- **Data Mapping:** Fields from the native TestUnix `process_t` are mapped to the corresponding `ki_*` fields.
- **Completeness:** Includes all spare fields and substructures (e.g., `timeval`, `sigset`) required for binary compatibility.

## API (Internal)
### `void map_proc_to_kinfo(process_t *p, struct kinfo_proc *ki)`
Translates a native process structure into the FreeBSD-compatible format.

## Constraints
- Targeting FreeBSD 14.3 (i386) specifically.
- Some fields may be stubbed if no equivalent exists in the native kernel (e.g., jail IDs).
