#ifndef _COFF_H
#define _COFF_H

#include <stdint.h>

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

int coff_load_file(void *file, uint32_t size);

#endif
