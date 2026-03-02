# `usr.lib/elfobj` — Multi-Architecture Tasklist

Goal: extend `libelfobj` with complete multi-architecture support for i386, x86-64, ARMv7 (ELF32, `EM_ARM`), AArch64 (ELF64, `EM_AARCH64`), MIPS/MIPS64 (`EM_MIPS`), RISC-V 32/64/128 (`EM_RISCV`), LoongArch 32/64 (`EM_LOONGARCH`), M68K (`EM_68K`), VAX (`EM_VAX`), Alpha (`EM_ALPHA`), PowerPC/PowerPC64 (`EM_PPC`/`EM_PPC64`), and IA-64 (`EM_IA_64`) across all library subsystems — constants, relocation backends, validation, ELF creation, DWARF, link planning, and testing.

---

## 0. Generic Tooling API Support

- [x] Add indexed section accessor `elf_section_get(const elfobj_t *, size_t)` for consumer utilities (`size`, `objdump`, `readelf`) that need stable section iteration.
- [x] Add section address accessor `elf_section_addr(const elf_section_t *)` for utilities that emit per-section address tables (for example SysV `size -A`).

---

## 1. ELF Constants and Header Definitions

### 1a. Machine Types (`elf_private.h` or upstream `elf.h`)
- [x] Define `EM_ARM` (40).
- [x] Define `EM_AARCH64` (183).

### 1b. ARM-Specific ELF Header Flags (`e_flags`)
- [x] `EF_ARM_ABI_VER5` (0x05000000) — EABI version 5.
- [x] `EF_ARM_ABI_FLOAT_HARD` (0x00000400) — hard-float ABI.
- [x] `EF_ARM_ABI_FLOAT_SOFT` (0x00000200) — soft-float ABI.
- [x] `EF_ARM_BE8` (0x00800000) — BE8 data format.
- [x] `EF_ARM_INTERWORK` (0x00000004) — ARM/Thumb interwork support.
- [x] `EF_ARM_APCS_26` (0x00000008).
- [x] `EF_ARM_APCS_FLOAT` (0x00000010).
- [x] `EF_ARM_VFP_FLOAT` (0x00000400).
- [x] `EF_ARM_MAVERICK_FLOAT` (0x00000800).
- [x] Parse and expose `e_flags` for ARM objects via `elf_flags()` accessor.
- [x] Validate `e_flags` ABI version field on read.

### 1c. AArch64-Specific ELF Header Flags
- [x] `EF_AARCH64_CHERI_PURECAP` (reserved).
- [x] AArch64 has no mandatory `e_flags` bits; validate that flags is 0 or recognized optional.

### 1d. ARM Relocation Type Constants
Every relocation type used by GCC/LLVM for ARM targets must be defined:

- [x] `R_ARM_NONE` (0)
- [x] `R_ARM_PC24` (1)
- [x] `R_ARM_ABS32` (2)
- [x] `R_ARM_REL32` (3)
- [x] `R_ARM_LDR_PC_G0` (4)
- [x] `R_ARM_ABS16` (5)
- [x] `R_ARM_ABS12` (6)
- [x] `R_ARM_THM_ABS5` (7)
- [x] `R_ARM_ABS8` (8)
- [x] `R_ARM_SBREL32` (9)
- [x] `R_ARM_THM_CALL` (10)
- [x] `R_ARM_THM_PC8` (11)
- [x] `R_ARM_BREL_ADJ` (12)
- [x] `R_ARM_TLS_DESC` (13)
- [x] `R_ARM_THM_SWI8` (14)
- [x] `R_ARM_XPC25` (15)
- [x] `R_ARM_THM_XPC22` (16)
- [x] `R_ARM_TLS_DTPMOD32` (17)
- [x] `R_ARM_TLS_DTPOFF32` (18)
- [x] `R_ARM_TLS_TPOFF32` (19)
- [x] `R_ARM_COPY` (20)
- [x] `R_ARM_GLOB_DAT` (21)
- [x] `R_ARM_JUMP_SLOT` (22)
- [x] `R_ARM_RELATIVE` (23)
- [x] `R_ARM_GOTOFF32` (24)
- [x] `R_ARM_BASE_PREL` (25) / `R_ARM_GOTPC`
- [x] `R_ARM_GOT_BREL` (26) / `R_ARM_GOT32`
- [x] `R_ARM_PLT32` (27)
- [x] `R_ARM_CALL` (28)
- [x] `R_ARM_JUMP24` (29)
- [x] `R_ARM_THM_JUMP24` (30)
- [x] `R_ARM_BASE_ABS` (31)
- [x] `R_ARM_ALU_PCREL_7_0` (32)
- [x] `R_ARM_ALU_PCREL_15_8` (33)
- [x] `R_ARM_ALU_PCREL_23_16` (34)
- [x] `R_ARM_LDR_SBREL_11_0_NC` (35)
- [x] `R_ARM_ALU_SBREL_19_12_NC` (36)
- [x] `R_ARM_ALU_SBREL_27_20_CK` (37)
- [x] `R_ARM_TARGET1` (38)
- [x] `R_ARM_SBREL31` (39)
- [x] `R_ARM_V4BX` (40)
- [x] `R_ARM_TARGET2` (41)
- [x] `R_ARM_PREL31` (42)
- [x] `R_ARM_MOVW_ABS_NC` (43)
- [x] `R_ARM_MOVT_ABS` (44)
- [x] `R_ARM_MOVW_PREL_NC` (45)
- [x] `R_ARM_MOVT_PREL` (46)
- [x] `R_ARM_THM_MOVW_ABS_NC` (47)
- [x] `R_ARM_THM_MOVT_ABS` (48)
- [x] `R_ARM_THM_MOVW_PREL_NC` (49)
- [x] `R_ARM_THM_MOVT_PREL` (50)
- [x] `R_ARM_THM_JUMP19` (51)
- [x] `R_ARM_THM_JUMP6` (52)
- [x] `R_ARM_THM_ALU_PREL_11_0` (53)
- [x] `R_ARM_THM_PC12` (54)
- [x] `R_ARM_ABS32_NOI` (55)
- [x] `R_ARM_REL32_NOI` (56)
- [x] `R_ARM_ALU_PC_G0_NC` (57)
- [x] `R_ARM_ALU_PC_G0` (58)
- [x] `R_ARM_ALU_PC_G1_NC` (59)
- [x] `R_ARM_ALU_PC_G1` (60)
- [x] `R_ARM_ALU_PC_G2` (61)
- [x] `R_ARM_LDR_PC_G1` (62)
- [x] `R_ARM_LDR_PC_G2` (63)
- [x] `R_ARM_LDRS_PC_G0` (64)
- [x] `R_ARM_LDRS_PC_G1` (65)
- [x] `R_ARM_LDRS_PC_G2` (66)
- [x] `R_ARM_LDC_PC_G0` (67)
- [x] `R_ARM_LDC_PC_G1` (68)
- [x] `R_ARM_LDC_PC_G2` (69)
- [x] `R_ARM_ALU_SB_G0_NC` (70)
- [x] `R_ARM_ALU_SB_G0` (71)
- [x] `R_ARM_ALU_SB_G1_NC` (72)
- [x] `R_ARM_ALU_SB_G1` (73)
- [x] `R_ARM_ALU_SB_G2` (74)
- [x] `R_ARM_LDR_SB_G0` (75)
- [x] `R_ARM_LDR_SB_G1` (76)
- [x] `R_ARM_LDR_SB_G2` (77)
- [x] `R_ARM_LDRS_SB_G0` (78)
- [x] `R_ARM_LDRS_SB_G1` (79)
- [x] `R_ARM_LDRS_SB_G2` (80)
- [x] `R_ARM_LDC_SB_G0` (81)
- [x] `R_ARM_LDC_SB_G1` (82)
- [x] `R_ARM_LDC_SB_G2` (83)
- [x] `R_ARM_MOVW_BREL_NC` (84)
- [x] `R_ARM_MOVT_BREL` (85)
- [x] `R_ARM_MOVW_BREL` (86)
- [x] `R_ARM_THM_MOVW_BREL_NC` (87)
- [x] `R_ARM_THM_MOVT_BREL` (88)
- [x] `R_ARM_THM_MOVW_BREL` (89)
- [x] `R_ARM_TLS_GOTDESC` (90)
- [x] `R_ARM_TLS_CALL` (91)
- [x] `R_ARM_TLS_DESCSEQ` (92)
- [x] `R_ARM_THM_TLS_CALL` (93)
- [x] `R_ARM_PLT32_ABS` (94)
- [x] `R_ARM_GOT_ABS` (95)
- [x] `R_ARM_GOT_PREL` (96)
- [x] `R_ARM_GOT_BREL12` (97)
- [x] `R_ARM_GOTOFF12` (98)
- [x] `R_ARM_GOTRELAX` (99)
- [x] `R_ARM_GNU_VTENTRY` (100)
- [x] `R_ARM_GNU_VTINHERIT` (101)
- [x] `R_ARM_THM_JUMP11` (102)
- [x] `R_ARM_THM_JUMP8` (103)
- [x] `R_ARM_TLS_GD32` (104)
- [x] `R_ARM_TLS_LDM32` (105)
- [x] `R_ARM_TLS_LDO32` (106)
- [x] `R_ARM_TLS_IE32` (107)
- [x] `R_ARM_TLS_LE32` (108)
- [x] `R_ARM_TLS_LDO12` (109)
- [x] `R_ARM_TLS_LE12` (110)
- [x] `R_ARM_TLS_IE12GP` (111)
- [x] `R_ARM_IRELATIVE` (160)
- [x] `R_ARM_RXPC25` (249)
- [x] `R_ARM_RSBREL32` (250)
- [x] `R_ARM_THM_RPC22` (251)
- [x] `R_ARM_RREL32` (252)
- [x] `R_ARM_RABS32` (253)
- [x] `R_ARM_RPC24` (254)
- [x] `R_ARM_RBASE` (255)

### 1e. AArch64 Relocation Type Constants
- [x] `R_AARCH64_NONE` (0)
- [x] `R_AARCH64_ABS64` (257), `R_AARCH64_ABS32` (258), `R_AARCH64_ABS16` (259)
- [x] `R_AARCH64_PREL64` (260), `R_AARCH64_PREL32` (261), `R_AARCH64_PREL16` (262)
- [x] `R_AARCH64_MOVW_UABS_G0` (263), `_G0_NC` (264), `_G1` (265), `_G1_NC` (266), `_G2` (267), `_G2_NC` (268), `_G3` (269)
- [x] `R_AARCH64_MOVW_SABS_G0` (270), `_G1` (271), `_G2` (272)
- [x] `R_AARCH64_LD_PREL_LO19` (273), `R_AARCH64_ADR_PREL_LO21` (274)
- [x] `R_AARCH64_ADR_PREL_PG_HI21` (275), `_NC` (276)
- [x] `R_AARCH64_ADD_ABS_LO12_NC` (277)
- [x] `R_AARCH64_LDST8_ABS_LO12_NC` (278)
- [x] `R_AARCH64_TSTBR14` (279), `R_AARCH64_CONDBR19` (280)
- [x] `R_AARCH64_JUMP26` (282), `R_AARCH64_CALL26` (283)
- [x] `R_AARCH64_LDST16_ABS_LO12_NC` (284), `R_AARCH64_LDST32_ABS_LO12_NC` (285), `R_AARCH64_LDST64_ABS_LO12_NC` (286), `R_AARCH64_LDST128_ABS_LO12_NC` (299)
- [x] `R_AARCH64_MOVW_PREL_G0` (287), `_G0_NC` (288), `_G1` (289), `_G1_NC` (290), `_G2` (291), `_G2_NC` (292), `_G3` (293)
- [x] `R_AARCH64_GOT_LD_PREL19` (309), `R_AARCH64_ADR_GOT_PAGE` (311), `R_AARCH64_LD64_GOT_LO12_NC` (312)
- [x] `R_AARCH64_LD64_GOTPAGE_LO15` (313)
- [x] `R_AARCH64_TLSGD_ADR_PREL21` (512), `R_AARCH64_TLSGD_ADR_PAGE21` (513), `R_AARCH64_TLSGD_ADD_LO12_NC` (514), `R_AARCH64_TLSGD_MOVW_G1` (515), `R_AARCH64_TLSGD_MOVW_G0_NC` (516)
- [x] `R_AARCH64_TLSLD_ADR_PREL21` (517), `R_AARCH64_TLSLD_ADR_PAGE21` (518), `R_AARCH64_TLSLD_ADD_LO12_NC` (519), `R_AARCH64_TLSLD_ADD_DTPREL_HI12` (528), `R_AARCH64_TLSLD_ADD_DTPREL_LO12` (529), `_NC` (530)
- [x] `R_AARCH64_TLSLD_LDST8_DTPREL_LO12` (531), `_NC` (532)
- [x] `R_AARCH64_TLSLD_LDST16/32/64/128_DTPREL_LO12{_NC}` (533–540)
- [x] `R_AARCH64_TLSLD_MOVW_DTPREL_G0{_NC}` (520,521), `_G1{_NC}` (522,523), `_G2` (524)
- [x] `R_AARCH64_TLSIE_MOVW_GOTTPREL_G1` (539), `_G0_NC` (540)
- [x] `R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21` (541), `R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC` (542), `R_AARCH64_TLSIE_LD_GOTTPREL_PREL19` (543)
- [x] `R_AARCH64_TLSLE_MOVW_TPREL_G2` (544), `_G1{_NC}` (545,546), `_G0{_NC}` (547,548)
- [x] `R_AARCH64_TLSLE_ADD_TPREL_HI12` (549), `R_AARCH64_TLSLE_ADD_TPREL_LO12` (550), `_NC` (551)
- [x] `R_AARCH64_TLSLE_LDST8/16/32/64/128_TPREL_LO12{_NC}` (552–561)
- [x] `R_AARCH64_TLSDESC_LD_PREL19` (560), `R_AARCH64_TLSDESC_ADR_PREL21` (561), `R_AARCH64_TLSDESC_ADR_PAGE21` (562), `R_AARCH64_TLSDESC_LD64_LO12` (563), `R_AARCH64_TLSDESC_ADD_LO12` (564), `R_AARCH64_TLSDESC_OFF_G1` (565), `R_AARCH64_TLSDESC_OFF_G0_NC` (566), `R_AARCH64_TLSDESC_LDR` (567), `R_AARCH64_TLSDESC_ADD` (568), `R_AARCH64_TLSDESC_CALL` (569), `R_AARCH64_TLSDESC` (1031)
- [x] Dynamic relocations: `R_AARCH64_COPY` (1024), `R_AARCH64_GLOB_DAT` (1025), `R_AARCH64_JUMP_SLOT` (1026), `R_AARCH64_RELATIVE` (1027), `R_AARCH64_TLS_DTPMOD64` (1028), `R_AARCH64_TLS_DTPREL64` (1029), `R_AARCH64_TLS_TPREL64` (1030), `R_AARCH64_IRELATIVE` (1032)

### 1f. ARM Section Types and Flags
- [x] `SHT_ARM_EXIDX` (0x70000001) — exception index table.
- [x] `SHT_ARM_PREEMPTMAP` (0x70000002).
- [x] `SHT_ARM_ATTRIBUTES` (0x70000003) — build attributes.
- [x] `SHF_ARM_PURECODE` (0x20000000) — execute-only section.
- [x] `PT_ARM_EXIDX` (0x70000001) — exception unwind segment.

### 1g. AArch64 Section Types
- [x] `SHT_AARCH64_ATTRIBUTES` (0x70000003).
- [x] `PT_AARCH64_MEMTAG_MTE` (0x70000002).

### 1h. ARM Special Section Names
- [x] `.ARM.exidx` — exception index table.
- [x] `.ARM.extab` — exception table data.
- [x] `.ARM.attributes` — build attributes.
- [x] `.note.gnu.property` — BTI/PAC properties (AArch64).

### 1i. Expanded x86 Relocation Type Constants

Beyond the currently-implemented core set, add the full x86 relocation roster:

#### i386 Missing Relocations
- [x] `R_386_NONE` (0)
- [x] `R_386_COPY` (5), `R_386_GLOB_DAT` (6), `R_386_JMP_SLOT` (7), `R_386_RELATIVE` (8)
- [x] `R_386_16` (20), `R_386_PC16` (21), `R_386_8` (22), `R_386_PC8` (23)
- [x] `R_386_TLS_DTPMOD32` (35), `R_386_TLS_DTPOFF32` (36)
- [x] `R_386_TLS_LE_32` (33), `R_386_TLS_TPOFF32` (37)
- [x] `R_386_SIZE32` (38)
- [x] `R_386_GOT32X` (43)
- [x] `R_386_IRELATIVE` (42)

#### x86-64 Missing Relocations
- [x] `R_X86_64_COPY` (5), `R_X86_64_GLOB_DAT` (6), `R_X86_64_JUMP_SLOT` (7), `R_X86_64_RELATIVE` (8)
- [x] `R_X86_64_16` (12), `R_X86_64_PC16` (13), `R_X86_64_8` (14), `R_X86_64_PC8` (15)
- [x] `R_X86_64_DTPMOD64` (16), `R_X86_64_DTPOFF64` (17), `R_X86_64_TPOFF64` (18)
- [x] `R_X86_64_TLSLD` (20), `R_X86_64_DTPOFF32` (21)
- [x] `R_X86_64_PC64` (24), `R_X86_64_GOTOFF64` (25), `R_X86_64_GOTPC32` (26)
- [x] `R_X86_64_SIZE32` (32), `R_X86_64_SIZE64` (33)
- [x] `R_X86_64_GOTPCRELX` (41), `R_X86_64_REX_GOTPCRELX` (42)
- [x] `R_X86_64_IRELATIVE` (37)
- [x] `R_X86_64_GOTPC32_TLSDESC` (34), `R_X86_64_TLSDESC_CALL` (35), `R_X86_64_TLSDESC` (36)

### 1j. x86 GNU Property Constants
- [x] `GNU_PROPERTY_X86_ISA_1_NEEDED` (0xc0008002)
- [x] `GNU_PROPERTY_X86_ISA_1_USED` (0xc0010002)
- [x] ISA level bits: `GNU_PROPERTY_X86_ISA_1_BASELINE` (1), `_V2` (2), `_V3` (4), `_V4` (8)
- [x] `GNU_PROPERTY_X86_FEATURE_1_AND` (0xc0000002)
- [x] Feature bits: `GNU_PROPERTY_X86_FEATURE_1_IBT` (1), `_SHSTK` (2)
- [x] `GNU_PROPERTY_AARCH64_FEATURE_1_AND` (0xc0000000)
- [x] AArch64 feature bits: `_BTI` (1), `_PAC` (2)

---

## 2. ARMv7 Relocation Backend (`elf_reloc.c`)

### 2a. `arm_reloc_size()` — Size of Each Relocation Result
- [x] `R_ARM_NONE` → 0
- [x] `R_ARM_ABS32`, `R_ARM_REL32`, `R_ARM_GOTOFF32`, `R_ARM_GOTPC`, `R_ARM_GOT_BREL`, `R_ARM_PLT32`, `R_ARM_CALL`, `R_ARM_JUMP24`, `R_ARM_TARGET1`, `R_ARM_TARGET2`, `R_ARM_PREL31`, `R_ARM_MOVW_ABS_NC`, `R_ARM_MOVT_ABS`, `R_ARM_MOVW_PREL_NC`, `R_ARM_MOVT_PREL`, `R_ARM_ABS32_NOI`, `R_ARM_REL32_NOI` → 4
- [x] `R_ARM_PC24` → 4 (24-bit field in 32-bit instruction)
- [x] `R_ARM_ABS16` → 2
- [x] `R_ARM_ABS12` → 4 (12-bit field in 32-bit instruction)
- [x] `R_ARM_ABS8` → 1
- [x] `R_ARM_THM_CALL`, `R_ARM_THM_JUMP24` → 4 (two 16-bit Thumb instructions)
- [x] `R_ARM_THM_JUMP11` → 2, `R_ARM_THM_JUMP8` → 2
- [x] `R_ARM_THM_MOVW_ABS_NC`, `R_ARM_THM_MOVT_ABS`, `R_ARM_THM_MOVW_PREL_NC`, `R_ARM_THM_MOVT_PREL` → 4
- [x] `R_ARM_THM_JUMP19` → 4, `R_ARM_THM_JUMP6` → 2
- [x] All TLS relocations (`R_ARM_TLS_GD32` through `R_ARM_TLS_LE12`) → 4
- [x] `R_ARM_COPY`, `R_ARM_GLOB_DAT`, `R_ARM_JUMP_SLOT`, `R_ARM_RELATIVE` → 4
- [x] `R_ARM_IRELATIVE` → 4
- [x] `R_ARM_V4BX` → 4 (instruction rewrite)
- [x] All group relocations (`R_ARM_ALU_PC_G*`, `R_ARM_LDR_PC_G*`, `R_ARM_LDRS_PC_G*`, `R_ARM_LDC_PC_G*`, SB variants) → 4

### 2b. `arm_is_pc_relative()` — Identify PC-Relative Relocations
- [x] PC-relative: `R_ARM_PC24`, `R_ARM_REL32`, `R_ARM_PLT32`, `R_ARM_CALL`, `R_ARM_JUMP24`, `R_ARM_THM_CALL`, `R_ARM_THM_JUMP24`, `R_ARM_THM_JUMP19`, `R_ARM_THM_JUMP11`, `R_ARM_THM_JUMP8`, `R_ARM_PREL31`, `R_ARM_MOVW_PREL_NC`, `R_ARM_MOVT_PREL`, `R_ARM_THM_MOVW_PREL_NC`, `R_ARM_THM_MOVT_PREL`, `R_ARM_GOTPC`, `R_ARM_BASE_PREL`, `R_ARM_GOT_PREL`, `R_ARM_REL32_NOI`, all `ALU_PC_G*`/`LDR_PC_G*`/`LDRS_PC_G*`/`LDC_PC_G*`
- [x] Absolute: `R_ARM_ABS32`, `R_ARM_ABS16`, `R_ARM_ABS12`, `R_ARM_ABS8`, `R_ARM_MOVW_ABS_NC`, `R_ARM_MOVT_ABS`, `R_ARM_THM_MOVW_ABS_NC`, `R_ARM_THM_MOVT_ABS`, `R_ARM_ABS32_NOI`, `R_ARM_GOTOFF32`, `R_ARM_GOT_BREL`

### 2c. `arm_is_tls()` — Identify TLS Relocations
- [x] TLS: `R_ARM_TLS_DTPMOD32`, `R_ARM_TLS_DTPOFF32`, `R_ARM_TLS_TPOFF32`, `R_ARM_TLS_GD32`, `R_ARM_TLS_LDM32`, `R_ARM_TLS_LDO32`, `R_ARM_TLS_IE32`, `R_ARM_TLS_LE32`, `R_ARM_TLS_LDO12`, `R_ARM_TLS_LE12`, `R_ARM_TLS_IE12GP`, `R_ARM_TLS_DESC`, `R_ARM_TLS_GOTDESC`, `R_ARM_TLS_CALL`, `R_ARM_TLS_DESCSEQ`, `R_ARM_THM_TLS_CALL`

### 2d. `arm_apply()` — Relocation Application
- [x] `R_ARM_NONE` → no-op
- [x] `R_ARM_ABS32` → S + A
- [x] `R_ARM_REL32` → S + A − P (signed 32-bit)
- [x] `R_ARM_PC24` → extract bits[23:0], compute ((S + A) | T) − P, check ±32MB range, reinsert with instruction mask
- [x] `R_ARM_CALL` → ((S + A) | T) − P, range ±32MB, encode in bits[23:0]
- [x] `R_ARM_JUMP24` → ((S + A) | T) − P, range ±32MB, encode in bits[23:0]
- [x] `R_ARM_PLT32` → ((S + A) | T) − P
- [x] `R_ARM_GOTOFF32` → ((S + A) | T) − GOT_ORG
- [x] `R_ARM_GOTPC` / `R_ARM_BASE_PREL` → GOT_ORG + A − P
- [x] `R_ARM_GOT_BREL` → GOT(S) + A − GOT_ORG
- [x] `R_ARM_PREL31` → (S + A − P) masked to 31 bits, preserve bit[31]
- [x] `R_ARM_MOVW_ABS_NC` → extract imm16 from instruction (bits[19:16]+bits[11:0]), compute (S + A) & 0xFFFF, reinsert; no overflow check
- [x] `R_ARM_MOVT_ABS` → ((S + A) >> 16) & 0xFFFF into imm16 field
- [x] `R_ARM_MOVW_PREL_NC` → ((S + A) | T) − P, low 16 bits
- [x] `R_ARM_MOVT_PREL` → (((S + A) | T) − P) >> 16, high 16 bits
- [x] `R_ARM_THM_CALL` → decode Thumb BL/BLX pair, compute ((S + A) | T) − P, range ±16MB (BL) / ±16MB even (BLX), re-encode J1/J2/imm10/imm11
- [x] `R_ARM_THM_JUMP24` → like THM_CALL but unconditional
- [x] `R_ARM_THM_JUMP19` → Thumb B.cond, 20-bit signed offset
- [x] `R_ARM_THM_JUMP11` → Thumb B, 11-bit unsigned offset
- [x] `R_ARM_THM_JUMP8` → Thumb B.cond, 8-bit signed offset
- [x] `R_ARM_THM_MOVW_ABS_NC` → extract Thumb MOVW imm16 (imm4:i:imm3:imm8), compute, reinsert
- [x] `R_ARM_THM_MOVT_ABS` → extract Thumb MOVT imm16, compute, reinsert
- [x] `R_ARM_THM_MOVW_PREL_NC` → PC-relative low 16 bits into Thumb MOVW
- [x] `R_ARM_THM_MOVT_PREL` → PC-relative high 16 bits into Thumb MOVT
- [x] `R_ARM_V4BX` → rewrite `BX Rm` to `MOV PC, Rm` for ARMv4 compat
- [x] `R_ARM_TARGET1` → platform-defined, typically `R_ARM_ABS32` or `R_ARM_REL32`
- [x] `R_ARM_TARGET2` → platform-defined, typically `R_ARM_GOT_PREL`
- [x] `R_ARM_ABS16` → S + A, check ±32K range or 0–64K unsigned
- [x] `R_ARM_ABS12` → (S + A) encoded in LDR immediate field, 12-bit unsigned
- [x] `R_ARM_ABS8` → S + A, check 0–255
- [x] `R_ARM_SBREL32` → S + A − B(S) (static base relative)
- [x] Group relocations (`ALU_PC/SB`, `LDR_PC/SB`, `LDRS_PC/SB`, `LDC_PC/SB` with G0/G1/G2): extract instruction-format-specific field width, apply group masking per ARM EABI §4.6.1.4
- [x] All TLS relocations: S + A (raw value passthrough for linker to fixup GOT/TP offsets)
- [x] Dynamic: `R_ARM_COPY`, `R_ARM_GLOB_DAT`, `R_ARM_JUMP_SLOT`, `R_ARM_RELATIVE`, `R_ARM_IRELATIVE` — produce raw S + A or B(S) + A

### 2e. ARM Thumb Interwork Bit (T)
- [x] Detect if target symbol is Thumb (`STT_FUNC` with `st_value` bit 0 set or `$t` mapping symbol).
- [x] Set T=1 for Thumb targets in branch relocations.

### 2f. ARM Relocation Backend Registration
- [x] Register `arm_apply`, `arm_reloc_size`, `arm_is_pc_relative` under `EM_ARM` in `register_builtin_backends_locked()`.
- [x] Add `arm_is_tls` to `elf_reloc_is_tls_for_machine()`.

---

## 3. AArch64 Relocation Backend (`elf_reloc.c`)

### 3a. `aarch64_reloc_size()`
- [x] `R_AARCH64_NONE` → 0
- [x] `R_AARCH64_ABS64`, `R_AARCH64_PREL64` → 8
- [x] `R_AARCH64_ABS32`, `R_AARCH64_PREL32` → 4
- [x] `R_AARCH64_ABS16`, `R_AARCH64_PREL16` → 2
- [x] All instruction-embedded relocations (`ADR_PREL_*`, `ADD_ABS_*`, `LDST*`, `MOVW_*`, `JUMP26`, `CALL26`, `CONDBR19`, `TSTBR14`) → 4 (instruction width)
- [x] All GOT/TLS instruction-embedded → 4
- [x] Dynamic (`COPY`, `GLOB_DAT`, `JUMP_SLOT`, `RELATIVE`, `IRELATIVE`, `TLS_DTPMOD64`, `TLS_DTPREL64`, `TLS_TPREL64`, `TLSDESC`) → 8

### 3b. `aarch64_is_pc_relative()`
- [x] PC-relative: `R_AARCH64_PREL64`, `R_AARCH64_PREL32`, `R_AARCH64_PREL16`, `R_AARCH64_ADR_PREL_LO21`, `R_AARCH64_ADR_PREL_PG_HI21{_NC}`, `R_AARCH64_JUMP26`, `R_AARCH64_CALL26`, `R_AARCH64_CONDBR19`, `R_AARCH64_TSTBR14`, `R_AARCH64_LD_PREL_LO19`, `R_AARCH64_GOT_LD_PREL19`, `R_AARCH64_ADR_GOT_PAGE`, `R_AARCH64_MOVW_PREL_G*`, all `TLSGD_ADR_PREL21`, `TLSLD_ADR_PREL21`, `TLSIE_LD_GOTTPREL_PREL19`, `TLSDESC_LD_PREL19`, `TLSDESC_ADR_PREL21`
- [x] Absolute: `R_AARCH64_ABS64/32/16`, `R_AARCH64_ADD_ABS_LO12_NC`, `R_AARCH64_LDST*_ABS_LO12_NC`, `R_AARCH64_MOVW_UABS_G*`, `R_AARCH64_MOVW_SABS_G*`

### 3c. `aarch64_is_tls()`
- [x] All `TLSGD_*`, `TLSLD_*`, `TLSIE_*`, `TLSLE_*`, `TLSDESC_*`, `TLS_DTPMOD64`, `TLS_DTPREL64`, `TLS_TPREL64`

### 3d. `aarch64_apply()` — Relocation Application
- [x] `R_AARCH64_ABS64` → S + A (64-bit)
- [x] `R_AARCH64_ABS32` → S + A, check unsigned 32-bit or signed 32-bit
- [x] `R_AARCH64_ABS16` → S + A, check ±32K
- [x] `R_AARCH64_PREL64` → S + A − P
- [x] `R_AARCH64_PREL32` → S + A − P, check signed 32-bit
- [x] `R_AARCH64_PREL16` → S + A − P, check signed 16-bit
- [x] `R_AARCH64_ADR_PREL_LO21` → extract ADR immediate (imm:immlo), compute S + A − P, check ±1MB, re-encode bits[23:5]+bits[30:29]
- [x] `R_AARCH64_ADR_PREL_PG_HI21` → Page(S + A) − Page(P), check ±4GB, encode as ADRP immediate (immhi:immlo)
- [x] `R_AARCH64_ADR_PREL_PG_HI21_NC` → same without overflow check
- [x] `R_AARCH64_ADD_ABS_LO12_NC` → (S + A) & 0xFFF, encode in ADD imm12 field (bits[21:10])
- [x] `R_AARCH64_LDST8_ABS_LO12_NC` → (S + A) & 0xFFF, encode in LDR/STR imm12 (no shift)
- [x] `R_AARCH64_LDST16_ABS_LO12_NC` → ((S + A) & 0xFFF) >> 1, check alignment
- [x] `R_AARCH64_LDST32_ABS_LO12_NC` → ((S + A) & 0xFFF) >> 2, check alignment
- [x] `R_AARCH64_LDST64_ABS_LO12_NC` → ((S + A) & 0xFFF) >> 3, check alignment
- [x] `R_AARCH64_LDST128_ABS_LO12_NC` → ((S + A) & 0xFFF) >> 4, check alignment
- [x] `R_AARCH64_MOVW_UABS_G0` → (S + A) & 0xFFFF, encode in MOVZ/MOVK imm16 (bits[20:5])
- [x] `R_AARCH64_MOVW_UABS_G0_NC` → same, no overflow check
- [x] `R_AARCH64_MOVW_UABS_G1` → ((S + A) >> 16) & 0xFFFF; G2 → >>32; G3 → >>48
- [x] `R_AARCH64_MOVW_SABS_G0` → signed version, may flip MOVZ↔MOVN
- [x] `R_AARCH64_JUMP26` → (S + A − P) >> 2, check ±128MB, encode in bits[25:0]
- [x] `R_AARCH64_CALL26` → same as JUMP26
- [x] `R_AARCH64_CONDBR19` → (S + A − P) >> 2, check ±1MB, encode in bits[23:5]
- [x] `R_AARCH64_TSTBR14` → (S + A − P) >> 2, check ±32KB, encode in bits[18:5]
- [x] `R_AARCH64_LD_PREL_LO19` → (S + A − P) >> 2, check ±1MB, encode in bits[23:5]
- [x] GOT relocations: compute GOT slot address, output GOT(S) + A or Page(GOT(S)) − Page(P)
- [x] TLS relocations: S + A passthrough (linker resolves GOT/TP offsets)
- [x] Dynamic: `R_AARCH64_COPY/GLOB_DAT/JUMP_SLOT/RELATIVE/IRELATIVE` → S + A or B(S) + A

### 3e. AArch64 Instruction Field Helpers
- [x] `aarch64_extract_imm26(uint32_t insn)` — bits[25:0] for B/BL
- [x] `aarch64_insert_imm26(uint32_t insn, int32_t value)` — encode into bits[25:0]
- [x] `aarch64_extract_imm19(uint32_t insn)` — bits[23:5] for B.cond/CBZ/LDR literal
- [x] `aarch64_insert_imm19(uint32_t insn, int32_t value)`
- [x] `aarch64_extract_imm14(uint32_t insn)` — bits[18:5] for TBZ
- [x] `aarch64_insert_imm14(uint32_t insn, int32_t value)`
- [x] `aarch64_extract_adr_imm(uint32_t insn)` — immhi(bits[23:5]):immlo(bits[30:29]) for ADR/ADRP
- [x] `aarch64_insert_adr_imm(uint32_t insn, int32_t value)`
- [x] `aarch64_extract_imm12(uint32_t insn)` — bits[21:10] for ADD/LDR
- [x] `aarch64_insert_imm12(uint32_t insn, uint32_t value)`
- [x] `aarch64_extract_movw_imm16(uint32_t insn)` — bits[20:5]
- [x] `aarch64_insert_movw_imm16(uint32_t insn, uint16_t value)`
- [x] `aarch64_page(uint64_t addr)` → addr & ~0xFFF

### 3f. AArch64 Relocation Backend Registration
- [x] Register `aarch64_apply`, `aarch64_reloc_size`, `aarch64_is_pc_relative` under `EM_AARCH64`.
- [x] Add `aarch64_is_tls` to `elf_reloc_is_tls_for_machine()`.

---

## 4. Validation (`elf_validate.c`)

### 4a. ARM Validation Rules
- [x] Accept `EM_ARM` with `ELFCLASS32` only.
- [x] Accept both `ELFDATA2LSB` (little-endian, common) and `ELFDATA2MSB` (big-endian).
- [x] Validate `e_flags` EABI version (≥ `EF_ARM_ABI_VER5` for modern toolchains).
- [x] Validate float ABI flags consistency (`HARD`/`SOFT` not both set).
- [x] Validate `SHT_ARM_EXIDX` sections have `SHF_LINK_ORDER` flag.
- [x] Validate `.ARM.attributes` section if present.
- [x] Validate alignment constraints for ARM instructions (4 for ARM, 2 for Thumb).
- [x] Validate `PT_ARM_EXIDX` segment if present points to `SHT_ARM_EXIDX` section.
- [x] Validate mapping symbols (`$a`, `$t`, `$d`) are present in code sections.

### 4b. AArch64 Validation Rules
- [x] Accept `EM_AARCH64` with `ELFCLASS64` only.
- [x] Accept both `ELFDATA2LSB` and `ELFDATA2MSB`.
- [x] Validate `e_flags` is 0 or recognized optional flags only.
- [x] Validate instruction alignment: all code sections 4-byte aligned.
- [x] Validate `.note.gnu.property` for BTI (`GNU_PROPERTY_AARCH64_FEATURE_1_BTI`) and PAC (`GNU_PROPERTY_AARCH64_FEATURE_1_PAC`) if present.
- [x] Validate ADRP+ADD/LDR pairs have consistent page references.
- [x] Check for unrecognized relocation types.
- [x] Validate mapping symbols (`$x`, `$d`) in code sections.

---

## 5. ELF Read (`elf_read.c`)

- [x] Recognize `EM_ARM` and `EM_AARCH64` as valid machine types.
- [x] ARM: parse REL relocations (no addend in struct; addend encoded in instruction).
- [x] AArch64: parse RELA relocations (explicit addend).
- [x] Parse `SHT_ARM_EXIDX` sections with proper `sh_link` interpretation.
- [x] Parse `SHT_ARM_ATTRIBUTES` / `SHT_AARCH64_ATTRIBUTES` build attribute sections.
- [x] Handle ARM `SHF_ARM_PURECODE` flag in section flags.
- [x] Read `PT_ARM_EXIDX` and `PT_AARCH64_MEMTAG_MTE` segment types.
- [x] Endian-aware reads: ARM objects can be big-endian (BE32 or BE8).

---

## 6. ELF Write (`elf_write.c`)

- [x] Write ELF32 (`EM_ARM`) with REL relocations.
- [x] Write ELF64 (`EM_AARCH64`) with RELA relocations.
- [x] Write correct `e_flags` for ARM: EABI version, float ABI, interwork.
- [x] Write `SHT_ARM_EXIDX` and `SHT_ARM_ATTRIBUTES` sections.
- [x] Write `PT_ARM_EXIDX` segment.
- [x] Write `.note.gnu.property` with BTI/PAC flags for AArch64.
- [x] Endian-correct output for big-endian ARM.
- [x] Correct `e_entry` with Thumb bit for ARM entry points.

---

## 7. ELF Creation (`elf_util.c`)

- [x] `elf_init_arm()` convenience: set up ELF32/EM_ARM/ELFDATA2LSB with EABI v5 flags, default `.text`/`.data`/`.bss` sections.
- [x] `elf_init_aarch64()` convenience: set up ELF64/EM_AARCH64/ELFDATA2LSB with empty flags.
- [x] `elf_set_flags()` / `elf_flags()` for manipulating `e_flags`.
- [x] `elf_add_arm_exidx()` helper for creating `.ARM.exidx` + `.ARM.extab` section pairs.
- [x] `elf_add_arm_attributes()` helper for adding build attributes.
- [x] `elf_add_gnu_property_aarch64()` helper for BTI/PAC feature bits.

---

## 8. DWARF Support (`elf_dwarf.c`)

- [x] ARM DWARF register mapping: R0–R15 → 0–15, VFP D0–D31 → 256–287.
- [x] AArch64 DWARF register mapping: X0–X30 → 0–30, SP → 31, V0–V31 → 64–95.
- [x] ARM CFA rules: typical frame pointer is R11 (FP) or R13 (SP).
- [x] AArch64 CFA rules: frame pointer is X29, link register is X30.
- [x] Handle `.debug_frame` vs `.eh_frame` CIE augmentation differences per arch.
- [x] Parse ARM-specific DWARF extensions (if any vendor extensions present).

---

## 9. Link Planning (`elf_link.c`)

- [x] Accept `EM_ARM` and `EM_AARCH64` inputs.
- [x] Reject class mismatches: ARM must be ELFCLASS32, AArch64 must be ELFCLASS64.
- [x] Merge `e_flags`: take union of float ABI flags; error on conflict.
- [x] Handle ARM/Thumb interwork symbol merging (symbol with T-bit → different treatment).
- [x] `.ARM.exidx` section merging: sort entries by covered address range.
- [x] `.ARM.attributes` merging: attribute compatibility checking per Build Attributes spec.
- [x] Section group/COMDAT handling for ARM is identical to x86.
- [x] AArch64 `.note.gnu.property` merging: AND of BTI/PAC bits across inputs.

---

## 10. ARM Build Attributes Parser

Per ARM EABI §2.2.3, `.ARM.attributes` contains vendor-specific attribute tags:

- [x] Parse attribute section format: subsection headers, vendor name, tag-value pairs.
- [x] `Tag_CPU_name` (4): CPU name string (e.g., "Cortex-A15").
- [x] `Tag_CPU_arch` (6): architecture version (1=v4, 6=v6, 10=v7, 13=v7E-M, 14=v8).
- [x] `Tag_CPU_arch_profile` (7): 'A' (Application), 'R' (Real-time), 'M' (Microcontroller).
- [x] `Tag_ARM_ISA_use` (8): 0=no, 1=yes.
- [x] `Tag_THUMB_ISA_use` (9): 0=no, 1=Thumb, 2=Thumb-2, 3=Armv8-M.baseline.
- [x] `Tag_FP_arch` (10): 0=none, 1=VFPv1, 2=VFPv2, 3=VFPv3, 4=VFPv3-D16, 5=VFPv4, 6=VFPv4-D16.
- [x] `Tag_WMMX_arch` (11): Wireless MMX.
- [x] `Tag_Advanced_SIMD_arch` (12): 0=none, 1=NEONv1, 2=NEONv1+fused-MAC, 3=ARMv8 NEON.
- [x] `Tag_PCS_config` (13): calling convention.
- [x] `Tag_ABI_PCS_R9_use` (14): R9 usage.
- [x] `Tag_ABI_PCS_RW_data` (15): RW data addressing.
- [x] `Tag_ABI_PCS_RO_data` (16): RO data addressing.
- [x] `Tag_ABI_PCS_GOT_use` (17): GOT addressing.
- [x] `Tag_ABI_PCS_wchar_t` (18): wchar_t size.
- [x] `Tag_ABI_FP_rounding` (19): rounding mode.
- [x] `Tag_ABI_FP_denormal` (20): denormal handling.
- [x] `Tag_ABI_FP_exceptions` (21): exception model.
- [x] `Tag_ABI_FP_user_exceptions` (22): user-mode FP exceptions.
- [x] `Tag_ABI_FP_number_model` (23): IEEE 754 conformance.
- [x] `Tag_ABI_align_needed` (24): alignment requirements.
- [x] `Tag_ABI_align_preserved` (25): alignment guarantees.
- [x] `Tag_ABI_enum_size` (26): enum sizing.
- [x] `Tag_ABI_HardFP_use` (27): hard-float VFP register usage.
- [x] `Tag_ABI_VFP_args` (28): VFP argument passing convention.
- [x] `Tag_ABI_optimization_goals` (30): optimization priorities.
- [x] `Tag_CPU_unaligned_access` (34): unaligned access support.
- [x] `Tag_FP_HP_extension` (36): half-precision extension.
- [x] `Tag_ABI_FP_16bit_format` (38): FP16 format (IEEE754/alternative).
- [x] `Tag_MPExtension_use` (42): multiprocessing extensions.
- [x] `Tag_DIV_use` (44): integer divide instruction usage.
- [x] `Tag_DSP_extension` (46): DSP extension usage.
- [x] `Tag_Virtualization_use` (68): virtualization extensions.
- [x] API: `elf_arm_attribute_count()`, `elf_arm_attribute_tag_at()`, `elf_arm_attribute_value_at()`, `elf_arm_attribute_string_at()`.
- [x] Validation: check compatibility of `Tag_CPU_arch` + `Tag_FP_arch` across link inputs.

---

## 11. Testing

### 11a. Relocation Backend Unit Tests
- [x] For each ARM relocation type: known input (S, A, P, GOT) → expected output value.
- [x] Overflow: ARM `R_ARM_CALL` with offset > ±32MB → error.
- [x] Overflow: AArch64 `R_AARCH64_JUMP26` with offset > ±128MB → error.
- [x] Overflow: `R_AARCH64_ADR_PREL_PG_HI21` with page delta > ±4GB → error.
- [x] Alignment: `R_AARCH64_LDST32_ABS_LO12_NC` with non-4-byte-aligned → error.
- [x] Thumb interwork: `R_ARM_CALL` to Thumb target → T bit set correctly.
- [x] AArch64 ADRP+ADD pair: page calculation correct for page-aligned and non-aligned addresses.
- [x] AArch64 MOVW_UABS_G0/G1/G2/G3: correct 16-bit slice extraction.
- [x] ARM MOVW/MOVT: correct instruction field insertion for known bit patterns.
- [x] Thumb BL encoding: J1/J2 bits encode correctly for positive and negative offsets.
- [x] PC-relative classification: every PC-relative reloc returns true, every absolute returns false.
- [x] TLS classification: every TLS reloc returns true, non-TLS returns false.
- [x] All reloc sizes match expected values.

### 11b. Read/Write Round-Trip Tests
- [x] Read ARM ELF32 object → inspect sections/symbols/relocs → write back → byte-compare.
- [x] Read AArch64 ELF64 object → inspect → write back → byte-compare.
- [x] Read ARM object with `.ARM.exidx` → section present with correct `sh_link`.
- [x] Read ARM object with `.ARM.attributes` → parse attributes, verify tag values.
- [x] Read AArch64 object with `.note.gnu.property` → BTI/PAC flags extracted.
- [x] Create ARM object from scratch → write → `readelf -a` validates headers/sections/symbols/relocs.
- [x] Create AArch64 object from scratch → write → `readelf -a` validates.
- [x] Big-endian ARM object: read and write with correct byte order.

### 11c. Validation Tests
- [x] ARM ELF with `ELFCLASS64` → rejected.
- [x] AArch64 ELF with `ELFCLASS32` → rejected.
- [x] ARM ELF with conflicting float ABI flags → diagnostic.
- [x] ARM ELF with missing `.ARM.exidx` `SHF_LINK_ORDER` → diagnostic.
- [x] AArch64 ELF with unknown `e_flags` → warning.
- [x] Unrecognized relocation type → diagnostic.

### 11d. Link Planning Tests
- [x] Merge two ARM objects → `e_flags` union is correct.
- [x] Merge hard-float + soft-float ARM objects → error.
- [x] Merge ARM + AArch64 objects → rejected (class mismatch).
- [x] `.ARM.attributes` merge: compatible objects → merged, incompatible → error.
- [x] AArch64 `.note.gnu.property` merge: BTI+PAC from both inputs → AND of features.

### 11e. Build Attributes Tests
- [x] Parse `.ARM.attributes` from GCC-produced ARM object.
- [x] All standard tags readable via API.
- [x] Unknown vendor subsections skipped without error.
- [x] Tag compatibility check across two inputs for `Tag_CPU_arch`, `Tag_FP_arch`, `Tag_ABI_VFP_args`.

### 11f. DWARF Tests
- [x] ARM DWARF register numbers map correctly in `.debug_frame` / `.eh_frame`.
- [x] AArch64 DWARF register numbers map correctly.
- [x] CFA restoration rules work for ARM R11 frame pointer.
- [x] CFA restoration rules work for AArch64 X29 frame pointer.

### 11g. Fuzz Tests
- [x] Fuzz ARM ELF object parsing → crash-free.
- [x] Fuzz AArch64 ELF object parsing → crash-free.
- [x] Fuzz `.ARM.attributes` section parsing → crash-free.

---

## 12. Expanded x86 Relocation Backend

### 12a. i386 Backend Expansion
- [x] `R_386_COPY` → no value (dynamic linker copies data)
- [x] `R_386_GLOB_DAT` → S (GOT slot fill)
- [x] `R_386_JMP_SLOT` → S (PLT GOT slot fill)
- [x] `R_386_RELATIVE` → B(S) + A (base-relative)
- [x] `R_386_16` → S + A, check unsigned 16-bit
- [x] `R_386_PC16` → S + A − P, check signed 16-bit
- [x] `R_386_8` → S + A, check unsigned 8-bit
- [x] `R_386_PC8` → S + A − P, check signed 8-bit
- [x] `R_386_SIZE32` → Z + A (symbol size)
- [x] `R_386_GOT32X` → GOT(S) + A − GOT_ORG (relaxable GOT reference)
- [x] `R_386_IRELATIVE` → indirect function resolution
- [x] `R_386_TLS_DTPMOD32` → module ID for TLS
- [x] `R_386_TLS_DTPOFF32` → offset within TLS block
- [x] `R_386_TLS_LE_32` → negative TP-relative offset
- [x] `R_386_TLS_TPOFF32` → negative TP-relative offset (variant)
- [x] Add `i386_is_tls()` for complete TLS classification: all `R_386_TLS_*` types

### 12b. x86-64 Backend Expansion
- [x] `R_X86_64_COPY` → no value
- [x] `R_X86_64_GLOB_DAT` → S
- [x] `R_X86_64_JUMP_SLOT` → S
- [x] `R_X86_64_RELATIVE` → B + A
- [x] `R_X86_64_16` → S + A, check unsigned 16-bit
- [x] `R_X86_64_PC16` → S + A − P, check signed 16-bit
- [x] `R_X86_64_8` → S + A, check unsigned 8-bit
- [x] `R_X86_64_PC8` → S + A − P, check signed 8-bit
- [x] `R_X86_64_PC64` → S + A − P (64-bit PC-relative)
- [x] `R_X86_64_GOTOFF64` → S + A − GOT_ORG
- [x] `R_X86_64_GOTPC32` → GOT_ORG + A − P
- [x] `R_X86_64_SIZE32` → Z + A (check 32-bit), `R_X86_64_SIZE64` → Z + A
- [x] `R_X86_64_GOTPCRELX` → GOT(S) + A − P (relaxable to LEA for non-preemptible)
- [x] `R_X86_64_REX_GOTPCRELX` → same with REX prefix
- [x] `R_X86_64_IRELATIVE` → indirect function resolution
- [x] `R_X86_64_DTPMOD64`, `R_X86_64_DTPOFF64`, `R_X86_64_TPOFF64` → TLS module/offset dynamic
- [x] `R_X86_64_TLSLD` → Local Dynamic TLS
- [x] `R_X86_64_DTPOFF32` → 32-bit DTP offset
- [x] `R_X86_64_GOTPC32_TLSDESC` → TLSDESC GOT-relative
- [x] `R_X86_64_TLSDESC_CALL` → TLSDESC call relocation
- [x] `R_X86_64_TLSDESC` → TLSDESC pair
- [x] Add `x64_is_tls()` for complete TLS classification: all `R_X86_64_TLS*`, `GOTTPOFF`, `TPOFF32`, `DTPMOD64`, `DTPOFF64`, `TPOFF64`, `DTPOFF32`, `GOTPC32_TLSDESC`, `TLSDESC_CALL`, `TLSDESC`

### 12c. x86 Relocation Name Strings
- [x] `elf_reloc_name_for_machine(machine, type)` → human-readable string (e.g., `"R_X86_64_PC32"`)
- [x] Complete name tables for all i386 and x86-64 relocation types.
- [x] Complete name tables for all ARM and AArch64 relocation types.
- [x] Complete name tables for all MIPS, RISC-V, LoongArch, M68K, VAX, Alpha, PPC, PPC64, and IA-64 relocation types.

---

## 13. x86-Specific Validation (`elf_validate.c`)

- [x] Validate `EM_386` with `ELFCLASS32` only.
- [x] Validate `EM_X86_64` with `ELFCLASS64` only.
- [x] Both x86 variants: `ELFDATA2LSB` only (x86 is always little-endian).
- [x] Validate `.note.gnu.property` structure for x86: NT_GNU_PROPERTY_TYPE_0, correct alignment (4-byte for ELF32, 8-byte for ELF64).
- [x] Validate `GNU_PROPERTY_X86_ISA_1_NEEDED` bit values (only defined bits set).
- [x] Validate `GNU_PROPERTY_X86_FEATURE_1_AND` bit values (only IBT/SHSTK).
- [x] Warn on unknown GNU properties (forward compatibility).
- [x] Validate `SHT_REL` used for i386, `SHT_RELA` for x86-64.
- [x] Validate `.eh_frame` CIE return address register (8 for i386/RA, 16 for x86-64/RA).

---

## 14. GNU Property / Note API

- [x] `elf_note_count(obj)` → number of notes across all note sections.
- [x] `elf_note_at(obj, index)` → `{name, type, desc_data, desc_size}`.
- [x] `elf_gnu_property_count(obj)` → number of GNU properties in `.note.gnu.property`.
- [x] `elf_gnu_property_at(obj, index)` → `{type, data, data_size}`.
- [x] `elf_x86_isa_level(obj)` → bitmask of `GNU_PROPERTY_X86_ISA_1_NEEDED` (0 if absent).
- [x] `elf_x86_feature_flags(obj)` → bitmask of `GNU_PROPERTY_X86_FEATURE_1_AND` (0 if absent).
- [x] `elf_aarch64_feature_flags(obj)` → bitmask of `GNU_PROPERTY_AARCH64_FEATURE_1_AND` (0 if absent).
- [x] `elf_add_gnu_property_x86(obj, isa_needed, isa_used, feature_1)` → create/update `.note.gnu.property`.
- [x] `elf_add_gnu_property_aarch64(obj, feature_1)` → create/update `.note.gnu.property`.
- [x] `elf_build_id(obj, out_data, out_size)` → extract `.note.gnu.build-id` contents.

---

## 15. x86-Specific Tests

### 15a. Expanded i386 Relocation Tests
- [x] `R_386_16` and `R_386_PC16`: verify 16-bit relocations.
- [x] `R_386_8` and `R_386_PC8`: verify 8-bit relocations.
- [x] `R_386_SIZE32`: verify symbol size relocation.
- [x] `R_386_GOT32X`: verify relaxable GOT reference.
- [x] `R_386_IRELATIVE`: verify indirect function.
- [x] `R_386_TLS_DTPMOD32`/`DTPOFF32`: verify TLS relocations.
- [x] All dynamic relocations (COPY/GLOB_DAT/JMP_SLOT/RELATIVE): verify.

### 15b. Expanded x86-64 Relocation Tests
- [x] `R_X86_64_PC64`: verify 64-bit PC-relative.
- [x] `R_X86_64_GOTOFF64`/`GOTPC32`: verify GOT-relative.
- [x] `R_X86_64_SIZE32`/`SIZE64`: verify size relocations.
- [x] `R_X86_64_GOTPCRELX`/`REX_GOTPCRELX`: verify relaxable GOT references.
- [x] `R_X86_64_IRELATIVE`: verify indirect function.
- [x] `R_X86_64_TLSLD`/`DTPOFF32`: verify Local Dynamic TLS.
- [x] `R_X86_64_GOTPC32_TLSDESC`/`TLSDESC_CALL`/`TLSDESC`: verify TLSDESC relocations.
- [x] All 16-bit and 8-bit relocations: verify.

### 15c. GNU Property Tests
- [x] Read x86-64 object with `.note.gnu.property` → extract ISA level bits.
- [x] Read AArch64 object → extract BTI/PAC flags.
- [x] Create object → add GNU property → write → read back → verify.
- [x] Property with ISA_1_V4 → `elf_x86_isa_level()` returns correct bitmask.
- [x] Object without `.note.gnu.property` → `elf_x86_isa_level()` returns 0.
- [x] Merge two objects with different ISA levels → OR result.
- [x] Merge two objects with FEATURE_1_AND → AND result.

### 15d. Relocation Name Tests
- [x] Every i386 relocation type → correct name string.
- [x] Every x86-64 relocation type → correct name string.
- [x] Every ARM relocation type → correct name string.
- [x] Every AArch64 relocation type → correct name string.
- [x] Unknown type → `"UNKNOWN(N)"` format.

### 15e. x86 Validation Tests
- [x] EM_386 with ELFCLASS64 → rejected.
- [x] EM_X86_64 with ELFCLASS32 → rejected.
- [x] x86 with ELFDATA2MSB → rejected.
- [x] `.note.gnu.property` with bad alignment → diagnostic.
- [x] `.note.gnu.property` with unknown property type → warning (not error).

---

## 16. Documentation

- [x] Update `README.md` with full multi-architecture support notes.
- [x] Document all relocation backend registration APIs.
- [x] Document ARM build attributes API.
- [x] Document GNU property API.
- [x] Document relocation name API.
- [x] Update `COMPATIBILITY_MATRIX.md` with all architecture entries.
- [x] Man page updates for `elfobj.3` with per-arch API functions.

---

## 17. MIPS / MIPS64 Support

### 17a. Machine Types and Constants
- [x] Define `EM_MIPS` (8).
- [x] Define MIPS `e_flags`: `EF_MIPS_NOREORDER`, `EF_MIPS_PIC`, `EF_MIPS_CPIC`, `EF_MIPS_ABI_O32`, `EF_MIPS_ABI_O64`, `EF_MIPS_ABI_EABI32`, `EF_MIPS_ABI_EABI64`.
- [x] Define MIPS ISA flags: `EF_MIPS_ARCH_1` through `EF_MIPS_ARCH_64R6`.
- [x] Define MIPS ASE flags: `EF_MIPS_MIPS16`, `EF_MIPS_MICROMIPS`, `EF_MIPS_ARCH_ASE_MDMX`, `EF_MIPS_ARCH_ASE_M16`, `EF_MIPS_ARCH_ASE_DSP`, `EF_MIPS_ARCH_ASE_DSPR2`, `EF_MIPS_ARCH_ASE_MSA`.
- [x] Define MIPS FP mode: `EF_MIPS_FP64` (FR=1).
- [x] Define MIPS section types: `SHT_MIPS_DWARF` (0x7000001E), `SHT_MIPS_ABIFLAGS` (0x7000002A).
- [x] Define MIPS segment types: `PT_MIPS_ABIFLAGS` (0x70000003), `PT_MIPS_REGINFO` (0x70000000).

### 17b. MIPS Relocation Type Constants
- [x] `R_MIPS_NONE` (0), `R_MIPS_16` (1), `R_MIPS_32` (2), `R_MIPS_REL32` (3)
- [x] `R_MIPS_26` (4), `R_MIPS_HI16` (5), `R_MIPS_LO16` (6)
- [x] `R_MIPS_GPREL16` (7), `R_MIPS_LITERAL` (8), `R_MIPS_GOT16` (9)
- [x] `R_MIPS_PC16` (10), `R_MIPS_CALL16` (11), `R_MIPS_GPREL32` (12)
- [x] `R_MIPS_SHIFT5` (16), `R_MIPS_SHIFT6` (17), `R_MIPS_64` (18)
- [x] `R_MIPS_GOT_DISP` (19), `R_MIPS_GOT_PAGE` (20), `R_MIPS_GOT_OFST` (21)
- [x] `R_MIPS_GOT_HI16` (22), `R_MIPS_GOT_LO16` (23)
- [x] `R_MIPS_SUB` (24), `R_MIPS_INSERT_A` (25), `R_MIPS_INSERT_B` (26), `R_MIPS_DELETE` (27)
- [x] `R_MIPS_HIGHER` (28), `R_MIPS_HIGHEST` (29)
- [x] `R_MIPS_CALL_HI16` (30), `R_MIPS_CALL_LO16` (31), `R_MIPS_SCN_DISP` (32)
- [x] `R_MIPS_REL16` (33), `R_MIPS_ADD_IMMEDIATE` (34)
- [x] `R_MIPS_PJUMP` (35), `R_MIPS_RELGOT` (36)
- [x] `R_MIPS_JALR` (37), `R_MIPS_GLOB_DAT` (51)
- [x] `R_MIPS_COPY` (126), `R_MIPS_JUMP_SLOT` (127)
- [x] TLS: `R_MIPS_TLS_DTPMOD32` (38), `R_MIPS_TLS_DTPREL32` (39), `R_MIPS_TLS_DTPMOD64` (40), `R_MIPS_TLS_DTPREL64` (41), `R_MIPS_TLS_GD` (42), `R_MIPS_TLS_LDM` (43), `R_MIPS_TLS_DTPREL_HI16` (44), `R_MIPS_TLS_DTPREL_LO16` (45), `R_MIPS_TLS_GOTTPREL` (46), `R_MIPS_TLS_TPREL32` (47), `R_MIPS_TLS_TPREL64` (48), `R_MIPS_TLS_TPREL_HI16` (49), `R_MIPS_TLS_TPREL_LO16` (50)
- [x] MicroMIPS: `R_MICROMIPS_26_S1` (133), `R_MICROMIPS_HI16` (134), `R_MICROMIPS_LO16` (135), `R_MICROMIPS_GPREL16` (136), `R_MICROMIPS_PC7_S1` (143), `R_MICROMIPS_PC10_S1` (144), `R_MICROMIPS_PC16_S1` (145), `R_MICROMIPS_PC23_S2` (172)

### 17c. MIPS Relocation Backend
- [x] `mips_reloc_size()` for all MIPS relocation types.
- [x] `mips_is_pc_relative()`: PC-relative classification.
- [x] `mips_is_tls()`: TLS classification.
- [x] `mips_apply()`: HI16/LO16 paired relocation with AHL computation.
- [x] `mips_apply()`: R_MIPS_26 with 256MB segment masking.
- [x] `mips_apply()`: GP-relative relocations (GPREL16, LITERAL).
- [x] `mips_apply()`: GOT16/CALL16 GOT slot references.
- [x] `mips_apply()`: N64 compound relocations (up to 3 relocs per entry).
- [x] `mips_apply()`: All TLS relocations.
- [x] `mips_apply()`: MicroMIPS branch/jump encoding.
- [x] Register under `EM_MIPS` in `register_builtin_backends_locked()`.
- [x] Relocation name strings for all MIPS types.

### 17d. MIPS ABIFLAGS Parser
- [x] Parse `.MIPS.abiflags` structure: `isa_level`, `isa_rev`, `gpr_size`, `cpr1_size`, `cpr2_size`, `fp_abi`, `isa_ext`, `ases`, `flags1`, `flags2`.
- [x] API: `elf_mips_abiflags()` returning parsed structure.
- [x] Validate ABIFLAGS consistency with `e_flags` ISA level.
- [x] ABIFLAGS merge rules across link inputs.

### 17e. MIPS Validation
- [x] Accept `EM_MIPS` with `ELFCLASS32` (O32/N32) or `ELFCLASS64` (N64).
- [x] Accept both `ELFDATA2LSB` (MIPSEL) and `ELFDATA2MSB` (MIPS).
- [x] Validate `e_flags` ISA level and ABI fields.
- [x] Validate `.MIPS.abiflags` section if present.
- [x] Validate N64 compound relocation entries.

### 17f. MIPS ELF Read/Write/Create
- [x] Recognize `EM_MIPS` as valid machine type.
- [x] MIPS32/N32: parse/write REL relocations. MIPS64/N64: parse/write RELA with compound entries.
- [x] Parse `SHT_MIPS_ABIFLAGS`, `PT_MIPS_ABIFLAGS`, `PT_MIPS_REGINFO`.
- [x] `elf_init_mips32()`: ELF32/EM_MIPS/ELFDATA2LSB with O32 flags.
- [x] `elf_init_mips64()`: ELF64/EM_MIPS/ELFDATA2LSB with N64 flags.
- [x] Endian-correct output for big-endian MIPS.

### 17g. MIPS DWARF
- [x] MIPS DWARF register mapping: $zero–$ra → 0–31, $f0–$f31 → 32–63, HI → 64, LO → 65.
- [x] MIPS CFA: frame pointer $fp ($30) or $sp ($29).

### 17h. MIPS Testing
- [x] Unit tests for all MIPS relocation types.
- [x] HI16/LO16 pairing correctness.
- [x] N64 compound relocation handling.
- [x] ABIFLAGS parsing and merge tests.
- [x] Round-trip read/write for MIPS32 and MIPS64 objects.
- [x] Validation: class/endian checks.
- [x] Fuzz MIPS ELF parsing → crash-free.

---

## 18. RISC-V Support (RV32 / RV64 / RV128)

### 18a. Machine Types and Constants
- [x] Define `EM_RISCV` (243).
- [x] Define RISC-V `e_flags`: `EF_RISCV_RVC` (0x0001), `EF_RISCV_FLOAT_ABI_SOFT` (0x0000), `EF_RISCV_FLOAT_ABI_SINGLE` (0x0002), `EF_RISCV_FLOAT_ABI_DOUBLE` (0x0004), `EF_RISCV_FLOAT_ABI_QUAD` (0x0006), `EF_RISCV_RVE` (0x0008), `EF_RISCV_TSO` (0x0010).
- [x] Define RISC-V section types: `SHT_RISCV_ATTRIBUTES` (0x70000003).

### 18b. RISC-V Relocation Type Constants
- [x] `R_RISCV_NONE` (0), `R_RISCV_32` (1), `R_RISCV_64` (2)
- [x] `R_RISCV_RELATIVE` (3), `R_RISCV_COPY` (4), `R_RISCV_JUMP_SLOT` (5), `R_RISCV_TLS_DTPMOD32` (6), `R_RISCV_TLS_DTPMOD64` (7), `R_RISCV_TLS_DTPREL32` (8), `R_RISCV_TLS_DTPREL64` (9), `R_RISCV_TLS_TPREL32` (10), `R_RISCV_TLS_TPREL64` (11)
- [x] `R_RISCV_BRANCH` (16), `R_RISCV_JAL` (17), `R_RISCV_CALL` (18), `R_RISCV_CALL_PLT` (19), `R_RISCV_GOT_HI20` (20)
- [x] `R_RISCV_TLS_GOT_HI20` (21), `R_RISCV_TLS_GD_HI20` (22), `R_RISCV_PCREL_HI20` (23), `R_RISCV_PCREL_LO12_I` (24), `R_RISCV_PCREL_LO12_S` (25)
- [x] `R_RISCV_HI20` (26), `R_RISCV_LO12_I` (27), `R_RISCV_LO12_S` (28)
- [x] `R_RISCV_TPREL_HI20` (29), `R_RISCV_TPREL_LO12_I` (30), `R_RISCV_TPREL_LO12_S` (31), `R_RISCV_TPREL_ADD` (32)
- [x] `R_RISCV_ADD8` (33), `R_RISCV_ADD16` (34), `R_RISCV_ADD32` (35), `R_RISCV_ADD64` (36)
- [x] `R_RISCV_SUB8` (37), `R_RISCV_SUB16` (38), `R_RISCV_SUB32` (39), `R_RISCV_SUB64` (40)
- [x] `R_RISCV_ALIGN` (43), `R_RISCV_RVC_BRANCH` (44), `R_RISCV_RVC_JUMP` (45), `R_RISCV_RVC_LUI` (46)
- [x] `R_RISCV_RELAX` (51), `R_RISCV_SUB6` (52), `R_RISCV_SET6` (53), `R_RISCV_SET8` (54), `R_RISCV_SET16` (55), `R_RISCV_SET32` (56)
- [x] `R_RISCV_32_PCREL` (57), `R_RISCV_IRELATIVE` (58)
- [x] RV128 (draft): `R_RISCV_128` (TBD when standardized).
- [x] Vendor: `R_RISCV_VENDOR` (reserved range).

### 18c. RISC-V Relocation Backend
- [x] `riscv_reloc_size()` for all RISC-V relocation types.
- [x] `riscv_is_pc_relative()`: BRANCH, JAL, CALL, CALL_PLT, PCREL_HI20, PCREL_LO12_I/S, RVC_BRANCH, RVC_JUMP, 32_PCREL.
- [x] `riscv_is_tls()`: all TLS_* and TPREL_* types.
- [x] `riscv_apply()`: U-type immediate insertion (HI20: bits[31:12]).
- [x] `riscv_apply()`: I-type immediate insertion (LO12_I: bits[31:20]).
- [x] `riscv_apply()`: S-type immediate insertion (LO12_S: bits[31:25]+bits[11:7]).
- [x] `riscv_apply()`: B-type branch encoding (BRANCH: imm[12|10:5|4:1|11]).
- [x] `riscv_apply()`: J-type jump encoding (JAL: imm[20|10:1|11|19:12]).
- [x] `riscv_apply()`: CALL/CALL_PLT (AUIPC+JALR pair).
- [x] `riscv_apply()`: RVC compressed branch/jump encoding.
- [x] `riscv_apply()`: ADD/SUB content relocations for DWARF.
- [x] `riscv_apply()`: RELAX marker handling (no-op, but must not error).
- [x] Register under `EM_RISCV`.
- [x] Relocation name strings for all RISC-V types.

### 18d. RISC-V Attributes Parser
- [x] Parse `.riscv.attributes` (`SHT_RISCV_ATTRIBUTES`).
- [x] Decode `Tag_RISCV_arch` ISA string (e.g., `"rv64i2p1_m2p0_a2p1_f2p2_d2p2_c2p0"`).
- [x] Decode `Tag_RISCV_stack_align` (stack alignment).
- [x] Decode `Tag_RISCV_unaligned_access` (unaligned access).
- [x] Decode `Tag_RISCV_priv_spec` / `Tag_RISCV_priv_spec_minor` / `Tag_RISCV_priv_spec_revision`.
- [x] API: `elf_riscv_attribute_count()`, `elf_riscv_attribute_tag_at()`, `elf_riscv_attribute_value_at()`.
- [x] ISA string compatibility checking across link inputs.

### 18e. RISC-V Validation
- [x] Accept `EM_RISCV` with `ELFCLASS32` (RV32) or `ELFCLASS64` (RV64/RV128).
- [x] Reject `ELFDATA2MSB` (RISC-V is little-endian only).
- [x] Validate `e_flags` float ABI, RVC, RVE, TSO flags.
- [x] Validate `.riscv.attributes` section if present.
- [x] Check for conflicting float ABI across link inputs.

### 18f. RISC-V ELF Read/Write/Create
- [x] Recognize `EM_RISCV` as valid machine type.
- [x] RV32: parse/write RELA relocations (ELF32). RV64: parse/write RELA relocations (ELF64).
- [x] Parse `SHT_RISCV_ATTRIBUTES` sections.
- [x] `elf_init_riscv32()`: ELF32/EM_RISCV/ELFDATA2LSB.
- [x] `elf_init_riscv64()`: ELF64/EM_RISCV/ELFDATA2LSB.

### 18g. RISC-V DWARF
- [x] RISC-V DWARF register mapping: x0–x31 → 0–31, f0–f31 → 32–63.
- [x] RISC-V CFA: frame pointer x8 (s0/fp) or x2 (sp), return address x1 (ra).

### 18h. RISC-V Testing
- [x] Unit tests for all RISC-V relocation types.
- [x] B-type and J-type immediate encoding correctness.
- [x] CALL/CALL_PLT AUIPC+JALR pair calculation.
- [x] RVC branch/jump compressed encoding.
- [x] ADD/SUB content relocations.
- [x] `.riscv.attributes` parsing and ISA string decode.
- [x] Round-trip read/write for RV32 and RV64 objects.
- [x] Validation: class/endian checks, float ABI conflicts.
- [x] Fuzz RISC-V ELF parsing → crash-free.

---

## 19. LoongArch Support (LA32 / LA64)

### 19a. Machine Types and Constants
- [x] Define `EM_LOONGARCH` (258).
- [x] Define LoongArch `e_flags`: `EF_LARCH_ABI_MODIFIER_MASK`, `EF_LARCH_ABI_SOFT_FLOAT` (0x1), `EF_LARCH_ABI_SINGLE_FLOAT` (0x2), `EF_LARCH_ABI_DOUBLE_FLOAT` (0x3), `EF_LARCH_OBJABI_V1` (0x40).

### 19b. LoongArch Relocation Type Constants
- [x] `R_LARCH_NONE` (0), `R_LARCH_32` (1), `R_LARCH_64` (2), `R_LARCH_RELATIVE` (3)
- [x] `R_LARCH_COPY` (4), `R_LARCH_JUMP_SLOT` (5), `R_LARCH_TLS_DTPMOD32` (6), `R_LARCH_TLS_DTPMOD64` (7), `R_LARCH_TLS_DTPREL32` (8), `R_LARCH_TLS_DTPREL64` (9), `R_LARCH_TLS_TPREL32` (10), `R_LARCH_TLS_TPREL64` (11), `R_LARCH_IRELATIVE` (12)
- [x] `R_LARCH_MARK_LA` (20), `R_LARCH_MARK_PCREL` (21), `R_LARCH_SOP_PUSH_PCREL` (22) through `R_LARCH_SOP_POP_32_S_10_16_S2` (43)
- [x] `R_LARCH_B16` (64), `R_LARCH_B21` (65), `R_LARCH_B26` (66)
- [x] `R_LARCH_ABS_HI20` (67), `R_LARCH_ABS_LO12` (68), `R_LARCH_ABS64_LO20` (69), `R_LARCH_ABS64_HI12` (70)
- [x] `R_LARCH_PCALA_HI20` (71), `R_LARCH_PCALA_LO12` (72), `R_LARCH_PCALA64_LO20` (73), `R_LARCH_PCALA64_HI12` (74)
- [x] `R_LARCH_GOT_PC_HI20` (75), `R_LARCH_GOT_PC_LO12` (76), `R_LARCH_GOT64_PC_LO20` (77), `R_LARCH_GOT64_PC_HI12` (78)
- [x] TLS: `R_LARCH_TLS_LE_HI20` (79), `R_LARCH_TLS_LE_LO12` (80), `R_LARCH_TLS_LE64_LO20` (81), `R_LARCH_TLS_LE64_HI12` (82), `R_LARCH_TLS_IE_PC_HI20` (83), `R_LARCH_TLS_IE_PC_LO12` (84), `R_LARCH_TLS_IE64_PC_LO20` (85), `R_LARCH_TLS_IE64_PC_HI12` (86), `R_LARCH_TLS_LD_PC_HI20` (87), `R_LARCH_TLS_GD_PC_HI20` (98)
- [x] TLSDESC: `R_LARCH_TLS_DESC_PC_HI20` (99), `R_LARCH_TLS_DESC_PC_LO12` (100), `R_LARCH_TLS_DESC64_PC_LO20` (101), `R_LARCH_TLS_DESC64_PC_HI12` (102), `R_LARCH_TLS_DESC_HI20` (103), `R_LARCH_TLS_DESC_LO12` (104), `R_LARCH_TLS_DESC64_LO20` (105), `R_LARCH_TLS_DESC64_HI12` (106), `R_LARCH_TLS_DESC_LD` (107), `R_LARCH_TLS_DESC_CALL` (108)
- [x] `R_LARCH_TLS_LE_HI20_R` (109), `R_LARCH_TLS_LE_ADD_R` (110), `R_LARCH_TLS_LE_LO12_R` (111)
- [x] Relaxation: `R_LARCH_RELAX` (100), `R_LARCH_ALIGN` (102)
- [x] Content: `R_LARCH_ADD6/8/16/32/64`, `R_LARCH_SUB6/8/16/32/64`

### 19c. LoongArch Relocation Backend
- [x] `larch_reloc_size()` for all LoongArch relocation types.
- [x] `larch_is_pc_relative()`: B16/B21/B26, PCALA_HI20/LO12, GOT_PC_*, TLS_IE_PC_*, TLSDESC_PC_*.
- [x] `larch_is_tls()`: all TLS_* and TLSDESC_* types.
- [x] `larch_apply()`: 20-bit HI20 immediate insertion (bits[24:5]).
- [x] `larch_apply()`: 12-bit LO12 immediate insertion (bits[21:10]).
- [x] `larch_apply()`: Branch B16 (16-bit signed offset << 2), B21 (21-bit << 2), B26 (26-bit << 2).
- [x] `larch_apply()`: PCALA page-aligned PC-relative pair.
- [x] `larch_apply()`: ADD/SUB content relocations.
- [x] Register under `EM_LOONGARCH`.
- [x] Relocation name strings for all LoongArch types.

### 19d. LoongArch Validation
- [x] Accept `EM_LOONGARCH` with `ELFCLASS32` (LA32) or `ELFCLASS64` (LA64).
- [x] Reject `ELFDATA2MSB` (LoongArch is little-endian only).
- [x] Validate `e_flags` ABI modifier and float ABI.

### 19e. LoongArch ELF Read/Write/Create
- [x] Recognize `EM_LOONGARCH` as valid machine type.
- [x] Parse/write RELA relocations.
- [x] `elf_init_loongarch32()`: ELF32/EM_LOONGARCH/ELFDATA2LSB.
- [x] `elf_init_loongarch64()`: ELF64/EM_LOONGARCH/ELFDATA2LSB.

### 19f. LoongArch DWARF
- [x] LoongArch DWARF register mapping: $r0–$r31 → 0–31, $f0–$f31 → 32–63.
- [x] LoongArch CFA: frame pointer $fp ($r22) or $sp ($r3), return address $ra ($r1).

### 19g. LoongArch Testing
- [x] Unit tests for all LoongArch relocation types.
- [x] HI20/LO12 pair calculation.
- [x] Branch encoding (B16, B21, B26).
- [x] TLS and TLSDESC relocation handling.
- [x] Round-trip read/write for LA32 and LA64 objects.
- [x] Validation: class/endian checks.
- [x] Fuzz LoongArch ELF parsing → crash-free.

---

## 20. Motorola 68000 (M68K) Support

### 20a. Machine Types and Constants
- [x] Define `EM_68K` (4).
- [x] M68K has no mandatory `e_flags`; validate flags is 0 or recognized.

### 20b. M68K Relocation Type Constants
- [x] `R_68K_NONE` (0), `R_68K_32` (1), `R_68K_16` (2), `R_68K_8` (3)
- [x] `R_68K_PC32` (4), `R_68K_PC16` (5), `R_68K_PC8` (6)
- [x] `R_68K_GOT32` (7), `R_68K_GOT16` (8), `R_68K_GOT8` (9)
- [x] `R_68K_GOT32O` (10), `R_68K_GOT16O` (11), `R_68K_GOT8O` (12)
- [x] `R_68K_PLT32` (13), `R_68K_PLT16` (14), `R_68K_PLT8` (15)
- [x] `R_68K_PLT32O` (16), `R_68K_PLT16O` (17), `R_68K_PLT8O` (18)
- [x] `R_68K_COPY` (19), `R_68K_GLOB_DAT` (20), `R_68K_JMP_SLOT` (21), `R_68K_RELATIVE` (22)
- [x] TLS: `R_68K_TLS_GD32` (25), `R_68K_TLS_GD16` (26), `R_68K_TLS_GD8` (27), `R_68K_TLS_LDM32` (28), `R_68K_TLS_LDM16` (29), `R_68K_TLS_LDM8` (30), `R_68K_TLS_LDO32` (31), `R_68K_TLS_LDO16` (32), `R_68K_TLS_LDO8` (33), `R_68K_TLS_IE32` (34), `R_68K_TLS_IE16` (35), `R_68K_TLS_IE8` (36), `R_68K_TLS_LE32` (37), `R_68K_TLS_LE16` (38), `R_68K_TLS_LE8` (39), `R_68K_TLS_DTPMOD32` (40), `R_68K_TLS_DTPREL32` (41), `R_68K_TLS_TPREL32` (42)

### 20c. M68K Relocation Backend
- [x] `m68k_reloc_size()` for all M68K relocation types.
- [x] `m68k_is_pc_relative()`: PC32, PC16, PC8.
- [x] `m68k_is_tls()`: all TLS_* types.
- [x] `m68k_apply()`: S + A for absolute, S + A − P for PC-relative, with 8/16/32-bit overflow checks.
- [x] `m68k_apply()`: GOT/PLT slot references.
- [x] `m68k_apply()`: All TLS relocations.
- [x] Register under `EM_68K`.
- [x] Relocation name strings for all M68K types.

### 20d. M68K Validation
- [x] Accept `EM_68K` with `ELFCLASS32` only.
- [x] Accept `ELFDATA2MSB` only (M68K is big-endian).
- [x] Validate `e_flags`.

### 20e. M68K ELF Read/Write/Create
- [x] Recognize `EM_68K` as valid machine type.
- [x] Parse/write RELA relocations (M68K uses RELA).
- [x] `elf_init_m68k()`: ELF32/EM_68K/ELFDATA2MSB.
- [x] Big-endian read/write.

### 20f. M68K DWARF
- [x] M68K DWARF register mapping: D0–D7 → 0–7, A0–A7 → 8–15, FP0–FP7 → 16–23.
- [x] M68K CFA: frame pointer A6 (FP), return address on stack.

### 20g. M68K Testing
- [x] Unit tests for all M68K relocation types (8/16/32-bit absolute and PC-relative).
- [x] TLS relocation handling.
- [x] Big-endian round-trip read/write.
- [x] Validation: reject ELFCLASS64, reject ELFDATA2LSB.
- [x] Fuzz M68K ELF parsing → crash-free.

---

## 21. VAX Support

### 21a. Machine Types and Constants
- [x] Define `EM_VAX` (75).
- [x] VAX has no mandatory `e_flags`; validate flags is 0.

### 21b. VAX Relocation Type Constants
- [x] `R_VAX_NONE` (0), `R_VAX_32` (1), `R_VAX_16` (2), `R_VAX_8` (3)
- [x] `R_VAX_PC32` (4), `R_VAX_PC16` (5), `R_VAX_PC8` (6)
- [x] `R_VAX_GOT32` (7), `R_VAX_PLT32` (13)
- [x] `R_VAX_COPY` (19), `R_VAX_GLOB_DAT` (20), `R_VAX_JMP_SLOT` (21), `R_VAX_RELATIVE` (22)

### 21c. VAX Relocation Backend
- [x] `vax_reloc_size()` for all VAX relocation types.
- [x] `vax_is_pc_relative()`: PC32, PC16, PC8.
- [x] `vax_apply()`: S + A for absolute, S + A − P for PC-relative.
- [x] Register under `EM_VAX`.
- [x] Relocation name strings for all VAX types.

### 21d. VAX Validation
- [x] Accept `EM_VAX` with `ELFCLASS32` only.
- [x] Accept `ELFDATA2LSB` only (VAX is little-endian).

### 21e. VAX ELF Read/Write/Create
- [x] Recognize `EM_VAX` as valid machine type.
- [x] Parse/write RELA relocations (VAX uses RELA).
- [x] `elf_init_vax()`: ELF32/EM_VAX/ELFDATA2LSB.

### 21f. VAX DWARF
- [x] VAX DWARF register mapping: R0–R15 → 0–15, AP → 12, FP → 13, SP → 14, PC → 15.

### 21g. VAX Testing
- [x] Unit tests for all VAX relocation types.
- [x] Round-trip read/write.
- [x] Validation: reject ELFCLASS64, reject ELFDATA2MSB.
- [x] Fuzz VAX ELF parsing → crash-free.

---

## 22. PowerPC / PowerPC64 Support

### 22a. Machine Types and Constants
- [x] Define `EM_PPC` (20), `EM_PPC64` (21).
- [x] Define PPC32 `e_flags`: `EF_PPC_EMB` (0x80000000).
- [x] Define PPC64 `e_flags`: `EF_PPC64_ABI_V1` (1), `EF_PPC64_ABI_V2` (2).
- [x] Define PPC section types: `SHT_PPC_TAGS` (0x70000000), `SHT_PPC64_OPD` (for `.opd` function descriptors).
- [x] Define PPC segment types: `PT_PPC_GNU_MBIND` variants if needed.

### 22b. PowerPC32 Relocation Type Constants
- [x] `R_PPC_NONE` (0), `R_PPC_ADDR32` (1), `R_PPC_ADDR24` (2), `R_PPC_ADDR16` (3)
- [x] `R_PPC_ADDR16_LO` (4), `R_PPC_ADDR16_HI` (5), `R_PPC_ADDR16_HA` (6)
- [x] `R_PPC_ADDR14` (7), `R_PPC_ADDR14_BRTAKEN` (8), `R_PPC_ADDR14_BRNTAKEN` (9)
- [x] `R_PPC_REL24` (10), `R_PPC_REL14` (11), `R_PPC_REL14_BRTAKEN` (12), `R_PPC_REL14_BRNTAKEN` (13)
- [x] `R_PPC_GOT16` (14), `R_PPC_GOT16_LO` (15), `R_PPC_GOT16_HI` (16), `R_PPC_GOT16_HA` (17)
- [x] `R_PPC_PLTREL24` (18), `R_PPC_COPY` (19), `R_PPC_GLOB_DAT` (20), `R_PPC_JMP_SLOT` (21), `R_PPC_RELATIVE` (22)
- [x] `R_PPC_LOCAL24PC` (23), `R_PPC_UADDR32` (24), `R_PPC_UADDR16` (25), `R_PPC_REL32` (26)
- [x] `R_PPC_PLT32` (27), `R_PPC_PLTREL32` (28), `R_PPC_PLT16_LO` (29), `R_PPC_PLT16_HI` (30), `R_PPC_PLT16_HA` (31)
- [x] `R_PPC_SDAREL16` (32), `R_PPC_SECTOFF` (33), `R_PPC_SECTOFF_LO` (34), `R_PPC_SECTOFF_HI` (35), `R_PPC_SECTOFF_HA` (36)
- [x] `R_PPC_IRELATIVE` (248)
- [x] TLS: `R_PPC_TLS` (67), `R_PPC_DTPMOD32` (68), `R_PPC_TPREL16` (69), `R_PPC_TPREL16_LO` (70), `R_PPC_TPREL16_HI` (71), `R_PPC_TPREL16_HA` (72), `R_PPC_TPREL32` (73), `R_PPC_DTPREL16` (74), `R_PPC_DTPREL16_LO` (75), `R_PPC_DTPREL16_HI` (76), `R_PPC_DTPREL16_HA` (77), `R_PPC_DTPREL32` (78), `R_PPC_GOT_TLSGD16` (79), `R_PPC_GOT_TLSGD16_LO` (80), `R_PPC_GOT_TLSGD16_HI` (81), `R_PPC_GOT_TLSGD16_HA` (82), `R_PPC_GOT_TLSLD16` (83), `R_PPC_GOT_TLSLD16_LO` (84), `R_PPC_GOT_TLSLD16_HI` (85), `R_PPC_GOT_TLSLD16_HA` (86), `R_PPC_GOT_TPREL16` (87), `R_PPC_GOT_TPREL16_LO` (88), `R_PPC_GOT_TPREL16_HI` (89), `R_PPC_GOT_TPREL16_HA` (90)

### 22c. PowerPC64 Relocation Type Constants
- [x] `R_PPC64_ADDR64` (38), `R_PPC64_ADDR16_HIGHER` (39), `R_PPC64_ADDR16_HIGHERA` (40), `R_PPC64_ADDR16_HIGHEST` (41), `R_PPC64_ADDR16_HIGHESTA` (42)
- [x] `R_PPC64_UADDR64` (43), `R_PPC64_REL64` (44), `R_PPC64_PLT64` (45), `R_PPC64_PLTREL64` (46)
- [x] `R_PPC64_TOC16` (47), `R_PPC64_TOC16_LO` (48), `R_PPC64_TOC16_HI` (49), `R_PPC64_TOC16_HA` (50), `R_PPC64_TOC` (51)
- [x] `R_PPC64_ADDR16_DS` (56), `R_PPC64_ADDR16_LO_DS` (57), `R_PPC64_GOT16_DS` (58), `R_PPC64_GOT16_LO_DS` (59), `R_PPC64_PLT16_LO_DS` (60)
- [x] `R_PPC64_SECTOFF_DS` (61), `R_PPC64_SECTOFF_LO_DS` (62), `R_PPC64_TOC16_DS` (63), `R_PPC64_TOC16_LO_DS` (64)
- [x] `R_PPC64_ENTRY` (118), `R_PPC64_PCREL34` (132), `R_PPC64_GOT_PCREL34` (133), `R_PPC64_PLT_PCREL34` (134)
- [x] `R_PPC64_IRELATIVE` (248)
- [x] TLS: `R_PPC64_TLS`, `R_PPC64_DTPMOD64`, `R_PPC64_TPREL64`, `R_PPC64_DTPREL64`, `R_PPC64_TPREL16_DS`, `R_PPC64_TPREL16_LO_DS`, `R_PPC64_TPREL16_HIGHER`, `R_PPC64_TPREL16_HIGHERA`, `R_PPC64_TPREL16_HIGHEST`, `R_PPC64_TPREL16_HIGHESTA`, `R_PPC64_DTPREL16_DS`, `R_PPC64_DTPREL16_LO_DS`, `R_PPC64_DTPREL16_HIGHER`, `R_PPC64_DTPREL16_HIGHERA`, `R_PPC64_DTPREL16_HIGHEST`, `R_PPC64_DTPREL16_HIGHESTA`, `R_PPC64_GOT_TLSGD16{_LO/_HI/_HA}`, `R_PPC64_GOT_TLSLD16{_LO/_HI/_HA}`, `R_PPC64_GOT_TPREL16_DS`, `R_PPC64_GOT_TPREL16_LO_DS`, `R_PPC64_GOT_TPREL16_HI`, `R_PPC64_GOT_TPREL16_HA`

### 22d. PowerPC Relocation Backend
- [x] `ppc_reloc_size()` for all PPC32 relocation types.
- [x] `ppc64_reloc_size()` for all PPC64 relocation types.
- [x] `ppc_is_pc_relative()`: REL24, REL14, REL32, LOCAL24PC, PLTREL24, PLTREL32.
- [x] `ppc64_is_pc_relative()`: above + REL64, PCREL34, GOT_PCREL34, PLT_PCREL34.
- [x] `ppc_is_tls()` / `ppc64_is_tls()`: all TLS types.
- [x] `ppc_apply()`: ADDR16_HI/HA with carry adjustment (`((S + A + 0x8000) >> 16) & 0xFFFF` for _HA).
- [x] `ppc_apply()`: REL24 branch (26-bit signed offset in bits[25:2]).
- [x] `ppc_apply()`: REL14 conditional branch (16-bit signed offset in bits[15:2]).
- [x] `ppc64_apply()`: TOC16/TOC16_LO/HI/HA with TOC pointer base.
- [x] `ppc64_apply()`: ADDR16_DS/LO_DS with 4-byte alignment check (low 2 bits must be 0).
- [x] `ppc64_apply()`: PCREL34 (34-bit signed PC-relative, PCRel ABI).
- [x] `ppc64_apply()`: ENTRY (local entry point offset).
- [x] Register under `EM_PPC` and `EM_PPC64`.
- [x] Relocation name strings for all PPC32 and PPC64 types.

### 22e. PowerPC64 OPD and TOC Handling
- [x] Parse `.opd` section for ELFv1 function descriptors (3-word entries: address, TOC, environment).
- [x] Handle ELFv2 local entry point offsets (encoded in `st_other`).
- [x] TOC base calculation for ELFv2 linking.

### 22f. PowerPC Validation
- [x] Accept `EM_PPC` with `ELFCLASS32` only.
- [x] Accept `EM_PPC64` with `ELFCLASS64` only.
- [x] Accept both `ELFDATA2LSB` (PPC64LE) and `ELFDATA2MSB` (PPC/PPC64).
- [x] Validate `e_flags` ELFv1/ELFv2 ABI version for PPC64.
- [x] Validate `.opd` section structure for ELFv1.

### 22g. PowerPC ELF Read/Write/Create
- [x] Recognize `EM_PPC` and `EM_PPC64` as valid machine types.
- [x] PPC32: parse/write RELA relocations (ELF32). PPC64: parse/write RELA relocations (ELF64).
- [x] `elf_init_ppc32()`: ELF32/EM_PPC/ELFDATA2MSB.
- [x] `elf_init_ppc64()`: ELF64/EM_PPC64/ELFDATA2LSB with ELFv2 flags.
- [x] Endian-correct output for both LE and BE variants.

### 22h. PowerPC DWARF
- [x] PPC32 DWARF register mapping: R0–R31 → 0–31, F0–F31 → 32–63, LR → 108, CTR → 109, CR → 68–75.
- [x] PPC64 DWARF register mapping: same base + V0–V31 → 77–108 (VMX/AltiVec).
- [x] PPC CFA: frame pointer R1 (SP), return address LR (R108).

### 22i. PowerPC Testing
- [x] Unit tests for all PPC32 relocation types.
- [x] Unit tests for all PPC64 relocation types.
- [x] HA carry adjustment correctness.
- [x] REL24 branch range checks.
- [x] TOC16 DS-form alignment checks.
- [x] PCREL34 encoding (PCRel ABI).
- [x] `.opd` parsing for ELFv1 objects.
- [x] ELFv2 local entry point offset decoding.
- [x] Round-trip read/write for PPC32 (BE) and PPC64 (LE + BE) objects.
- [x] Validation: class/endian checks, ELFv1/v2 flag validation.
- [x] Fuzz PPC32 and PPC64 ELF parsing → crash-free.

---

## 23. Alpha Support

### 23a. Machine Types and Constants
- [x] Define `EM_ALPHA` (0x9026).
- [x] Alpha has no mandatory `e_flags`; validate flags is 0.

### 23b. Alpha Relocation Type Constants
- [x] `R_ALPHA_NONE` (0), `R_ALPHA_REFLONG` (1), `R_ALPHA_REFQUAD` (2)
- [x] `R_ALPHA_GPREL32` (3), `R_ALPHA_LITERAL` (4), `R_ALPHA_LITUSE` (5)
- [x] `R_ALPHA_GPDISP` (6), `R_ALPHA_BRADDR` (7), `R_ALPHA_HINT` (8)
- [x] `R_ALPHA_SREL16` (9), `R_ALPHA_SREL32` (10), `R_ALPHA_SREL64` (11)
- [x] `R_ALPHA_GPRELHIGH` (17), `R_ALPHA_GPRELLOW` (18), `R_ALPHA_GPREL16` (19)
- [x] `R_ALPHA_COPY` (24), `R_ALPHA_GLOB_DAT` (25), `R_ALPHA_JMP_SLOT` (26), `R_ALPHA_RELATIVE` (27)
- [x] TLS: `R_ALPHA_TLSGD` (25), `R_ALPHA_TLSLDM` (26), `R_ALPHA_DTPMOD64` (27), `R_ALPHA_GOTDTPREL` (28), `R_ALPHA_DTPREL64` (29), `R_ALPHA_DTPRELHI` (30), `R_ALPHA_DTPRELLO` (31), `R_ALPHA_DTPREL16` (32), `R_ALPHA_GOTTPREL` (33), `R_ALPHA_TPREL64` (34), `R_ALPHA_TPRELHI` (35), `R_ALPHA_TPRELLO` (36), `R_ALPHA_TPREL16` (37)

### 23c. Alpha Relocation Backend
- [x] `alpha_reloc_size()` for all Alpha relocation types.
- [x] `alpha_is_pc_relative()`: BRADDR, HINT, SREL16/32/64.
- [x] `alpha_is_tls()`: all TLS types.
- [x] `alpha_apply()`: REFLONG (S + A, 32-bit), REFQUAD (S + A, 64-bit).
- [x] `alpha_apply()`: BRADDR (21-bit signed PC-relative branch, bits[20:0]).
- [x] `alpha_apply()`: HINT (14-bit hint for JMP/JSR).
- [x] `alpha_apply()`: GPREL32 (S + A − GP).
- [x] `alpha_apply()`: LITERAL/LITUSE (GOT slot + optimization hints).
- [x] `alpha_apply()`: GPDISP (GP displacement pair).
- [x] `alpha_apply()`: SREL16/32/64 (S + A − P).
- [x] `alpha_apply()`: All TLS relocations.
- [x] Register under `EM_ALPHA`.
- [x] Relocation name strings for all Alpha types.

### 23d. Alpha Validation
- [x] Accept `EM_ALPHA` with `ELFCLASS64` only.
- [x] Accept `ELFDATA2LSB` only (Alpha is little-endian).
- [x] Validate `e_flags`.

### 23e. Alpha ELF Read/Write/Create
- [x] Recognize `EM_ALPHA` as valid machine type.
- [x] Parse/write RELA relocations (Alpha uses RELA).
- [x] `elf_init_alpha()`: ELF64/EM_ALPHA/ELFDATA2LSB.

### 23f. Alpha DWARF
- [x] Alpha DWARF register mapping: -e– → 0–30, – → 32–62,  → 30,  → 26.
- [x] Alpha CFA: frame pointer  (FP) or  (SP), return address  (RA).

### 23g. Alpha Testing
- [x] Unit tests for all Alpha relocation types.
- [x] BRADDR range check (±4MB).
- [x] GPREL/LITERAL/GPDISP GP-displacement correctness.
- [x] TLS relocation handling.
- [x] Round-trip read/write for Alpha ELF64 objects.
- [x] Validation: reject ELFCLASS32, reject ELFDATA2MSB.
- [x] Fuzz Alpha ELF parsing → crash-free.

---

## 24. IA-64 (Itanium) Support

### 24a. Machine Types and Constants
- [x] Define `EM_IA_64` (50).
- [x] Define IA-64 `e_flags`: `EF_IA_64_ABI64` (0x10), `EF_IA_64_ARCH` (0xFF000000).

### 24b. IA-64 Relocation Type Constants
- [ ] `R_IA64_NONE` (0)
- [ ] `R_IA64_IMM14` (0x21), `R_IA64_IMM22` (0x22), `R_IA64_IMM64` (0x23)
- [ ] `R_IA64_DIR32MSB` (0x24), `R_IA64_DIR32LSB` (0x25), `R_IA64_DIR64MSB` (0x26), `R_IA64_DIR64LSB` (0x27)
- [ ] `R_IA64_GPREL22` (0x2A), `R_IA64_GPREL64I` (0x2B), `R_IA64_GPREL32MSB` (0x2C), `R_IA64_GPREL32LSB` (0x2D), `R_IA64_GPREL64MSB` (0x2E), `R_IA64_GPREL64LSB` (0x2F)
- [ ] `R_IA64_LTOFF22` (0x32), `R_IA64_LTOFF64I` (0x33)
- [ ] `R_IA64_PLTOFF22` (0x3A), `R_IA64_PLTOFF64I` (0x3B), `R_IA64_PLTOFF64MSB` (0x3E), `R_IA64_PLTOFF64LSB` (0x3F)
- [ ] `R_IA64_FPTR64I` (0x43), `R_IA64_FPTR32MSB` (0x44), `R_IA64_FPTR32LSB` (0x45), `R_IA64_FPTR64MSB` (0x46), `R_IA64_FPTR64LSB` (0x47)
- [ ] `R_IA64_PCREL21B` (0x49), `R_IA64_PCREL21M` (0x4A), `R_IA64_PCREL21F` (0x4B), `R_IA64_PCREL32MSB` (0x4C), `R_IA64_PCREL32LSB` (0x4D), `R_IA64_PCREL64MSB` (0x4E), `R_IA64_PCREL64LSB` (0x4F)
- [ ] `R_IA64_LTOFF_FPTR22` (0x52), `R_IA64_LTOFF_FPTR64I` (0x53), `R_IA64_LTOFF_FPTR32MSB` (0x54), `R_IA64_LTOFF_FPTR32LSB` (0x55), `R_IA64_LTOFF_FPTR64MSB` (0x56), `R_IA64_LTOFF_FPTR64LSB` (0x57)
- [ ] `R_IA64_SEGREL32MSB` (0x5C), `R_IA64_SEGREL32LSB` (0x5D), `R_IA64_SEGREL64MSB` (0x5E), `R_IA64_SEGREL64LSB` (0x5F)
- [ ] `R_IA64_SECREL32MSB` (0x64), `R_IA64_SECREL32LSB` (0x65), `R_IA64_SECREL64MSB` (0x66), `R_IA64_SECREL64LSB` (0x67)
- [ ] `R_IA64_REL32MSB` (0x6C), `R_IA64_REL32LSB` (0x6D), `R_IA64_REL64MSB` (0x6E), `R_IA64_REL64LSB` (0x6F)
- [ ] Dynamic: `R_IA64_IPLTMSB` (0x80), `R_IA64_IPLTLSB` (0x81), `R_IA64_COPY` (0x84), `R_IA64_SUB` (0x85)
- [ ] TLS: `R_IA64_LTOFF_DTPMOD22` (0xAA), `R_IA64_DTPMOD64MSB` (0xAE), `R_IA64_DTPMOD64LSB` (0xAF), `R_IA64_LTOFF_DTPREL22` (0xB2), `R_IA64_DTPREL14` (0xB1), `R_IA64_DTPREL22` (0xB2), `R_IA64_DTPREL64I` (0xB3), `R_IA64_DTPREL32MSB` (0xB4), `R_IA64_DTPREL32LSB` (0xB5), `R_IA64_DTPREL64MSB` (0xB6), `R_IA64_DTPREL64LSB` (0xB7), `R_IA64_LTOFF_TPREL22` (0xBA), `R_IA64_TPREL14` (0xC1), `R_IA64_TPREL22` (0xC2), `R_IA64_TPREL64I` (0xC3), `R_IA64_TPREL64MSB` (0xC6), `R_IA64_TPREL64LSB` (0xC7)

### 24c. IA-64 Relocation Backend
- [ ] `ia64_reloc_size()` for all IA-64 relocation types.
- [ ] `ia64_is_pc_relative()`: PCREL21B/M/F, PCREL32MSB/LSB, PCREL64MSB/LSB.
- [ ] `ia64_is_tls()`: all TLS types.
- [ ] `ia64_apply()`: DIR32/64 MSB/LSB (S + A, 32/64-bit, both endiannesses).
- [ ] `ia64_apply()`: PCREL21B (21-bit signed PC-relative branch in bundle slot).
- [ ] `ia64_apply()`: PCREL21M/F (slot-specific PC-relative).
- [ ] `ia64_apply()`: IMM22 (22-bit immediate in instruction slot).
- [ ] `ia64_apply()`: IMM64 (64-bit immediate spread across MOVL bundle).
- [ ] `ia64_apply()`: IMM14 (14-bit immediate in instruction slot).
- [ ] `ia64_apply()`: GPREL22/64I/32/64 (GP-relative).
- [ ] `ia64_apply()`: LTOFF22/64I (linkage table offset, GOT-relative).
- [ ] `ia64_apply()`: FPTR (function pointer descriptor).
- [ ] `ia64_apply()`: SEGREL/SECREL (segment/section relative).
- [ ] Bundle slot decoding: extract template byte, identify slot positions, decode/encode instruction immediates.
- [ ] Register under `EM_IA_64`.
- [ ] Relocation name strings for all IA-64 types.

### 24d. IA-64 Validation
- [ ] Accept `EM_IA_64` with `ELFCLASS64` only.
- [ ] Accept both `ELFDATA2LSB` and `ELFDATA2MSB`.
- [ ] Validate `e_flags` `EF_IA_64_ABI64` and `EF_IA_64_ARCH`.

### 24e. IA-64 ELF Read/Write/Create
- [ ] Recognize `EM_IA_64` as valid machine type.
- [ ] Parse/write RELA relocations (IA-64 uses RELA).
- [ ] `elf_init_ia64()`: ELF64/EM_IA_64/ELFDATA2LSB.
- [ ] Endian-correct output for big-endian IA-64 (HP-UX).

### 24f. IA-64 DWARF
- [ ] IA-64 DWARF register mapping: GR0–GR127 → 0–127, FR0–FR127 → 128–255, BR0–BR7 → 320–327, PR0–PR63 → 256–319.
- [ ] IA-64 CFA: frame pointer GR12 (SP), return address BR0 (RP).

### 24g. IA-64 Testing
- [ ] Unit tests for all IA-64 relocation types.
- [ ] Bundle slot encoding/decoding correctness.
- [ ] IMM22/IMM64/IMM14 immediate insertion.
- [ ] PCREL21B branch range checks.
- [ ] GP-relative and LTOFF relocations.
- [ ] FPTR function descriptor handling.
- [ ] Round-trip read/write for IA-64 ELF64 objects.
- [ ] Validation: reject ELFCLASS32, e_flags checks.
- [ ] Fuzz IA-64 ELF parsing → crash-free.
