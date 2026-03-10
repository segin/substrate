# LDT API Usage For Personality Developers

## 1. Scope

This document describes the in-tree LDT API used by personality code on i386.
It is intended for personality implementations such as ELKS that need
process-private 16-bit or otherwise non-native segment layouts.

## 2. Ownership Model

Per-process LDT state lives on `process_t`:

- `proc->ldt`
- `proc->ldt_entry_count`

The lifecycle rules are:

- allocate with `ldt_alloc_process(proc, entry_count)`
- duplicate with `ldt_clone_process(dst, src)` during `fork`
- free with `ldt_free_process(proc)` on exit/reap or replacement
- load into hardware with `ldt_activate(proc)` when the process becomes active

The process manager is responsible for the lifecycle boundaries. Personality
code is responsible for descriptor contents.

## 3. Descriptor Construction

Personality code should populate `struct user_desc` values and then write them
into the process LDT with:

```c
fill_ldt_entry((uint8_t *)proc->ldt + index * 8, &desc);
```

Important contract points:

- descriptors must be ring-3 user descriptors
- conforming code segments are rejected by validation
- call gates are not part of the supported personality API
- limits are byte-granular unless `limit_in_pages` is explicitly requested

## 4. Address Translation Helpers

The supported helpers are:

- `ldt_entry_base()`
- `ldt_entry_limit()`
- `ldt_translate_selector_offset()`

`ldt_translate_selector_offset()` is the safe way to turn a user selector and a
16-bit or 32-bit offset into a linear kernel address when personality code must
copy data from a segmented userspace ABI.

## 5. Switching And Exec

Personality code should not try to manage `lldt` directly.
The correct pattern is:

1. allocate or replace the process LDT
2. populate descriptors
3. call `ldt_activate(proc)` if the process is current

For exec-time replacement, install the new LDT before returning to userspace so
the new image enters with the correct selectors immediately.

## 6. ELKS Usage Pattern

The in-tree ELKS personality uses four descriptors:

- `CS`
- `DS`
- `SS`
- `ES`

The ELKS loader:

- allocates the process LDT
- builds the descriptor layout from the ELKS load plan
- writes the entries with `fill_ldt_entry()`
- activates the LDT before entering 16-bit userspace

The ELKS personality then uses `ldt_translate_selector_offset()` for:

- far callback signal handlers
- near-pointer data access through `DS`
- packed 16-bit `execve` stack decoding

## 7. Safety Rules

- never trust a selector from userspace without validating the TI bit and index
- never compute `base + offset` manually when `ldt_translate_selector_offset()`
  already expresses the intended bounds check
- free replaced or abandoned LDTs through the process lifecycle helpers, not by
  open-coded `kfree()`

## 8. Related Interfaces

- `sys/arch/i386/ldt.c`
- `sys/include/sys/ldt.h`
- `sys/exec/formats/elks_aout.c`
- `sys/exec/perso/perso_elks.c`
