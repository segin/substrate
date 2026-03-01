# `usr.lib/elfobj` — Multi-Architecture Tasklist

Goal: extend `libelfobj` with complete multi-architecture support for i386, x86-64, ARMv7 (ELF32, `EM_ARM`), and AArch64 (ELF64, `EM_AARCH64`) across all library subsystems — constants, relocation backends, validation, ELF creation, DWARF, link planning, and testing.

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

- [ ] Accept `EM_ARM` and `EM_AARCH64` inputs.
- [ ] Reject class mismatches: ARM must be ELFCLASS32, AArch64 must be ELFCLASS64.
- [ ] Merge `e_flags`: take union of float ABI flags; error on conflict.
- [ ] Handle ARM/Thumb interwork symbol merging (symbol with T-bit → different treatment).
- [ ] `.ARM.exidx` section merging: sort entries by covered address range.
- [ ] `.ARM.attributes` merging: attribute compatibility checking per Build Attributes spec.
- [ ] Section group/COMDAT handling for ARM is identical to x86.
- [ ] AArch64 `.note.gnu.property` merging: AND of BTI/PAC bits across inputs.

---

## 10. ARM Build Attributes Parser

Per ARM EABI §2.2.3, `.ARM.attributes` contains vendor-specific attribute tags:

- [ ] Parse attribute section format: subsection headers, vendor name, tag-value pairs.
- [ ] `Tag_CPU_name` (4): CPU name string (e.g., "Cortex-A15").
- [ ] `Tag_CPU_arch` (6): architecture version (1=v4, 6=v6, 10=v7, 13=v7E-M, 14=v8).
- [ ] `Tag_CPU_arch_profile` (7): 'A' (Application), 'R' (Real-time), 'M' (Microcontroller).
- [ ] `Tag_ARM_ISA_use` (8): 0=no, 1=yes.
- [ ] `Tag_THUMB_ISA_use` (9): 0=no, 1=Thumb, 2=Thumb-2, 3=Armv8-M.baseline.
- [ ] `Tag_FP_arch` (10): 0=none, 1=VFPv1, 2=VFPv2, 3=VFPv3, 4=VFPv3-D16, 5=VFPv4, 6=VFPv4-D16.
- [ ] `Tag_WMMX_arch` (11): Wireless MMX.
- [ ] `Tag_Advanced_SIMD_arch` (12): 0=none, 1=NEONv1, 2=NEONv1+fused-MAC, 3=ARMv8 NEON.
- [ ] `Tag_PCS_config` (13): calling convention.
- [ ] `Tag_ABI_PCS_R9_use` (14): R9 usage.
- [ ] `Tag_ABI_PCS_RW_data` (15): RW data addressing.
- [ ] `Tag_ABI_PCS_RO_data` (16): RO data addressing.
- [ ] `Tag_ABI_PCS_GOT_use` (17): GOT addressing.
- [ ] `Tag_ABI_PCS_wchar_t` (18): wchar_t size.
- [ ] `Tag_ABI_FP_rounding` (19): rounding mode.
- [ ] `Tag_ABI_FP_denormal` (20): denormal handling.
- [ ] `Tag_ABI_FP_exceptions` (21): exception model.
- [ ] `Tag_ABI_FP_user_exceptions` (22): user-mode FP exceptions.
- [ ] `Tag_ABI_FP_number_model` (23): IEEE 754 conformance.
- [ ] `Tag_ABI_align_needed` (24): alignment requirements.
- [ ] `Tag_ABI_align_preserved` (25): alignment guarantees.
- [ ] `Tag_ABI_enum_size` (26): enum sizing.
- [ ] `Tag_ABI_HardFP_use` (27): hard-float VFP register usage.
- [ ] `Tag_ABI_VFP_args` (28): VFP argument passing convention.
- [ ] `Tag_ABI_optimization_goals` (30): optimization priorities.
- [ ] `Tag_CPU_unaligned_access` (34): unaligned access support.
- [ ] `Tag_FP_HP_extension` (36): half-precision extension.
- [ ] `Tag_ABI_FP_16bit_format` (38): FP16 format (IEEE754/alternative).
- [ ] `Tag_MPExtension_use` (42): multiprocessing extensions.
- [ ] `Tag_DIV_use` (44): integer divide instruction usage.
- [ ] `Tag_DSP_extension` (46): DSP extension usage.
- [ ] `Tag_Virtualization_use` (68): virtualization extensions.
- [ ] API: `elf_arm_attribute_count()`, `elf_arm_attribute_tag_at()`, `elf_arm_attribute_value_at()`, `elf_arm_attribute_string_at()`.
- [ ] Validation: check compatibility of `Tag_CPU_arch` + `Tag_FP_arch` across link inputs.

---

## 11. Testing

### 11a. Relocation Backend Unit Tests
- [ ] For each ARM relocation type: known input (S, A, P, GOT) → expected output value.
- [ ] Overflow: ARM `R_ARM_CALL` with offset > ±32MB → error.
- [ ] Overflow: AArch64 `R_AARCH64_JUMP26` with offset > ±128MB → error.
- [ ] Overflow: `R_AARCH64_ADR_PREL_PG_HI21` with page delta > ±4GB → error.
- [ ] Alignment: `R_AARCH64_LDST32_ABS_LO12_NC` with non-4-byte-aligned → error.
- [ ] Thumb interwork: `R_ARM_CALL` to Thumb target → T bit set correctly.
- [ ] AArch64 ADRP+ADD pair: page calculation correct for page-aligned and non-aligned addresses.
- [ ] AArch64 MOVW_UABS_G0/G1/G2/G3: correct 16-bit slice extraction.
- [ ] ARM MOVW/MOVT: correct instruction field insertion for known bit patterns.
- [ ] Thumb BL encoding: J1/J2 bits encode correctly for positive and negative offsets.
- [ ] PC-relative classification: every PC-relative reloc returns true, every absolute returns false.
- [ ] TLS classification: every TLS reloc returns true, non-TLS returns false.
- [ ] All reloc sizes match expected values.

### 11b. Read/Write Round-Trip Tests
- [ ] Read ARM ELF32 object → inspect sections/symbols/relocs → write back → byte-compare.
- [ ] Read AArch64 ELF64 object → inspect → write back → byte-compare.
- [ ] Read ARM object with `.ARM.exidx` → section present with correct `sh_link`.
- [ ] Read ARM object with `.ARM.attributes` → parse attributes, verify tag values.
- [ ] Read AArch64 object with `.note.gnu.property` → BTI/PAC flags extracted.
- [ ] Create ARM object from scratch → write → `readelf -a` validates headers/sections/symbols/relocs.
- [ ] Create AArch64 object from scratch → write → `readelf -a` validates.
- [ ] Big-endian ARM object: read and write with correct byte order.

### 11c. Validation Tests
- [ ] ARM ELF with `ELFCLASS64` → rejected.
- [ ] AArch64 ELF with `ELFCLASS32` → rejected.
- [ ] ARM ELF with conflicting float ABI flags → diagnostic.
- [ ] ARM ELF with missing `.ARM.exidx` `SHF_LINK_ORDER` → diagnostic.
- [ ] AArch64 ELF with unknown `e_flags` → warning.
- [ ] Unrecognized relocation type → diagnostic.

### 11d. Link Planning Tests
- [ ] Merge two ARM objects → `e_flags` union is correct.
- [ ] Merge hard-float + soft-float ARM objects → error.
- [ ] Merge ARM + AArch64 objects → rejected (class mismatch).
- [ ] `.ARM.attributes` merge: compatible objects → merged, incompatible → error.
- [ ] AArch64 `.note.gnu.property` merge: BTI+PAC from both inputs → AND of features.

### 11e. Build Attributes Tests
- [ ] Parse `.ARM.attributes` from GCC-produced ARM object.
- [ ] All standard tags readable via API.
- [ ] Unknown vendor subsections skipped without error.
- [ ] Tag compatibility check across two inputs for `Tag_CPU_arch`, `Tag_FP_arch`, `Tag_ABI_VFP_args`.

### 11f. DWARF Tests
- [ ] ARM DWARF register numbers map correctly in `.debug_frame` / `.eh_frame`.
- [ ] AArch64 DWARF register numbers map correctly.
- [ ] CFA restoration rules work for ARM R11 frame pointer.
- [ ] CFA restoration rules work for AArch64 X29 frame pointer.

### 11g. Fuzz Tests
- [ ] Fuzz ARM ELF object parsing → crash-free.
- [ ] Fuzz AArch64 ELF object parsing → crash-free.
- [ ] Fuzz `.ARM.attributes` section parsing → crash-free.

---

## 12. Expanded x86 Relocation Backend

### 12a. i386 Backend Expansion
- [ ] `R_386_COPY` → no value (dynamic linker copies data)
- [ ] `R_386_GLOB_DAT` → S (GOT slot fill)
- [ ] `R_386_JMP_SLOT` → S (PLT GOT slot fill)
- [ ] `R_386_RELATIVE` → B(S) + A (base-relative)
- [ ] `R_386_16` → S + A, check unsigned 16-bit
- [ ] `R_386_PC16` → S + A − P, check signed 16-bit
- [ ] `R_386_8` → S + A, check unsigned 8-bit
- [ ] `R_386_PC8` → S + A − P, check signed 8-bit
- [ ] `R_386_SIZE32` → Z + A (symbol size)
- [ ] `R_386_GOT32X` → GOT(S) + A − GOT_ORG (relaxable GOT reference)
- [ ] `R_386_IRELATIVE` → indirect function resolution
- [ ] `R_386_TLS_DTPMOD32` → module ID for TLS
- [ ] `R_386_TLS_DTPOFF32` → offset within TLS block
- [ ] `R_386_TLS_LE_32` → negative TP-relative offset
- [ ] `R_386_TLS_TPOFF32` → negative TP-relative offset (variant)
- [ ] Add `i386_is_tls()` for complete TLS classification: all `R_386_TLS_*` types

### 12b. x86-64 Backend Expansion
- [ ] `R_X86_64_COPY` → no value
- [ ] `R_X86_64_GLOB_DAT` → S
- [ ] `R_X86_64_JUMP_SLOT` → S
- [ ] `R_X86_64_RELATIVE` → B + A
- [ ] `R_X86_64_16` → S + A, check unsigned 16-bit
- [ ] `R_X86_64_PC16` → S + A − P, check signed 16-bit
- [ ] `R_X86_64_8` → S + A, check unsigned 8-bit
- [ ] `R_X86_64_PC8` → S + A − P, check signed 8-bit
- [ ] `R_X86_64_PC64` → S + A − P (64-bit PC-relative)
- [ ] `R_X86_64_GOTOFF64` → S + A − GOT_ORG
- [ ] `R_X86_64_GOTPC32` → GOT_ORG + A − P
- [ ] `R_X86_64_SIZE32` → Z + A (check 32-bit), `R_X86_64_SIZE64` → Z + A
- [ ] `R_X86_64_GOTPCRELX` → GOT(S) + A − P (relaxable to LEA for non-preemptible)
- [ ] `R_X86_64_REX_GOTPCRELX` → same with REX prefix
- [ ] `R_X86_64_IRELATIVE` → indirect function resolution
- [ ] `R_X86_64_DTPMOD64`, `R_X86_64_DTPOFF64`, `R_X86_64_TPOFF64` → TLS module/offset dynamic
- [ ] `R_X86_64_TLSLD` → Local Dynamic TLS
- [ ] `R_X86_64_DTPOFF32` → 32-bit DTP offset
- [ ] `R_X86_64_GOTPC32_TLSDESC` → TLSDESC GOT-relative
- [ ] `R_X86_64_TLSDESC_CALL` → TLSDESC call relocation
- [ ] `R_X86_64_TLSDESC` → TLSDESC pair
- [ ] Add `x64_is_tls()` for complete TLS classification: all `R_X86_64_TLS*`, `GOTTPOFF`, `TPOFF32`, `DTPMOD64`, `DTPOFF64`, `TPOFF64`, `DTPOFF32`, `GOTPC32_TLSDESC`, `TLSDESC_CALL`, `TLSDESC`

### 12c. x86 Relocation Name Strings
- [ ] `elf_reloc_name_for_machine(machine, type)` → human-readable string (e.g., `"R_X86_64_PC32"`)
- [ ] Complete name tables for all i386 and x86-64 relocation types.
- [ ] Complete name tables for all ARM and AArch64 relocation types.
- [ ] Unknown types → `"UNKNOWN(N)"` format.

---

## 13. x86-Specific Validation (`elf_validate.c`)

- [ ] Validate `EM_386` with `ELFCLASS32` only.
- [ ] Validate `EM_X86_64` with `ELFCLASS64` only.
- [ ] Both x86 variants: `ELFDATA2LSB` only (x86 is always little-endian).
- [ ] Validate `.note.gnu.property` structure for x86: NT_GNU_PROPERTY_TYPE_0, correct alignment (4-byte for ELF32, 8-byte for ELF64).
- [ ] Validate `GNU_PROPERTY_X86_ISA_1_NEEDED` bit values (only defined bits set).
- [ ] Validate `GNU_PROPERTY_X86_FEATURE_1_AND` bit values (only IBT/SHSTK).
- [ ] Warn on unknown GNU properties (forward compatibility).
- [ ] Validate `SHT_REL` used for i386, `SHT_RELA` for x86-64.
- [ ] Validate `.eh_frame` CIE return address register (8 for i386/RA, 16 for x86-64/RA).

---

## 14. GNU Property / Note API

- [ ] `elf_note_count(obj)` → number of notes across all note sections.
- [ ] `elf_note_at(obj, index)` → `{name, type, desc_data, desc_size}`.
- [ ] `elf_gnu_property_count(obj)` → number of GNU properties in `.note.gnu.property`.
- [ ] `elf_gnu_property_at(obj, index)` → `{type, data, data_size}`.
- [ ] `elf_x86_isa_level(obj)` → bitmask of `GNU_PROPERTY_X86_ISA_1_NEEDED` (0 if absent).
- [ ] `elf_x86_feature_flags(obj)` → bitmask of `GNU_PROPERTY_X86_FEATURE_1_AND` (0 if absent).
- [ ] `elf_aarch64_feature_flags(obj)` → bitmask of `GNU_PROPERTY_AARCH64_FEATURE_1_AND` (0 if absent).
- [ ] `elf_add_gnu_property_x86(obj, isa_needed, isa_used, feature_1)` → create/update `.note.gnu.property`.
- [ ] `elf_add_gnu_property_aarch64(obj, feature_1)` → create/update `.note.gnu.property`.
- [ ] `elf_build_id(obj, out_data, out_size)` → extract `.note.gnu.build-id` contents.

---

## 15. x86-Specific Tests

### 15a. Expanded i386 Relocation Tests
- [ ] `R_386_16` and `R_386_PC16`: verify 16-bit relocations.
- [ ] `R_386_8` and `R_386_PC8`: verify 8-bit relocations.
- [ ] `R_386_SIZE32`: verify symbol size relocation.
- [ ] `R_386_GOT32X`: verify relaxable GOT reference.
- [ ] `R_386_IRELATIVE`: verify indirect function.
- [ ] `R_386_TLS_DTPMOD32`/`DTPOFF32`: verify TLS relocations.
- [ ] All dynamic relocations (COPY/GLOB_DAT/JMP_SLOT/RELATIVE): verify.

### 15b. Expanded x86-64 Relocation Tests
- [ ] `R_X86_64_PC64`: verify 64-bit PC-relative.
- [ ] `R_X86_64_GOTOFF64`/`GOTPC32`: verify GOT-relative.
- [ ] `R_X86_64_SIZE32`/`SIZE64`: verify size relocations.
- [ ] `R_X86_64_GOTPCRELX`/`REX_GOTPCRELX`: verify relaxable GOT references.
- [ ] `R_X86_64_IRELATIVE`: verify indirect function.
- [ ] `R_X86_64_TLSLD`/`DTPOFF32`: verify Local Dynamic TLS.
- [ ] `R_X86_64_GOTPC32_TLSDESC`/`TLSDESC_CALL`/`TLSDESC`: verify TLSDESC relocations.
- [ ] All 16-bit and 8-bit relocations: verify.

### 15c. GNU Property Tests
- [ ] Read x86-64 object with `.note.gnu.property` → extract ISA level bits.
- [ ] Read AArch64 object → extract BTI/PAC flags.
- [ ] Create object → add GNU property → write → read back → verify.
- [ ] Property with ISA_1_V4 → `elf_x86_isa_level()` returns correct bitmask.
- [ ] Object without `.note.gnu.property` → `elf_x86_isa_level()` returns 0.
- [ ] Merge two objects with different ISA levels → OR result.
- [ ] Merge two objects with FEATURE_1_AND → AND result.

### 15d. Relocation Name Tests
- [ ] Every i386 relocation type → correct name string.
- [ ] Every x86-64 relocation type → correct name string.
- [ ] Every ARM relocation type → correct name string.
- [ ] Every AArch64 relocation type → correct name string.
- [ ] Unknown type → `"UNKNOWN(N)"` format.

### 15e. x86 Validation Tests
- [ ] EM_386 with ELFCLASS64 → rejected.
- [ ] EM_X86_64 with ELFCLASS32 → rejected.
- [ ] x86 with ELFDATA2MSB → rejected.
- [ ] `.note.gnu.property` with bad alignment → diagnostic.
- [ ] `.note.gnu.property` with unknown property type → warning (not error).

---

## 16. Documentation

- [ ] Update `README.md` with full multi-architecture support notes.
- [ ] Document all relocation backend registration APIs.
- [ ] Document ARM build attributes API.
- [ ] Document GNU property API.
- [ ] Document relocation name API.
- [ ] Update `COMPATIBILITY_MATRIX.md` with ARM/AArch64 entries.
- [ ] Man page updates for `elfobj.3` with per-arch API functions.
