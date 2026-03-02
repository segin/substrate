#ifndef _ELFOBJ_H_
#define _ELFOBJ_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct elfobj elfobj_t;
typedef struct elf_section elf_section_t;
typedef struct elf_symbol elf_symbol_t;
typedef struct elf_reloc elf_reloc_t;
typedef struct elf_segment elf_segment_t;
typedef struct elf_link_plan elf_link_plan_t;

#define ELFOBJ_API_VERSION 1

typedef enum {
    ELF_OK = 0,
    ELF_ERR_IO,
    ELF_ERR_FORMAT,
    ELF_ERR_RELOC,
    ELF_ERR_OOM,
    ELF_ERR_BOUNDS,
    ELF_ERR_UNSUPPORTED,
    ELF_ERR_STATE,
    ELF_ERR_NOTFOUND
} elf_err_t;

typedef enum {
    ELFOBJ_CLASS_NONE = 0,
    ELFOBJ_CLASS_32 = 1,
    ELFOBJ_CLASS_64 = 2
} elfobj_class_t;

typedef enum {
    ELFOBJ_ENDIAN_NONE = 0,
    ELFOBJ_ENDIAN_LE = 1,
    ELFOBJ_ENDIAN_BE = 2
} elfobj_endian_t;

#ifndef ET_NONE
#define ET_NONE 0
#define ET_REL 1
#define ET_EXEC 2
#define ET_DYN 3
#define ET_CORE 4
#endif

#ifndef EM_386
#define EM_386 3
#endif
#ifndef EM_X86_64
#define EM_X86_64 62
#endif
#ifndef EM_MIPS
#define EM_MIPS 8
#endif
#ifndef EM_ARM
#define EM_ARM 40
#endif
#ifndef EM_AARCH64
#define EM_AARCH64 183
#endif
#ifndef EM_RISCV
#define EM_RISCV 243
#endif
#ifndef EM_LOONGARCH
#define EM_LOONGARCH 258
#endif
#ifndef EM_68K
#define EM_68K 4
#endif
#ifndef EM_VAX
#define EM_VAX 75
#endif
#ifndef EM_PPC
#define EM_PPC 20
#endif
#ifndef EM_PPC64
#define EM_PPC64 21
#endif
#ifndef EM_ALPHA
#define EM_ALPHA 0x9026
#endif
#ifndef EM_IA_64
#define EM_IA_64 50
#endif
#ifndef ELFOSABI_NONE
#define ELFOSABI_NONE 0
#endif
#ifndef ELFOSABI_SYSV
#define ELFOSABI_SYSV 0
#endif
#ifndef ELFOSABI_LINUX
#define ELFOSABI_LINUX 3
#endif
#ifndef ELFOSABI_FREEBSD
#define ELFOSABI_FREEBSD 9
#endif
#ifndef ELFOSABI_SUBSTRATE
#define ELFOSABI_SUBSTRATE 64
#endif

#ifndef SHT_NULL
#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_HASH 5
#define SHT_DYNAMIC 6
#define SHT_NOTE 7
#define SHT_NOBITS 8
#define SHT_REL 9
#define SHT_DYNSYM 11
#endif

#ifndef SHF_WRITE
#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4
#define SHF_MERGE 0x10
#define SHF_STRINGS 0x20
#define SHF_GROUP 0x200
#define SHF_TLS 0x400
#endif
#ifndef SHF_COMPRESSED
#define SHF_COMPRESSED 0x800
#endif

#define ELFOBJ_OPEN_NOCOPY 0x1u
#define ELFOBJ_OPEN_USE_MMAP 0x2u
#define ELFOBJ_OPEN_LAZY_PARSE 0x4u

#ifndef PT_NULL
#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_PHDR 6
#define PT_TLS 7
#endif

#ifndef DT_NULL
#define DT_NULL 0
#define DT_NEEDED 1
#define DT_STRTAB 5
#define DT_SYMTAB 6
#endif

#ifndef STB_LOCAL
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#endif

#ifndef STT_NOTYPE
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define STT_FILE 4
#define STT_TLS 6
#endif

#ifndef SHN_UNDEF
#define SHN_UNDEF 0
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2
#endif

#ifndef R_386_NONE
#define R_386_NONE 0
#define R_386_32 1
#define R_386_PC32 2
#define R_386_GOT32 3
#define R_386_PLT32 4
#define R_386_COPY 5
#define R_386_GLOB_DAT 6
#define R_386_JMP_SLOT 7
#define R_386_RELATIVE 8
#define R_386_GOTOFF 9
#define R_386_GOTPC 10
#define R_386_TLS_TPOFF 14
#define R_386_TLS_IE 15
#define R_386_TLS_GOTIE 16
#define R_386_TLS_LE 17
#define R_386_TLS_GD 18
#define R_386_TLS_LDM 19
#define R_386_16 20
#define R_386_PC16 21
#define R_386_8 22
#define R_386_PC8 23
#define R_386_TLS_LDO_32 32
#define R_386_TLS_LE_32 33
#define R_386_TLS_DTPMOD32 35
#define R_386_TLS_DTPOFF32 36
#define R_386_TLS_TPOFF32 37
#define R_386_SIZE32 38
#define R_386_IRELATIVE 42
#define R_386_GOT32X 43
#endif

#ifndef R_X86_64_NONE
#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_GOT32 3
#define R_X86_64_PLT32 4
#define R_X86_64_COPY 5
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE 8
#define R_X86_64_GOTPCREL 9
#define R_X86_64_32 10
#define R_X86_64_32S 11
#define R_X86_64_16 12
#define R_X86_64_PC16 13
#define R_X86_64_8 14
#define R_X86_64_PC8 15
#define R_X86_64_DTPMOD64 16
#define R_X86_64_DTPOFF64 17
#define R_X86_64_TPOFF64 18
#define R_X86_64_TLSGD 19
#define R_X86_64_TLSLD 20
#define R_X86_64_DTPOFF32 21
#define R_X86_64_GOTTPOFF 22
#define R_X86_64_TPOFF32 23
#define R_X86_64_PC64 24
#define R_X86_64_GOTOFF64 25
#define R_X86_64_GOTPC32 26
#define R_X86_64_SIZE32 32
#define R_X86_64_SIZE64 33
#define R_X86_64_GOTPC32_TLSDESC 34
#define R_X86_64_TLSDESC_CALL 35
#define R_X86_64_TLSDESC 36
#define R_X86_64_IRELATIVE 37
#define R_X86_64_GOTPCRELX 41
#define R_X86_64_REX_GOTPCRELX 42
#endif

#ifndef EF_ARM_ABI_VER5
#define EF_ARM_ABI_VER5 0x05000000u
#define EF_ARM_ABI_FLOAT_SOFT 0x00000200u
#define EF_ARM_ABI_FLOAT_HARD 0x00000400u
#define EF_ARM_INTERWORK 0x00000004u
#define EF_ARM_APCS_26 0x00000008u
#define EF_ARM_APCS_FLOAT 0x00000010u
#define EF_ARM_VFP_FLOAT 0x00000400u
#define EF_ARM_MAVERICK_FLOAT 0x00000800u
#define EF_ARM_BE8 0x00800000u
#endif

#ifndef EF_AARCH64_CHERI_PURECAP
#define EF_AARCH64_CHERI_PURECAP 0x00000001u
#endif

#ifndef EF_MIPS_NOREORDER
#define EF_MIPS_NOREORDER 0x00000001u
#define EF_MIPS_PIC 0x00000002u
#define EF_MIPS_CPIC 0x00000004u
#define EF_MIPS_ABI_O32 0x00001000u
#define EF_MIPS_ABI_O64 0x00002000u
#define EF_MIPS_ABI_EABI32 0x00003000u
#define EF_MIPS_ABI_EABI64 0x00004000u
#define EF_MIPS_ARCH_1 0x00000000u
#define EF_MIPS_ARCH_2 0x10000000u
#define EF_MIPS_ARCH_3 0x20000000u
#define EF_MIPS_ARCH_4 0x30000000u
#define EF_MIPS_ARCH_5 0x40000000u
#define EF_MIPS_ARCH_32 0x50000000u
#define EF_MIPS_ARCH_64 0x60000000u
#define EF_MIPS_ARCH_32R2 0x70000000u
#define EF_MIPS_ARCH_64R2 0x80000000u
#define EF_MIPS_ARCH_32R6 0x90000000u
#define EF_MIPS_ARCH_64R6 0xa0000000u
#define EF_MIPS_MIPS16 0x00000020u
#define EF_MIPS_MICROMIPS 0x02000000u
#define EF_MIPS_ARCH_ASE_MDMX 0x08000000u
#define EF_MIPS_ARCH_ASE_M16 0x04000000u
#define EF_MIPS_ARCH_ASE_DSP 0x04000000u
#define EF_MIPS_ARCH_ASE_DSPR2 0x08000000u
#define EF_MIPS_ARCH_ASE_MSA 0x20000000u
#define EF_MIPS_FP64 0x00000200u
#endif

#ifndef SHT_MIPS_DWARF
#define SHT_MIPS_DWARF 0x7000001eu
#define SHT_MIPS_ABIFLAGS 0x7000002au
#endif

#ifndef PT_MIPS_REGINFO
#define PT_MIPS_REGINFO 0x70000000u
#define PT_MIPS_ABIFLAGS 0x70000003u
#endif

#ifndef EF_RISCV_RVC
#define EF_RISCV_RVC 0x0001u
#define EF_RISCV_FLOAT_ABI_SOFT 0x0000u
#define EF_RISCV_FLOAT_ABI_SINGLE 0x0002u
#define EF_RISCV_FLOAT_ABI_DOUBLE 0x0004u
#define EF_RISCV_FLOAT_ABI_QUAD 0x0006u
#define EF_RISCV_RVE 0x0008u
#define EF_RISCV_TSO 0x0010u
#endif

#ifndef SHT_RISCV_ATTRIBUTES
#define SHT_RISCV_ATTRIBUTES 0x70000003u
#endif

#ifndef EF_LARCH_ABI_MODIFIER_MASK
#define EF_LARCH_ABI_MODIFIER_MASK 0x7u
#define EF_LARCH_ABI_SOFT_FLOAT 0x1u
#define EF_LARCH_ABI_SINGLE_FLOAT 0x2u
#define EF_LARCH_ABI_DOUBLE_FLOAT 0x3u
#define EF_LARCH_OBJABI_V1 0x40u
#endif

#ifndef SHT_ARM_EXIDX
#define SHT_ARM_EXIDX 0x70000001u
#define SHT_ARM_PREEMPTMAP 0x70000002u
#define SHT_ARM_ATTRIBUTES 0x70000003u
#endif

#ifndef SHF_ARM_PURECODE
#define SHF_ARM_PURECODE 0x20000000u
#endif

#ifndef PT_ARM_EXIDX
#define PT_ARM_EXIDX 0x70000001u
#endif

#ifndef SHT_AARCH64_ATTRIBUTES
#define SHT_AARCH64_ATTRIBUTES 0x70000003u
#endif

#ifndef PT_AARCH64_MEMTAG_MTE
#define PT_AARCH64_MEMTAG_MTE 0x70000002u
#endif

#ifndef ELFOBJ_SEC_ARM_EXIDX
#define ELFOBJ_SEC_ARM_EXIDX ".ARM.exidx"
#define ELFOBJ_SEC_ARM_EXTAB ".ARM.extab"
#define ELFOBJ_SEC_ARM_ATTRIBUTES ".ARM.attributes"
#define ELFOBJ_SEC_NOTE_GNU_PROPERTY ".note.gnu.property"
#endif

#ifndef SHF_LINK_ORDER
#define SHF_LINK_ORDER 0x80
#endif

#ifndef R_ARM_NONE
#define R_ARM_NONE 0
#define R_ARM_PC24 1
#define R_ARM_ABS32 2
#define R_ARM_REL32 3
#define R_ARM_LDR_PC_G0 4
#define R_ARM_ABS16 5
#define R_ARM_ABS12 6
#define R_ARM_THM_ABS5 7
#define R_ARM_ABS8 8
#define R_ARM_SBREL32 9
#define R_ARM_THM_CALL 10
#define R_ARM_THM_PC8 11
#define R_ARM_BREL_ADJ 12
#define R_ARM_TLS_DESC 13
#define R_ARM_THM_SWI8 14
#define R_ARM_XPC25 15
#define R_ARM_THM_XPC22 16
#define R_ARM_TLS_DTPMOD32 17
#define R_ARM_TLS_DTPOFF32 18
#define R_ARM_TLS_TPOFF32 19
#define R_ARM_COPY 20
#define R_ARM_GLOB_DAT 21
#define R_ARM_JUMP_SLOT 22
#define R_ARM_RELATIVE 23
#define R_ARM_GOTOFF32 24
#define R_ARM_BASE_PREL 25
#define R_ARM_GOTPC R_ARM_BASE_PREL
#define R_ARM_GOT_BREL 26
#define R_ARM_GOT32 R_ARM_GOT_BREL
#define R_ARM_PLT32 27
#define R_ARM_CALL 28
#define R_ARM_JUMP24 29
#define R_ARM_THM_JUMP24 30
#define R_ARM_BASE_ABS 31
#define R_ARM_ALU_PCREL_7_0 32
#define R_ARM_ALU_PCREL_15_8 33
#define R_ARM_ALU_PCREL_23_16 34
#define R_ARM_LDR_SBREL_11_0_NC 35
#define R_ARM_ALU_SBREL_19_12_NC 36
#define R_ARM_ALU_SBREL_27_20_CK 37
#define R_ARM_TARGET1 38
#define R_ARM_SBREL31 39
#define R_ARM_V4BX 40
#define R_ARM_TARGET2 41
#define R_ARM_PREL31 42
#define R_ARM_MOVW_ABS_NC 43
#define R_ARM_MOVT_ABS 44
#define R_ARM_MOVW_PREL_NC 45
#define R_ARM_MOVT_PREL 46
#define R_ARM_THM_MOVW_ABS_NC 47
#define R_ARM_THM_MOVT_ABS 48
#define R_ARM_THM_MOVW_PREL_NC 49
#define R_ARM_THM_MOVT_PREL 50
#define R_ARM_THM_JUMP19 51
#define R_ARM_THM_JUMP6 52
#define R_ARM_THM_ALU_PREL_11_0 53
#define R_ARM_THM_PC12 54
#define R_ARM_ABS32_NOI 55
#define R_ARM_REL32_NOI 56
#define R_ARM_ALU_PC_G0_NC 57
#define R_ARM_ALU_PC_G0 58
#define R_ARM_ALU_PC_G1_NC 59
#define R_ARM_ALU_PC_G1 60
#define R_ARM_ALU_PC_G2 61
#define R_ARM_LDR_PC_G1 62
#define R_ARM_LDR_PC_G2 63
#define R_ARM_LDRS_PC_G0 64
#define R_ARM_LDRS_PC_G1 65
#define R_ARM_LDRS_PC_G2 66
#define R_ARM_LDC_PC_G0 67
#define R_ARM_LDC_PC_G1 68
#define R_ARM_LDC_PC_G2 69
#define R_ARM_ALU_SB_G0_NC 70
#define R_ARM_ALU_SB_G0 71
#define R_ARM_ALU_SB_G1_NC 72
#define R_ARM_ALU_SB_G1 73
#define R_ARM_ALU_SB_G2 74
#define R_ARM_LDR_SB_G0 75
#define R_ARM_LDR_SB_G1 76
#define R_ARM_LDR_SB_G2 77
#define R_ARM_LDRS_SB_G0 78
#define R_ARM_LDRS_SB_G1 79
#define R_ARM_LDRS_SB_G2 80
#define R_ARM_LDC_SB_G0 81
#define R_ARM_LDC_SB_G1 82
#define R_ARM_LDC_SB_G2 83
#define R_ARM_MOVW_BREL_NC 84
#define R_ARM_MOVT_BREL 85
#define R_ARM_MOVW_BREL 86
#define R_ARM_THM_MOVW_BREL_NC 87
#define R_ARM_THM_MOVT_BREL 88
#define R_ARM_THM_MOVW_BREL 89
#define R_ARM_TLS_GOTDESC 90
#define R_ARM_TLS_CALL 91
#define R_ARM_TLS_DESCSEQ 92
#define R_ARM_THM_TLS_CALL 93
#define R_ARM_PLT32_ABS 94
#define R_ARM_GOT_ABS 95
#define R_ARM_GOT_PREL 96
#define R_ARM_GOT_BREL12 97
#define R_ARM_GOTOFF12 98
#define R_ARM_GOTRELAX 99
#define R_ARM_GNU_VTENTRY 100
#define R_ARM_GNU_VTINHERIT 101
#define R_ARM_THM_JUMP11 102
#define R_ARM_THM_JUMP8 103
#define R_ARM_TLS_GD32 104
#define R_ARM_TLS_LDM32 105
#define R_ARM_TLS_LDO32 106
#define R_ARM_TLS_IE32 107
#define R_ARM_TLS_LE32 108
#define R_ARM_TLS_LDO12 109
#define R_ARM_TLS_LE12 110
#define R_ARM_TLS_IE12GP 111
#define R_ARM_IRELATIVE 160
#define R_ARM_RXPC25 249
#define R_ARM_RSBREL32 250
#define R_ARM_THM_RPC22 251
#define R_ARM_RREL32 252
#define R_ARM_RABS32 253
#define R_ARM_RPC24 254
#define R_ARM_RBASE 255
#endif

#ifndef R_AARCH64_NONE
#define R_AARCH64_NONE 0
#define R_AARCH64_ABS64 257
#define R_AARCH64_ABS32 258
#define R_AARCH64_ABS16 259
#define R_AARCH64_PREL64 260
#define R_AARCH64_PREL32 261
#define R_AARCH64_PREL16 262
#define R_AARCH64_MOVW_UABS_G0 263
#define R_AARCH64_MOVW_UABS_G0_NC 264
#define R_AARCH64_MOVW_UABS_G1 265
#define R_AARCH64_MOVW_UABS_G1_NC 266
#define R_AARCH64_MOVW_UABS_G2 267
#define R_AARCH64_MOVW_UABS_G2_NC 268
#define R_AARCH64_MOVW_UABS_G3 269
#define R_AARCH64_MOVW_SABS_G0 270
#define R_AARCH64_MOVW_SABS_G1 271
#define R_AARCH64_MOVW_SABS_G2 272
#define R_AARCH64_LD_PREL_LO19 273
#define R_AARCH64_ADR_PREL_LO21 274
#define R_AARCH64_ADR_PREL_PG_HI21 275
#define R_AARCH64_ADR_PREL_PG_HI21_NC 276
#define R_AARCH64_ADD_ABS_LO12_NC 277
#define R_AARCH64_LDST8_ABS_LO12_NC 278
#define R_AARCH64_TSTBR14 279
#define R_AARCH64_CONDBR19 280
#define R_AARCH64_JUMP26 282
#define R_AARCH64_CALL26 283
#define R_AARCH64_LDST16_ABS_LO12_NC 284
#define R_AARCH64_LDST32_ABS_LO12_NC 285
#define R_AARCH64_LDST64_ABS_LO12_NC 286
#define R_AARCH64_MOVW_PREL_G0 287
#define R_AARCH64_MOVW_PREL_G0_NC 288
#define R_AARCH64_MOVW_PREL_G1 289
#define R_AARCH64_MOVW_PREL_G1_NC 290
#define R_AARCH64_MOVW_PREL_G2 291
#define R_AARCH64_MOVW_PREL_G2_NC 292
#define R_AARCH64_MOVW_PREL_G3 293
#define R_AARCH64_LDST128_ABS_LO12_NC 299
#define R_AARCH64_GOT_LD_PREL19 309
#define R_AARCH64_ADR_GOT_PAGE 311
#define R_AARCH64_LD64_GOT_LO12_NC 312
#define R_AARCH64_LD64_GOTPAGE_LO15 313
#define R_AARCH64_TLSGD_ADR_PREL21 512
#define R_AARCH64_TLSGD_ADR_PAGE21 513
#define R_AARCH64_TLSGD_ADD_LO12_NC 514
#define R_AARCH64_TLSGD_MOVW_G1 515
#define R_AARCH64_TLSGD_MOVW_G0_NC 516
#define R_AARCH64_TLSLD_ADR_PREL21 517
#define R_AARCH64_TLSLD_ADR_PAGE21 518
#define R_AARCH64_TLSLD_ADD_LO12_NC 519
#define R_AARCH64_TLSLD_MOVW_DTPREL_G2 524
#define R_AARCH64_TLSLD_ADD_DTPREL_HI12 528
#define R_AARCH64_TLSLD_ADD_DTPREL_LO12 529
#define R_AARCH64_TLSLD_ADD_DTPREL_LO12_NC 530
#define R_AARCH64_TLSLD_LDST8_DTPREL_LO12 531
#define R_AARCH64_TLSLD_LDST8_DTPREL_LO12_NC 532
#define R_AARCH64_TLSLD_LDST16_DTPREL_LO12 533
#define R_AARCH64_TLSLD_LDST16_DTPREL_LO12_NC 534
#define R_AARCH64_TLSLD_LDST32_DTPREL_LO12 535
#define R_AARCH64_TLSLD_LDST32_DTPREL_LO12_NC 536
#define R_AARCH64_TLSLD_LDST64_DTPREL_LO12 537
#define R_AARCH64_TLSLD_LDST64_DTPREL_LO12_NC 538
#define R_AARCH64_TLSIE_MOVW_GOTTPREL_G1 539
#define R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC 540
#define R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21 541
#define R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC 542
#define R_AARCH64_TLSIE_LD_GOTTPREL_PREL19 543
#define R_AARCH64_TLSLE_MOVW_TPREL_G2 544
#define R_AARCH64_TLSLE_MOVW_TPREL_G1 545
#define R_AARCH64_TLSLE_MOVW_TPREL_G1_NC 546
#define R_AARCH64_TLSLE_MOVW_TPREL_G0 547
#define R_AARCH64_TLSLE_MOVW_TPREL_G0_NC 548
#define R_AARCH64_TLSLE_ADD_TPREL_HI12 549
#define R_AARCH64_TLSLE_ADD_TPREL_LO12 550
#define R_AARCH64_TLSLE_ADD_TPREL_LO12_NC 551
#define R_AARCH64_TLSLE_LDST8_TPREL_LO12 552
#define R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC 553
#define R_AARCH64_TLSLE_LDST16_TPREL_LO12 554
#define R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC 555
#define R_AARCH64_TLSLE_LDST32_TPREL_LO12 556
#define R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC 557
#define R_AARCH64_TLSLE_LDST64_TPREL_LO12 558
#define R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC 559
#define R_AARCH64_TLSDESC_LD_PREL19 560
#define R_AARCH64_TLSDESC_ADR_PREL21 561
#define R_AARCH64_TLSDESC_ADR_PAGE21 562
#define R_AARCH64_TLSDESC_LD64_LO12 563
#define R_AARCH64_TLSDESC_ADD_LO12 564
#define R_AARCH64_TLSDESC_OFF_G1 565
#define R_AARCH64_TLSDESC_OFF_G0_NC 566
#define R_AARCH64_TLSDESC_LDR 567
#define R_AARCH64_TLSDESC_ADD 568
#define R_AARCH64_TLSDESC_CALL 569
#define R_AARCH64_TLSLD_MOVW_DTPREL_G0 520
#define R_AARCH64_TLSLD_MOVW_DTPREL_G0_NC 521
#define R_AARCH64_TLSLD_MOVW_DTPREL_G1 522
#define R_AARCH64_TLSLD_MOVW_DTPREL_G1_NC 523
#define R_AARCH64_TLSLD_LDST128_DTPREL_LO12 539
#define R_AARCH64_TLSLD_LDST128_DTPREL_LO12_NC 540
#define R_AARCH64_TLSLE_LDST128_TPREL_LO12 560
#define R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC 561
#define R_AARCH64_COPY 1024
#define R_AARCH64_GLOB_DAT 1025
#define R_AARCH64_JUMP_SLOT 1026
#define R_AARCH64_RELATIVE 1027
#define R_AARCH64_TLS_DTPMOD64 1028
#define R_AARCH64_TLS_DTPREL64 1029
#define R_AARCH64_TLS_TPREL64 1030
#define R_AARCH64_TLSDESC 1031
#define R_AARCH64_IRELATIVE 1032
#endif

#ifndef R_MIPS_NONE
#define R_MIPS_NONE 0
#define R_MIPS_16 1
#define R_MIPS_32 2
#define R_MIPS_REL32 3
#define R_MIPS_26 4
#define R_MIPS_HI16 5
#define R_MIPS_LO16 6
#define R_MIPS_GPREL16 7
#define R_MIPS_LITERAL 8
#define R_MIPS_GOT16 9
#define R_MIPS_PC16 10
#define R_MIPS_CALL16 11
#define R_MIPS_GPREL32 12
#define R_MIPS_SHIFT5 16
#define R_MIPS_SHIFT6 17
#define R_MIPS_64 18
#define R_MIPS_GOT_DISP 19
#define R_MIPS_GOT_PAGE 20
#define R_MIPS_GOT_OFST 21
#define R_MIPS_GOT_HI16 22
#define R_MIPS_GOT_LO16 23
#define R_MIPS_SUB 24
#define R_MIPS_INSERT_A 25
#define R_MIPS_INSERT_B 26
#define R_MIPS_DELETE 27
#define R_MIPS_HIGHER 28
#define R_MIPS_HIGHEST 29
#define R_MIPS_CALL_HI16 30
#define R_MIPS_CALL_LO16 31
#define R_MIPS_SCN_DISP 32
#define R_MIPS_REL16 33
#define R_MIPS_ADD_IMMEDIATE 34
#define R_MIPS_PJUMP 35
#define R_MIPS_RELGOT 36
#define R_MIPS_JALR 37
#define R_MIPS_TLS_DTPMOD32 38
#define R_MIPS_TLS_DTPREL32 39
#define R_MIPS_TLS_DTPMOD64 40
#define R_MIPS_TLS_DTPREL64 41
#define R_MIPS_TLS_GD 42
#define R_MIPS_TLS_LDM 43
#define R_MIPS_TLS_DTPREL_HI16 44
#define R_MIPS_TLS_DTPREL_LO16 45
#define R_MIPS_TLS_GOTTPREL 46
#define R_MIPS_TLS_TPREL32 47
#define R_MIPS_TLS_TPREL64 48
#define R_MIPS_TLS_TPREL_HI16 49
#define R_MIPS_TLS_TPREL_LO16 50
#define R_MIPS_GLOB_DAT 51
#define R_MIPS_COPY 126
#define R_MIPS_JUMP_SLOT 127
#define R_MICROMIPS_26_S1 133
#define R_MICROMIPS_HI16 134
#define R_MICROMIPS_LO16 135
#define R_MICROMIPS_GPREL16 136
#define R_MICROMIPS_PC7_S1 143
#define R_MICROMIPS_PC10_S1 144
#define R_MICROMIPS_PC16_S1 145
#define R_MICROMIPS_PC23_S2 172
#endif

#ifndef R_RISCV_NONE
#define R_RISCV_NONE 0
#define R_RISCV_32 1
#define R_RISCV_64 2
#define R_RISCV_RELATIVE 3
#define R_RISCV_COPY 4
#define R_RISCV_JUMP_SLOT 5
#define R_RISCV_TLS_DTPMOD32 6
#define R_RISCV_TLS_DTPMOD64 7
#define R_RISCV_TLS_DTPREL32 8
#define R_RISCV_TLS_DTPREL64 9
#define R_RISCV_TLS_TPREL32 10
#define R_RISCV_TLS_TPREL64 11
#define R_RISCV_BRANCH 16
#define R_RISCV_JAL 17
#define R_RISCV_CALL 18
#define R_RISCV_CALL_PLT 19
#define R_RISCV_GOT_HI20 20
#define R_RISCV_TLS_GOT_HI20 21
#define R_RISCV_TLS_GD_HI20 22
#define R_RISCV_PCREL_HI20 23
#define R_RISCV_PCREL_LO12_I 24
#define R_RISCV_PCREL_LO12_S 25
#define R_RISCV_HI20 26
#define R_RISCV_LO12_I 27
#define R_RISCV_LO12_S 28
#define R_RISCV_TPREL_HI20 29
#define R_RISCV_TPREL_LO12_I 30
#define R_RISCV_TPREL_LO12_S 31
#define R_RISCV_TPREL_ADD 32
#define R_RISCV_ADD8 33
#define R_RISCV_ADD16 34
#define R_RISCV_ADD32 35
#define R_RISCV_ADD64 36
#define R_RISCV_SUB8 37
#define R_RISCV_SUB16 38
#define R_RISCV_SUB32 39
#define R_RISCV_SUB64 40
#define R_RISCV_ALIGN 43
#define R_RISCV_RVC_BRANCH 44
#define R_RISCV_RVC_JUMP 45
#define R_RISCV_RVC_LUI 46
#define R_RISCV_RELAX 51
#define R_RISCV_SUB6 52
#define R_RISCV_SET6 53
#define R_RISCV_SET8 54
#define R_RISCV_SET16 55
#define R_RISCV_SET32 56
#define R_RISCV_32_PCREL 57
#define R_RISCV_IRELATIVE 58
#define R_RISCV_128 59
#define R_RISCV_VENDOR 191
#endif

#ifndef R_LARCH_NONE
#define R_LARCH_NONE 0
#define R_LARCH_32 1
#define R_LARCH_64 2
#define R_LARCH_RELATIVE 3
#define R_LARCH_COPY 4
#define R_LARCH_JUMP_SLOT 5
#define R_LARCH_TLS_DTPMOD32 6
#define R_LARCH_TLS_DTPMOD64 7
#define R_LARCH_TLS_DTPREL32 8
#define R_LARCH_TLS_DTPREL64 9
#define R_LARCH_TLS_TPREL32 10
#define R_LARCH_TLS_TPREL64 11
#define R_LARCH_IRELATIVE 12
#define R_LARCH_MARK_LA 20
#define R_LARCH_MARK_PCREL 21
#define R_LARCH_SOP_PUSH_PCREL 22
#define R_LARCH_SOP_PUSH_ABSOLUTE 23
#define R_LARCH_SOP_PUSH_DUP 24
#define R_LARCH_SOP_PUSH_GPREL 25
#define R_LARCH_SOP_PUSH_TLS_TPREL 26
#define R_LARCH_SOP_PUSH_TLS_GOT 27
#define R_LARCH_SOP_PUSH_TLS_GD 28
#define R_LARCH_SOP_PUSH_PLT_PCREL 29
#define R_LARCH_SOP_ASSERT 30
#define R_LARCH_SOP_NOT 31
#define R_LARCH_SOP_SUB 32
#define R_LARCH_SOP_SL 33
#define R_LARCH_SOP_SR 34
#define R_LARCH_SOP_ADD 35
#define R_LARCH_SOP_AND 36
#define R_LARCH_SOP_IF_ELSE 37
#define R_LARCH_SOP_POP_32_S_10_5 38
#define R_LARCH_SOP_POP_32_U_10_12 39
#define R_LARCH_SOP_POP_32_S_10_12 40
#define R_LARCH_SOP_POP_32_S_10_16 41
#define R_LARCH_SOP_POP_32_S_10_16_S2 43
#define R_LARCH_B16 64
#define R_LARCH_B21 65
#define R_LARCH_B26 66
#define R_LARCH_ABS_HI20 67
#define R_LARCH_ABS_LO12 68
#define R_LARCH_ABS64_LO20 69
#define R_LARCH_ABS64_HI12 70
#define R_LARCH_PCALA_HI20 71
#define R_LARCH_PCALA_LO12 72
#define R_LARCH_PCALA64_LO20 73
#define R_LARCH_PCALA64_HI12 74
#define R_LARCH_GOT_PC_HI20 75
#define R_LARCH_GOT_PC_LO12 76
#define R_LARCH_GOT64_PC_LO20 77
#define R_LARCH_GOT64_PC_HI12 78
#define R_LARCH_TLS_LE_HI20 79
#define R_LARCH_TLS_LE_LO12 80
#define R_LARCH_TLS_LE64_LO20 81
#define R_LARCH_TLS_LE64_HI12 82
#define R_LARCH_TLS_IE_PC_HI20 83
#define R_LARCH_TLS_IE_PC_LO12 84
#define R_LARCH_TLS_IE64_PC_LO20 85
#define R_LARCH_TLS_IE64_PC_HI12 86
#define R_LARCH_TLS_LD_PC_HI20 87
#define R_LARCH_TLS_GD_PC_HI20 98
#define R_LARCH_TLS_DESC_PC_HI20 99
#define R_LARCH_TLS_DESC_PC_LO12 100
#define R_LARCH_TLS_DESC64_PC_LO20 101
#define R_LARCH_TLS_DESC64_PC_HI12 102
#define R_LARCH_TLS_DESC_HI20 103
#define R_LARCH_TLS_DESC_LO12 104
#define R_LARCH_TLS_DESC64_LO20 105
#define R_LARCH_TLS_DESC64_HI12 106
#define R_LARCH_TLS_DESC_LD 107
#define R_LARCH_TLS_DESC_CALL 108
#define R_LARCH_TLS_LE_HI20_R 109
#define R_LARCH_TLS_LE_ADD_R 110
#define R_LARCH_TLS_LE_LO12_R 111
#define R_LARCH_RELAX 130
#define R_LARCH_ALIGN 131
#define R_LARCH_ADD6 120
#define R_LARCH_ADD8 121
#define R_LARCH_ADD16 122
#define R_LARCH_ADD32 123
#define R_LARCH_ADD64 124
#define R_LARCH_SUB6 125
#define R_LARCH_SUB8 126
#define R_LARCH_SUB16 127
#define R_LARCH_SUB32 128
#define R_LARCH_SUB64 129
#endif

#ifndef GNU_PROPERTY_X86_ISA_1_NEEDED
#define GNU_PROPERTY_X86_ISA_1_NEEDED 0xc0008002u
#define GNU_PROPERTY_X86_ISA_1_USED 0xc0010002u
#define GNU_PROPERTY_X86_ISA_1_BASELINE 0x1u
#define GNU_PROPERTY_X86_ISA_1_V2 0x2u
#define GNU_PROPERTY_X86_ISA_1_V3 0x4u
#define GNU_PROPERTY_X86_ISA_1_V4 0x8u
#define GNU_PROPERTY_X86_FEATURE_1_AND 0xc0000002u
#define GNU_PROPERTY_X86_FEATURE_1_IBT 0x1u
#define GNU_PROPERTY_X86_FEATURE_1_SHSTK 0x2u
#define GNU_PROPERTY_AARCH64_FEATURE_1_AND 0xc0000000u
#define GNU_PROPERTY_AARCH64_FEATURE_1_BTI 0x1u
#define GNU_PROPERTY_AARCH64_FEATURE_1_PAC 0x2u
#endif

typedef struct {
    uint32_t machine;
    uint8_t use_rela;
    uint8_t reserved[3];
} elfobj_reloc_ctx_t;

struct elf_reloc_backend {
    uint32_t machine;
    int (*apply_reloc)(const elfobj_reloc_ctx_t *ctx,
                       uint32_t type,
                       uint64_t place,
                       uint64_t sym_value,
                       int64_t addend,
                       uint64_t *out_value);
    int (*reloc_size)(uint32_t type);
    int (*is_pc_relative)(uint32_t type);
};

typedef struct {
    int (*before_apply)(const elf_reloc_t *reloc, void *user);
    void (*after_apply)(const elf_reloc_t *reloc, uint64_t relocated_value, void *user);
    void (*incremental_note)(const char *key, uint64_t value, void *user);
} elf_reloc_hooks_t;

typedef enum {
    ELF_LINK_MERGE_APPEND = 0,
    ELF_LINK_MERGE_REPLACE = 1,
    ELF_LINK_MERGE_SKIP = 2
} elf_link_merge_action_t;

typedef elf_link_merge_action_t (*elf_link_section_merge_hook_t)(
    const char *section_name, const elf_section_t *existing, const elf_section_t *incoming, void *user);
typedef int (*elf_link_archive_hook_t)(const char *archive_path, const char *member_name, void *user);
typedef int (*elf_link_gc_hook_t)(const elf_section_t *section, void *user);
typedef void (*elf_link_incremental_hook_t)(const char *key, const char *value, void *user);
typedef int (*elf_link_version_hook_t)(const char *symbol_name, const char *version_name, void *user);

typedef struct {
    const char *symbol_name;
    const char *section_name;
    const char *input_name;
    uint64_t value;
    size_t input_index;
} elf_link_map_entry_t;

typedef enum {
    ELF_VALIDATE_PERMISSIVE = 0,
    ELF_VALIDATE_STRICT = 1
} elf_validate_mode_t;

typedef enum {
    ELF_DIAG_INFO = 0,
    ELF_DIAG_WARNING = 1,
    ELF_DIAG_ERROR = 2
} elf_diag_level_t;

typedef struct {
    elf_diag_level_t level;
    elf_err_t code;
    uint64_t index;
    const char *message;
} elf_diag_entry_t;

typedef struct {
    elf_validate_mode_t mode;
    size_t max_errors;
} elf_validate_options_t;

elf_err_t elf_open(const char *path, elfobj_t **out);
elf_err_t elf_open_memory(const void *buf, size_t size, elfobj_t **out);
elf_err_t elf_open_with_options(const char *path, uint32_t flags, elfobj_t **out);
elf_err_t elf_open_memory_with_options(const void *buf, size_t size, uint32_t flags,
                                       elfobj_t **out);
elf_err_t elf_open_memory_nocopy(const void *buf, size_t size, elfobj_t **out);
elf_err_t elf_write_file(elfobj_t *obj, const char *path);
void elf_close(elfobj_t *obj);
int elf_uses_mmap(const elfobj_t *obj);
int elf_is_lazy_parse_enabled(const elfobj_t *obj);

elf_section_t *elf_add_section(elfobj_t *obj, const char *name, uint32_t type, uint64_t flags);
elf_err_t elf_section_set_data(elf_section_t *section, const void *data, size_t size);
elf_err_t elf_section_set_align(elf_section_t *section, uint64_t align);
elf_err_t elf_section_set_type(elf_section_t *section, uint32_t type);
elf_err_t elf_section_set_flags(elf_section_t *section, uint64_t flags);
elf_err_t elf_section_set_name(elf_section_t *section, const char *name);
elf_err_t elf_section_set_group(elf_section_t *section, uint32_t group, int comdat);
elf_err_t elf_section_set_merge(elf_section_t *section, uint64_t entsize, int strings);
elf_err_t elf_section_set_tls(elf_section_t *section, int enable);
elf_err_t elf_section_set_note_info(elf_section_t *section, uint32_t note_type, const char *note_name);
elf_err_t elf_remove_section(elfobj_t *obj, elf_section_t *section);
elf_err_t elf_reorder_section(elfobj_t *obj, elf_section_t *section, size_t new_index);
elf_section_t *elf_find_section(elfobj_t *obj, const char *name);

elf_symbol_t *elf_add_symbol(elfobj_t *obj, const char *name, uint64_t value,
                              uint64_t size, uint8_t bind, uint8_t type);
elf_err_t elf_symbol_define(elf_symbol_t *symbol, elf_section_t *section, uint64_t value);
elf_symbol_t *elf_find_symbol(elfobj_t *obj, const char *name);
elf_symbol_t *elf_symbol_at(elfobj_t *obj, size_t index);
elf_err_t elf_symbol_set_binding(elf_symbol_t *symbol, uint8_t bind);
elf_err_t elf_symbol_set_type(elf_symbol_t *symbol, uint8_t type);
elf_err_t elf_symbol_set_visibility(elf_symbol_t *symbol, uint8_t visibility);
elf_err_t elf_symbol_set_version(elf_symbol_t *symbol, uint16_t version_index);
uint16_t elf_symbol_version(const elf_symbol_t *symbol);
elf_err_t elf_symbol_set_shndx(elf_symbol_t *symbol, uint16_t shndx);
int elf_symbol_is_duplicate_global(const elfobj_t *obj, const char *name, uint8_t bind);
elf_err_t elf_symbols_sort_deterministic(elfobj_t *obj, size_t *first_global_out);
elf_symbol_t *elf_symbol_lookup_sysv(elfobj_t *obj, const char *name);
elf_symbol_t *elf_symbol_lookup_gnu(elfobj_t *obj, const char *name);

elf_err_t elf_add_relocation(elf_section_t *section, uint64_t offset, elf_symbol_t *symbol,
                             uint32_t type, int64_t addend);
size_t elf_section_reloc_count(const elf_section_t *section);
elf_reloc_t *elf_section_reloc_at(elf_section_t *section, size_t index);
elf_reloc_t *elf_reloc_at(elfobj_t *obj, size_t index);
uint64_t elf_reloc_offset(const elf_reloc_t *reloc);
uint32_t elf_reloc_type(const elf_reloc_t *reloc);
int64_t elf_reloc_addend(const elf_reloc_t *reloc);
int elf_reloc_has_addend(const elf_reloc_t *reloc);
elf_symbol_t *elf_reloc_symbol(const elf_reloc_t *reloc);
elf_section_t *elf_reloc_section(const elf_reloc_t *reloc);
elf_err_t elf_set_reloc_hooks(elfobj_t *obj, const elf_reloc_hooks_t *hooks, void *user);
elf_err_t elf_apply_relocation(const elf_reloc_t *reloc, uint64_t place, uint64_t sym_value,
                               uint64_t *out_value);
elf_err_t elf_apply_relocation_value(const elfobj_t *obj, uint32_t type, uint64_t place,
                                     uint64_t sym_value, int64_t addend, uint64_t *out_value);
int elf_reloc_size_for_machine(uint16_t machine, uint32_t type);
int elf_reloc_is_pc_relative_for_machine(uint16_t machine, uint32_t type);
int elf_reloc_is_tls_for_machine(uint16_t machine, uint32_t type);

elf_err_t elf_link(elfobj_t **inputs, size_t count, elfobj_t **output);
elf_link_plan_t *elf_link_plan_create(void);
void elf_link_plan_destroy(elf_link_plan_t *plan);
elf_err_t elf_link_plan_add_input(elf_link_plan_t *plan, elfobj_t *obj, const char *name);
size_t elf_link_plan_input_count(const elf_link_plan_t *plan);
elf_err_t elf_link_plan_set_section_merge_hook(elf_link_plan_t *plan,
                                               elf_link_section_merge_hook_t hook,
                                               void *user);
elf_err_t elf_link_plan_set_archive_hook(elf_link_plan_t *plan, elf_link_archive_hook_t hook,
                                         void *user);
elf_err_t elf_link_plan_set_gc_hook(elf_link_plan_t *plan, elf_link_gc_hook_t hook, void *user);
elf_err_t elf_link_plan_set_incremental_hook(elf_link_plan_t *plan,
                                              elf_link_incremental_hook_t hook, void *user);
elf_err_t elf_link_plan_set_version_hook(elf_link_plan_t *plan, elf_link_version_hook_t hook,
                                         void *user);
elf_err_t elf_link_plan_consider_archive_member(elf_link_plan_t *plan, const char *archive_path,
                                                 const char *member_name, int *should_extract_out);
elf_err_t elf_link_plan_note_incremental(elf_link_plan_t *plan, const char *key,
                                         const char *value);
elf_err_t elf_link_plan_link(elf_link_plan_t *plan, elfobj_t **output);
size_t elf_link_plan_map_count(const elf_link_plan_t *plan);
int elf_link_plan_map_entry(const elf_link_plan_t *plan, size_t index,
                            elf_link_map_entry_t *out_entry);
elf_err_t elf_link_load_objects(const char **paths, size_t count, elfobj_t ***out_objs,
                                size_t *out_count);
void elf_link_unload_objects(elfobj_t **objs, size_t count);
elf_symbol_t *elf_link_resolve_symbol(elfobj_t **inputs, size_t count, const char *name,
                                      size_t *input_index_out);
elf_section_t *elf_link_add_got_section(elfobj_t *obj, size_t entries);
elf_section_t *elf_link_add_plt_section(elfobj_t *obj, size_t entries);
elf_err_t elf_link_add_dynamic_entry(elfobj_t *obj, int64_t tag, uint64_t value);
elf_err_t elf_validate_ex(elfobj_t *obj, const elf_validate_options_t *options, char **diagnostics);
elf_err_t elf_validate(elfobj_t *obj, char **diagnostics);
elf_err_t elf_set_validation_mode(elfobj_t *obj, elf_validate_mode_t mode);
elf_validate_mode_t elf_get_validation_mode(const elfobj_t *obj);
size_t elf_diag_count(const elfobj_t *obj);
int elf_diag_entry(const elfobj_t *obj, size_t index, elf_diag_entry_t *out);

const char *elf_errstr(elf_err_t err);
elf_err_t elf_last_error(const elfobj_t *obj);
const char *elf_last_diagnostics(const elfobj_t *obj);
elf_err_t elf_register_reloc_backend(const struct elf_reloc_backend *backend);

elfobj_t *elf_create(uint16_t type, uint16_t machine, elfobj_class_t cls, elfobj_endian_t endian);
elfobj_t *elf_init_i386(void);
elfobj_t *elf_init_x86_64(void);
elfobj_t *elf_init_arm(void);
elfobj_t *elf_init_aarch64(void);
elfobj_t *elf_init_mips32(void);
elfobj_t *elf_init_mips64(void);
elfobj_t *elf_init_riscv32(void);
elfobj_t *elf_init_riscv64(void);
elf_err_t elf_finalize(elfobj_t *obj);
elf_err_t elf_set_type(elfobj_t *obj, uint16_t type);
elf_err_t elf_set_machine(elfobj_t *obj, uint16_t machine);
elf_err_t elf_set_osabi(elfobj_t *obj, uint8_t osabi);
elf_err_t elf_set_abiversion(elfobj_t *obj, uint8_t abiversion);
elf_err_t elf_set_flags(elfobj_t *obj, uint32_t flags);
elf_err_t elf_set_entry(elfobj_t *obj, uint64_t entry);
elf_section_t *elf_add_arm_exidx(elfobj_t *obj);
elf_err_t elf_add_arm_attributes(elfobj_t *obj, const void *attrs_data, size_t attrs_size);
elf_err_t elf_add_gnu_property_aarch64(elfobj_t *obj, uint32_t feature_1);
uint16_t elf_type(const elfobj_t *obj);
uint16_t elf_machine(const elfobj_t *obj);
uint8_t elf_osabi(const elfobj_t *obj);
uint8_t elf_abiversion(const elfobj_t *obj);
uint32_t elf_flags(const elfobj_t *obj);
uint64_t elf_entry(const elfobj_t *obj);
elfobj_class_t elf_class(const elfobj_t *obj);
elfobj_endian_t elf_endian(const elfobj_t *obj);

size_t elf_section_count(const elfobj_t *obj);
size_t elf_symbol_count(const elfobj_t *obj);
size_t elf_reloc_count(const elfobj_t *obj);
uint16_t elf_program_header_count(const elfobj_t *obj);
size_t elf_segment_count(const elfobj_t *obj);
uint32_t elf_program_header_type(const elfobj_t *obj, size_t index);
uint32_t elf_program_header_flags(const elfobj_t *obj, size_t index);
uint64_t elf_program_header_align(const elfobj_t *obj, size_t index);
elf_section_t *elf_section_get(const elfobj_t *obj, size_t index);
elf_err_t elf_program_header_set_type(elfobj_t *obj, size_t index, uint32_t type);
elf_err_t elf_program_header_set_flags(elfobj_t *obj, size_t index, uint32_t flags);
elf_err_t elf_program_header_set_align(elfobj_t *obj, size_t index, uint64_t align);

const char *elf_section_name(const elf_section_t *section);
uint32_t elf_section_type(const elf_section_t *section);
uint64_t elf_section_flags(const elf_section_t *section);
uint64_t elf_section_addr(const elf_section_t *section);
uint64_t elf_section_size(const elf_section_t *section);
const void *elf_section_data(const elf_section_t *section, size_t *size_out);

const char *elf_symbol_name(const elf_symbol_t *symbol);
uint8_t elf_symbol_bind(const elf_symbol_t *symbol);
uint8_t elf_symbol_type(const elf_symbol_t *symbol);
uint16_t elf_symbol_shndx(const elf_symbol_t *symbol);
uint64_t elf_symbol_value(const elf_symbol_t *symbol);
uint64_t elf_symbol_size(const elf_symbol_t *symbol);

elf_segment_t *elf_add_segment(elfobj_t *obj, uint32_t type, uint32_t flags, uint64_t align);
elf_segment_t *elf_add_load_segment(elfobj_t *obj, uint32_t flags, uint64_t align);
elf_segment_t *elf_add_dynamic_segment(elfobj_t *obj, uint64_t align);
elf_segment_t *elf_add_tls_segment(elfobj_t *obj, uint64_t align);
elf_segment_t *elf_add_interp_segment(elfobj_t *obj, const char *interp_path);
elf_err_t elf_segment_add_section(elf_segment_t *segment, elf_section_t *section);
uint32_t elf_segment_type(const elf_segment_t *segment);
uint32_t elf_segment_flags(const elf_segment_t *segment);
uint64_t elf_segment_align(const elf_segment_t *segment);
size_t elf_segment_section_count(const elf_segment_t *segment);
int elf_segment_contains_section(const elf_segment_t *segment, const elf_section_t *section);

uint32_t elf_hash_sysv(const char *name);
uint32_t elf_hash_gnu(const char *name);

int elf_section_is_debug(const elf_section_t *section);
int elf_section_is_cfi(const elf_section_t *section);
int elf_section_is_split_dwarf(const elf_section_t *section);
int elf_section_is_compressed_debug(const elf_section_t *section);
elf_err_t elf_debug_set_compression_hint(elf_section_t *section, uint32_t ch_type,
                                         uint64_t uncompressed_size, uint64_t addralign);
int elf_debug_get_compression_hint(const elf_section_t *section, uint32_t *ch_type_out,
                                   uint64_t *uncompressed_size_out, uint64_t *addralign_out);
elf_err_t elf_eh_frame_stats(const elf_section_t *section, size_t *cie_count_out,
                             size_t *fde_count_out);
elf_err_t elf_debug_validate(elfobj_t *obj, char **diagnostics);
elf_err_t elf_debug_sort_sections(elfobj_t *obj);

size_t elf_arm_attribute_count(const elfobj_t *obj);
uint32_t elf_arm_attribute_tag_at(const elfobj_t *obj, size_t index);
uint64_t elf_arm_attribute_value_at(const elfobj_t *obj, size_t index);
const char *elf_arm_attribute_string_at(const elfobj_t *obj, size_t index);
size_t elf_riscv_attribute_count(const elfobj_t *obj);
uint32_t elf_riscv_attribute_tag_at(const elfobj_t *obj, size_t index);
uint64_t elf_riscv_attribute_value_at(const elfobj_t *obj, size_t index);

typedef struct {
    const char *name;
    uint32_t type;
    const void *desc_data;
    size_t desc_size;
} elf_note_info_t;

typedef struct {
    uint32_t type;
    const void *data;
    size_t data_size;
} elf_gnu_property_info_t;

typedef struct {
    uint16_t version;
    uint8_t isa_level;
    uint8_t isa_rev;
    uint8_t gpr_size;
    uint8_t cpr1_size;
    uint8_t cpr2_size;
    uint8_t fp_abi;
    uint32_t isa_ext;
    uint32_t ases;
    uint32_t flags1;
    uint32_t flags2;
} elf_mips_abiflags_t;

size_t elf_note_count(const elfobj_t *obj);
int elf_note_at(const elfobj_t *obj, size_t index, elf_note_info_t *out);
size_t elf_gnu_property_count(const elfobj_t *obj);
int elf_gnu_property_at(const elfobj_t *obj, size_t index, elf_gnu_property_info_t *out);
uint32_t elf_x86_isa_level(const elfobj_t *obj);
uint32_t elf_x86_feature_flags(const elfobj_t *obj);
uint32_t elf_aarch64_feature_flags(const elfobj_t *obj);
elf_err_t elf_add_gnu_property_x86(elfobj_t *obj, uint32_t isa_needed, uint32_t isa_used,
                                   uint32_t feature_1);
int elf_build_id(const elfobj_t *obj, const uint8_t **out_data, size_t *out_size);
int elf_mips_abiflags(const elfobj_t *obj, elf_mips_abiflags_t *out);

const char *elf_reloc_name_for_machine(uint16_t machine, uint32_t type);

#ifdef __cplusplus
}
#endif

#endif
