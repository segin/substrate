# Substrate `as` Per-Architecture Reference Appendix

This appendix summarizes the instruction-family coverage staged in the in-tree assembler modules and exercised by `tests/usr.bin/as/`.

## x86 / i386 Baseline

Core encoding families:
- Legacy ModR/M + SIB + displacement addressing forms.
- Operand/address-size and segment override prefixes.
- Integer data movement, arithmetic/logic, control-flow, stack, flags, string, and I/O classes.

Relocation families:
- `R_386_32`, `R_386_PC32`, `R_386_GOT32`, `R_386_PLT32`, `R_386_GOTOFF`, `R_386_GOTPC`.
- TLS: `R_386_TLS_GD`, `R_386_TLS_LDM`, `R_386_TLS_IE`, `R_386_TLS_LE`.

## x86-64 (v1-v4)

Core and extension families:
- REX/RIP-relative and x86-64 base forms.
- VEX (AVX/AVX2/BMI/FMA/F16C) and EVEX (AVX-512) prefix packing.
- x86-64-v2: `POPCNT`, SSE3/SSSE3/SSE4.1/SSE4.2 sets.
- x86-64-v3: AVX/AVX2/BMI1/BMI2/F16C/FMA/LZCNT/MOVBE/XSAVE families.
- x86-64-v4: AVX-512F/BW/CD/DQ/VL families.

Relocation families:
- `R_X86_64_64`, `R_X86_64_PC32`, `R_X86_64_32`, `R_X86_64_32S`.
- GOT/PLT: `R_X86_64_GOT32`, `R_X86_64_PLT32`, `R_X86_64_GOTPCREL`, `R_X86_64_GOTPCRELX`, `R_X86_64_REX_GOTPCRELX`.
- TLS: `R_X86_64_TLSGD`, `R_X86_64_TLSLD`, `R_X86_64_GOTTPOFF`, `R_X86_64_TPOFF32`.

## ARMv7 (AArch32)

Encoding families:
- ARM-state opcode classes and condition fields.
- Thumb/Thumb-2 narrow/wide forms and unified syntax controls.
- Data processing, branch/interwork, load/store addressing modes.
- System/coprocessor barriers/hints and transfer families.
- VFPv3/v4 and NEON instruction families.

Relocation families:
- Core branch and absolute/relative: `R_ARM_ABS32`, `R_ARM_REL32`, `R_ARM_PC24`, `R_ARM_CALL`, `R_ARM_JUMP24`.
- Thumb branch/movw/movt relocation families.
- GOT/PLT/GOTOFF/GOTPC and TLS GD/LDM/IE/LE families.
- `R_ARM_PREL31`.

## AArch64 (ARMv8.0-v8.1)

Encoding families:
- Data processing (immediate/register), branch, load/store, system.
- Advanced SIMD/FP (`NEON`) core families.
- ARMv8.1 extensions: LSE atomics (including store-only aliases), RDMA (`sqrdmlah/sqrdmlsh`), LOR (`ldlar/stllr`), and EL2/TCR register-access patterns used by VHE/HPD-oriented flows.

Relocation families:
- Absolute/PC-relative: `R_AARCH64_ABS*`, `R_AARCH64_PREL*`, `R_AARCH64_ADR_PREL_*`, `R_AARCH64_ADD_ABS_LO12_NC`, `R_AARCH64_LDST*_ABS_LO12_NC`.
- MOVW split-immediate: `R_AARCH64_MOVW_UABS_G0/G1/G2/G3{_NC}`.
- Branch/test: `R_AARCH64_JUMP26`, `R_AARCH64_CALL26`, `R_AARCH64_CONDBR19`, `R_AARCH64_TSTBR14`.
- GOT and TLS families: `R_AARCH64_GOT_LD_PREL19`, `R_AARCH64_ADR_GOT_PAGE`, `R_AARCH64_LD64_GOT_LO12_NC`, plus TLS GD/LD/IE/LE page/add/load variants.

## `.note.gnu.property` x86-64 ISA semantics

For x86-64-v2/v3/v4 objects, the assembler emits `.note.gnu.property` using:
- Property type: `GNU_PROPERTY_X86_ISA_1_NEEDED` (`0xc0008002`).
- Bit flags: `GNU_PROPERTY_X86_ISA_1_BASELINE`, `GNU_PROPERTY_X86_ISA_1_V2`, `GNU_PROPERTY_X86_ISA_1_V3`, `GNU_PROPERTY_X86_ISA_1_V4`.

The encoded mask communicates the minimum ISA level required by the object.
