# `usr.lib/elfobj` — ARM/AArch64 Support Tasklist

Goal: extend `libelfobj` with full ARMv7 (ELF32, `EM_ARM`) and AArch64 (ELF64, `EM_AARCH64`) support across all library subsystems — constants, relocation backends, validation, ELF creation, DWARF, link planning, and testing.

---

## 0. Generic Tooling API Support

- [x] Add indexed section accessor `elf_section_get(const elfobj_t *, size_t)` for consumer utilities (`size`, `objdump`, `readelf`) that need stable section iteration.

---

## 1. ELF Constants and Header Definitions

### 1a. Machine Types (`elf_private.h` or upstream `elf.h`)
- [ ] Define `EM_ARM` (40).
- [ ] Define `EM_AARCH64` (183).

### 1b. ARM-Specific ELF Header Flags (`e_flags`)
- [ ] `EF_ARM_ABI_VER5` (0x05000000) — EABI version 5.
- [ ] `EF_ARM_ABI_FLOAT_HARD` (0x00000400) — hard-float ABI.
- [ ] `EF_ARM_ABI_FLOAT_SOFT` (0x00000200) — soft-float ABI.
- [ ] `EF_ARM_BE8` (0x00800000) — BE8 data format.
- [ ] `EF_ARM_INTERWORK` (0x00000004) — ARM/Thumb interwork support.
- [ ] `EF_ARM_APCS_26` (0x00000008).
- [ ] `EF_ARM_APCS_FLOAT` (0x00000010).
- [ ] `EF_ARM_VFP_FLOAT` (0x00000400).
- [ ] `EF_ARM_MAVERICK_FLOAT` (0x00000800).
- [ ] Parse and expose `e_flags` for ARM objects via `elf_flags()` accessor.
- [ ] Validate `e_flags` ABI version field on read.

### 1c. AArch64-Specific ELF Header Flags
- [ ] `EF_AARCH64_CHERI_PURECAP` (reserved).
- [ ] AArch64 has no mandatory `e_flags` bits; validate that flags is 0 or recognized optional.

### 1d. ARM Relocation Type Constants
Every relocation type used by GCC/LLVM for ARM targets must be defined:

- [ ] `R_ARM_NONE` (0)
- [ ] `R_ARM_PC24` (1)
- [ ] `R_ARM_ABS32` (2)
- [ ] `R_ARM_REL32` (3)
- [ ] `R_ARM_LDR_PC_G0` (4)
- [ ] `R_ARM_ABS16` (5)
- [ ] `R_ARM_ABS12` (6)
- [ ] `R_ARM_THM_ABS5` (7)
- [ ] `R_ARM_ABS8` (8)
- [ ] `R_ARM_SBREL32` (9)
- [ ] `R_ARM_THM_CALL` (10)
- [ ] `R_ARM_THM_PC8` (11)
- [ ] `R_ARM_BREL_ADJ` (12)
- [ ] `R_ARM_TLS_DESC` (13)
- [ ] `R_ARM_THM_SWI8` (14)
- [ ] `R_ARM_XPC25` (15)
- [ ] `R_ARM_THM_XPC22` (16)
- [ ] `R_ARM_TLS_DTPMOD32` (17)
- [ ] `R_ARM_TLS_DTPOFF32` (18)
- [ ] `R_ARM_TLS_TPOFF32` (19)
- [ ] `R_ARM_COPY` (20)
- [ ] `R_ARM_GLOB_DAT` (21)
- [ ] `R_ARM_JUMP_SLOT` (22)
- [ ] `R_ARM_RELATIVE` (23)
- [ ] `R_ARM_GOTOFF32` (24)
- [ ] `R_ARM_BASE_PREL` (25) / `R_ARM_GOTPC`
- [ ] `R_ARM_GOT_BREL` (26) / `R_ARM_GOT32`
- [ ] `R_ARM_PLT32` (27)
- [ ] `R_ARM_CALL` (28)
- [ ] `R_ARM_JUMP24` (29)
- [ ] `R_ARM_THM_JUMP24` (30)
- [ ] `R_ARM_BASE_ABS` (31)
- [ ] `R_ARM_ALU_PCREL_7_0` (32)
- [ ] `R_ARM_ALU_PCREL_15_8` (33)
- [ ] `R_ARM_ALU_PCREL_23_16` (34)
- [ ] `R_ARM_LDR_SBREL_11_0_NC` (35)
- [ ] `R_ARM_ALU_SBREL_19_12_NC` (36)
- [ ] `R_ARM_ALU_SBREL_27_20_CK` (37)
- [ ] `R_ARM_TARGET1` (38)
- [ ] `R_ARM_SBREL31` (39)
- [ ] `R_ARM_V4BX` (40)
- [ ] `R_ARM_TARGET2` (41)
- [ ] `R_ARM_PREL31` (42)
- [ ] `R_ARM_MOVW_ABS_NC` (43)
- [ ] `R_ARM_MOVT_ABS` (44)
- [ ] `R_ARM_MOVW_PREL_NC` (45)
- [ ] `R_ARM_MOVT_PREL` (46)
- [ ] `R_ARM_THM_MOVW_ABS_NC` (47)
- [ ] `R_ARM_THM_MOVT_ABS` (48)
- [ ] `R_ARM_THM_MOVW_PREL_NC` (49)
- [ ] `R_ARM_THM_MOVT_PREL` (50)
- [ ] `R_ARM_THM_JUMP19` (51)
- [ ] `R_ARM_THM_JUMP6` (52)
- [ ] `R_ARM_THM_ALU_PREL_11_0` (53)
- [ ] `R_ARM_THM_PC12` (54)
- [ ] `R_ARM_ABS32_NOI` (55)
- [ ] `R_ARM_REL32_NOI` (56)
- [ ] `R_ARM_ALU_PC_G0_NC` (57)
- [ ] `R_ARM_ALU_PC_G0` (58)
- [ ] `R_ARM_ALU_PC_G1_NC` (59)
- [ ] `R_ARM_ALU_PC_G1` (60)
- [ ] `R_ARM_ALU_PC_G2` (61)
- [ ] `R_ARM_LDR_PC_G1` (62)
- [ ] `R_ARM_LDR_PC_G2` (63)
- [ ] `R_ARM_LDRS_PC_G0` (64)
- [ ] `R_ARM_LDRS_PC_G1` (65)
- [ ] `R_ARM_LDRS_PC_G2` (66)
- [ ] `R_ARM_LDC_PC_G0` (67)
- [ ] `R_ARM_LDC_PC_G1` (68)
- [ ] `R_ARM_LDC_PC_G2` (69)
- [ ] `R_ARM_ALU_SB_G0_NC` (70)
- [ ] `R_ARM_ALU_SB_G0` (71)
- [ ] `R_ARM_ALU_SB_G1_NC` (72)
- [ ] `R_ARM_ALU_SB_G1` (73)
- [ ] `R_ARM_ALU_SB_G2` (74)
- [ ] `R_ARM_LDR_SB_G0` (75)
- [ ] `R_ARM_LDR_SB_G1` (76)
- [ ] `R_ARM_LDR_SB_G2` (77)
- [ ] `R_ARM_LDRS_SB_G0` (78)
- [ ] `R_ARM_LDRS_SB_G1` (79)
- [ ] `R_ARM_LDRS_SB_G2` (80)
- [ ] `R_ARM_LDC_SB_G0` (81)
- [ ] `R_ARM_LDC_SB_G1` (82)
- [ ] `R_ARM_LDC_SB_G2` (83)
- [ ] `R_ARM_MOVW_BREL_NC` (84)
- [ ] `R_ARM_MOVT_BREL` (85)
- [ ] `R_ARM_MOVW_BREL` (86)
- [ ] `R_ARM_THM_MOVW_BREL_NC` (87)
- [ ] `R_ARM_THM_MOVT_BREL` (88)
- [ ] `R_ARM_THM_MOVW_BREL` (89)
- [ ] `R_ARM_TLS_GOTDESC` (90)
- [ ] `R_ARM_TLS_CALL` (91)
- [ ] `R_ARM_TLS_DESCSEQ` (92)
- [ ] `R_ARM_THM_TLS_CALL` (93)
- [ ] `R_ARM_PLT32_ABS` (94)
- [ ] `R_ARM_GOT_ABS` (95)
- [ ] `R_ARM_GOT_PREL` (96)
- [ ] `R_ARM_GOT_BREL12` (97)
- [ ] `R_ARM_GOTOFF12` (98)
- [ ] `R_ARM_GOTRELAX` (99)
- [ ] `R_ARM_GNU_VTENTRY` (100)
- [ ] `R_ARM_GNU_VTINHERIT` (101)
- [ ] `R_ARM_THM_JUMP11` (102)
- [ ] `R_ARM_THM_JUMP8` (103)
- [ ] `R_ARM_TLS_GD32` (104)
- [ ] `R_ARM_TLS_LDM32` (105)
- [ ] `R_ARM_TLS_LDO32` (106)
- [ ] `R_ARM_TLS_IE32` (107)
- [ ] `R_ARM_TLS_LE32` (108)
- [ ] `R_ARM_TLS_LDO12` (109)
- [ ] `R_ARM_TLS_LE12` (110)
- [ ] `R_ARM_TLS_IE12GP` (111)
- [ ] `R_ARM_IRELATIVE` (160)
- [ ] `R_ARM_RXPC25` (249)
- [ ] `R_ARM_RSBREL32` (250)
- [ ] `R_ARM_THM_RPC22` (251)
- [ ] `R_ARM_RREL32` (252)
- [ ] `R_ARM_RABS32` (253)
- [ ] `R_ARM_RPC24` (254)
- [ ] `R_ARM_RBASE` (255)

### 1e. AArch64 Relocation Type Constants
- [ ] `R_AARCH64_NONE` (0)
- [ ] `R_AARCH64_ABS64` (257), `R_AARCH64_ABS32` (258), `R_AARCH64_ABS16` (259)
- [ ] `R_AARCH64_PREL64` (260), `R_AARCH64_PREL32` (261), `R_AARCH64_PREL16` (262)
- [ ] `R_AARCH64_MOVW_UABS_G0` (263), `_G0_NC` (264), `_G1` (265), `_G1_NC` (266), `_G2` (267), `_G2_NC` (268), `_G3` (269)
- [ ] `R_AARCH64_MOVW_SABS_G0` (270), `_G1` (271), `_G2` (272)
- [ ] `R_AARCH64_LD_PREL_LO19` (273), `R_AARCH64_ADR_PREL_LO21` (274)
- [ ] `R_AARCH64_ADR_PREL_PG_HI21` (275), `_NC` (276)
- [ ] `R_AARCH64_ADD_ABS_LO12_NC` (277)
- [ ] `R_AARCH64_LDST8_ABS_LO12_NC` (278)
- [ ] `R_AARCH64_TSTBR14` (279), `R_AARCH64_CONDBR19` (280)
- [ ] `R_AARCH64_JUMP26` (282), `R_AARCH64_CALL26` (283)
- [ ] `R_AARCH64_LDST16_ABS_LO12_NC` (284), `R_AARCH64_LDST32_ABS_LO12_NC` (285), `R_AARCH64_LDST64_ABS_LO12_NC` (286), `R_AARCH64_LDST128_ABS_LO12_NC` (299)
- [ ] `R_AARCH64_MOVW_PREL_G0` (287), `_G0_NC` (288), `_G1` (289), `_G1_NC` (290), `_G2` (291), `_G2_NC` (292), `_G3` (293)
- [ ] `R_AARCH64_GOT_LD_PREL19` (309), `R_AARCH64_ADR_GOT_PAGE` (311), `R_AARCH64_LD64_GOT_LO12_NC` (312)
- [ ] `R_AARCH64_LD64_GOTPAGE_LO15` (313)
- [ ] `R_AARCH64_TLSGD_ADR_PREL21` (512), `R_AARCH64_TLSGD_ADR_PAGE21` (513), `R_AARCH64_TLSGD_ADD_LO12_NC` (514), `R_AARCH64_TLSGD_MOVW_G1` (515), `R_AARCH64_TLSGD_MOVW_G0_NC` (516)
- [ ] `R_AARCH64_TLSLD_ADR_PREL21` (517), `R_AARCH64_TLSLD_ADR_PAGE21` (518), `R_AARCH64_TLSLD_ADD_LO12_NC` (519), `R_AARCH64_TLSLD_ADD_DTPREL_HI12` (528), `R_AARCH64_TLSLD_ADD_DTPREL_LO12` (529), `_NC` (530)
- [ ] `R_AARCH64_TLSLD_LDST8_DTPREL_LO12` (531), `_NC` (532)
- [ ] `R_AARCH64_TLSLD_LDST16/32/64/128_DTPREL_LO12{_NC}` (533–540)
- [ ] `R_AARCH64_TLSLD_MOVW_DTPREL_G0{_NC}` (520,521), `_G1{_NC}` (522,523), `_G2` (524)
- [ ] `R_AARCH64_TLSIE_MOVW_GOTTPREL_G1` (539), `_G0_NC` (540)
- [ ] `R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21` (541), `R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC` (542), `R_AARCH64_TLSIE_LD_GOTTPREL_PREL19` (543)
- [ ] `R_AARCH64_TLSLE_MOVW_TPREL_G2` (544), `_G1{_NC}` (545,546), `_G0{_NC}` (547,548)
- [ ] `R_AARCH64_TLSLE_ADD_TPREL_HI12` (549), `R_AARCH64_TLSLE_ADD_TPREL_LO12` (550), `_NC` (551)
- [ ] `R_AARCH64_TLSLE_LDST8/16/32/64/128_TPREL_LO12{_NC}` (552–561)
- [ ] `R_AARCH64_TLSDESC_LD_PREL19` (560), `R_AARCH64_TLSDESC_ADR_PREL21` (561), `R_AARCH64_TLSDESC_ADR_PAGE21` (562), `R_AARCH64_TLSDESC_LD64_LO12` (563), `R_AARCH64_TLSDESC_ADD_LO12` (564), `R_AARCH64_TLSDESC_OFF_G1` (565), `R_AARCH64_TLSDESC_OFF_G0_NC` (566), `R_AARCH64_TLSDESC_LDR` (567), `R_AARCH64_TLSDESC_ADD` (568), `R_AARCH64_TLSDESC_CALL` (569), `R_AARCH64_TLSDESC` (1031)
- [ ] Dynamic relocations: `R_AARCH64_COPY` (1024), `R_AARCH64_GLOB_DAT` (1025), `R_AARCH64_JUMP_SLOT` (1026), `R_AARCH64_RELATIVE` (1027), `R_AARCH64_TLS_DTPMOD64` (1028), `R_AARCH64_TLS_DTPREL64` (1029), `R_AARCH64_TLS_TPREL64` (1030), `R_AARCH64_IRELATIVE` (1032)

### 1f. ARM Section Types and Flags
- [ ] `SHT_ARM_EXIDX` (0x70000001) — exception index table.
- [ ] `SHT_ARM_PREEMPTMAP` (0x70000002).
- [ ] `SHT_ARM_ATTRIBUTES` (0x70000003) — build attributes.
- [ ] `SHF_ARM_PURECODE` (0x20000000) — execute-only section.
- [ ] `PT_ARM_EXIDX` (0x70000001) — exception unwind segment.

### 1g. AArch64 Section Types
- [ ] `SHT_AARCH64_ATTRIBUTES` (0x70000003).
- [ ] `PT_AARCH64_MEMTAG_MTE` (0x70000002).

### 1h. ARM Special Section Names
- [ ] `.ARM.exidx` — exception index table.
- [ ] `.ARM.extab` — exception table data.
- [ ] `.ARM.attributes` — build attributes.
- [ ] `.note.gnu.property` — BTI/PAC properties (AArch64).

---

## 2. ARMv7 Relocation Backend (`elf_reloc.c`)

### 2a. `arm_reloc_size()` — Size of Each Relocation Result
- [ ] `R_ARM_NONE` → 0
- [ ] `R_ARM_ABS32`, `R_ARM_REL32`, `R_ARM_GOTOFF32`, `R_ARM_GOTPC`, `R_ARM_GOT_BREL`, `R_ARM_PLT32`, `R_ARM_CALL`, `R_ARM_JUMP24`, `R_ARM_TARGET1`, `R_ARM_TARGET2`, `R_ARM_PREL31`, `R_ARM_MOVW_ABS_NC`, `R_ARM_MOVT_ABS`, `R_ARM_MOVW_PREL_NC`, `R_ARM_MOVT_PREL`, `R_ARM_ABS32_NOI`, `R_ARM_REL32_NOI` → 4
- [ ] `R_ARM_PC24` → 4 (24-bit field in 32-bit instruction)
- [ ] `R_ARM_ABS16` → 2
- [ ] `R_ARM_ABS12` → 4 (12-bit field in 32-bit instruction)
- [ ] `R_ARM_ABS8` → 1
- [ ] `R_ARM_THM_CALL`, `R_ARM_THM_JUMP24` → 4 (two 16-bit Thumb instructions)
- [ ] `R_ARM_THM_JUMP11` → 2, `R_ARM_THM_JUMP8` → 2
- [ ] `R_ARM_THM_MOVW_ABS_NC`, `R_ARM_THM_MOVT_ABS`, `R_ARM_THM_MOVW_PREL_NC`, `R_ARM_THM_MOVT_PREL` → 4
- [ ] `R_ARM_THM_JUMP19` → 4, `R_ARM_THM_JUMP6` → 2
- [ ] All TLS relocations (`R_ARM_TLS_GD32` through `R_ARM_TLS_LE12`) → 4
- [ ] `R_ARM_COPY`, `R_ARM_GLOB_DAT`, `R_ARM_JUMP_SLOT`, `R_ARM_RELATIVE` → 4
- [ ] `R_ARM_IRELATIVE` → 4
- [ ] `R_ARM_V4BX` → 4 (instruction rewrite)
- [ ] All group relocations (`R_ARM_ALU_PC_G*`, `R_ARM_LDR_PC_G*`, `R_ARM_LDRS_PC_G*`, `R_ARM_LDC_PC_G*`, SB variants) → 4

### 2b. `arm_is_pc_relative()` — Identify PC-Relative Relocations
- [ ] PC-relative: `R_ARM_PC24`, `R_ARM_REL32`, `R_ARM_PLT32`, `R_ARM_CALL`, `R_ARM_JUMP24`, `R_ARM_THM_CALL`, `R_ARM_THM_JUMP24`, `R_ARM_THM_JUMP19`, `R_ARM_THM_JUMP11`, `R_ARM_THM_JUMP8`, `R_ARM_PREL31`, `R_ARM_MOVW_PREL_NC`, `R_ARM_MOVT_PREL`, `R_ARM_THM_MOVW_PREL_NC`, `R_ARM_THM_MOVT_PREL`, `R_ARM_GOTPC`, `R_ARM_BASE_PREL`, `R_ARM_GOT_PREL`, `R_ARM_REL32_NOI`, all `ALU_PC_G*`/`LDR_PC_G*`/`LDRS_PC_G*`/`LDC_PC_G*`
- [ ] Absolute: `R_ARM_ABS32`, `R_ARM_ABS16`, `R_ARM_ABS12`, `R_ARM_ABS8`, `R_ARM_MOVW_ABS_NC`, `R_ARM_MOVT_ABS`, `R_ARM_THM_MOVW_ABS_NC`, `R_ARM_THM_MOVT_ABS`, `R_ARM_ABS32_NOI`, `R_ARM_GOTOFF32`, `R_ARM_GOT_BREL`

### 2c. `arm_is_tls()` — Identify TLS Relocations
- [ ] TLS: `R_ARM_TLS_DTPMOD32`, `R_ARM_TLS_DTPOFF32`, `R_ARM_TLS_TPOFF32`, `R_ARM_TLS_GD32`, `R_ARM_TLS_LDM32`, `R_ARM_TLS_LDO32`, `R_ARM_TLS_IE32`, `R_ARM_TLS_LE32`, `R_ARM_TLS_LDO12`, `R_ARM_TLS_LE12`, `R_ARM_TLS_IE12GP`, `R_ARM_TLS_DESC`, `R_ARM_TLS_GOTDESC`, `R_ARM_TLS_CALL`, `R_ARM_TLS_DESCSEQ`, `R_ARM_THM_TLS_CALL`

### 2d. `arm_apply()` — Relocation Application
- [ ] `R_ARM_NONE` → no-op
- [ ] `R_ARM_ABS32` → S + A
- [ ] `R_ARM_REL32` → S + A − P (signed 32-bit)
- [ ] `R_ARM_PC24` → extract bits[23:0], compute ((S + A) | T) − P, check ±32MB range, reinsert with instruction mask
- [ ] `R_ARM_CALL` → ((S + A) | T) − P, range ±32MB, encode in bits[23:0]
- [ ] `R_ARM_JUMP24` → ((S + A) | T) − P, range ±32MB, encode in bits[23:0]
- [ ] `R_ARM_PLT32` → ((S + A) | T) − P
- [ ] `R_ARM_GOTOFF32` → ((S + A) | T) − GOT_ORG
- [ ] `R_ARM_GOTPC` / `R_ARM_BASE_PREL` → GOT_ORG + A − P
- [ ] `R_ARM_GOT_BREL` → GOT(S) + A − GOT_ORG
- [ ] `R_ARM_PREL31` → (S + A − P) masked to 31 bits, preserve bit[31]
- [ ] `R_ARM_MOVW_ABS_NC` → extract imm16 from instruction (bits[19:16]+bits[11:0]), compute (S + A) & 0xFFFF, reinsert; no overflow check
- [ ] `R_ARM_MOVT_ABS` → ((S + A) >> 16) & 0xFFFF into imm16 field
- [ ] `R_ARM_MOVW_PREL_NC` → ((S + A) | T) − P, low 16 bits
- [ ] `R_ARM_MOVT_PREL` → (((S + A) | T) − P) >> 16, high 16 bits
- [ ] `R_ARM_THM_CALL` → decode Thumb BL/BLX pair, compute ((S + A) | T) − P, range ±16MB (BL) / ±16MB even (BLX), re-encode J1/J2/imm10/imm11
- [ ] `R_ARM_THM_JUMP24` → like THM_CALL but unconditional
- [ ] `R_ARM_THM_JUMP19` → Thumb B.cond, 20-bit signed offset
- [ ] `R_ARM_THM_JUMP11` → Thumb B, 11-bit unsigned offset
- [ ] `R_ARM_THM_JUMP8` → Thumb B.cond, 8-bit signed offset
- [ ] `R_ARM_THM_MOVW_ABS_NC` → extract Thumb MOVW imm16 (imm4:i:imm3:imm8), compute, reinsert
- [ ] `R_ARM_THM_MOVT_ABS` → extract Thumb MOVT imm16, compute, reinsert
- [ ] `R_ARM_THM_MOVW_PREL_NC` → PC-relative low 16 bits into Thumb MOVW
- [ ] `R_ARM_THM_MOVT_PREL` → PC-relative high 16 bits into Thumb MOVT
- [ ] `R_ARM_V4BX` → rewrite `BX Rm` to `MOV PC, Rm` for ARMv4 compat
- [ ] `R_ARM_TARGET1` → platform-defined, typically `R_ARM_ABS32` or `R_ARM_REL32`
- [ ] `R_ARM_TARGET2` → platform-defined, typically `R_ARM_GOT_PREL`
- [ ] `R_ARM_ABS16` → S + A, check ±32K range or 0–64K unsigned
- [ ] `R_ARM_ABS12` → (S + A) encoded in LDR immediate field, 12-bit unsigned
- [ ] `R_ARM_ABS8` → S + A, check 0–255
- [ ] `R_ARM_SBREL32` → S + A − B(S) (static base relative)
- [ ] Group relocations (`ALU_PC/SB`, `LDR_PC/SB`, `LDRS_PC/SB`, `LDC_PC/SB` with G0/G1/G2): extract instruction-format-specific field width, apply group masking per ARM EABI §4.6.1.4
- [ ] All TLS relocations: S + A (raw value passthrough for linker to fixup GOT/TP offsets)
- [ ] Dynamic: `R_ARM_COPY`, `R_ARM_GLOB_DAT`, `R_ARM_JUMP_SLOT`, `R_ARM_RELATIVE`, `R_ARM_IRELATIVE` — produce raw S + A or B(S) + A

### 2e. ARM Thumb Interwork Bit (T)
- [ ] Detect if target symbol is Thumb (`STT_FUNC` with `st_value` bit 0 set or `$t` mapping symbol).
- [ ] Set T=1 for Thumb targets in branch relocations.

### 2f. ARM Relocation Backend Registration
- [ ] Register `arm_apply`, `arm_reloc_size`, `arm_is_pc_relative` under `EM_ARM` in `register_builtin_backends_locked()`.
- [ ] Add `arm_is_tls` to `elf_reloc_is_tls_for_machine()`.

---

## 3. AArch64 Relocation Backend (`elf_reloc.c`)

### 3a. `aarch64_reloc_size()`
- [ ] `R_AARCH64_NONE` → 0
- [ ] `R_AARCH64_ABS64`, `R_AARCH64_PREL64` → 8
- [ ] `R_AARCH64_ABS32`, `R_AARCH64_PREL32` → 4
- [ ] `R_AARCH64_ABS16`, `R_AARCH64_PREL16` → 2
- [ ] All instruction-embedded relocations (`ADR_PREL_*`, `ADD_ABS_*`, `LDST*`, `MOVW_*`, `JUMP26`, `CALL26`, `CONDBR19`, `TSTBR14`) → 4 (instruction width)
- [ ] All GOT/TLS instruction-embedded → 4
- [ ] Dynamic (`COPY`, `GLOB_DAT`, `JUMP_SLOT`, `RELATIVE`, `IRELATIVE`, `TLS_DTPMOD64`, `TLS_DTPREL64`, `TLS_TPREL64`, `TLSDESC`) → 8

### 3b. `aarch64_is_pc_relative()`
- [ ] PC-relative: `R_AARCH64_PREL64`, `R_AARCH64_PREL32`, `R_AARCH64_PREL16`, `R_AARCH64_ADR_PREL_LO21`, `R_AARCH64_ADR_PREL_PG_HI21{_NC}`, `R_AARCH64_JUMP26`, `R_AARCH64_CALL26`, `R_AARCH64_CONDBR19`, `R_AARCH64_TSTBR14`, `R_AARCH64_LD_PREL_LO19`, `R_AARCH64_GOT_LD_PREL19`, `R_AARCH64_ADR_GOT_PAGE`, `R_AARCH64_MOVW_PREL_G*`, all `TLSGD_ADR_PREL21`, `TLSLD_ADR_PREL21`, `TLSIE_LD_GOTTPREL_PREL19`, `TLSDESC_LD_PREL19`, `TLSDESC_ADR_PREL21`
- [ ] Absolute: `R_AARCH64_ABS64/32/16`, `R_AARCH64_ADD_ABS_LO12_NC`, `R_AARCH64_LDST*_ABS_LO12_NC`, `R_AARCH64_MOVW_UABS_G*`, `R_AARCH64_MOVW_SABS_G*`

### 3c. `aarch64_is_tls()`
- [ ] All `TLSGD_*`, `TLSLD_*`, `TLSIE_*`, `TLSLE_*`, `TLSDESC_*`, `TLS_DTPMOD64`, `TLS_DTPREL64`, `TLS_TPREL64`

### 3d. `aarch64_apply()` — Relocation Application
- [ ] `R_AARCH64_ABS64` → S + A (64-bit)
- [ ] `R_AARCH64_ABS32` → S + A, check unsigned 32-bit or signed 32-bit
- [ ] `R_AARCH64_ABS16` → S + A, check ±32K
- [ ] `R_AARCH64_PREL64` → S + A − P
- [ ] `R_AARCH64_PREL32` → S + A − P, check signed 32-bit
- [ ] `R_AARCH64_PREL16` → S + A − P, check signed 16-bit
- [ ] `R_AARCH64_ADR_PREL_LO21` → extract ADR immediate (imm:immlo), compute S + A − P, check ±1MB, re-encode bits[23:5]+bits[30:29]
- [ ] `R_AARCH64_ADR_PREL_PG_HI21` → Page(S + A) − Page(P), check ±4GB, encode as ADRP immediate (immhi:immlo)
- [ ] `R_AARCH64_ADR_PREL_PG_HI21_NC` → same without overflow check
- [ ] `R_AARCH64_ADD_ABS_LO12_NC` → (S + A) & 0xFFF, encode in ADD imm12 field (bits[21:10])
- [ ] `R_AARCH64_LDST8_ABS_LO12_NC` → (S + A) & 0xFFF, encode in LDR/STR imm12 (no shift)
- [ ] `R_AARCH64_LDST16_ABS_LO12_NC` → ((S + A) & 0xFFF) >> 1, check alignment
- [ ] `R_AARCH64_LDST32_ABS_LO12_NC` → ((S + A) & 0xFFF) >> 2, check alignment
- [ ] `R_AARCH64_LDST64_ABS_LO12_NC` → ((S + A) & 0xFFF) >> 3, check alignment
- [ ] `R_AARCH64_LDST128_ABS_LO12_NC` → ((S + A) & 0xFFF) >> 4, check alignment
- [ ] `R_AARCH64_MOVW_UABS_G0` → (S + A) & 0xFFFF, encode in MOVZ/MOVK imm16 (bits[20:5])
- [ ] `R_AARCH64_MOVW_UABS_G0_NC` → same, no overflow check
- [ ] `R_AARCH64_MOVW_UABS_G1` → ((S + A) >> 16) & 0xFFFF; G2 → >>32; G3 → >>48
- [ ] `R_AARCH64_MOVW_SABS_G0` → signed version, may flip MOVZ↔MOVN
- [ ] `R_AARCH64_JUMP26` → (S + A − P) >> 2, check ±128MB, encode in bits[25:0]
- [ ] `R_AARCH64_CALL26` → same as JUMP26
- [ ] `R_AARCH64_CONDBR19` → (S + A − P) >> 2, check ±1MB, encode in bits[23:5]
- [ ] `R_AARCH64_TSTBR14` → (S + A − P) >> 2, check ±32KB, encode in bits[18:5]
- [ ] `R_AARCH64_LD_PREL_LO19` → (S + A − P) >> 2, check ±1MB, encode in bits[23:5]
- [ ] GOT relocations: compute GOT slot address, output GOT(S) + A or Page(GOT(S)) − Page(P)
- [ ] TLS relocations: S + A passthrough (linker resolves GOT/TP offsets)
- [ ] Dynamic: `R_AARCH64_COPY/GLOB_DAT/JUMP_SLOT/RELATIVE/IRELATIVE` → S + A or B(S) + A

### 3e. AArch64 Instruction Field Helpers
- [ ] `aarch64_extract_imm26(uint32_t insn)` — bits[25:0] for B/BL
- [ ] `aarch64_insert_imm26(uint32_t insn, int32_t value)` — encode into bits[25:0]
- [ ] `aarch64_extract_imm19(uint32_t insn)` — bits[23:5] for B.cond/CBZ/LDR literal
- [ ] `aarch64_insert_imm19(uint32_t insn, int32_t value)`
- [ ] `aarch64_extract_imm14(uint32_t insn)` — bits[18:5] for TBZ
- [ ] `aarch64_insert_imm14(uint32_t insn, int32_t value)`
- [ ] `aarch64_extract_adr_imm(uint32_t insn)` — immhi(bits[23:5]):immlo(bits[30:29]) for ADR/ADRP
- [ ] `aarch64_insert_adr_imm(uint32_t insn, int32_t value)`
- [ ] `aarch64_extract_imm12(uint32_t insn)` — bits[21:10] for ADD/LDR
- [ ] `aarch64_insert_imm12(uint32_t insn, uint32_t value)`
- [ ] `aarch64_extract_movw_imm16(uint32_t insn)` — bits[20:5]
- [ ] `aarch64_insert_movw_imm16(uint32_t insn, uint16_t value)`
- [ ] `aarch64_page(uint64_t addr)` → addr & ~0xFFF

### 3f. AArch64 Relocation Backend Registration
- [ ] Register `aarch64_apply`, `aarch64_reloc_size`, `aarch64_is_pc_relative` under `EM_AARCH64`.
- [ ] Add `aarch64_is_tls` to `elf_reloc_is_tls_for_machine()`.

---

## 4. Validation (`elf_validate.c`)

### 4a. ARM Validation Rules
- [ ] Accept `EM_ARM` with `ELFCLASS32` only.
- [ ] Accept both `ELFDATA2LSB` (little-endian, common) and `ELFDATA2MSB` (big-endian).
- [ ] Validate `e_flags` EABI version (≥ `EF_ARM_ABI_VER5` for modern toolchains).
- [ ] Validate float ABI flags consistency (`HARD`/`SOFT` not both set).
- [ ] Validate `SHT_ARM_EXIDX` sections have `SHF_LINK_ORDER` flag.
- [ ] Validate `.ARM.attributes` section if present.
- [ ] Validate alignment constraints for ARM instructions (4 for ARM, 2 for Thumb).
- [ ] Validate `PT_ARM_EXIDX` segment if present points to `SHT_ARM_EXIDX` section.
- [ ] Validate mapping symbols (`$a`, `$t`, `$d`) are present in code sections.

### 4b. AArch64 Validation Rules
- [ ] Accept `EM_AARCH64` with `ELFCLASS64` only.
- [ ] Accept both `ELFDATA2LSB` and `ELFDATA2MSB`.
- [ ] Validate `e_flags` is 0 or recognized optional flags only.
- [ ] Validate instruction alignment: all code sections 4-byte aligned.
- [ ] Validate `.note.gnu.property` for BTI (`GNU_PROPERTY_AARCH64_FEATURE_1_BTI`) and PAC (`GNU_PROPERTY_AARCH64_FEATURE_1_PAC`) if present.
- [ ] Validate ADRP+ADD/LDR pairs have consistent page references.
- [ ] Check for unrecognized relocation types.
- [ ] Validate mapping symbols (`$x`, `$d`) in code sections.

---

## 5. ELF Read (`elf_read.c`)

- [ ] Recognize `EM_ARM` and `EM_AARCH64` as valid machine types.
- [ ] ARM: parse REL relocations (no addend in struct; addend encoded in instruction).
- [ ] AArch64: parse RELA relocations (explicit addend).
- [ ] Parse `SHT_ARM_EXIDX` sections with proper `sh_link` interpretation.
- [ ] Parse `SHT_ARM_ATTRIBUTES` / `SHT_AARCH64_ATTRIBUTES` build attribute sections.
- [ ] Handle ARM `SHF_ARM_PURECODE` flag in section flags.
- [ ] Read `PT_ARM_EXIDX` and `PT_AARCH64_MEMTAG_MTE` segment types.
- [ ] Endian-aware reads: ARM objects can be big-endian (BE32 or BE8).

---

## 6. ELF Write (`elf_write.c`)

- [ ] Write ELF32 (`EM_ARM`) with REL relocations.
- [ ] Write ELF64 (`EM_AARCH64`) with RELA relocations.
- [ ] Write correct `e_flags` for ARM: EABI version, float ABI, interwork.
- [ ] Write `SHT_ARM_EXIDX` and `SHT_ARM_ATTRIBUTES` sections.
- [ ] Write `PT_ARM_EXIDX` segment.
- [ ] Write `.note.gnu.property` with BTI/PAC flags for AArch64.
- [ ] Endian-correct output for big-endian ARM.
- [ ] Correct `e_entry` with Thumb bit for ARM entry points.

---

## 7. ELF Creation (`elf_util.c`)

- [ ] `elf_init_arm()` convenience: set up ELF32/EM_ARM/ELFDATA2LSB with EABI v5 flags, default `.text`/`.data`/`.bss` sections.
- [ ] `elf_init_aarch64()` convenience: set up ELF64/EM_AARCH64/ELFDATA2LSB with empty flags.
- [ ] `elf_set_flags()` / `elf_flags()` for manipulating `e_flags`.
- [ ] `elf_add_arm_exidx()` helper for creating `.ARM.exidx` + `.ARM.extab` section pairs.
- [ ] `elf_add_arm_attributes()` helper for adding build attributes.
- [ ] `elf_add_gnu_property_aarch64()` helper for BTI/PAC feature bits.

---

## 8. DWARF Support (`elf_dwarf.c`)

- [ ] ARM DWARF register mapping: R0–R15 → 0–15, VFP D0–D31 → 256–287.
- [ ] AArch64 DWARF register mapping: X0–X30 → 0–30, SP → 31, V0–V31 → 64–95.
- [ ] ARM CFA rules: typical frame pointer is R11 (FP) or R13 (SP).
- [ ] AArch64 CFA rules: frame pointer is X29, link register is X30.
- [ ] Handle `.debug_frame` vs `.eh_frame` CIE augmentation differences per arch.
- [ ] Parse ARM-specific DWARF extensions (if any vendor extensions present).

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

## 12. Documentation

- [ ] Update `README.md` with ARM/AArch64 support notes.
- [ ] Document ARM/AArch64 relocation backend registration.
- [ ] Document ARM build attributes API.
- [ ] Update `COMPATIBILITY_MATRIX.md` with ARM/AArch64 entries.
- [ ] Man page updates for `elfobj.3` with ARM-specific API functions.
