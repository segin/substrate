# Substrate `as` Per-Architecture Reference Appendix

This appendix summarizes what the in-tree assembler assembles for each machine, as exercised by `tests/usr.bin/as/`.  It says what there is; `docs/specs/as_spec.md` says what was asked for, and its Appendix C lists where the two part.

## x86 / i386 Baseline

Core encoding families:
- Legacy ModR/M + SIB + displacement addressing forms, in 32-bit code and, after `.code16`, in 16-bit.
- Operand/address-size and segment override prefixes.
- Integer data movement, arithmetic/logic, control-flow, stack, flags, string, and I/O classes; x87.

Relocation families:
- `R_386_32`, `R_386_PC32`, `R_386_16`, `R_386_8`, `R_386_GOT32`, `R_386_GOT32X`, `R_386_PLT32`, `R_386_GOTOFF`, `R_386_GOTPC`.
- TLS: `R_386_TLS_GD`, `R_386_TLS_LDM`, `R_386_TLS_IE`, `R_386_TLS_LE` and the others the operators of `as_reloc_op.c` name.

## x86-64 (v1-v4)

Core and extension families:
- REX/RIP-relative and x86-64 base forms.
- VEX (AVX/AVX2/BMI/FMA/F16C) and EVEX (AVX-512) prefix packing, from one table (`as_x86_vec_table.c`).
- x86-64-v2: `POPCNT`, SSE3/SSSE3/SSE4.1/SSE4.2 sets.
- x86-64-v3: AVX/AVX2/BMI1/BMI2/F16C/FMA/LZCNT/MOVBE/XSAVE families.
- x86-64-v4: AVX-512F/BW/CD/DQ/VL families.

Relocation families:
- `R_X86_64_64`, `R_X86_64_PC32`, `R_X86_64_32`, `R_X86_64_32S`, `R_X86_64_16`, `R_X86_64_8`.
- GOT/PLT: `R_X86_64_GOT32`, `R_X86_64_PLT32`, `R_X86_64_GOTPCREL`, `R_X86_64_GOTPCRELX`, `R_X86_64_REX_GOTPCRELX`.
- TLS: `R_X86_64_TLSGD`, `R_X86_64_TLSLD`, `R_X86_64_GOTTPOFF`, `R_X86_64_TPOFF32`.

## ARMv7 (AArch32)

Chosen by running the assembler as `arm-as` or with `--target=arm`.  `tests/usr.bin/as/arm_forms.txt` is the list of forms, each held to llvm-mc's bytes.

Encoding families:
- ARM state (`as_arm.c`): data processing with every form of the second operand; multiply and divide; loads and stores of a word, a byte, a half word and two words, with every addressing mode; loads and stores of several registers by every name, `push` and `pop`; branches and `bx`/`blx`; `svc`, `bkpt`, `mrs`, `msr`, `mrc`, `mcr`, the hints, `adr`.  A mnemonic's `s` and condition in either order.
- Thumb-2 (`as_arm_thumb.c`), after `.thumb` or with `-mthumb`: data processing in 16 bits and in 32, the flags set by the mnemonic and by the IT block; shifts, `mul`, `movw`/`movt`; loads and stores; `push`/`pop`; branches (32 bits unless `.n`, not relaxed), `cbz`/`cbnz`, `it`; `svc`, `bkpt`, the hints.
- VFP and NEON (`as_arm_simd.c`): arithmetic, fused multiply, comparisons, conversions, moves between registers, to and from core registers and of numbers, `vldr`/`vstr`, `vpush`/`vpop`, `vldm`/`vstm`; the vector arithmetic, logic, comparisons and shifts, `vdup`, `vld1`/`vst1`.
- `ldr reg, =value` with literal pools at `.ltorg`, `.pool` and the end of the section.

Relocation families:
- Data: `R_ARM_ABS32`, `R_ARM_REL32`, `R_ARM_ABS16`, `R_ARM_ABS8`.
- ARM: `R_ARM_CALL`, `R_ARM_JUMP24`, `R_ARM_MOVW_ABS_NC`, `R_ARM_MOVT_ABS`.
- Thumb: `R_ARM_THM_CALL`, `R_ARM_THM_JUMP24`, `R_ARM_THM_JUMP19`, `R_ARM_THM_JUMP11`, `R_ARM_THM_JUMP8`, `R_ARM_THM_MOVW_ABS_NC`, `R_ARM_THM_MOVT_ABS`.

Not assembled: GOT, PLT and TLS relocations; the unwinding tables (`.fnstart` and the rest are refused); mapping symbols.

## AArch64 (ARMv8.0)

Chosen by running the assembler as `aarch64-as` or with `--target=aarch64`.

Encoding families:
- `as_a64.c`: add and subtract with a number, a shifted register and an extended register; the logical instructions with a bitmask number and a shifted register; `mov`, `movz`, `movn`, `movk`; shifts and bit fields; multiply, divide and the two-register instructions; conditional selection and comparison; loads and stores of one register (scaled, unscaled, indexed, pre- and post-indexed, by label) and of two; branches, `cbz`, `tbz`, `ret`, `br`, `blr`; `adr`/`adrp`; `svc`, `hvc`, `smc`, `brk`, the hints and barriers; `mrs` and `msr` with the system registers by name or by number.
- `as_a64_simd.c`: the arithmetic, comparison, conversion and rounding of scalar floats, `fmov`, `fcsel`, fused multiply; vector arithmetic, logic, comparisons and shifts; `movi`, `dup`, `ins`, `umov`, `smov`, `addv`; `ld1`/`st1`.
- `ldr reg, =value` with literal pools.

Relocation families:
- Data: `R_AARCH64_ABS64`, `R_AARCH64_ABS32`, `R_AARCH64_ABS16`, `R_AARCH64_PREL64`, `R_AARCH64_PREL32`, `R_AARCH64_PREL16`.
- `R_AARCH64_ADR_PREL_LO21`, `R_AARCH64_ADR_PREL_PG_HI21`, `R_AARCH64_ADD_ABS_LO12_NC`, `R_AARCH64_LDST8/16/32/64/128_ABS_LO12_NC`, `R_AARCH64_LD_PREL_LO19`.
- MOVW: `R_AARCH64_MOVW_UABS_G0`, `G0_NC`, `G1`, `G1_NC`, `G2`, `G2_NC`, `G3`.
- Branch/test: `R_AARCH64_JUMP26`, `R_AARCH64_CALL26`, `R_AARCH64_CONDBR19`, `R_AARCH64_TSTBR14`.

Not assembled: anything of ARMv8.1 (LSE atomics, RDMA, LOR); `dc`, `ic`, `at`, `tlbi`; GOT and TLS relocations; mapping symbols.

## `.note.gnu.property`

The assembler writes none of its own, at any `-march`: an object does not say which x86-64 level its instructions need.  (This appendix said that it did, with `GNU_PROPERTY_X86_ISA_1_NEEDED`; it never has, and GNU `as` writes no such note by itself either.)
