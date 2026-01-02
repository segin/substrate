# COFF Loader Specification

## Overview
The COFF (Common Object File Format) Loader is responsible for parsing and loading 32-bit COFF executables into memory. This format is primarily used by Xenix/386 and other System V-derived systems.

## Design
- **Header Parsing:** Reads the COFF file header (`coff_filehdr_t`) and optional A.OUT header (`coff_aouthdr_t`).
- **Section Management:** Iterates through section headers (`coff_scnhdr_t`) to identify `.text`, `.data`, and `.bss` segments.
- **Loading:** Maps segments into the process's virtual address space using the VM subsystem.
- **Relocation:** (Future) Support for COFF relocation entries if needed for dynamic linking.

## API
### `int coff_load_file(void *file, uint32_t size)`
- `file`: Pointer to the raw COFF file data in kernel memory.
- `size`: Size of the COFF file.
- **Returns:** 0 on success, -1 on error.

## Constraints
- i386 specific (currently supports only `COFF_MAGIC_I386`).
- Requires functional `vm_map` and `vm_fault` for segment mapping.
