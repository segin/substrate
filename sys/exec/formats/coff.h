#ifndef _COFF_H
#define _COFF_H

#include <stdint.h>
#include <stddef.h>

// COFF File Header
typedef struct {
    uint16_t f_magic;         // Magic number
    uint16_t f_nscns;         // Number of sections
    int32_t  f_timdat;        // Time & date stamp
    int32_t  f_symptr;        // File pointer to symbol table
    int32_t  f_nsyms;         // Number of symbols
    uint16_t f_opthdr;        // Size of optional header
    uint16_t f_flags;         // Flags
} coff_filehdr_t;

// COFF Optional Header (A.OUT header)
typedef struct {
    uint16_t magic;           // Optional header magic
    uint16_t vstamp;          // Version stamp
    int32_t  tsize;           // Text size
    int32_t  dsize;           // Data size
    int32_t  bsize;           // BSS size
    int32_t  entry;           // Entry point
    int32_t  text_start;      // Base of text
    int32_t  data_start;      // Base of data
} coff_aouthdr_t;

// Relocation information declaration
typedef struct {
    int32_t r_vaddr;          // Address of reference
    int32_t r_symndx;         // Index into symbol table
    uint16_t r_type;          // Relocation type
} coff_reloc_t;

// Symbol Table Entry
typedef struct {
    union {
        char e_name[8];
        struct {
            int32_t e_zeroes;
            int32_t e_offset;
        } e;
    } e;
    int32_t e_value;          // Value of symbol
    int16_t e_scnum;          // Section number
    uint16_t e_type;          // Symbol type
    uint8_t e_sclass;         // Storage class
    uint8_t e_numaux;         // Number of auxiliary entries
} coff_syment_t;

// Section Flags
#define STYP_REG    0x0000
#define STYP_DSECT  0x0001
#define STYP_NOLOAD 0x0002
#define STYP_GROUP  0x0004
#define STYP_PAD    0x0008
#define STYP_COPY   0x0010
#define STYP_TEXT   0x0020
#define STYP_DATA   0x0040
#define STYP_BSS    0x0080

// Relocation Types (i386)
#define R_DIR32     0x0006
#define R_PCRLONG   0x0014

// COFF Section Header
typedef struct {
    char     s_name[8];       // Section name
    int32_t  s_paddr;         // Physical address
    int32_t  s_vaddr;         // Virtual address
    int32_t  s_size;          // Section size
    int32_t  s_scnptr;        // File pointer to raw data
    int32_t  s_relptr;        // File pointer to relocation
    int32_t  s_lnnoptr;       // File pointer to line numbers
    uint16_t s_nreloc;        // Number of relocations
    uint16_t s_nlnno;         // Number of line numbers
    int32_t  s_flags;         // Flags
} coff_scnhdr_t;

#define COFF_MAGIC_I386 0x14c

/*
 * COFF optional-header (a.out-style) magic numbers.  ZMAGIC is the only form
 * shipped by SCO/SVR3 demand-paged binaries; OMAGIC and NMAGIC describe older
 * unpaged or pure-text layouts that the kernel does not load.
 */
#define AOUT_OMAGIC 0x0107
#define AOUT_NMAGIC 0x0108
#define AOUT_ZMAGIC 0x010B

/* COFF page granularity for ZMAGIC layouts (matches SVR3 PAGSIZ). */
#define COFF_PAGE_SIZE 4096U

/*
 * coff_validate_filehdr - cheap structural check on the file header.
 * Returns 0 if the header is well-formed for an i386 COFF executable that
 * is at least file_size bytes long; negative error otherwise.
 */
int coff_validate_filehdr(const coff_filehdr_t *fh, uint32_t file_size);

/*
 * coff_validate_aouthdr - validate the ZMAGIC optional header that follows
 * the file header.  Verifies magic, segment sizes, page alignment, and that
 * the entry point lies within [text_start, text_start + tsize).  Caller is
 * responsible for confirming f_opthdr >= sizeof(coff_aouthdr_t) before call.
 *
 * Returns 0 on success, negative error otherwise.  No allocations, no
 * dependencies on kernel state — host-test friendly.
 */
int coff_validate_aouthdr(const coff_aouthdr_t *opt);

/*
 * coff_apply_relocation - apply a single COFF i386 relocation in place.
 *
 *   section_data     pointer to the section's loaded raw bytes (kernel VA)
 *   section_va       the user VA the section was mapped at (used for PC-rel)
 *   section_size     length in bytes of section_data
 *   r                the relocation record being processed
 *   symbol_value     resolved value for the referenced symbol
 *
 * Supports R_DIR32 (absolute 32-bit) and R_PCRLONG (PC-relative 32-bit).
 * Validates that r->r_vaddr falls within [section_va, section_va+size-4]
 * so the 4-byte fixup cannot run off either end.  Returns 0 on success,
 * -1 on unsupported type or OOB site.
 */
int coff_apply_relocation(uint8_t *section_data, uint32_t section_va,
                          uint32_t section_size, const coff_reloc_t *r,
                          uint32_t symbol_value);

/*
 * coff_apply_relocations - walk an array of relocations and apply each.
 * Identical resolver model: the caller resolves symbol values via
 * resolve(symndx, ctx) which must return the absolute VA of the named
 * symbol (or 0 on failure — cannot be distinguished from a real 0 here,
 * so resolvers should signal failure by writing through *err_out instead
 * via the loader-side wrapper).  Returns 0 if every relocation applied,
 * -1 on the first failure.
 */
typedef uint32_t (*coff_symbol_resolver_t)(int32_t symndx, void *ctx);
int coff_apply_relocations(uint8_t *section_data, uint32_t section_va,
                           uint32_t section_size,
                           const coff_reloc_t *relocs, uint32_t nrelocs,
                           coff_symbol_resolver_t resolve, void *ctx);

int coff_load_file(void *file, uint32_t size);

#endif
