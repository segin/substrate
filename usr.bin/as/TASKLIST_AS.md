# `usr.bin/as` — Standalone Assembler Tasklist

Goal: replace the current GCC/GAS wrapper with a native multi-architecture assembler producing ELF object files directly via `libelfobj`. Target architectures: i386, x86-64 (v1–v4), ARMv7, AArch64 (ARMv8.0–8.1).

---

## 1. Assembler Core Architecture

### 1a. Lexer
- [x] Tokenize mnemonics, registers, immediates (decimal/hex/octal/binary), labels, directives, strings.
- [x] AT&T syntax (default) and Intel syntax (`-msyntax=intel`).
- [x] Line comments (`#`, `//`, `;`), block comments (`/* */`).
- [x] `.include` file inclusion with search path (`-I`).
- [x] Preprocessor integration: `.if`/`.ifdef`/`.ifndef`/`.else`/`.endif`, `.macro`/`.endm`, `.rept`/`.endr`, `.irp`/`.irpc`.
- [x] String escapes in `.ascii`/`.asciz`/`.string` directives.

### 1b. Parser
- [x] Instruction parsing: mnemonic + operand list.
- [x] Operand types: register, immediate, memory (base+index*scale+disp), label reference.
- [x] Expression parser: `+`, `-`, `*`, `/`, `%`, `|`, `&`, `^`, `~`, `<<`, `>>`, unary `-`.
- [x] Symbol references in expressions (forward/backward).
- [x] Local labels (`0:`–`9:`, `0b`/`0f` references).
- [x] Instruction prefixes: `lock`, `rep`/`repe`/`repne`, segment overrides, `rex` prefixes.
- [x] ARM: condition codes, shift operands, register lists, coprocessor operands.

### 1c. Symbol Table
- [x] Local, global, weak, common symbol types.
- [x] `.globl`/`.global`, `.local`, `.weak`, `.comm`, `.lcomm`.
- [x] `.type` (function/object/tls_object/common/notype), `.size`.
- [x] `.hidden`, `.protected`, `.internal` visibility.
- [x] `.symver` symbol versioning.
- [x] Forward reference resolution (two-pass or relaxation).

### 1d. Section Management
- [x] `.text`, `.data`, `.bss`, `.rodata` built-in sections.
- [x] `.section name, "flags", @type` with SHF_*/SHT_* mapping.
- [x] `.pushsection`/`.popsection`, `.previous`.
- [x] `.subsection` ordering.
- [x] `.group` / COMDAT support.
- [x] Section alignment via `.balign`/`.p2align`/`.align`.

### 1e. Data Directives
- [x] `.byte`, `.short`/`.hword`, `.long`/`.int`, `.quad`/`.8byte`.
- [x] `.float`, `.double`.
- [x] `.ascii`, `.asciz`/`.string`.
- [x] `.zero`/`.space`, `.fill`.
- [x] `.skip`, `.org`.
- [x] `.incbin` for binary inclusion.

### 1f. ELF Output (via `libelfobj`)
- [x] Generate ET_REL ELF32 (i386, ARMv7) and ELF64 (x86-64, AArch64).
- [x] Emit section headers, symbol table (`.symtab`/`.strtab`), section name table (`.shstrtab`).
- [x] Emit relocations: REL (i386, ARM) and RELA (x86-64, AArch64).
- [x] `.note.gnu.property` for x86-64-v2/v3/v4 ISA level marking.
- [x] `.note.GNU-stack` for executable stack control.
- [x] Debug sections passthrough (`.debug_*`, `.eh_frame`).
- [x] DWARF `.debug_line` generation from `.loc`/`.file` directives.
- [x] `.eh_frame` / `.eh_frame_hdr` CFI via `.cfi_*` directives.

### 1g. Relaxation Engine
- [x] Branch relaxation: short→near→far jump promotion.
- [x] x86: `jmp rel8` → `jmp rel32` relaxation.
- [x] ARM: branch offset range checking; Thumb→ARM interwork veneer insertion.
- [x] Iterative relaxation until stable.

### 1h. CLI Interface
- [x] `-o output.o`, `-c` (ignored compat), `-g` (debug info).
- [x] `-march=ARCH`, `-mtune=TUNE` (validation per arch).
- [x] `-32`/`-64` / `--32`/`--64` mode selection.
- [x] `-msyntax=att`/`-msyntax=intel`.
- [x] `-W`/`--warn`, `--fatal-warnings`, `--no-warn`.
- [x] `-I dir` include path, `-D sym=val` defines.
- [x] `-al` listing output.
- [x] `--defsym sym=val`.
- [x] `--statistics` timing/memory report.
- [x] `--target-help` per-arch instruction summary.

---

## 2. x86 Encoding Engine (i386 + x86-64)

### 2a. Legacy Encoding (i386 baseline)
- [x] ModR/M + SIB + displacement encoding.
- [x] All addressing modes: `[base]`, `[base+disp]`, `[base+idx*s+disp]`, `[disp32]`.
- [x] Operand size prefix (`0x66`), address size prefix (`0x67`).
- [x] Segment override prefixes (`CS`/`DS`/`ES`/`FS`/`GS`/`SS`).
- [x] All i386 base instructions: data movement, arithmetic, logic, shifts, string ops, control flow, stack, flags, I/O, misc.

### 2b. x86-64 Extensions
- [x] REX prefix encoding (REX.W/R/X/B).
- [x] RIP-relative addressing (default for x86-64).
- [x] 64-bit register operands (R8–R15, RAX–RSP).
- [x] `SYSCALL`/`SYSRET`, `SWAPGS`, `CMPXCHG16B`.

### 2c. VEX Prefix Encoding (AVX/AVX2/BMI/FMA/F16C)
- [x] 2-byte VEX (`C5`), 3-byte VEX (`C4`) selection.
- [x] VEX.L (128 vs 256), VEX.vvvv (3rd operand), VEX.W, VEX.pp.
- [x] Non-destructive 3-operand form for all VEX instructions.

### 2d. EVEX Prefix Encoding (AVX-512)
- [x] 4-byte EVEX prefix: P0 (R/X/B/R'/mm), P1 (W/vvvv/pp), P2 (z/L'L/b/V'/aaa).
- [x] Opmask register selection (`{k1}`–`{k7}`).
- [x] Zeroing-masking (`{z}`).
- [x] Embedded broadcast (`{1to2}`, `{1to4}`, `{1to8}`, `{1to16}`).
- [x] Static rounding mode (`{rn-sae}`, `{rd-sae}`, `{ru-sae}`, `{rz-sae}`).
- [x] Suppress-all-exceptions (`{sae}`).

### 2e. x86 Relocation Emission
- [x] `R_386_32`, `R_386_PC32`, `R_386_GOT32`, `R_386_PLT32`, `R_386_GOTOFF`, `R_386_GOTPC`.
- [x] `R_386_TLS_GD`, `R_386_TLS_LDM`, `R_386_TLS_IE`, `R_386_TLS_LE`.
- [x] `R_X86_64_64`, `R_X86_64_PC32`, `R_X86_64_32`, `R_X86_64_32S`.
- [x] `R_X86_64_GOT32`, `R_X86_64_PLT32`, `R_X86_64_GOTPCREL`, `R_X86_64_GOTPCRELX`.
- [x] `R_X86_64_REX_GOTPCRELX`.
- [x] `R_X86_64_TLSGD`, `R_X86_64_TLSLD`, `R_X86_64_GOTTPOFF`, `R_X86_64_TPOFF32`.

---

## 3. x86-64-v2 Instructions

> CMPXCHG16B, LAHF/SAHF (long mode), POPCNT, SSE3, SSSE3, SSE4.1, SSE4.2.

### 3a. CMPXCHG16B / LAHF / SAHF / POPCNT
- [x] `CMPXCHG16B m128` — `0F C7 /1` + REX.W
- [x] `LAHF` — `9F`, `SAHF` — `9E`
- [x] `POPCNT r16/r32/r64, r/m16/32/64` — `F3 0F B8 /r`

### 3b. SSE3
- [x] `ADDSUBPD`, `ADDSUBPS`, `HADDPD`, `HADDPS`, `HSUBPD`, `HSUBPS`
- [x] `LDDQU`, `MOVDDUP`, `MOVSHDUP`, `MOVSLDUP`
- [x] `FISTTP` (m16/m32/m64), `MONITOR`, `MWAIT`

### 3c. SSSE3
- [ ] `PABSB/W/D`, `PALIGNR`, `PHADDW/D/SW`, `PHSUBW/D/SW`
- [ ] `PMADDUBSW`, `PMULHRSW`, `PSHUFB`, `PSIGNB/W/D`

### 3d. SSE4.1
- [ ] `BLENDPD/PS`, `BLENDVPD/VPS`, `DPPD/DPPS`
- [ ] `EXTRACTPS`, `INSERTPS`, `MOVNTDQA`, `MPSADBW`, `PACKUSDW`
- [ ] `PBLENDVB`, `PBLENDW`, `PCMPEQQ`
- [ ] `PEXTRB/D/Q/W` (new forms), `PINSRB/D/Q`
- [ ] `PMAXSB/SD/UD/UW`, `PMINSB/SD/UD/UW`
- [ ] `PMOVSXBW/BD/BQ/WD/WQ/DQ`, `PMOVZXBW/BD/BQ/WD/WQ/DQ`
- [ ] `PMULDQ`, `PMULLD`, `PTEST`
- [ ] `ROUNDPD/PS/SD/SS`
- [ ] `PHMINPOSUW`

### 3e. SSE4.2
- [ ] `CRC32` (r32/r64 × r/m8/16/32/64)
- [ ] `PCMPESTRI/M`, `PCMPISTRI/M`, `PCMPGTQ`

---

## 4. x86-64-v3 Instructions

> AVX, AVX2, BMI1, BMI2, F16C, FMA, LZCNT, MOVBE, XSAVE.

### 4a. AVX (VEX-encoded SSE promotion + new)
- [ ] All SSE/SSE2/SSE3/SSSE3/SSE4 → VEX 128-bit + 256-bit forms
- [ ] `VBROADCASTSS/SD`, `VBROADCASTF128`, `VEXTRACTF128`, `VINSERTF128`
- [ ] `VMASKMOVPS/PD` (load/store), `VPERM2F128`, `VPERMILPS/PD`
- [ ] `VTESTPS/PD`, `VZEROALL`, `VZEROUPPER`

### 4b. AVX2 (256-bit integer)
- [ ] All SSE2 integer → 256-bit VEX forms
- [ ] `VBROADCASTI128`, `VEXTRACTI128`, `VINSERTI128`, `VPBLENDD`
- [ ] `VPBROADCASTB/W/D/Q`, `VPERMD/PS/PD/Q`, `VPERM2I128`
- [ ] `VPMASKMOVD/Q`, `VPSLLVD/Q`, `VPSRLVD/Q`, `VPSRAVD`
- [ ] `VGATHERDPS/DPD/QPS/QPD`, `VPGATHERDD/DQ/QD/QQ` (VSIB)

### 4c. BMI1
- [ ] `ANDN`, `BEXTR`, `BLSI`, `BLSMSK`, `BLSR`, `TZCNT` (r32/r64)

### 4d. BMI2
- [ ] `BZHI`, `MULX`, `PDEP`, `PEXT`, `RORX`, `SARX`, `SHLX`, `SHRX` (r32/r64)

### 4e. F16C
- [ ] `VCVTPH2PS`, `VCVTPS2PH`

### 4f. FMA (132/213/231 × PS/PD/SS/SD)
- [ ] `VFMADD`, `VFMSUB`, `VFNMADD`, `VFNMSUB` (all 12 forms each)
- [ ] `VFMADDSUB`, `VFMSUBADD` (all 6 forms each)

### 4g. LZCNT / MOVBE / XSAVE
- [ ] `LZCNT r16/32/64, r/m16/32/64`
- [ ] `MOVBE r16/32/64, m` and `MOVBE m, r16/32/64`
- [ ] `XSAVE/XRSTOR/XGETBV/XSETBV/XSAVEOPT/XSAVEC/XSAVES` + 64-bit forms

---

## 5. x86-64-v4 Instructions

> AVX-512F, AVX-512BW, AVX-512CD, AVX-512DQ, AVX-512VL.

### 5a. AVX-512F (Foundation)
- [ ] 512-bit arithmetic: `VADDPS/PD`, `VSUB`, `VMUL`, `VDIV`, `VMAX`, `VMIN`, `VSQRT` (zmm)
- [ ] `VRSQRT14`, `VRCP14`, FMA 512-bit forms
- [ ] Conversions: `VCVT{PS,PD,DQ,UDQ,QQ,UQQ}2{PS,PD,DQ,UDQ}` (all signed/unsigned)
- [ ] Broadcasts: `VBROADCASTSS/SD/F32X4/F64X4` (zmm)
- [ ] Insert/Extract: `VINSERTF32X4/F64X4`, `VEXTRACTF32X4/F64X4`
- [ ] Permutes: `VPERMPS/PD/D/Q`, `VPERMI2{PS,PD,D,Q,W,B}`, `VPERMT2{PS,PD,D,Q,W,B}`
- [ ] Shuffles: `VSHUFF32X4/F64X2`, `VSHUFI32X4/I64X2`
- [ ] Compress/Expand: `VCOMPRESSPS/PD`, `VPCOMPRESSD/Q`, `VEXPANDPS/PD`, `VPEXPANDD/Q`
- [ ] FP special: `VGETEXPPS/PD/SS/SD`, `VGETMANTPS/PD/SS/SD`, `VSCALEFPS/PD/SS/SD`, `VFIXUPIMMPS/PD/SS/SD`, `VRNDSCALEPS/PD/SS/SD`
- [ ] Compares-to-mask: `VCMPPS/PD`, `VPCMPD/Q/UD/UQ`
- [ ] Blend/Logic: `VPBLENDMD/MQ`, `VPTERNLOGD/Q`
- [ ] Down-converts: `VPMOVDB/DW/QB/QD/QW` (truncate/signed-sat/unsigned-sat)
- [ ] Masked moves: `VMOVDQA32/64`, `VMOVDQU8/16/32/64`
- [ ] Gather/Scatter: `VGATHER{D,Q}{PS,PD}`, `VPGATHER{DD,DQ,QD,QQ}`, `VSCATTER{D,Q}{PS,PD}`, `VPSCATTER{DD,DQ,QD,QQ}` (zmm)
- [ ] **Opmask:** `KMOV{B,W,D,Q}`, `KAND/OR/XOR/XNOR/NOT/ANDN{B,W,D,Q}`, `KORTEST/KTEST{B,W,D,Q}`, `KSHIFTL/R{B,W,D,Q}`, `KUNPCK{BW,WD,DQ}`, `KADD{W,D,Q}`

### 5b. AVX-512BW (Byte/Word)
- [ ] 512-bit byte/word arithmetic, pack, unpack, shuffle, shift, compare-to-mask
- [ ] `VPSLLVW`, `VPSRLVW`, `VPSRAVW`, `VDBPSADBW`
- [ ] `VPCMPB/W/UB/UW → k`, `VPMOVB2M/W2M`, `VPMOVM2B/W`
- [ ] `VPERMW`, `VPERMI2W`, `VPERMT2W`, `VPBLENDMB/MW`
- [ ] `VPTESTNMB/MW`

### 5c. AVX-512CD (Conflict Detection)
- [ ] `VPCONFLICTD/Q`, `VPLZCNTD/Q`, `VPBROADCASTMB2Q`, `VPBROADCASTMW2D`

### 5d. AVX-512DQ (Doubleword/Quadword)
- [ ] QQ conversions: `VCVT{PS,PD}2{QQ,UQQ}`, `VCVT{QQ,UQQ}2{PS,PD}` (truncating forms too)
- [ ] `VPMULLQ`, `VPMOVM2D/Q`, `VPMOVD2M/Q2M`
- [ ] Insert/Extract 64x2/32x8 forms
- [ ] `VRANGEPS/PD/SS/SD`, `VREDUCEPS/PD/SS/SD`, `VFPCLASSPS/PD/SS/SD → k`
- [ ] Bitwise FP: `VANDPS/PD`, `VORPS/PD`, `VXORPS/PD`, `VANDNPS/PD` (EVEX zmm)
- [ ] `VBROADCASTF32X2/F32X8/I32X2/I32X8`

### 5e. AVX-512VL (Vector Length)
- [ ] All 512-bit instructions also available at EVEX.128 (xmm) and EVEX.256 (ymm)
- [ ] Opmask + zeroing on 128/256-bit forms
- [ ] Embedded broadcast on 128/256-bit memory operands

---

## 6. ARMv7 (AArch32) Encoding Engine

### 6a. ARM State Encoding Infrastructure
- [ ] 32-bit ARM instruction encoding (condition field, opcode classes).
- [ ] Condition codes: EQ/NE/CS/CC/MI/PL/VS/VC/HI/LS/GE/LT/GT/LE/AL/NV.
- [ ] Addressing modes: immediate, register, scaled register, pre/post-indexed, LDM/STM modes (IA/IB/DA/DB/FD/FA/ED/EA).
- [ ] Barrel shifter operands: LSL/LSR/ASR/ROR/RRX (immediate and register).
- [ ] S-suffix (set flags), conditional execution on all instructions.
- [ ] ARM/Thumb interwork: `.arm`, `.thumb`, `.thumb_func`, BX/BLX interwork.
- [ ] IT blocks (Thumb-2): `IT{T,E}{T,E}{T,E}` (up to 4 conditional instructions).

### 6b. Thumb / Thumb-2 Encoding
- [ ] 16-bit narrow Thumb encoding for basic operations.
- [ ] 32-bit wide Thumb-2 encoding for extended operations.
- [ ] Automatic narrow/wide selection based on operand range.
- [ ] `.syntax unified` (default): single mnemonic for ARM/Thumb with `.w`/`.n` suffixes.

### 6c. Data Processing Instructions
- [ ] `ADD`, `ADC`, `SUB`, `SBC`, `RSB`, `RSC` (immediate, register, shifted register)
- [ ] `AND`, `ORR`, `EOR`, `BIC`, `ORN` (Thumb-2)
- [ ] `MOV`, `MVN` (immediate, register); `MOVW`, `MOVT` (16-bit immediate)
- [ ] `CMP`, `CMN`, `TST`, `TEQ`
- [ ] `MUL`, `MLA`, `MLS`, `UMULL`, `UMLAL`, `SMULL`, `SMLAL`
- [ ] `SMULBB/BT/TB/TT`, `SMLABB/BT/TB/TT`, `SMLALBB/BT/TB/TT`
- [ ] `SMUAD/X`, `SMLAD/X`, `SMLALD/X`, `SMUSD/X`, `SMLSD/X`, `SMLSLD/X`
- [ ] `UMAAL`, `SDIV`, `UDIV`
- [ ] `CLZ`, `RBIT`, `REV`, `REV16`, `REVSH`
- [ ] `PKH{BT,TB}`, `SSAT`, `USAT`, `SSAT16`, `USAT16`
- [ ] `SBFX`, `UBFX`, `BFI`, `BFC`
- [ ] `SXTB/H`, `UXTB/H`, `SXTAB/H`, `UXTAB/H`, `SXTB16`, `UXTB16`, `SXTAB16`, `UXTAB16`
- [ ] `QADD`, `QDADD`, `QSUB`, `QDSUB`
- [ ] `SADD8/16`, `UADD8/16`, `SSUB8/16`, `USUB8/16`, `SHADD8/16`, `UHADD8/16`, `SHSUB8/16`, `UHSUB8/16`
- [ ] `QADD8/16`, `UQADD8/16`, `QSUB8/16`, `UQSUB8/16`
- [ ] `SASX`, `UASX`, `SSAX`, `USAX`, `SHASX`, `UHASX`, `SHSAX`, `UHSAX`, `QASX`, `UQASX`, `QSAX`, `UQSAX`
- [ ] `USAD8`, `USADA8`, `SEL`

### 6d. Branch Instructions
- [ ] `B`, `BL` (immediate offset, conditional)
- [ ] `BX`, `BLX` (register, Thumb interwork)
- [ ] `CBZ`, `CBNZ` (Thumb-2, compare-and-branch)
- [ ] `TBB`, `TBH` (Thumb-2, table branch byte/halfword)

### 6e. Load/Store Instructions
- [ ] `LDR/STR` (word, byte, halfword, signed byte, signed halfword, doubleword)
- [ ] `LDRB/STRB`, `LDRH/STRH`, `LDRSB`, `LDRSH`, `LDRD/STRD`
- [ ] Pre-indexed, post-indexed, offset addressing with immediate/register offsets
- [ ] `LDM/STM` (IA/IB/DA/DB variants), `PUSH/POP`
- [ ] `LDREX/STREX`, `LDREXB/STREXB`, `LDREXH/STREXH`, `LDREXD/STREXD` (exclusive)
- [ ] `LDR` pseudo-instruction (literal pool generation)
- [ ] `LDRT/STRT`, `LDRBT/STRBT`, `LDRHT/STRHT` (user-mode access)
- [ ] `PLD`, `PLDW`, `PLI` (preload)

### 6f. Coprocessor and System
- [ ] `SVC`/`SWI`, `BKPT`, `HLT`
- [ ] `MRS`, `MSR` (CPSR/SPSR/APSR fields)
- [ ] `CPS{IE,ID}` (interrupt enable/disable), `SETEND`
- [ ] `DMB`, `DSB`, `ISB` (barriers)
- [ ] `WFI`, `WFE`, `SEV`, `YIELD`, `NOP`, `DBG`
- [ ] `CDP`, `CDP2`, `MCR`, `MCR2`, `MRC`, `MRC2`, `MCRR`, `MCRR2`, `MRRC`, `MRRC2`
- [ ] `LDC`, `LDC2`, `STC`, `STC2`
- [ ] `CLREX`

### 6g. VFPv3/v4 (Floating Point)
- [ ] `VADD.F32/F64`, `VSUB`, `VMUL`, `VDIV`, `VNMUL`, `VMLA`, `VMLS`, `VNMLA`, `VNMLS`
- [ ] `VFMA.F32/F64`, `VFMS`, `VFNMA`, `VFNMS` (VFPv4)
- [ ] `VMOV` (imm, reg, between ARM/VFP), `VMRS`, `VMSR`
- [ ] `VCMP`, `VCMPE`, `VCVT` (int↔float, float↔float, fixed-point), `VCVT{T,B}.F16.F32` (half-precision)
- [ ] `VABS`, `VNEG`, `VSQRT`
- [ ] `VLDR`, `VSTR`, `VLDM`, `VSTM`, `VPUSH`, `VPOP`
- [ ] `VCVTA/M/N/P` (ARMv8 rounding modes)

### 6h. NEON (Advanced SIMD)
- [ ] **Data types:** `.I8/.I16/.I32/.I64`, `.U8/.U16/.U32/.U64`, `.S8/.S16/.S32/.S64`, `.F16/.F32`, `.P8/.P16`
- [ ] **Arithmetic:** `VADD`, `VSUB`, `VMUL`, `VMLA`, `VMLS`, `VABA`, `VABD`, `VPADD`, `VPMIN/MAX`, `VMAX/MIN`, `VHADD`, `VHSUB`, `VRHADD`, `VQADD`, `VQSUB`, `VMULL`, `VMLAL`, `VMLSL`, `VQDMULL`, `VQDMLAL`, `VQDMLSL`, `VQDMULH`, `VQRDMULH`, `VRSHL`, `VQRSHL`, `VSHL`, `VQSHL`, `VSHR`, `VRSHR`, `VSRA`, `VRSRA`, `VSLI`, `VSRI`, `VQSHLU`, `VSHLL`, `VSHRN`, `VQSHRN`, `VQRSHRN`, `VQSHRUN`, `VQRSHRUN`, `VMOVN`, `VQMOVN`, `VQMOVUN`, `VMOVL`
- [ ] **Logical:** `VAND`, `VORR`, `VEOR`, `VBIC`, `VORN`, `VBIT`, `VBIF`, `VBSL`, `VMOV`, `VMVN`
- [ ] **Compare:** `VCEQ`, `VCGE`, `VCGT`, `VCLE`, `VCLT`, `VACGE`, `VACGT`, `VTST`
- [ ] **Table lookup:** `VTBL`, `VTBX`
- [ ] **Transpose/Interleave:** `VTRN`, `VUZP`, `VZIP`, `VSWP`, `VEXT`, `VREV16/32/64`
- [ ] **Load/Store:** `VLD1/2/3/4`, `VST1/2/3/4` (single/multi-element, all lane, one lane)
- [ ] **Duplicate:** `VDUP` (scalar→vector, ARM reg→vector)
- [ ] **Convert:** `VCVT` (integer↔float within NEON), `VRECPE`, `VRECPS`, `VRSQRTE`, `VRSQRTS`
- [ ] **Accumulate:** `VPADAL`, `VPADDL`, `VCNT`, `VCLZ`, `VCLS`

### 6i. ARMv7 Relocations
- [ ] `R_ARM_ABS32`, `R_ARM_REL32`, `R_ARM_PC24`, `R_ARM_CALL`, `R_ARM_JUMP24`
- [ ] `R_ARM_THM_CALL`, `R_ARM_THM_JUMP24`, `R_ARM_THM_JUMP11`, `R_ARM_THM_JUMP8`
- [ ] `R_ARM_MOVW_ABS_NC`, `R_ARM_MOVT_ABS`, `R_ARM_THM_MOVW_ABS_NC`, `R_ARM_THM_MOVT_ABS`
- [ ] `R_ARM_GOT_BREL`, `R_ARM_PLT32`, `R_ARM_GOTOFF32`, `R_ARM_GOTPC`
- [ ] `R_ARM_TLS_GD32`, `R_ARM_TLS_LDM32`, `R_ARM_TLS_IE32`, `R_ARM_TLS_LE32`
- [ ] `R_ARM_PREL31` (exception tables)

---

## 7. AArch64 (ARMv8.0 + ARMv8.1) Encoding Engine

### 7a. A64 Encoding Infrastructure
- [ ] Fixed-width 32-bit instruction encoding.
- [ ] Register file: X0–X30 (64-bit), W0–W30 (32-bit), SP, XZR/WZR, PC (not directly encodable).
- [ ] Condition codes: EQ/NE/CS/CC/MI/PL/VS/VC/HI/LS/GE/LT/GT/LE/AL/NV.
- [ ] Immediate encoding: logical immediates (bitmask), move-wide immediates, PC-relative (ADR/ADRP pages).

### 7b. Data Processing — Immediate
- [ ] `ADD/SUB{S} Xd, Xn, #imm{, shift}` (12-bit immediate, optional LSL #12)
- [ ] `AND/ORR/EOR/ANDS Xd, Xn, #bitmask` (logical immediate)
- [ ] `MOVN/MOVZ/MOVK Xd, #imm16{, LSL #shift}` (16-bit wide move)
- [ ] `ADR Xd, label` (PC-relative ±1MB), `ADRP Xd, label` (PC-relative ±4GB page)
- [ ] `BFM/SBFM/UBFM` (bitfield move); aliases: `BFI`, `BFXIL`, `SBFX`, `UBFX`, `SXTB/H/W`, `UXTB/H`
- [ ] `EXTR Xd, Xn, Xm, #lsb` (extract/rotate)

### 7c. Data Processing — Register
- [ ] `ADD/SUB{S} Xd, Xn, Xm{, shift #amount}` (LSL/LSR/ASR/ROR)
- [ ] `ADD/SUB{S} Xd, Xn, Wm, extend{#amount}` (UXTB/UXTH/UXTW/UXTX/SXTB/SXTH/SXTW/SXTX)
- [ ] `AND/ORR/EOR/ORN/EON/BIC/BICS/ANDS` (shifted register)
- [ ] `ADC/SBC{S}` (add/sub with carry)
- [ ] `MADD/MSUB` (multiply-add); aliases: `MUL`, `MNEG`
- [ ] `SMADDL/SMSUBL/UMADDL/UMSUBL` (widening multiply-add); aliases: `SMULL`, `UMULL`
- [ ] `SMULH`, `UMULH` (high multiply)
- [ ] `SDIV`, `UDIV`
- [ ] `CLS`, `CLZ`, `RBIT`, `REV`, `REV16`, `REV32`
- [ ] `CSEL/CSINC/CSINV/CSNEG` (conditional select); aliases: `CINC`, `CINV`, `CNEG`, `CSET`, `CSETM`
- [ ] `CCMN/CCMP` (conditional compare)

### 7d. Branch Instructions
- [ ] `B label` (±128MB), `BL label` (±128MB)
- [ ] `B.cond label` (±1MB)
- [ ] `BR Xn`, `BLR Xn`, `RET {Xn}`
- [ ] `CBZ/CBNZ Xt, label` (±1MB)
- [ ] `TBZ/TBNZ Xt, #bit, label` (±32KB)
- [ ] `SVC #imm16`, `HVC #imm16`, `SMC #imm16`, `BRK #imm16`, `HLT #imm16`

### 7e. Load/Store Instructions
- [ ] `LDR/STR` (byte/half/word/dword): immediate offset, pre/post-index, register offset
- [ ] `LDRB/STRB`, `LDRH/STRH`, `LDRSB/LDRSH`, `LDRSW`
- [ ] `LDP/STP` (pair load/store), `LDPSW`
- [ ] `LDNP/STNP` (non-temporal pair)
- [ ] `LDR Xt, =value` (literal pool), `LDR Xt, label` (PC-relative literal)
- [ ] `LDXR/STXR`, `LDXRB/STXRB`, `LDXRH/STXRH`, `LDXP/STXP` (exclusive)
- [ ] `LDAR/STLR`, `LDARB/STLRB`, `LDARH/STLRH` (acquire/release)
- [ ] `LDAXR/STLXR`, `LDAXRB/STLXRB`, `LDAXRH/STLXRH`, `LDAXP/STLXP` (acquire-exclusive)
- [ ] `PRFM` (prefetch memory)

### 7f. System Instructions
- [ ] `MSR/MRS` (system register access)
- [ ] `NOP`, `YIELD`, `WFE`, `WFI`, `SEV`, `SEVL`
- [ ] `DMB`, `DSB`, `ISB` (barriers with domain: SY/ST/LD/ISH/ISHST/ISHLD/NSH/NSHST/NSHLD/OSH/OSHST/OSHLD)
- [ ] `CLREX`
- [ ] `SYS`, `SYSL` (generic system instruction)
- [ ] `DC`, `IC`, `AT`, `TLBI` (cache/TLB maintenance aliases)
- [ ] `HINT #imm`

### 7g. SIMD & FP (NEON / AArch64 Advanced SIMD)
- [ ] Vector registers: V0–V31, Bn/Hn/Sn/Dn/Qn sub-register views
- [ ] Element specifiers: `V0.16B`, `V0.8H`, `V0.4S`, `V0.2D`, `V0.8B`, `V0.4H`, `V0.2S`, `V0.1D`
- [ ] **Arithmetic:** `ADD/SUB`, `MUL/MLA/MLS`, `FADD/FSUB/FMUL/FDIV/FMLA/FMLS`, `FMADD/FMSUB/FNMADD/FNMSUB`
- [ ] `ADDP`, `FADDP`, `SADDL/SADDW/UADDL/UADDW`, `SSUBL/SSUBW/USUBL/USUBW`
- [ ] `SMULL/UMULL/SMLAL/UMLAL/SMLSL/UMLSL` (widening)
- [ ] `SQDMULL/SQDMLAL/SQDMLSL`, `SQRDMULH`, `SQDMULH`
- [ ] `SQADD/UQADD/SQSUB/UQSUB`, `SHADD/UHADD/SHSUB/UHSUB/SRHADD/URHADD`
- [ ] `SMAX/UMAX/SMIN/UMIN`, `SMAXP/UMAXP/SMINP/UMINP`, `SMAXV/UMAXV/SMINV/UMINV`
- [ ] `SABS/SQABS/SQNEG/NEG/ABS`
- [ ] **Shifts:** `SHL/SSHL/USHL/SRSHL/URSHL`, `SQSHL/UQSHL/SQRSHL/UQRSHL`, `SSHR/USHR/SSRA/USRA/SRSHR/URSHR/SRSRA/URSRA`, `SRI/SLI`, `SHRN/RSHRN/SQSHRN/SQRSHRN/UQSHRN/UQRSHRN/SQSHRUN/SQRSHRUN`, `SSHLL/USHLL`
- [ ] **Logical:** `AND/ORR/EOR/ORN/BIC/BIF/BIT/BSL`, `NOT/MVN`
- [ ] **Compare:** `CMEQ/CMGE/CMGT/CMHI/CMHS/CMLE/CMLT/CMTST`, `FCMEQ/FCMGE/FCMGT/FCMLE/FCMLT/FACGE/FACGT`
- [ ] **Permute:** `TBL/TBX`, `TRN1/TRN2`, `UZP1/UZP2`, `ZIP1/ZIP2`, `EXT`, `REV16/32/64`, `DUP`, `INS`, `SMOV/UMOV`
- [ ] **Load/Store:** `LD1/LD2/LD3/LD4`, `ST1/ST2/ST3/ST4` (multi-struct), `LD1R/LD2R/LD3R/LD4R` (replicate)
- [ ] **FP:** `FCVT`, `FCVTZS/FCVTZU`, `SCVTF/UCVTF`, `FMOV`, `FABS`, `FNEG`, `FSQRT`, `FMAX/FMIN/FMAXNM/FMINNM`, `FRINTI/FRINTX/FRINTA/FRINTN/FRINTP/FRINTM/FRINTZ`, `FRECPE/FRECPS/FRSQRTE/FRSQRTS`
- [ ] **Crypto (optional):** `AESE/AESD/AESMC/AESIMC`, `SHA1C/P/M/H/SU0/SU1`, `SHA256H/H2/SU0/SU1`
- [ ] **CRC32 (optional):** `CRC32B/H/W/X/CB/CH/CW/CX`

### 7h. ARMv8.1 Extensions
- [ ] **LSE (Large System Extensions) — Atomics:**
    - [ ] `LDADD{A,L,AL}{B,H,}` — atomic add
    - [ ] `LDCLR{A,L,AL}{B,H,}` — atomic bit clear
    - [ ] `LDEOR{A,L,AL}{B,H,}` — atomic exclusive OR
    - [ ] `LDSET{A,L,AL}{B,H,}` — atomic bit set
    - [ ] `LDSMAX/LDSMIN/LDUMAX/LDUMIN{A,L,AL}{B,H,}` — atomic signed/unsigned max/min
    - [ ] `SWP{A,L,AL}{B,H,}` — atomic swap
    - [ ] `CAS{A,L,AL}{B,H,}` — compare and swap (single)
    - [ ] `CASP{A,L,AL}` — compare and swap pair
    - [ ] `STADD{L}{B,H,}`, `STCLR{L}`, `STEOR{L}`, `STSET{L}`, `STSMAX{L}`, `STSMIN{L}`, `STUMAX{L}`, `STUMIN{L}` — store-only atomics (aliases with XZR destination)
- [ ] **RDMA (Rounding Double Multiply Accumulate):**
    - [ ] `SQRDMLAH` (vector/element) — signed saturating rounding doubling multiply accumulate high
    - [ ] `SQRDMLSH` (vector/element) — signed saturating rounding doubling multiply subtract high
- [ ] **LOR (Limited Ordering Regions):**
    - [ ] `LDLAR{B,H}` — load LOAcquire
    - [ ] `STLLR{B,H}` — store LORelease
- [ ] **VHE (Virtualization Host Extensions):**
    - [ ] New system register access patterns (EL2 registers)
- [ ] **PAN (Privileged Access Never):**
    - [ ] `LDTR/STTR` unprivileged load/store (existing, but PAN makes them significant)
- [ ] **HPD/HPDS (Hierarchical Permission Disables):**
    - [ ] System register configuration (TCR_EL1.HPD bits)

### 7i. AArch64 Relocations
- [ ] `R_AARCH64_ABS64`, `R_AARCH64_ABS32`, `R_AARCH64_ABS16`
- [ ] `R_AARCH64_PREL64`, `R_AARCH64_PREL32`, `R_AARCH64_PREL16`
- [ ] `R_AARCH64_ADR_PREL_PG_HI21`, `R_AARCH64_ADR_PREL_LO21`
- [ ] `R_AARCH64_ADD_ABS_LO12_NC`, `R_AARCH64_LDST8/16/32/64/128_ABS_LO12_NC`
- [ ] `R_AARCH64_MOVW_UABS_G0/G1/G2/G3{_NC}`
- [ ] `R_AARCH64_JUMP26`, `R_AARCH64_CALL26`, `R_AARCH64_CONDBR19`, `R_AARCH64_TSTBR14`
- [ ] `R_AARCH64_GOT_LD_PREL19`, `R_AARCH64_ADR_GOT_PAGE`, `R_AARCH64_LD64_GOT_LO12_NC`
- [ ] `R_AARCH64_TLSGD_ADR_PAGE21`, `R_AARCH64_TLSGD_ADD_LO12_NC`
- [ ] `R_AARCH64_TLSLE_ADD_TPREL_HI12`, `R_AARCH64_TLSLE_ADD_TPREL_LO12{_NC}`
- [ ] `R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21`, `R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC`
- [ ] `R_AARCH64_TLSLD_ADR_PAGE21`, `R_AARCH64_TLSLD_ADD_LO12_NC`

---

## 8. Testing

### 8a. Unit Tests (per-arch)
- [ ] Encode each instruction, verify machine code bytes match reference.
- [ ] Operand range limits: reject out-of-range immediates.
- [ ] Relocation emission: correct type and addend for each pattern.
- [ ] Relaxation: verify short→long branch promotion.
- [ ] Expression evaluation: arithmetic, forward references, absolute vs. relocatable.

### 8b. Integration Tests
- [ ] `as -o test.o test.s` → `readelf -a test.o` validates headers/sections/symbols.
- [ ] `as` + `ld` → running executable for each target arch.
- [ ] Cross-arch rejection: x86 source fails cleanly under ARM mode.
- [ ] Round-trip: disassemble with `objdump` → reassemble → bit-identical output.
- [ ] Compatibility: Substrate `as` output linkable by GNU `ld`, and vice versa.

### 8c. Fuzz Testing
- [ ] Grammar-aware fuzzer for instruction parser.
- [ ] Byte-level fuzzer for ELF output validation.
- [ ] Crash-free guarantee on arbitrary input.

## 9. Documentation
- [ ] `as.1` man page: all options, directives, per-arch syntax.
- [ ] Per-arch instruction reference appendix.
- [ ] `.note.gnu.property` x86-64 ISA level semantics.

## 10. Build System
- [ ] Recursive Makefile, `NATIVE_BUILD=1` for host testing.
- [ ] `install` to `$(DESTDIR)/usr/bin/as`.
- [ ] Arch-specific symlinks or multi-call binary (`arm-as`, `aarch64-as`).
- [ ] `libelfobj.a` dependency.
