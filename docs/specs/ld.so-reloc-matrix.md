# ld.so Relocation Matrix (Substrate)

This matrix enumerates relocation types `ld.so` must support per architecture.
The i386 table is mandatory for the current target; the x86_64 table documents
planned support. Each entry lists the relocation formula, typical usage, and
notes on processing order.

Legend:
- **S**: symbol value
- **A**: addend (implicit for REL, explicit for RELA)
- **P**: place (relocation address)
- **B**: base address (load bias)
- **GOT**: GOT base
- **L**: PLT entry
- **TP**: thread pointer (TLS)

## i386 (System V ABI, REL)
| Relocation | Formula | Use | Notes |
| --- | --- | --- | --- |
| `R_386_NONE` | — | No-op | Ignored. |
| `R_386_32` | S + A | Absolute | Used for data references. |
| `R_386_PC32` | S + A - P | PC-relative | Used for call/jmp relocs. |
| `R_386_GOT32` | S + A - GOT | GOT entry | For position-independent data. |
| `R_386_GOTOFF` | S + A - GOT | GOT offset | Used for GOT-relative data. |
| `R_386_GOTPC` | GOT + A - P | GOT base | Computes GOT-relative base. |
| `R_386_PLT32` | L + A - P | PLT entry | For position-independent calls. |
| `R_386_GLOB_DAT` | S | GOT entry | Non-PLT symbol resolution. |
| `R_386_JMP_SLOT` | S | PLT/GOT | Resolved on first call unless bound now. |
| `R_386_RELATIVE` | B + A | Relative | Fast path; no symbol lookup. |
| `R_386_COPY` | S | Copy reloc | Executable copies DSO data. |
| `R_386_16` | S + A | Absolute | Truncated to 16 bits. |
| `R_386_PC16` | S + A - P | PC-relative | Truncated to 16 bits. |
| `R_386_8` | S + A | Absolute | Truncated to 8 bits. |
| `R_386_PC8` | S + A - P | PC-relative | Truncated to 8 bits. |
| `R_386_TLS_TPOFF` | S + A - TP | TLS LE | Offset from thread pointer. |
| `R_386_TLS_IE` | S + A - GOT | TLS IE | Offset via GOT entry. |
| `R_386_TLS_GOTIE` | S + A - GOT | TLS IE | GOT entry for TPOFF. |
| `R_386_TLS_LE` | S + A - TP | TLS LE | Local-exec model. |
| `R_386_TLS_GD` | GD | TLS GD | Global dynamic descriptor. |
| `R_386_TLS_LDM` | LD | TLS LD | Local dynamic module ref. |
| `R_386_TLS_LDO_32` | S + A | TLS LD | Offset within TLS block. |

## i386 Additional Notes
- `R_386_RELATIVE` must be applied before any symbol-dependent relocations.
- `R_386_COPY` must be applied after all other relocations across all DSOs.
- `R_386_TEXTREL` is not a relocation; `DT_TEXTREL` is allowed with warning.

## x86_64 (System V AMD64 ABI, RELA - planned)
| Relocation | Formula | Use | Notes |
| --- | --- | --- | --- |
| `R_X86_64_NONE` | — | No-op | Ignored. |
| `R_X86_64_64` | S + A | Absolute | 64-bit absolute relocation. |
| `R_X86_64_PC32` | S + A - P | PC-relative | 32-bit signed displacement. |
| `R_X86_64_GOT32` | S + A - GOT | GOT entry | 32-bit GOT offset. |
| `R_X86_64_PLT32` | L + A - P | PLT entry | 32-bit signed displacement. |
| `R_X86_64_GLOB_DAT` | S | GOT entry | Non-PLT symbol resolution. |
| `R_X86_64_JUMP_SLOT` | S | PLT/GOT | Resolved on first call unless bound now. |
| `R_X86_64_RELATIVE` | B + A | Relative | Fast path; no symbol lookup. |
| `R_X86_64_COPY` | S | Copy reloc | Executable copies DSO data. |
| `R_X86_64_GOTPCREL` | S + A - P | GOT | RIP-relative GOT access. |
| `R_X86_64_32` | S + A | Absolute | Truncated to 32 bits. |
| `R_X86_64_32S` | S + A | Absolute | Sign-extended to 32 bits. |
| `R_X86_64_16` | S + A | Absolute | Truncated to 16 bits. |
| `R_X86_64_8` | S + A | Absolute | Truncated to 8 bits. |
| `R_X86_64_DTPMOD64` | TLS MOD | TLS GD/LD | Module ID for TLS. |
| `R_X86_64_DTPOFF64` | TLS OFF | TLS GD/LD | Offset within TLS block. |
| `R_X86_64_TPOFF64` | TLS OFF | TLS LE | Offset from thread pointer. |
| `R_X86_64_TLSGD` | GD | TLS GD | GD descriptor. |
| `R_X86_64_TLSLD` | LD | TLS LD | LD descriptor. |
| `R_X86_64_GOTTPOFF` | TLS OFF | TLS IE | GOT entry for TPOFF. |
| `R_X86_64_TPOFF32` | TLS OFF | TLS LE | Offset from TP (truncated). |
| `R_X86_64_DTPOFF32` | TLS OFF | TLS GD/LD | Offset (truncated). |

## Processing Notes
- Apply `R_*_RELATIVE` before any symbol lookups.
- Apply non-PLT relocations before PLT relocations.
- Apply `R_*_COPY` last (after all DSOs are relocated).
- For TLS relocations, ensure module IDs and TLS block layout are fixed before
  applying relocations that reference them.

## ABI Differences (i386 vs x86_64)
- **Relocation format:** i386 uses REL; x86_64 uses RELA.
- **Pointer size:** 32-bit vs 64-bit affects truncation rules.
- **TLS ABI:** i386 uses GS segment; x86_64 uses FS base.
- **PLT/GOT conventions:** i386 PLT stubs are absolute; x86_64 uses RIP-relative
  addressing with GOTPCREL relocations.

## Acceptance Criteria
- The relocation matrix covers all targeted architectures and expected
  relocations for i386 (mandatory) and x86_64 (planned).
