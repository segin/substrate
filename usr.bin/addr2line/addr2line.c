#include <elfobj.h>
#include <demangle.h>

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ADDR2LINE_HAVE_ZLIB)
#include <zlib.h>
#endif

#define ADDR2LINE_VERSION "0.1.0"

#define DW_FORM_addr 0x01u
#define DW_FORM_data2 0x05u
#define DW_FORM_data4 0x06u
#define DW_FORM_data8 0x07u
#define DW_FORM_string 0x08u
#define DW_FORM_block 0x09u
#define DW_FORM_block1 0x0au
#define DW_FORM_data1 0x0bu
#define DW_FORM_flag 0x0cu
#define DW_FORM_sdata 0x0du
#define DW_FORM_strp 0x0eu
#define DW_FORM_udata 0x0fu
#define DW_FORM_ref_addr 0x10u
#define DW_FORM_ref1 0x11u
#define DW_FORM_ref2 0x12u
#define DW_FORM_ref4 0x13u
#define DW_FORM_ref8 0x14u
#define DW_FORM_ref_udata 0x15u
#define DW_FORM_indirect 0x16u
#define DW_FORM_sec_offset 0x17u
#define DW_FORM_exprloc 0x18u
#define DW_FORM_flag_present 0x19u
#define DW_FORM_strx 0x1au
#define DW_FORM_addrx 0x1bu
#define DW_FORM_data16 0x1eu
#define DW_FORM_line_strp 0x1fu
#define DW_FORM_implicit_const 0x21u
#define DW_FORM_loclistx 0x22u
#define DW_FORM_rnglistx 0x23u
#define DW_FORM_strx1 0x25u
#define DW_FORM_strx2 0x26u
#define DW_FORM_strx3 0x27u
#define DW_FORM_strx4 0x28u
#define DW_FORM_addrx1 0x29u
#define DW_FORM_addrx2 0x2au
#define DW_FORM_addrx3 0x2bu
#define DW_FORM_addrx4 0x2cu

#define DW_TAG_compile_unit 0x11u
#define DW_TAG_inlined_subroutine 0x1du
#define DW_TAG_subprogram 0x2eu

#define DW_AT_name 0x03u
#define DW_AT_stmt_list 0x10u
#define DW_AT_low_pc 0x11u
#define DW_AT_high_pc 0x12u
#define DW_AT_abstract_origin 0x31u
#define DW_AT_specification 0x47u
#define DW_AT_call_column 0x57u
#define DW_AT_call_file 0x58u
#define DW_AT_call_line 0x59u
#define DW_AT_ranges 0x55u
#define DW_AT_linkage_name 0x6eu
#define DW_AT_MIPS_linkage_name 0x2007u

#define DW_LNCT_path 0x1u
#define DW_LNCT_directory_index 0x2u

#define DW_LNS_copy 0x01u
#define DW_LNS_advance_pc 0x02u
#define DW_LNS_advance_line 0x03u
#define DW_LNS_set_file 0x04u
#define DW_LNS_set_column 0x05u
#define DW_LNS_negate_stmt 0x06u
#define DW_LNS_set_basic_block 0x07u
#define DW_LNS_const_add_pc 0x08u
#define DW_LNS_fixed_advance_pc 0x09u
#define DW_LNS_set_prologue_end 0x0au
#define DW_LNS_set_epilogue_begin 0x0bu
#define DW_LNS_set_isa 0x0cu

#define DW_LNE_end_sequence 0x01u
#define DW_LNE_set_address 0x02u
#define DW_LNE_define_file 0x03u
#define DW_LNE_set_discriminator 0x04u

typedef struct {
    const char *exe_path;
    const char **addr_args;
    size_t addr_argc;
    int show_column;
    int show_functions;
    int show_inlines;
    const char *section_name;
    int basenames;
    int demangle;   /* demangle(3) style flags; 0 leaves names as they are */
    int no_recurse_limit;   /* -r: lift the demangler's nesting bound */
    const char *target;     /* -b: BFD target name the file must match */
    int pretty;
    int show_addresses;
} addr2line_opts_t;

typedef struct {
    uint64_t value;
    uint64_t size;
    const char *name;
} func_symbol_t;

typedef struct {
    const char *canonical_name;
    const char *resolved_name;
    const uint8_t *data;
    size_t size;
    uint8_t *owned_data;
    int present;
    int compressed;
} debug_blob_t;

typedef struct {
    char *name;
    uint64_t dir_index;
} line_file_t;

typedef struct {
    uint64_t content_type;
    uint64_t form;
} line_format_t;

typedef struct {
    uint64_t unit_offset;
    uint64_t unit_length;
    uint64_t header_length;
    uint16_t version;
    uint8_t addr_size;
    uint8_t segment_selector_size;
    size_t offset_size;

    uint8_t min_insn_length;
    uint8_t max_ops_per_insn;
    uint8_t default_is_stmt;
    int8_t line_base;
    uint8_t line_range;
    uint8_t opcode_base;

    uint8_t *std_opcode_lengths;
    size_t std_opcode_count;

    char **include_dirs;
    size_t include_dir_count;
    size_t include_dir_cap;

    line_file_t *files;
    size_t file_count;
    size_t file_cap;

    const uint8_t *program;
    size_t program_size;
} line_unit_t;

typedef struct {
    uint64_t address;
    uint32_t file;
    uint32_t line;
    uint32_t column;
    uint8_t is_stmt;
    uint8_t basic_block;
    uint8_t end_sequence;
    uint8_t prologue_end;
    uint8_t epilogue_begin;
    uint32_t isa;
    uint32_t discriminator;
    size_t unit_index;
} line_row_t;

/*
 * One row of a line-number sequence, covering [low, high).  seq_low and
 * seq_high bound the whole sequence the row belongs to; max_high is the
 * largest high of this entry and every entry sorted before it, which
 * lets a lookup stop scanning back once nothing earlier can cover the
 * address.
 */
typedef struct {
    uint64_t low;
    uint64_t high;
    uint64_t seq_low;
    uint64_t seq_high;
    uint64_t max_high;
    uint32_t file;
    uint32_t line;
    uint32_t column;
    size_t unit_index;
} line_entry_t;

typedef struct {
    uint64_t name;
    uint64_t form;
    int64_t implicit_const;
    uint8_t has_implicit_const;
} dwarf_attr_spec_t;

typedef struct {
    uint64_t code;
    uint64_t tag;
    uint8_t has_children;
    dwarf_attr_spec_t *attrs;
    size_t attr_count;
    size_t attr_capacity;
} dwarf_abbrev_t;

typedef struct {
    dwarf_abbrev_t *items;
    size_t count;
    size_t capacity;
} dwarf_abbrev_table_t;

typedef struct {
    uint64_t low;
    uint64_t high;
} addr_range_t;

typedef struct {
    char *name;
    char *linkage_name;
    /* .debug_info offsets of the DIEs this one takes its name from, when
     * it has none of its own (out-of-line C++ member definitions, concrete
     * copies of inline functions); 0 when absent. */
    uint64_t abstract_origin;
    uint64_t specification;
    addr_range_t *ranges;
    size_t range_count;
    size_t range_cap;
} dwarf_subprogram_t;

typedef struct {
    uint64_t die_offset;
    char *name;
    char *linkage_name;
    uint64_t abstract_origin;
    uint64_t specification;
} dwarf_die_name_t;

typedef struct {
    uint64_t die_offset;
    uint64_t abstract_origin;
    uint64_t specification;
    char *name;
    char *linkage_name;
    addr_range_t *ranges;
    size_t range_count;
    size_t range_cap;
    uint32_t call_file;
    uint32_t call_line;
    uint32_t call_column;
    size_t parent_inline;
    size_t line_unit_index;
} dwarf_inline_t;

typedef struct {
    char *name;
    uint64_t addr;
} elf_section_meta_t;

typedef struct {
    elfobj_t *elf;
    char *path;
    elfobj_class_t elf_class;
    elfobj_endian_t elf_endian;
    uint16_t elf_type;

    debug_blob_t debug_line;
    debug_blob_t debug_info;
    debug_blob_t debug_abbrev;
    debug_blob_t debug_str;
    debug_blob_t debug_line_str;
    debug_blob_t debug_ranges;
    debug_blob_t debug_rnglists;

    int has_symtab;
    int has_dynsym;
    func_symbol_t *func_syms;
    size_t func_count;
    size_t func_cap;

    line_unit_t *line_units;
    size_t line_unit_count;
    size_t line_unit_cap;

    line_row_t *line_rows;
    size_t line_row_count;
    size_t line_row_cap;

    line_entry_t *line_entries;
    size_t line_entry_count;
    size_t line_entry_cap;

    dwarf_subprogram_t *subprograms;
    size_t subprogram_count;
    size_t subprogram_cap;

    dwarf_die_name_t *die_names;
    size_t die_name_count;
    size_t die_name_cap;

    dwarf_inline_t *inlines;
    size_t inline_count;
    size_t inline_cap;

    elf_section_meta_t *sections;
    size_t section_count;
    size_t section_cap;
    uint64_t min_load_vaddr;
    int has_min_load_vaddr;
} addr2line_image_t;

static const char *g_progname = "addr2line";
static char *line_path_for_unit_file(const addr2line_image_t *img,
                                     size_t unit_index,
                                     uint32_t file_index);
static const char *path_basename_view(const char *path);
static void output_unresolved(const addr2line_opts_t *opts);
static void emit_frame_result(const addr2line_opts_t *opts,
                              const char *function_name,
                              const char *path,
                              uint32_t line,
                              uint32_t column);
static void emit_unresolved_frame(const addr2line_opts_t *opts);

static void usage(FILE *out) {
    fprintf(out,
            "usage: %s [-afiCpcsrR] [-b target] [-e file] [-j section] [addr ...]\n"
            "       %s --help\n"
            "       %s --version\n",
            g_progname, g_progname, g_progname);
}

static void print_version(void) {
    printf("%s %s\n", g_progname, ADDR2LINE_VERSION);
}

static void warnf(const char *fmt, const char *arg) {
    fprintf(stderr, "%s: ", g_progname);
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
}

static uint32_t rd_u32(const uint8_t *p, elfobj_endian_t e) {
    if (e == ELFOBJ_ENDIAN_BE) {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
               (uint32_t)p[3];
    }
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t rd_u64(const uint8_t *p, elfobj_endian_t e) {
    if (e == ELFOBJ_ENDIAN_BE) {
        return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) |
               ((uint64_t)p[3] << 32) | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
               ((uint64_t)p[6] << 8) | (uint64_t)p[7];
    }
    return ((uint64_t)p[0]) | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

static uint64_t rd_be64(const uint8_t *p) {
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) |
           ((uint64_t)p[3] << 32) | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8) | (uint64_t)p[7];
}

static char *dup_bytes_as_cstr(const uint8_t *data, size_t n) {
    char *s;
    if (data == NULL) {
        return NULL;
    }
    s = (char *)malloc(n + 1u);
    if (s == NULL) {
        return NULL;
    }
    memcpy(s, data, n);
    s[n] = '\0';
    return s;
}

static int read_exact(const uint8_t **pp, const uint8_t *end, size_t n,
                      const uint8_t **out) {
    const uint8_t *p = *pp;
    if ((size_t)(end - p) < n) {
        return -1;
    }
    if (out != NULL) {
        *out = p;
    }
    *pp = p + n;
    return 0;
}

static int read_u8_cursor(const uint8_t **pp, const uint8_t *end, uint8_t *out) {
    const uint8_t *p;
    if (read_exact(pp, end, 1, &p) != 0) {
        return -1;
    }
    *out = p[0];
    return 0;
}

static int read_u16_cursor(const uint8_t **pp, const uint8_t *end,
                           elfobj_endian_t e, uint16_t *out) {
    const uint8_t *p;
    if (read_exact(pp, end, 2, &p) != 0) {
        return -1;
    }
    if (e == ELFOBJ_ENDIAN_BE) {
        *out = (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
    } else {
        *out = (uint16_t)(((uint16_t)p[1] << 8) | p[0]);
    }
    return 0;
}

static int read_u32_cursor(const uint8_t **pp, const uint8_t *end,
                           elfobj_endian_t e, uint32_t *out) {
    const uint8_t *p;
    if (read_exact(pp, end, 4, &p) != 0) {
        return -1;
    }
    *out = rd_u32(p, e);
    return 0;
}

static int read_u64_cursor(const uint8_t **pp, const uint8_t *end,
                           elfobj_endian_t e, uint64_t *out) {
    const uint8_t *p;
    if (read_exact(pp, end, 8, &p) != 0) {
        return -1;
    }
    *out = rd_u64(p, e);
    return 0;
}

static int read_uleb128(const uint8_t **pp, const uint8_t *end, uint64_t *out) {
    uint64_t value = 0;
    unsigned shift = 0;
    const uint8_t *p = *pp;

    while (p < end) {
        uint8_t byte = *p++;
        uint64_t chunk = (uint64_t)(byte & 0x7fu);
        if (shift >= 64u) {
            return -1;
        }
        if (shift == 63u && chunk > 1u) {
            return -1;
        }
        if (shift < 63u && chunk > (UINT64_MAX >> shift)) {
            return -1;
        }
        value |= chunk << shift;
        if ((byte & 0x80u) == 0) {
            *pp = p;
            *out = value;
            return 0;
        }
        shift += 7u;
        if (shift > 63u) {
            return -1;
        }
    }

    return -1;
}

static int read_sleb128(const uint8_t **pp, const uint8_t *end, int64_t *out) {
    uint64_t value = 0;
    unsigned shift = 0;
    uint8_t byte = 0;
    const uint8_t *p = *pp;

    while (p < end) {
        uint64_t chunk;
        byte = *p++;
        chunk = (uint64_t)(byte & 0x7fu);
        if (shift >= 64u) {
            return -1;
        }
        if (shift == 63u && chunk > 1u) {
            return -1;
        }
        if (shift < 63u && chunk > (UINT64_MAX >> shift)) {
            return -1;
        }
        value |= chunk << shift;
        shift += 7u;
        if ((byte & 0x80u) == 0u) {
            break;
        }
        if (shift > 63u) {
            return -1;
        }
    }

    if ((byte & 0x80u) != 0u) {
        return -1;
    }
    if ((byte & 0x40u) != 0u) {
        if (shift < 64u) {
            value |= (~0ull) << shift;
        }
        if ((value & (1ull << 63)) == 0u) {
            return -1;
        }
    } else if ((value & (1ull << 63)) != 0u) {
        return -1;
    }
    *pp = p;
    *out = (int64_t)value;
    return 0;
}

static int read_offset(const uint8_t **pp, const uint8_t *end, elfobj_endian_t e,
                       size_t offset_size, uint64_t *out) {
    if (offset_size == 4u) {
        uint32_t v = 0;
        if (read_u32_cursor(pp, end, e, &v) != 0) {
            return -1;
        }
        *out = (uint64_t)v;
        return 0;
    }
    if (offset_size == 8u) {
        return read_u64_cursor(pp, end, e, out);
    }
    return -1;
}

static int read_cstring_dup(const uint8_t **pp, const uint8_t *end, char **out) {
    const uint8_t *start = *pp;
    const uint8_t *p = start;
    while (p < end && *p != 0) {
        p++;
    }
    if (p >= end) {
        return -1;
    }
    *out = dup_bytes_as_cstr(start, (size_t)(p - start));
    if (*out == NULL) {
        return -1;
    }
    *pp = p + 1;
    return 0;
}

static int line_unit_add_dir(line_unit_t *u, char *dir) {
    char **next;
    if (u->include_dir_count == u->include_dir_cap) {
        size_t new_cap = u->include_dir_cap == 0 ? 16u : u->include_dir_cap * 2u;
        next = (char **)realloc(u->include_dirs, new_cap * sizeof(*next));
        if (next == NULL) {
            free(dir);
            return -1;
        }
        u->include_dirs = next;
        u->include_dir_cap = new_cap;
    }
    u->include_dirs[u->include_dir_count++] = dir;
    return 0;
}

static int line_unit_add_file(line_unit_t *u, line_file_t file) {
    line_file_t *next;
    if (u->file_count == u->file_cap) {
        size_t new_cap = u->file_cap == 0 ? 16u : u->file_cap * 2u;
        next = (line_file_t *)realloc(u->files, new_cap * sizeof(*next));
        if (next == NULL) {
            free(file.name);
            return -1;
        }
        u->files = next;
        u->file_cap = new_cap;
    }
    u->files[u->file_count++] = file;
    return 0;
}

static void line_unit_free(line_unit_t *u) {
    size_t i;
    if (u == NULL) {
        return;
    }

    free(u->std_opcode_lengths);
    for (i = 0; i < u->include_dir_count; ++i) {
        free(u->include_dirs[i]);
    }
    free(u->include_dirs);

    for (i = 0; i < u->file_count; ++i) {
        free(u->files[i].name);
    }
    free(u->files);
    memset(u, 0, sizeof(*u));
}

static void dwarf_subprogram_free(dwarf_subprogram_t *sp) {
    if (sp == NULL) {
        return;
    }
    free(sp->name);
    free(sp->linkage_name);
    free(sp->ranges);
    memset(sp, 0, sizeof(*sp));
}

static int subprogram_add_range(dwarf_subprogram_t *sp, uint64_t low, uint64_t high) {
    addr_range_t *next;
    if (high <= low) {
        return 0;
    }
    if (sp->range_count == sp->range_cap) {
        size_t new_cap = sp->range_cap == 0 ? 4u : sp->range_cap * 2u;
        next = (addr_range_t *)realloc(sp->ranges, new_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        sp->ranges = next;
        sp->range_cap = new_cap;
    }
    sp->ranges[sp->range_count].low = low;
    sp->ranges[sp->range_count].high = high;
    sp->range_count++;
    return 0;
}

static int image_append_subprogram(addr2line_image_t *img, dwarf_subprogram_t *sp) {
    dwarf_subprogram_t *next;
    if (img->subprogram_count == img->subprogram_cap) {
        size_t new_cap = img->subprogram_cap == 0 ? 128u : img->subprogram_cap * 2u;
        next = (dwarf_subprogram_t *)realloc(img->subprograms, new_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        img->subprograms = next;
        img->subprogram_cap = new_cap;
    }
    img->subprograms[img->subprogram_count++] = *sp;
    memset(sp, 0, sizeof(*sp));
    return 0;
}

static void dwarf_inline_free(dwarf_inline_t *inl) {
    if (inl == NULL) {
        return;
    }
    free(inl->name);
    free(inl->linkage_name);
    free(inl->ranges);
    memset(inl, 0, sizeof(*inl));
}

static int inline_add_range(dwarf_inline_t *inl, uint64_t low, uint64_t high) {
    addr_range_t *next;
    if (high <= low) {
        return 0;
    }
    if (inl->range_count == inl->range_cap) {
        size_t new_cap = inl->range_cap == 0 ? 4u : inl->range_cap * 2u;
        next = (addr_range_t *)realloc(inl->ranges, new_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        inl->ranges = next;
        inl->range_cap = new_cap;
    }
    inl->ranges[inl->range_count].low = low;
    inl->ranges[inl->range_count].high = high;
    inl->range_count++;
    return 0;
}

static int image_append_inline(addr2line_image_t *img, dwarf_inline_t *inl) {
    dwarf_inline_t *next;
    if (img->inline_count == img->inline_cap) {
        size_t new_cap = img->inline_cap == 0 ? 64u : img->inline_cap * 2u;
        next = (dwarf_inline_t *)realloc(img->inlines, new_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        img->inlines = next;
        img->inline_cap = new_cap;
    }
    img->inlines[img->inline_count++] = *inl;
    memset(inl, 0, sizeof(*inl));
    return 0;
}

static int image_add_die_name(addr2line_image_t *img,
                              uint64_t die_offset,
                              const char *name,
                              const char *linkage_name,
                              uint64_t abstract_origin,
                              uint64_t specification) {
    dwarf_die_name_t *next;
    dwarf_die_name_t *slot;
    size_t i;
    if ((name == NULL || name[0] == '\0') && (linkage_name == NULL || linkage_name[0] == '\0') &&
        abstract_origin == 0 && specification == 0) {
        return 0;
    }
    for (i = 0; i < img->die_name_count; ++i) {
        if (img->die_names[i].die_offset == die_offset) {
            slot = &img->die_names[i];
            if (slot->name == NULL && name != NULL && name[0] != '\0') {
                slot->name = strdup(name);
                if (slot->name == NULL) {
                    return -1;
                }
            }
            if (slot->linkage_name == NULL && linkage_name != NULL && linkage_name[0] != '\0') {
                slot->linkage_name = strdup(linkage_name);
                if (slot->linkage_name == NULL) {
                    return -1;
                }
            }
            if (slot->abstract_origin == 0) {
                slot->abstract_origin = abstract_origin;
            }
            if (slot->specification == 0) {
                slot->specification = specification;
            }
            return 0;
        }
    }
    if (img->die_name_count == img->die_name_cap) {
        size_t new_cap = img->die_name_cap == 0 ? 128u : img->die_name_cap * 2u;
        next = (dwarf_die_name_t *)realloc(img->die_names, new_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        img->die_names = next;
        img->die_name_cap = new_cap;
    }
    slot = &img->die_names[img->die_name_count++];
    memset(slot, 0, sizeof(*slot));
    slot->die_offset = die_offset;
    slot->abstract_origin = abstract_origin;
    slot->specification = specification;
    if (name != NULL && name[0] != '\0') {
        slot->name = strdup(name);
        if (slot->name == NULL) {
            return -1;
        }
    }
    if (linkage_name != NULL && linkage_name[0] != '\0') {
        slot->linkage_name = strdup(linkage_name);
        if (slot->linkage_name == NULL) {
            return -1;
        }
    }
    return 0;
}

static const dwarf_die_name_t *find_die_name(const addr2line_image_t *img, uint64_t die_offset) {
    size_t i;
    for (i = 0; i < img->die_name_count; ++i) {
        if (img->die_names[i].die_offset == die_offset) {
            return &img->die_names[i];
        }
    }
    return NULL;
}

static int image_append_line_unit(addr2line_image_t *img, line_unit_t *u) {
    line_unit_t *next;
    if (img->line_unit_count == img->line_unit_cap) {
        size_t new_cap = img->line_unit_cap == 0 ? 16u : img->line_unit_cap * 2u;
        next = (line_unit_t *)realloc(img->line_units, new_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        img->line_units = next;
        img->line_unit_cap = new_cap;
    }
    img->line_units[img->line_unit_count++] = *u;
    memset(u, 0, sizeof(*u));
    return 0;
}

static const char *lookup_debug_str(const debug_blob_t *blob, uint64_t off) {
    size_t i;
    if (blob == NULL || !blob->present || blob->data == NULL || off >= blob->size) {
        return NULL;
    }
    for (i = (size_t)off; i < blob->size; ++i) {
        if (blob->data[i] == 0) {
            return (const char *)(blob->data + off);
        }
    }
    return NULL;
}

static int parse_line_form_value(const addr2line_image_t *img,
                                 const uint8_t **pp,
                                 const uint8_t *end,
                                 size_t offset_size,
                                 uint8_t addr_size,
                                 uint64_t form,
                                 char **out_string,
                                 uint64_t *out_u64) {
    uint64_t v = 0;
    *out_string = NULL;
    *out_u64 = 0;

    switch (form) {
    case DW_FORM_string:
        return read_cstring_dup(pp, end, out_string);
    case DW_FORM_data1: {
        uint8_t x = 0;
        if (read_u8_cursor(pp, end, &x) != 0) {
            return -1;
        }
        *out_u64 = x;
        return 0;
    }
    case DW_FORM_data2: {
        uint16_t x = 0;
        if (read_u16_cursor(pp, end, img->elf_endian, &x) != 0) {
            return -1;
        }
        *out_u64 = x;
        return 0;
    }
    case DW_FORM_ref1: {
        uint8_t x = 0;
        if (read_u8_cursor(pp, end, &x) != 0) {
            return -1;
        }
        *out_u64 = x;
        return 0;
    }
    case DW_FORM_ref2: {
        uint16_t x = 0;
        if (read_u16_cursor(pp, end, img->elf_endian, &x) != 0) {
            return -1;
        }
        *out_u64 = x;
        return 0;
    }
    case DW_FORM_data4:
    case DW_FORM_ref4:
    case DW_FORM_strp:
    case DW_FORM_line_strp:
    case DW_FORM_sec_offset: {
        uint32_t x = 0;
        if (offset_size == 8u &&
            (form == DW_FORM_strp || form == DW_FORM_line_strp || form == DW_FORM_sec_offset)) {
            if (read_u64_cursor(pp, end, img->elf_endian, &v) != 0) {
                return -1;
            }
            *out_u64 = v;
            if (form == DW_FORM_strp || form == DW_FORM_line_strp) {
                const debug_blob_t *strsec =
                    (form == DW_FORM_line_strp) ? &img->debug_line_str : &img->debug_str;
                const char *s = lookup_debug_str(strsec, v);
                if (s != NULL) {
                    *out_string = strdup(s);
                    if (*out_string == NULL) {
                        return -1;
                    }
                }
            }
            return 0;
        }
        if (read_u32_cursor(pp, end, img->elf_endian, &x) != 0) {
            return -1;
        }
        *out_u64 = (uint64_t)x;
        if (form == DW_FORM_strp || form == DW_FORM_line_strp) {
            const debug_blob_t *strsec =
                (form == DW_FORM_line_strp) ? &img->debug_line_str : &img->debug_str;
            const char *s = lookup_debug_str(strsec, *out_u64);
            if (s != NULL) {
                *out_string = strdup(s);
                if (*out_string == NULL) {
                    return -1;
                }
            }
        }
        return 0;
    }
    case DW_FORM_data8:
    case DW_FORM_ref8:
        if (read_u64_cursor(pp, end, img->elf_endian, &v) != 0) {
            return -1;
        }
        *out_u64 = v;
        return 0;
    case DW_FORM_udata:
    case DW_FORM_ref_udata:
        return read_uleb128(pp, end, out_u64);
    case DW_FORM_ref_addr:
        return read_offset(pp, end, img->elf_endian, offset_size, out_u64);
    case DW_FORM_addr:
        if (addr_size == 8u) {
            if (read_u64_cursor(pp, end, img->elf_endian, &v) != 0) {
                return -1;
            }
            *out_u64 = v;
            return 0;
        }
        if (addr_size == 4u) {
            uint32_t x = 0;
            if (read_u32_cursor(pp, end, img->elf_endian, &x) != 0) {
                return -1;
            }
            *out_u64 = x;
            return 0;
        }
        return -1;
    default:
        return -1;
    }
}

static int parse_line_v5_table(const addr2line_image_t *img,
                               line_unit_t *u,
                               const uint8_t **pp,
                               const uint8_t *end,
                               int file_table) {
    uint64_t fmt_count = 0;
    uint64_t entry_count = 0;
    line_format_t *fmts = NULL;
    uint64_t i;

    if (read_uleb128(pp, end, &fmt_count) != 0) {
        return -1;
    }
    fmts = (line_format_t *)calloc((size_t)fmt_count, sizeof(*fmts));
    if (fmt_count > 0 && fmts == NULL) {
        return -1;
    }

    for (i = 0; i < fmt_count; ++i) {
        if (read_uleb128(pp, end, &fmts[i].content_type) != 0 ||
            read_uleb128(pp, end, &fmts[i].form) != 0) {
            free(fmts);
            return -1;
        }
    }

    if (read_uleb128(pp, end, &entry_count) != 0) {
        free(fmts);
        return -1;
    }

    for (i = 0; i < entry_count; ++i) {
        char *path = NULL;
        uint64_t dir_index = 0;
        uint64_t j;

        for (j = 0; j < fmt_count; ++j) {
            char *s = NULL;
            uint64_t n = 0;
            if (parse_line_form_value(img,
                                      pp,
                                      end,
                                      u->offset_size,
                                      u->addr_size,
                                      fmts[j].form,
                                      &s,
                                      &n) != 0) {
                free(path);
                free(fmts);
                return -1;
            }

            if (fmts[j].content_type == DW_LNCT_path) {
                free(path);
                path = s;
                s = NULL;
            } else if (file_table && fmts[j].content_type == DW_LNCT_directory_index) {
                dir_index = n;
            }
            free(s);
        }

        if (path == NULL) {
            path = strdup("");
            if (path == NULL) {
                free(fmts);
                return -1;
            }
        }

        if (file_table) {
            line_file_t f;
            f.name = path;
            f.dir_index = dir_index;
            if (line_unit_add_file(u, f) != 0) {
                free(fmts);
                return -1;
            }
        } else {
            if (line_unit_add_dir(u, path) != 0) {
                free(fmts);
                return -1;
            }
        }
    }

    free(fmts);
    return 0;
}

static int parse_line_v2_v4_tables(line_unit_t *u, const uint8_t **pp, const uint8_t *end) {
    while (*pp < end && **pp != 0) {
        char *dir = NULL;
        if (read_cstring_dup(pp, end, &dir) != 0) {
            return -1;
        }
        if (line_unit_add_dir(u, dir) != 0) {
            return -1;
        }
    }
    if (*pp >= end) {
        return -1;
    }
    (*pp)++;

    while (*pp < end && **pp != 0) {
        line_file_t f;
        uint64_t unused = 0;
        memset(&f, 0, sizeof(f));
        if (read_cstring_dup(pp, end, &f.name) != 0 ||
            read_uleb128(pp, end, &f.dir_index) != 0 ||
            read_uleb128(pp, end, &unused) != 0 ||
            read_uleb128(pp, end, &unused) != 0) {
            free(f.name);
            return -1;
        }
        if (line_unit_add_file(u, f) != 0) {
            return -1;
        }
    }
    if (*pp >= end) {
        return -1;
    }
    (*pp)++;
    return 0;
}

static int image_append_line_row(addr2line_image_t *img, const line_row_t *row) {
    line_row_t *next;
    if (img->line_row_count == img->line_row_cap) {
        size_t new_cap = img->line_row_cap == 0 ? 256u : img->line_row_cap * 2u;
        next = (line_row_t *)realloc(img->line_rows, new_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        img->line_rows = next;
        img->line_row_cap = new_cap;
    }
    img->line_rows[img->line_row_count++] = *row;
    return 0;
}

static void line_state_advance_pc(const line_unit_t *u,
                                  uint64_t *address,
                                  uint64_t *op_index,
                                  uint64_t operation_advance) {
    uint64_t max_ops = (uint64_t)(u->max_ops_per_insn == 0 ? 1u : u->max_ops_per_insn);
    uint64_t op = *op_index + operation_advance;
    *address += (uint64_t)u->min_insn_length * (op / max_ops);
    *op_index = op % max_ops;
}

static void line_state_reset_defaults(const line_unit_t *u,
                                      uint64_t *address,
                                      uint64_t *op_index,
                                      int64_t *line,
                                      uint64_t *file,
                                      uint64_t *column,
                                      uint8_t *is_stmt,
                                      uint8_t *basic_block,
                                      uint8_t *prologue_end,
                                      uint8_t *epilogue_begin,
                                      uint64_t *isa,
                                      uint32_t *discriminator) {
    *address = 0;
    *op_index = 0;
    *line = 1;
    *file = 1;
    *column = 0;
    *is_stmt = u->default_is_stmt;
    *basic_block = 0;
    *prologue_end = 0;
    *epilogue_begin = 0;
    *isa = 0;
    *discriminator = 0;
}

static int decode_line_program_standard_ops(addr2line_image_t *img,
                                            line_unit_t *u,
                                            size_t unit_index) {
    const uint8_t *p = u->program;
    const uint8_t *end = u->program + u->program_size;
    uint64_t address = 0;
    uint64_t op_index = 0;
    int64_t line = 1;
    uint64_t file = 1;
    uint64_t column = 0;
    uint8_t is_stmt = u->default_is_stmt;
    uint8_t basic_block = 0;
    uint8_t prologue_end = 0;
    uint8_t epilogue_begin = 0;
    uint64_t isa = 0;
    uint32_t discriminator = 0;

    line_state_reset_defaults(u,
                              &address,
                              &op_index,
                              &line,
                              &file,
                              &column,
                              &is_stmt,
                              &basic_block,
                              &prologue_end,
                              &epilogue_begin,
                              &isa,
                              &discriminator);

    while (p < end) {
        uint8_t opcode = *p++;

        if (opcode == 0u) {
            uint64_t ext_len = 0;
            const uint8_t *ext_end;
            uint8_t ext_opcode = 0;

            if (read_uleb128(&p, end, &ext_len) != 0) {
                return -1;
            }
            if ((uint64_t)(end - p) < ext_len) {
                return -1;
            }
            ext_end = p + (size_t)ext_len;
            if (ext_len == 0u) {
                p = ext_end;
                continue;
            }
            if (read_u8_cursor(&p, ext_end, &ext_opcode) != 0) {
                return -1;
            }

            switch (ext_opcode) {
            case DW_LNE_end_sequence: {
                line_row_t row;
                memset(&row, 0, sizeof(row));
                row.address = address;
                row.file = (uint32_t)file;
                row.line = line > 0 ? (uint32_t)line : 0u;
                row.column = (uint32_t)column;
                row.is_stmt = is_stmt;
                row.basic_block = basic_block;
                row.end_sequence = 1;
                row.prologue_end = prologue_end;
                row.epilogue_begin = epilogue_begin;
                row.isa = (uint32_t)isa;
                row.discriminator = discriminator;
                row.unit_index = unit_index;
                if (image_append_line_row(img, &row) != 0) {
                    return -1;
                }
                line_state_reset_defaults(u,
                                          &address,
                                          &op_index,
                                          &line,
                                          &file,
                                          &column,
                                          &is_stmt,
                                          &basic_block,
                                          &prologue_end,
                                          &epilogue_begin,
                                          &isa,
                                          &discriminator);
                break;
            }
            case DW_LNE_set_address:
                if (u->addr_size == 8u) {
                    uint64_t v = 0;
                    if (read_u64_cursor(&p, ext_end, img->elf_endian, &v) != 0) {
                        return -1;
                    }
                    address = v;
                } else if (u->addr_size == 4u) {
                    uint32_t v = 0;
                    if (read_u32_cursor(&p, ext_end, img->elf_endian, &v) != 0) {
                        return -1;
                    }
                    address = v;
                } else {
                    return -1;
                }
                op_index = 0;
                break;
            case DW_LNE_define_file: {
                line_file_t f;
                uint64_t ignored = 0;
                memset(&f, 0, sizeof(f));
                if (read_cstring_dup(&p, ext_end, &f.name) != 0 ||
                    read_uleb128(&p, ext_end, &f.dir_index) != 0 ||
                    read_uleb128(&p, ext_end, &ignored) != 0 ||
                    read_uleb128(&p, ext_end, &ignored) != 0) {
                    free(f.name);
                    return -1;
                }
                if (line_unit_add_file(u, f) != 0) {
                    return -1;
                }
                break;
            }
            case DW_LNE_set_discriminator: {
                uint64_t v = 0;
                if (read_uleb128(&p, ext_end, &v) != 0) {
                    return -1;
                }
                discriminator = (uint32_t)v;
                break;
            }
            default:
                break;
            }
            p = ext_end;
            continue;
        }

        if (opcode >= u->opcode_base) {
            uint64_t adjusted = (uint64_t)(opcode - u->opcode_base);
            uint64_t op_advance = adjusted / u->line_range;
            int64_t line_inc = (int64_t)u->line_base + (int64_t)(adjusted % u->line_range);
            line_row_t row;

            line_state_advance_pc(u, &address, &op_index, op_advance);
            line += line_inc;

            memset(&row, 0, sizeof(row));
            row.address = address;
            row.file = (uint32_t)file;
            row.line = line > 0 ? (uint32_t)line : 0u;
            row.column = (uint32_t)column;
            row.is_stmt = is_stmt;
            row.basic_block = basic_block;
            row.end_sequence = 0;
            row.prologue_end = prologue_end;
            row.epilogue_begin = epilogue_begin;
            row.isa = (uint32_t)isa;
            row.discriminator = discriminator;
            row.unit_index = unit_index;
            if (image_append_line_row(img, &row) != 0) {
                return -1;
            }
            basic_block = 0;
            prologue_end = 0;
            epilogue_begin = 0;
            discriminator = 0;
            continue;
        }

        switch (opcode) {
        case DW_LNS_copy: {
            line_row_t row;
            memset(&row, 0, sizeof(row));
            row.address = address;
            row.file = (uint32_t)file;
            row.line = line > 0 ? (uint32_t)line : 0u;
            row.column = (uint32_t)column;
            row.is_stmt = is_stmt;
            row.basic_block = basic_block;
            row.end_sequence = 0;
            row.prologue_end = prologue_end;
            row.epilogue_begin = epilogue_begin;
            row.isa = (uint32_t)isa;
            row.discriminator = discriminator;
            row.unit_index = unit_index;
            if (image_append_line_row(img, &row) != 0) {
                return -1;
            }
            basic_block = 0;
            prologue_end = 0;
            epilogue_begin = 0;
            discriminator = 0;
            break;
        }
        case DW_LNS_advance_pc: {
            uint64_t op_advance = 0;
            if (read_uleb128(&p, end, &op_advance) != 0) {
                return -1;
            }
            line_state_advance_pc(u, &address, &op_index, op_advance);
            break;
        }
        case DW_LNS_advance_line: {
            int64_t delta = 0;
            if (read_sleb128(&p, end, &delta) != 0) {
                return -1;
            }
            line += delta;
            break;
        }
        case DW_LNS_set_file: {
            uint64_t v = 0;
            if (read_uleb128(&p, end, &v) != 0) {
                return -1;
            }
            file = v;
            break;
        }
        case DW_LNS_set_column: {
            uint64_t v = 0;
            if (read_uleb128(&p, end, &v) != 0) {
                return -1;
            }
            column = v;
            break;
        }
        case DW_LNS_negate_stmt:
            is_stmt = (uint8_t)!is_stmt;
            break;
        case DW_LNS_set_basic_block:
            basic_block = 1;
            break;
        case DW_LNS_const_add_pc: {
            uint64_t adjusted = 255u - u->opcode_base;
            uint64_t op_advance = adjusted / u->line_range;
            line_state_advance_pc(u, &address, &op_index, op_advance);
            break;
        }
        case DW_LNS_fixed_advance_pc: {
            uint16_t adv = 0;
            if (read_u16_cursor(&p, end, img->elf_endian, &adv) != 0) {
                return -1;
            }
            address += adv;
            op_index = 0;
            break;
        }
        case DW_LNS_set_prologue_end:
            prologue_end = 1;
            break;
        case DW_LNS_set_epilogue_begin:
            epilogue_begin = 1;
            break;
        case DW_LNS_set_isa: {
            uint64_t v = 0;
            if (read_uleb128(&p, end, &v) != 0) {
                return -1;
            }
            isa = v;
            break;
        }
        default: {
            size_t operand_count = 0;
            size_t i;
            if (opcode > 0u && (size_t)(opcode - 1u) < u->std_opcode_count) {
                operand_count = u->std_opcode_lengths[opcode - 1u];
            }
            for (i = 0; i < operand_count; ++i) {
                uint64_t ignored = 0;
                if (read_uleb128(&p, end, &ignored) != 0) {
                    return -1;
                }
            }
            break;
        }
        }
    }

    return 0;
}

static int line_entry_cmp(const void *lhs, const void *rhs) {
    const line_entry_t *a = (const line_entry_t *)lhs;
    const line_entry_t *b = (const line_entry_t *)rhs;
    if (a->low < b->low) {
        return -1;
    }
    if (a->low > b->low) {
        return 1;
    }
    if (a->high < b->high) {
        return -1;
    }
    if (a->high > b->high) {
        return 1;
    }
    if (a->unit_index < b->unit_index) {
        return -1;
    }
    if (a->unit_index > b->unit_index) {
        return 1;
    }
    return 0;
}

static int image_append_line_entry(addr2line_image_t *img, const line_entry_t *entry) {
    line_entry_t *next;
    if (img->line_entry_count == img->line_entry_cap) {
        size_t new_cap = img->line_entry_cap == 0 ? 256u : img->line_entry_cap * 2u;
        next = (line_entry_t *)realloc(img->line_entries, new_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        img->line_entries = next;
        img->line_entry_cap = new_cap;
    }
    img->line_entries[img->line_entry_count++] = *entry;
    return 0;
}

/**
 * @brief Turn the decoded line-number rows into sorted address ranges.
 *
 * Rows are taken one sequence at a time.  A sequence runs from its
 * first row to its DW_LNE_end_sequence row, or, when the line program
 * ends without one, to the address of its last row (as GNU addr2line
 * bounds it): every row but that last one covers the addresses up to
 * the next row, and an unterminated sequence's last row covers nothing.
 * The entries are then sorted by start address and each is given the
 * running maximum of the end addresses before it.
 *
 * @param img Image whose line_rows are converted into line_entries.
 * @return 0 on success, -1 when memory runs out.
 */
static int build_line_entries(addr2line_image_t *img) {
    size_t i = 0;
    size_t k;
    uint64_t max_high = 0;

    while (i < img->line_row_count) {
        size_t start = i;
        size_t end = i;
        uint64_t seq_low = img->line_rows[start].address;
        uint64_t seq_high;
        int terminated;

        /* Find the row that ends this sequence: end_sequence, or the
         * last row before the unit (or the whole table) runs out. */
        while (end < img->line_row_count &&
               img->line_rows[end].unit_index == img->line_rows[start].unit_index &&
               !img->line_rows[end].end_sequence) {
            end++;
        }
        terminated = end < img->line_row_count &&
                     img->line_rows[end].unit_index == img->line_rows[start].unit_index;
        if (terminated) {
            seq_high = img->line_rows[end].address;
            i = end + 1u;
        } else {
            seq_high = img->line_rows[end - 1u].address;
            i = end;
        }

        for (k = start; k < end; ++k) {
            const line_row_t *row = &img->line_rows[k];
            line_entry_t entry;
            if (k + 1u == end && !terminated) {
                break;
            }
            entry.low = row->address;
            entry.high = img->line_rows[k + 1u].address;
            if (entry.high <= entry.low) {
                entry.high = entry.low + 1u;
            }
            entry.seq_low = seq_low;
            entry.seq_high = seq_high;
            entry.max_high = 0;
            entry.file = row->file;
            entry.line = row->line;
            entry.column = row->column;
            entry.unit_index = row->unit_index;
            if (image_append_line_entry(img, &entry) != 0) {
                return -1;
            }
        }
    }

    if (img->line_entry_count > 1u) {
        qsort(img->line_entries,
              img->line_entry_count,
              sizeof(img->line_entries[0]),
              line_entry_cmp);
    }
    for (k = 0; k < img->line_entry_count; ++k) {
        if (img->line_entries[k].high > max_high) {
            max_high = img->line_entries[k].high;
        }
        img->line_entries[k].max_high = max_high;
    }
    return 0;
}

/**
 * @brief Decide which of two line entries covering one address wins.
 *
 * Sequences may overlap (sections discarded by the linker and left at
 * address 0, or the unrelocated sections of an object file).  As GNU
 * addr2line does, the sequence that starts first wins, then the longer
 * one, then the earlier unit.
 *
 * @param a Candidate entry.
 * @param b The best entry so far.
 * @return Non-zero when @p a should be preferred over @p b.
 */
static int line_entry_better(const line_entry_t *a, const line_entry_t *b) {
    if (a->seq_low != b->seq_low) {
        return a->seq_low < b->seq_low;
    }
    if (a->seq_high != b->seq_high) {
        return a->seq_high > b->seq_high;
    }
    return a->unit_index < b->unit_index;
}

/**
 * @brief Find the line entry that covers an address.
 *
 * Binary-searches for the last entry starting at or below @p addr, then
 * scans back over every earlier entry that could still reach it (an
 * earlier entry can be longer than a later one, so a covering entry
 * need not be the nearest), stopping once max_high shows that none can.
 *
 * @param img  Image with sorted line_entries.
 * @param addr Address to look up.
 * @return The covering entry, or NULL when no sequence covers @p addr.
 */
static const line_entry_t *lookup_line_entry(const addr2line_image_t *img, uint64_t addr) {
    size_t lo = 0;
    size_t hi = img->line_entry_count;
    size_t pos;
    const line_entry_t *best = NULL;

    if (img->line_entry_count == 0u) {
        return NULL;
    }

    while (lo < hi) {
        size_t mid = lo + ((hi - lo) / 2u);
        if (img->line_entries[mid].low <= addr) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }

    for (pos = lo; pos > 0u; --pos) {
        const line_entry_t *e = &img->line_entries[pos - 1u];
        if (e->max_high <= addr) {
            break;
        }
        if (addr < e->high && (best == NULL || line_entry_better(e, best))) {
            best = e;
        }
    }
    return best;
}

static size_t find_line_unit_by_stmt_list(const addr2line_image_t *img, uint64_t stmt_list) {
    size_t i;
    for (i = 0; i < img->line_unit_count; ++i) {
        if (img->line_units[i].unit_offset == stmt_list) {
            return i;
        }
    }
    return SIZE_MAX;
}

static int parse_debug_line_units(addr2line_image_t *img) {
    const uint8_t *p = img->debug_line.data;
    const uint8_t *end = img->debug_line.data + img->debug_line.size;

    while (p < end) {
        const uint8_t *unit_start = p;
        line_unit_t u;
        uint32_t len32 = 0;
        uint64_t unit_length = 0;
        const uint8_t *unit_end;
        const uint8_t *header_start;
        const uint8_t *header_end;
        uint64_t header_len = 0;
        uint16_t version = 0;
        uint8_t byte = 0;
        size_t i;

        memset(&u, 0, sizeof(u));

        if ((size_t)(end - p) < 4u) {
            return -1;
        }
        if (read_u32_cursor(&p, end, img->elf_endian, &len32) != 0) {
            return -1;
        }
        if (len32 == 0u) {
            break;
        }
        if (len32 == 0xffffffffu) {
            u.offset_size = 8u;
            if (read_u64_cursor(&p, end, img->elf_endian, &unit_length) != 0) {
                return -1;
            }
        } else {
            u.offset_size = 4u;
            unit_length = len32;
        }
        if ((uint64_t)(end - p) < unit_length) {
            return -1;
        }
        unit_end = p + (size_t)unit_length;

        if (read_u16_cursor(&p, unit_end, img->elf_endian, &version) != 0) {
            return -1;
        }
        if (version < 2u || version > 5u) {
            return -1;
        }
        u.version = version;
        u.unit_offset = (uint64_t)(unit_start - img->debug_line.data);
        u.unit_length = unit_length;
        u.addr_size = (img->elf_class == ELFOBJ_CLASS_64) ? 8u : 4u;
        u.segment_selector_size = 0u;

        if (version >= 5u) {
            if (read_u8_cursor(&p, unit_end, &u.addr_size) != 0 ||
                read_u8_cursor(&p, unit_end, &u.segment_selector_size) != 0) {
                return -1;
            }
        }

        if (read_offset(&p, unit_end, img->elf_endian, u.offset_size, &header_len) != 0) {
            return -1;
        }
        u.header_length = header_len;
        header_start = p;
        if ((uint64_t)(unit_end - p) < header_len) {
            return -1;
        }
        header_end = p + (size_t)header_len;

        if (read_u8_cursor(&p, header_end, &u.min_insn_length) != 0) {
            return -1;
        }
        if (version >= 4u) {
            if (read_u8_cursor(&p, header_end, &u.max_ops_per_insn) != 0) {
                return -1;
            }
        } else {
            u.max_ops_per_insn = 1u;
        }
        if (u.max_ops_per_insn == 0u) {
            u.max_ops_per_insn = 1u;
        }
        if (read_u8_cursor(&p, header_end, &u.default_is_stmt) != 0 ||
            read_u8_cursor(&p, header_end, &byte) != 0 ||
            read_u8_cursor(&p, header_end, &u.line_range) != 0 ||
            read_u8_cursor(&p, header_end, &u.opcode_base) != 0) {
            return -1;
        }
        u.line_base = (int8_t)byte;
        if (u.line_range == 0u || u.opcode_base == 0u) {
            return -1;
        }

        u.std_opcode_count = (size_t)(u.opcode_base - 1u);
        if (u.std_opcode_count > 0u) {
            u.std_opcode_lengths = (uint8_t *)malloc(u.std_opcode_count);
            if (u.std_opcode_lengths == NULL) {
                return -1;
            }
            for (i = 0; i < u.std_opcode_count; ++i) {
                if (read_u8_cursor(&p, header_end, &u.std_opcode_lengths[i]) != 0) {
                    line_unit_free(&u);
                    return -1;
                }
            }
        }

        if (version >= 5u) {
            if (parse_line_v5_table(img, &u, &p, header_end, 0) != 0 ||
                parse_line_v5_table(img, &u, &p, header_end, 1) != 0) {
                line_unit_free(&u);
                return -1;
            }
        } else {
            if (parse_line_v2_v4_tables(&u, &p, header_end) != 0) {
                line_unit_free(&u);
                return -1;
            }
        }

        if (p != header_end) {
            line_unit_free(&u);
            return -1;
        }
        (void)header_start;

        u.program = header_end;
        u.program_size = (size_t)(unit_end - header_end);

        if (image_append_line_unit(img, &u) != 0) {
            line_unit_free(&u);
            return -1;
        }
        if (decode_line_program_standard_ops(img,
                                             &img->line_units[img->line_unit_count - 1u],
                                             img->line_unit_count - 1u) != 0) {
            return -1;
        }
        p = unit_end;
    }

    return 0;
}

static int func_symbol_cmp(const void *lhs, const void *rhs) {
    const func_symbol_t *a = (const func_symbol_t *)lhs;
    const func_symbol_t *b = (const func_symbol_t *)rhs;

    if (a->value < b->value) {
        return -1;
    }
    if (a->value > b->value) {
        return 1;
    }
    if (a->size < b->size) {
        return -1;
    }
    if (a->size > b->size) {
        return 1;
    }
    return strcmp(a->name, b->name);
}

#if defined(ADDR2LINE_HAVE_ZLIB)
static int inflate_payload(const uint8_t *payload, size_t payload_size,
                          uint64_t expected_size, uint8_t **out_data,
                          size_t *out_size) {
    z_stream stream;
    uint8_t *buf;
    size_t cap;
    int rc;

    if (payload == NULL || out_data == NULL || out_size == NULL) {
        return -1;
    }

    cap = expected_size > 0 ? (size_t)expected_size : (payload_size * 8u + 1024u);
    if (cap < 1024u) {
        cap = 1024u;
    }

    buf = (uint8_t *)malloc(cap);
    if (buf == NULL) {
        return -1;
    }

    memset(&stream, 0, sizeof(stream));
    stream.next_in = (Bytef *)payload;
    stream.avail_in = (uInt)payload_size;
    stream.next_out = (Bytef *)buf;
    stream.avail_out = (uInt)cap;

    rc = inflateInit(&stream);
    if (rc != Z_OK) {
        free(buf);
        return -1;
    }

    while (1) {
        rc = inflate(&stream, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) {
            break;
        }
        if (rc != Z_OK) {
            inflateEnd(&stream);
            free(buf);
            return -1;
        }
        if (stream.avail_out == 0) {
            uint8_t *next;
            size_t used = (size_t)stream.total_out;
            size_t new_cap = cap * 2u;
            if (new_cap <= cap) {
                inflateEnd(&stream);
                free(buf);
                return -1;
            }
            next = (uint8_t *)realloc(buf, new_cap);
            if (next == NULL) {
                inflateEnd(&stream);
                free(buf);
                return -1;
            }
            buf = next;
            cap = new_cap;
            stream.next_out = (Bytef *)(buf + used);
            stream.avail_out = (uInt)(cap - used);
        }
    }

    inflateEnd(&stream);

    if (expected_size != 0 && (uint64_t)stream.total_out != expected_size) {
        free(buf);
        return -1;
    }

    *out_data = buf;
    *out_size = (size_t)stream.total_out;
    return 0;
}
#else
static int inflate_payload(const uint8_t *payload, size_t payload_size,
                          uint64_t expected_size, uint8_t **out_data,
                          size_t *out_size) {
    (void)payload;
    (void)payload_size;
    (void)expected_size;
    (void)out_data;
    (void)out_size;
    return -1;
}
#endif

static int decompress_debug_section(const elf_section_t *sec,
                                    elfobj_class_t elf_class,
                                    elfobj_endian_t elf_endian,
                                    const uint8_t *in,
                                    size_t in_size,
                                    uint8_t **out,
                                    size_t *out_size) {
    const char *name;
    const uint8_t *payload;
    size_t payload_size;
    uint64_t expect_size;

    if (sec == NULL || in == NULL || out == NULL || out_size == NULL) {
        return -1;
    }

    name = elf_section_name(sec);
    payload = NULL;
    payload_size = 0;
    expect_size = 0;

    if (name != NULL && strncmp(name, ".zdebug_", 8) == 0) {
        if (in_size < 12) {
            return -1;
        }
        if (memcmp(in, "ZLIB", 4) != 0) {
            return -1;
        }
        expect_size = rd_be64(in + 4);
        payload = in + 12;
        payload_size = in_size - 12;
    } else if ((elf_section_flags(sec) & SHF_COMPRESSED) != 0) {
        uint32_t ch_type;

        if (elf_class == ELFOBJ_CLASS_64) {
            if (in_size < 24) {
                return -1;
            }
            ch_type = rd_u32(in, elf_endian);
            expect_size = rd_u64(in + 8, elf_endian);
            payload = in + 24;
            payload_size = in_size - 24;
        } else {
            if (in_size < 12) {
                return -1;
            }
            ch_type = rd_u32(in, elf_endian);
            expect_size = rd_u32(in + 4, elf_endian);
            payload = in + 12;
            payload_size = in_size - 12;
        }

        if (ch_type != 1u) {
            return -1;
        }
    } else {
        return -1;
    }

    if (inflate_payload(payload, payload_size, expect_size, out, out_size) != 0) {
        return -1;
    }

    return 0;
}

static int load_debug_blob(addr2line_image_t *img, const char *canonical_name,
                           debug_blob_t *blob) {
    elf_section_t *sec;
    size_t size = 0;
    const uint8_t *data;

    memset(blob, 0, sizeof(*blob));
    blob->canonical_name = canonical_name;

    sec = elf_find_section(img->elf, canonical_name);
    if (sec == NULL && strncmp(canonical_name, ".debug_", 7) == 0) {
        char alt_name[128];
        int n = snprintf(alt_name, sizeof(alt_name), ".zdebug_%s", canonical_name + 7);
        if (n > 0 && (size_t)n < sizeof(alt_name)) {
            sec = elf_find_section(img->elf, alt_name);
        }
    }

    if (sec == NULL) {
        return 0;
    }

    data = (const uint8_t *)elf_section_data(sec, &size);
    if (size > 0 && data == NULL) {
        warnf("section has no payload: %s", canonical_name);
        return -1;
    }

    blob->present = 1;
    blob->resolved_name = elf_section_name(sec);

    if (elf_section_is_compressed_debug(sec)) {
        uint8_t *decompressed = NULL;
        size_t decompressed_size = 0;

        if (decompress_debug_section(sec,
                                     img->elf_class,
                                     img->elf_endian,
                                     data,
                                     size,
                                     &decompressed,
                                     &decompressed_size) != 0) {
            warnf("failed to decompress debug section: %s", canonical_name);
            return -1;
        }

        blob->compressed = 1;
        blob->owned_data = decompressed;
        blob->data = decompressed;
        blob->size = decompressed_size;
        return 0;
    }

    blob->data = data;
    blob->size = size;
    return 0;
}

static void image_reset(addr2line_image_t *img) {
    size_t i;

    if (img == NULL) {
        return;
    }

    if (img->debug_line.owned_data != NULL) {
        free(img->debug_line.owned_data);
    }
    if (img->debug_info.owned_data != NULL) {
        free(img->debug_info.owned_data);
    }
    if (img->debug_abbrev.owned_data != NULL) {
        free(img->debug_abbrev.owned_data);
    }
    if (img->debug_str.owned_data != NULL) {
        free(img->debug_str.owned_data);
    }
    if (img->debug_line_str.owned_data != NULL) {
        free(img->debug_line_str.owned_data);
    }
    if (img->debug_ranges.owned_data != NULL) {
        free(img->debug_ranges.owned_data);
    }
    if (img->debug_rnglists.owned_data != NULL) {
        free(img->debug_rnglists.owned_data);
    }

    free(img->func_syms);
    free(img->line_entries);
    free(img->line_rows);
    for (i = 0; i < img->subprogram_count; ++i) {
        dwarf_subprogram_free(&img->subprograms[i]);
    }
    free(img->subprograms);
    for (i = 0; i < img->inline_count; ++i) {
        dwarf_inline_free(&img->inlines[i]);
    }
    free(img->inlines);
    for (i = 0; i < img->die_name_count; ++i) {
        free(img->die_names[i].name);
        free(img->die_names[i].linkage_name);
    }
    free(img->die_names);
    for (i = 0; i < img->section_count; ++i) {
        free(img->sections[i].name);
    }
    free(img->sections);
    for (i = 0; i < img->line_unit_count; ++i) {
        line_unit_free(&img->line_units[i]);
    }
    free(img->line_units);
    free(img->path);

    if (img->elf != NULL) {
        elf_close(img->elf);
    }

    memset(img, 0, sizeof(*img));
}

static void image_clear_line_cache(addr2line_image_t *img) {
    size_t i;
    free(img->line_entries);
    img->line_entries = NULL;
    img->line_entry_count = 0;
    img->line_entry_cap = 0;

    free(img->line_rows);
    img->line_rows = NULL;
    img->line_row_count = 0;
    img->line_row_cap = 0;

    for (i = 0; i < img->line_unit_count; ++i) {
        line_unit_free(&img->line_units[i]);
    }
    free(img->line_units);
    img->line_units = NULL;
    img->line_unit_count = 0;
    img->line_unit_cap = 0;
}

static void image_clear_debug_info_cache(addr2line_image_t *img) {
    size_t i;
    for (i = 0; i < img->subprogram_count; ++i) {
        dwarf_subprogram_free(&img->subprograms[i]);
    }
    free(img->subprograms);
    img->subprograms = NULL;
    img->subprogram_count = 0;
    img->subprogram_cap = 0;

    for (i = 0; i < img->inline_count; ++i) {
        dwarf_inline_free(&img->inlines[i]);
    }
    free(img->inlines);
    img->inlines = NULL;
    img->inline_count = 0;
    img->inline_cap = 0;

    for (i = 0; i < img->die_name_count; ++i) {
        free(img->die_names[i].name);
        free(img->die_names[i].linkage_name);
    }
    free(img->die_names);
    img->die_names = NULL;
    img->die_name_count = 0;
    img->die_name_cap = 0;
}

static int image_append_func_symbol(addr2line_image_t *img,
                                    uint64_t value,
                                    uint64_t size,
                                    const char *name) {
    func_symbol_t *next;

    if (img->func_count == img->func_cap) {
        size_t new_cap = img->func_cap == 0 ? 128u : img->func_cap * 2u;
        next = (func_symbol_t *)realloc(img->func_syms, new_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        img->func_syms = next;
        img->func_cap = new_cap;
    }

    img->func_syms[img->func_count].value = value;
    img->func_syms[img->func_count].size = size;
    img->func_syms[img->func_count].name = name;
    img->func_count++;
    return 0;
}

static int image_collect_symbols(addr2line_image_t *img) {
    size_t i;
    size_t count = elf_symbol_count(img->elf);

    img->has_symtab = elf_find_section(img->elf, ".symtab") != NULL;
    img->has_dynsym = elf_find_section(img->elf, ".dynsym") != NULL;

    for (i = 0; i < count; ++i) {
        elf_symbol_t *sym = elf_symbol_at(img->elf, i);
        const char *name;

        if (sym == NULL) {
            continue;
        }
        if (elf_symbol_type(sym) != STT_FUNC) {
            continue;
        }
        name = elf_symbol_name(sym);
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        if (image_append_func_symbol(img,
                                     elf_symbol_value(sym),
                                     elf_symbol_size(sym),
                                     name) != 0) {
            return -1;
        }
    }

    if (img->func_count > 1) {
        qsort(img->func_syms, img->func_count, sizeof(img->func_syms[0]), func_symbol_cmp);
    }

    return 0;
}

static int image_append_section_meta(addr2line_image_t *img, const char *name, uint64_t addr) {
    elf_section_meta_t *next;
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (img->section_count == img->section_cap) {
        size_t new_cap = img->section_cap == 0 ? 32u : img->section_cap * 2u;
        next = (elf_section_meta_t *)realloc(img->sections, new_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        img->sections = next;
        img->section_cap = new_cap;
    }
    img->sections[img->section_count].name = strdup(name);
    if (img->sections[img->section_count].name == NULL) {
        return -1;
    }
    img->sections[img->section_count].addr = addr;
    img->section_count++;
    return 0;
}

/**
 * @brief Collect section addresses and the lowest PT_LOAD address.
 *
 * Everything comes from the object elf_open() already parsed and
 * validated, so the file is read once: its section table (extended
 * numbering included) and program headers are taken as libelfobj
 * resolved them, not decoded a second time from a fresh read that
 * could see a different file.
 *
 * @param img  Image whose @c elf is open; @c sections and
 *             @c min_load_vaddr are filled in.
 * @return 0 on success, -1 when memory runs out.
 */
static int parse_elf_metadata(addr2line_image_t *img) {
    size_t count;
    size_t i;

    count = elf_program_header_count(img->elf);
    for (i = 0; i < count; ++i) {
        uint64_t vaddr;

        if (elf_program_header_type(img->elf, i) != PT_LOAD) {
            continue;
        }
        vaddr = elf_program_header_vaddr(img->elf, i);
        if (!img->has_min_load_vaddr || vaddr < img->min_load_vaddr) {
            img->min_load_vaddr = vaddr;
            img->has_min_load_vaddr = 1;
        }
    }

    count = elf_section_count(img->elf);
    for (i = 0; i < count; ++i) {
        const elf_section_t *sec = elf_section_get(img->elf, i);

        if (sec == NULL) {
            continue;
        }
        if (image_append_section_meta(img, elf_section_name(sec), elf_section_addr(sec)) != 0) {
            return -1;
        }
    }

    return 0;
}

static int find_section_addr(const addr2line_image_t *img,
                             const char *name,
                             uint64_t *out_addr) {
    size_t i;
    for (i = 0; i < img->section_count; ++i) {
        if (strcmp(img->sections[i].name, name) == 0) {
            *out_addr = img->sections[i].addr;
            return 0;
        }
    }
    return -1;
}

static void dwarf_abbrev_table_free(dwarf_abbrev_table_t *tab) {
    size_t i;
    if (tab == NULL) {
        return;
    }
    for (i = 0; i < tab->count; ++i) {
        free(tab->items[i].attrs);
    }
    free(tab->items);
    memset(tab, 0, sizeof(*tab));
}

static const dwarf_abbrev_t *dwarf_abbrev_lookup(const dwarf_abbrev_table_t *tab,
                                                 uint64_t code) {
    size_t i;
    if (tab == NULL) {
        return NULL;
    }
    for (i = 0; i < tab->count; ++i) {
        if (tab->items[i].code == code) {
            return &tab->items[i];
        }
    }
    return NULL;
}

static int dwarf_abbrev_append(dwarf_abbrev_table_t *tab, dwarf_abbrev_t *ab) {
    if (tab->count >= tab->capacity) {
        size_t new_cap = tab->capacity == 0 ? 16 : tab->capacity * 2;
        dwarf_abbrev_t *next =
            (dwarf_abbrev_t *)realloc(tab->items, new_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        tab->items = next;
        tab->capacity = new_cap;
    }
    tab->items[tab->count++] = *ab;
    memset(ab, 0, sizeof(*ab));
    return 0;
}

static int dwarf_abbrev_add_attr(dwarf_abbrev_t *ab,
                                 uint64_t name,
                                 uint64_t form,
                                 int has_implicit_const,
                                 int64_t implicit_const) {
    if (ab->attr_count >= ab->attr_capacity) {
        size_t new_cap = ab->attr_capacity == 0 ? 8 : ab->attr_capacity * 2;
        dwarf_attr_spec_t *next =
            (dwarf_attr_spec_t *)realloc(ab->attrs, new_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        ab->attrs = next;
        ab->attr_capacity = new_cap;
    }
    ab->attrs[ab->attr_count].name = name;
    ab->attrs[ab->attr_count].form = form;
    ab->attrs[ab->attr_count].has_implicit_const = (uint8_t)(has_implicit_const ? 1 : 0);
    ab->attrs[ab->attr_count].implicit_const = implicit_const;
    ab->attr_count++;
    return 0;
}

static int parse_dwarf_abbrev_table(const addr2line_image_t *img,
                                    uint64_t offset,
                                    dwarf_abbrev_table_t *tab) {
    const uint8_t *p;
    const uint8_t *end;

    memset(tab, 0, sizeof(*tab));

    if (!img->debug_abbrev.present || img->debug_abbrev.data == NULL ||
        offset >= img->debug_abbrev.size) {
        return -1;
    }

    p = img->debug_abbrev.data + offset;
    end = img->debug_abbrev.data + img->debug_abbrev.size;

    while (p < end) {
        uint64_t code = 0;
        dwarf_abbrev_t ab;

        memset(&ab, 0, sizeof(ab));

        if (read_uleb128(&p, end, &code) != 0) {
            dwarf_abbrev_table_free(tab);
            return -1;
        }
        if (code == 0) {
            break;
        }
        ab.code = code;
        if (read_uleb128(&p, end, &ab.tag) != 0 ||
            read_u8_cursor(&p, end, &ab.has_children) != 0) {
            dwarf_abbrev_table_free(tab);
            return -1;
        }
        while (1) {
            uint64_t name = 0;
            uint64_t form = 0;
            int64_t implicit_const = 0;
            if (read_uleb128(&p, end, &name) != 0 || read_uleb128(&p, end, &form) != 0) {
                free(ab.attrs);
                dwarf_abbrev_table_free(tab);
                return -1;
            }
            if (name == 0 && form == 0) {
                break;
            }
            if (form == DW_FORM_implicit_const &&
                read_sleb128(&p, end, &implicit_const) != 0) {
                free(ab.attrs);
                dwarf_abbrev_table_free(tab);
                return -1;
            }
            if (dwarf_abbrev_add_attr(&ab,
                                      name,
                                      form,
                                      form == DW_FORM_implicit_const,
                                      implicit_const) != 0) {
                free(ab.attrs);
                dwarf_abbrev_table_free(tab);
                return -1;
            }
        }
        if (dwarf_abbrev_append(tab, &ab) != 0) {
            free(ab.attrs);
            dwarf_abbrev_table_free(tab);
            return -1;
        }
    }

    return 0;
}

static int read_addr_sized(const uint8_t **pp,
                           const uint8_t *end,
                           elfobj_endian_t e,
                           uint8_t addr_size,
                           uint64_t *out) {
    if (addr_size == 8u) {
        return read_u64_cursor(pp, end, e, out);
    }
    if (addr_size == 4u) {
        uint32_t v = 0;
        if (read_u32_cursor(pp, end, e, &v) != 0) {
            return -1;
        }
        *out = v;
        return 0;
    }
    return -1;
}

static int skip_form_value(const addr2line_image_t *img,
                           const uint8_t **pp,
                           const uint8_t *end,
                           size_t offset_size,
                           uint8_t addr_size,
                           uint16_t version,
                           uint64_t form);

static int skip_form_value(const addr2line_image_t *img,
                           const uint8_t **pp,
                           const uint8_t *end,
                           size_t offset_size,
                           uint8_t addr_size,
                           uint16_t version,
                           uint64_t form) {
    uint64_t len = 0;
    uint64_t dummy = 0;
    const uint8_t *tmp;

    while (form == DW_FORM_indirect) {
        if (read_uleb128(pp, end, &form) != 0) {
            return -1;
        }
    }

    switch (form) {
    case DW_FORM_addr:
        return read_addr_sized(pp, end, img->elf_endian, addr_size, &dummy);
    case DW_FORM_data1:
    case DW_FORM_flag:
    case DW_FORM_ref1:
    case DW_FORM_strx1:
        return read_exact(pp, end, 1u, NULL);
    case DW_FORM_data2:
    case DW_FORM_ref2:
    case DW_FORM_strx2:
        return read_exact(pp, end, 2u, NULL);
    case DW_FORM_data4:
    case DW_FORM_ref4:
    case DW_FORM_strx4:
        return read_exact(pp, end, 4u, NULL);
    case DW_FORM_data8:
    case DW_FORM_ref8:
        return read_exact(pp, end, 8u, NULL);
    case DW_FORM_data16:
        return read_exact(pp, end, 16u, NULL);
    case DW_FORM_udata:
    case DW_FORM_sdata:
    case DW_FORM_ref_udata:
    case DW_FORM_strx:
    case DW_FORM_addrx:
    case DW_FORM_loclistx:
    case DW_FORM_rnglistx:
        return read_uleb128(pp, end, &dummy);
    case DW_FORM_string: {
        char *s = NULL;
        int rc = read_cstring_dup(pp, end, &s);
        free(s);
        return rc;
    }
    case DW_FORM_strp:
    case DW_FORM_sec_offset:
    case DW_FORM_line_strp:
        return read_offset(pp, end, img->elf_endian, offset_size, &dummy);
    case DW_FORM_ref_addr:
        if (version <= 2u) {
            return read_addr_sized(pp, end, img->elf_endian, addr_size, &dummy);
        }
        return read_offset(pp, end, img->elf_endian, offset_size, &dummy);
    case DW_FORM_exprloc:
    case DW_FORM_block:
        if (read_uleb128(pp, end, &len) != 0) {
            return -1;
        }
        return read_exact(pp, end, (size_t)len, &tmp);
    case DW_FORM_block1: {
        uint8_t l = 0;
        if (read_u8_cursor(pp, end, &l) != 0) {
            return -1;
        }
        return read_exact(pp, end, l, &tmp);
    }
    case DW_FORM_flag_present:
    case DW_FORM_implicit_const:
        return 0;
    case DW_FORM_strx3:
        return read_exact(pp, end, 3u, NULL);
    case DW_FORM_addrx1:
        return read_exact(pp, end, 1u, NULL);
    case DW_FORM_addrx2:
        return read_exact(pp, end, 2u, NULL);
    case DW_FORM_addrx3:
        return read_exact(pp, end, 3u, NULL);
    case DW_FORM_addrx4:
        return read_exact(pp, end, 4u, NULL);
    default:
        return -1;
    }
}

static int parse_legacy_ranges(const addr2line_image_t *img,
                               uint64_t offset,
                               uint8_t addr_size,
                               uint64_t base,
                               dwarf_subprogram_t *sp) {
    const uint8_t *p;
    const uint8_t *end;
    uint64_t max_addr = addr_size == 8u ? UINT64_MAX : 0xffffffffu;

    if (!img->debug_ranges.present || offset >= img->debug_ranges.size) {
        return -1;
    }
    p = img->debug_ranges.data + offset;
    end = img->debug_ranges.data + img->debug_ranges.size;

    while (p < end) {
        uint64_t start = 0;
        uint64_t stop = 0;
        if (read_addr_sized(&p, end, img->elf_endian, addr_size, &start) != 0 ||
            read_addr_sized(&p, end, img->elf_endian, addr_size, &stop) != 0) {
            return -1;
        }
        if (start == 0 && stop == 0) {
            break;
        }
        if (start == max_addr) {
            base = stop;
            continue;
        }
        if (subprogram_add_range(sp, base + start, base + stop) != 0) {
            return -1;
        }
    }

    return 0;
}

static int parse_rnglists_ranges(const addr2line_image_t *img,
                                 uint64_t offset,
                                 uint8_t addr_size,
                                 uint64_t base,
                                 dwarf_subprogram_t *sp) {
    const uint8_t *p;
    const uint8_t *end;

    if (!img->debug_rnglists.present || offset >= img->debug_rnglists.size) {
        return -1;
    }
    p = img->debug_rnglists.data + offset;
    end = img->debug_rnglists.data + img->debug_rnglists.size;

    while (p < end) {
        uint8_t kind = 0;
        if (read_u8_cursor(&p, end, &kind) != 0) {
            return -1;
        }
        if (kind == 0u) {
            break;
        }
        if (kind == 4u) {
            uint64_t a = 0;
            uint64_t b = 0;
            if (read_uleb128(&p, end, &a) != 0 || read_uleb128(&p, end, &b) != 0) {
                return -1;
            }
            if (subprogram_add_range(sp, base + a, base + b) != 0) {
                return -1;
            }
            continue;
        }
        if (kind == 5u) {
            if (read_addr_sized(&p, end, img->elf_endian, addr_size, &base) != 0) {
                return -1;
            }
            continue;
        }
        if (kind == 6u) {
            uint64_t a = 0;
            uint64_t b = 0;
            if (read_addr_sized(&p, end, img->elf_endian, addr_size, &a) != 0 ||
                read_addr_sized(&p, end, img->elf_endian, addr_size, &b) != 0) {
                return -1;
            }
            if (subprogram_add_range(sp, a, b) != 0) {
                return -1;
            }
            continue;
        }
        if (kind == 7u) {
            uint64_t a = 0;
            uint64_t len = 0;
            if (read_addr_sized(&p, end, img->elf_endian, addr_size, &a) != 0 ||
                read_uleb128(&p, end, &len) != 0) {
                return -1;
            }
            if (subprogram_add_range(sp, a, a + len) != 0) {
                return -1;
            }
            continue;
        }
        return -1;
    }

    return 0;
}

static int parse_legacy_ranges_inline(const addr2line_image_t *img,
                                      uint64_t offset,
                                      uint8_t addr_size,
                                      uint64_t base,
                                      dwarf_inline_t *inl) {
    const uint8_t *p;
    const uint8_t *end;
    uint64_t max_addr = addr_size == 8u ? UINT64_MAX : 0xffffffffu;

    if (!img->debug_ranges.present || offset >= img->debug_ranges.size) {
        return -1;
    }
    p = img->debug_ranges.data + offset;
    end = img->debug_ranges.data + img->debug_ranges.size;

    while (p < end) {
        uint64_t start = 0;
        uint64_t stop = 0;
        if (read_addr_sized(&p, end, img->elf_endian, addr_size, &start) != 0 ||
            read_addr_sized(&p, end, img->elf_endian, addr_size, &stop) != 0) {
            return -1;
        }
        if (start == 0 && stop == 0) {
            break;
        }
        if (start == max_addr) {
            base = stop;
            continue;
        }
        if (inline_add_range(inl, base + start, base + stop) != 0) {
            return -1;
        }
    }
    return 0;
}

static int parse_rnglists_ranges_inline(const addr2line_image_t *img,
                                        uint64_t offset,
                                        uint8_t addr_size,
                                        uint64_t base,
                                        dwarf_inline_t *inl) {
    const uint8_t *p;
    const uint8_t *end;

    if (!img->debug_rnglists.present || offset >= img->debug_rnglists.size) {
        return -1;
    }
    p = img->debug_rnglists.data + offset;
    end = img->debug_rnglists.data + img->debug_rnglists.size;

    while (p < end) {
        uint8_t kind = 0;
        if (read_u8_cursor(&p, end, &kind) != 0) {
            return -1;
        }
        if (kind == 0u) {
            break;
        }
        if (kind == 4u) {
            uint64_t a = 0;
            uint64_t b = 0;
            if (read_uleb128(&p, end, &a) != 0 || read_uleb128(&p, end, &b) != 0) {
                return -1;
            }
            if (inline_add_range(inl, base + a, base + b) != 0) {
                return -1;
            }
            continue;
        }
        if (kind == 5u) {
            if (read_addr_sized(&p, end, img->elf_endian, addr_size, &base) != 0) {
                return -1;
            }
            continue;
        }
        if (kind == 6u) {
            uint64_t a = 0;
            uint64_t b = 0;
            if (read_addr_sized(&p, end, img->elf_endian, addr_size, &a) != 0 ||
                read_addr_sized(&p, end, img->elf_endian, addr_size, &b) != 0) {
                return -1;
            }
            if (inline_add_range(inl, a, b) != 0) {
                return -1;
            }
            continue;
        }
        if (kind == 7u) {
            uint64_t a = 0;
            uint64_t len = 0;
            if (read_addr_sized(&p, end, img->elf_endian, addr_size, &a) != 0 ||
                read_uleb128(&p, end, &len) != 0) {
                return -1;
            }
            if (inline_add_range(inl, a, a + len) != 0) {
                return -1;
            }
            continue;
        }
        return -1;
    }
    return 0;
}

/**
 * @brief Tell whether a DIE reference form is relative to its unit.
 *
 * DW_FORM_ref1, ref2, ref4, ref8 and ref_udata hold an offset from the
 * first byte of the referencing unit's header; DW_FORM_ref_addr is
 * already an offset into .debug_info.  An implicit constant can only be
 * shared by every unit that uses the abbreviation if it is read the
 * same way, so it is treated as unit-relative too.
 *
 * @param form The attribute's form after DW_FORM_indirect is resolved.
 * @param implicit Nonzero when the value is an abbreviation's implicit
 *        constant.
 * @return 1 when the unit's offset must be added to the value, else 0.
 */
static int dwarf_ref_is_unit_relative(uint64_t form, int implicit) {
    if (implicit) {
        return 1;
    }
    switch (form) {
    case DW_FORM_ref1:
    case DW_FORM_ref2:
    case DW_FORM_ref4:
    case DW_FORM_ref8:
    case DW_FORM_ref_udata:
        return 1;
    default:
        return 0;
    }
}

static int parse_debug_info_subprograms(addr2line_image_t *img) {
    const uint8_t *p;
    const uint8_t *end;

    if (!img->debug_info.present || !img->debug_abbrev.present || img->debug_info.data == NULL) {
        return 0;
    }

    p = img->debug_info.data;
    end = img->debug_info.data + img->debug_info.size;

    while (p < end) {
        const uint8_t *unit_payload;
        const uint8_t *unit_end;
        uint64_t unit_offset = (uint64_t)(p - img->debug_info.data);
        uint32_t len32 = 0;
        uint64_t unit_length = 0;
        size_t offset_size = 4u;
        uint16_t version = 0;
        uint64_t abbrev_off = 0;
        uint8_t addr_size = (img->elf_class == ELFOBJ_CLASS_64) ? 8u : 4u;
        uint8_t unit_type = 0;
        dwarf_abbrev_table_t abbrev;
        uint64_t cu_low_pc = 0;
        int cu_has_low_pc = 0;
        size_t cu_line_unit_index = SIZE_MAX;
        size_t depth = 0;
        size_t inline_stack[257];

        memset(&abbrev, 0, sizeof(abbrev));

        if ((size_t)(end - p) < 4u || read_u32_cursor(&p, end, img->elf_endian, &len32) != 0) {
            return -1;
        }
        if (len32 == 0u) {
            break;
        }
        if (len32 == 0xffffffffu) {
            offset_size = 8u;
            if (read_u64_cursor(&p, end, img->elf_endian, &unit_length) != 0) {
                return -1;
            }
        } else {
            unit_length = len32;
        }
        if ((uint64_t)(end - p) < unit_length) {
            return -1;
        }
        unit_end = p + (size_t)unit_length;
        if (read_u16_cursor(&p, unit_end, img->elf_endian, &version) != 0) {
            return -1;
        }

        if (version >= 5u) {
            if (read_u8_cursor(&p, unit_end, &unit_type) != 0 ||
                read_u8_cursor(&p, unit_end, &addr_size) != 0 ||
                read_offset(&p, unit_end, img->elf_endian, offset_size, &abbrev_off) != 0) {
                return -1;
            }
        } else {
            (void)unit_type;
            if (read_offset(&p, unit_end, img->elf_endian, offset_size, &abbrev_off) != 0 ||
                read_u8_cursor(&p, unit_end, &addr_size) != 0) {
                return -1;
            }
        }

        if (parse_dwarf_abbrev_table(img, abbrev_off, &abbrev) != 0) {
            return -1;
        }
        inline_stack[0] = SIZE_MAX;

        unit_payload = p;
        while (p < unit_end) {
            uint64_t die_offset = (uint64_t)(p - img->debug_info.data);
            uint64_t code = 0;
            const dwarf_abbrev_t *ab;
            size_t ai;
            uint64_t low_pc = 0;
            uint64_t high_pc = 0;
            uint64_t ranges_off = 0;
            uint64_t high_form = 0;
            uint64_t abstract_origin = 0;
            uint64_t specification = 0;
            uint64_t stmt_list = 0;
            int has_low_pc = 0;
            int has_high_pc = 0;
            int has_ranges = 0;
            int has_abstract_origin = 0;
            int has_specification = 0;
            int has_stmt_list = 0;
            uint32_t call_file = 0;
            uint32_t call_line = 0;
            uint32_t call_column = 0;
            int has_call_file = 0;
            int has_call_line = 0;
            int has_call_column = 0;
            size_t parent_inline = inline_stack[depth];
            size_t this_inline = parent_inline;
            char *name = NULL;
            char *linkage_name = NULL;

            (void)unit_payload;

            if (read_uleb128(&p, unit_end, &code) != 0) {
                dwarf_abbrev_table_free(&abbrev);
                return -1;
            }
            if (code == 0) {
                if (depth > 0u) {
                    depth--;
                }
                continue;
            }

            ab = dwarf_abbrev_lookup(&abbrev, code);
            if (ab == NULL) {
                dwarf_abbrev_table_free(&abbrev);
                return -1;
            }

            for (ai = 0; ai < ab->attr_count; ++ai) {
                uint64_t attr_name = ab->attrs[ai].name;
                uint64_t form = ab->attrs[ai].form;
                int implicit = ab->attrs[ai].has_implicit_const;
                int64_t implicit_value = ab->attrs[ai].implicit_const;

                while (form == DW_FORM_indirect) {
                    if (read_uleb128(&p, unit_end, &form) != 0) {
                        free(name);
                        free(linkage_name);
                        dwarf_abbrev_table_free(&abbrev);
                        return -1;
                    }
                }

                if (attr_name == DW_AT_name || attr_name == DW_AT_linkage_name ||
                    attr_name == DW_AT_MIPS_linkage_name) {
                    char *tmp = NULL;
                    uint64_t off = 0;
                    if (implicit) {
                        continue;
                    }
                    if (parse_line_form_value(img,
                                              &p,
                                              unit_end,
                                              offset_size,
                                              addr_size,
                                              form,
                                              &tmp,
                                              &off) != 0) {
                        if (skip_form_value(img,
                                            &p,
                                            unit_end,
                                            offset_size,
                                            addr_size,
                                            version,
                                            form) != 0) {
                            free(name);
                            free(linkage_name);
                            dwarf_abbrev_table_free(&abbrev);
                            return -1;
                        }
                        free(tmp);
                        continue;
                    }
                    if (tmp != NULL) {
                        if (attr_name == DW_AT_name) {
                            free(name);
                            name = tmp;
                        } else {
                            free(linkage_name);
                            linkage_name = tmp;
                        }
                    }
                    continue;
                }

                if (attr_name == DW_AT_low_pc) {
                    char *tmp_str = NULL;
                    if (implicit) {
                        low_pc = (uint64_t)implicit_value;
                        has_low_pc = 1;
                        continue;
                    }
                    if (parse_line_form_value(img,
                                              &p,
                                              unit_end,
                                              offset_size,
                                              addr_size,
                                              form,
                                              &tmp_str,
                                              &low_pc) != 0) {
                        free(tmp_str);
                        if (skip_form_value(img,
                                            &p,
                                            unit_end,
                                            offset_size,
                                            addr_size,
                                            version,
                                            form) != 0) {
                            free(name);
                            free(linkage_name);
                            dwarf_abbrev_table_free(&abbrev);
                            return -1;
                        }
                        continue;
                    }
                    free(tmp_str);
                    has_low_pc = 1;
                    continue;
                }
                if (attr_name == DW_AT_high_pc) {
                    char *tmp_str = NULL;
                    if (implicit) {
                        high_pc = (uint64_t)implicit_value;
                        high_form = DW_FORM_udata;
                        has_high_pc = 1;
                        continue;
                    }
                    if (parse_line_form_value(img,
                                              &p,
                                              unit_end,
                                              offset_size,
                                              addr_size,
                                              form,
                                              &tmp_str,
                                              &high_pc) != 0) {
                        free(tmp_str);
                        if (skip_form_value(img,
                                            &p,
                                            unit_end,
                                            offset_size,
                                            addr_size,
                                            version,
                                            form) != 0) {
                            free(name);
                            free(linkage_name);
                            dwarf_abbrev_table_free(&abbrev);
                            return -1;
                        }
                        continue;
                    }
                    free(tmp_str);
                    high_form = form;
                    has_high_pc = 1;
                    continue;
                }
                if (attr_name == DW_AT_ranges) {
                    char *tmp_str = NULL;
                    if (implicit) {
                        ranges_off = (uint64_t)implicit_value;
                        has_ranges = 1;
                        continue;
                    }
                    if (parse_line_form_value(img,
                                              &p,
                                              unit_end,
                                              offset_size,
                                              addr_size,
                                              form,
                                              &tmp_str,
                                              &ranges_off) != 0) {
                        free(tmp_str);
                        if (skip_form_value(img,
                                            &p,
                                            unit_end,
                                            offset_size,
                                            addr_size,
                                            version,
                                            form) != 0) {
                            free(name);
                            free(linkage_name);
                            dwarf_abbrev_table_free(&abbrev);
                            return -1;
                        }
                        continue;
                    }
                    free(tmp_str);
                    has_ranges = 1;
                    continue;
                }
                if (attr_name == DW_AT_stmt_list) {
                    char *tmp_str = NULL;
                    if (implicit) {
                        stmt_list = (uint64_t)implicit_value;
                        has_stmt_list = 1;
                        continue;
                    }
                    if (parse_line_form_value(img,
                                              &p,
                                              unit_end,
                                              offset_size,
                                              addr_size,
                                              form,
                                              &tmp_str,
                                              &stmt_list) != 0) {
                        free(tmp_str);
                        if (skip_form_value(img,
                                            &p,
                                            unit_end,
                                            offset_size,
                                            addr_size,
                                            version,
                                            form) != 0) {
                            free(name);
                            free(linkage_name);
                            dwarf_abbrev_table_free(&abbrev);
                            return -1;
                        }
                        continue;
                    }
                    free(tmp_str);
                    has_stmt_list = 1;
                    continue;
                }
                if (attr_name == DW_AT_call_file || attr_name == DW_AT_call_line ||
                    attr_name == DW_AT_call_column) {
                    char *tmp_str = NULL;
                    uint64_t v = 0;
                    if (implicit) {
                        v = (uint64_t)implicit_value;
                    } else {
                        if (parse_line_form_value(img,
                                                  &p,
                                                  unit_end,
                                                  offset_size,
                                                  addr_size,
                                                  form,
                                                  &tmp_str,
                                                  &v) != 0) {
                            free(tmp_str);
                            if (skip_form_value(img,
                                                &p,
                                                unit_end,
                                                offset_size,
                                                addr_size,
                                                version,
                                                form) != 0) {
                                free(name);
                                free(linkage_name);
                                dwarf_abbrev_table_free(&abbrev);
                                return -1;
                            }
                            continue;
                        }
                    }
                    free(tmp_str);
                    if (attr_name == DW_AT_call_file) {
                        call_file = (uint32_t)v;
                        has_call_file = 1;
                    } else if (attr_name == DW_AT_call_line) {
                        call_line = (uint32_t)v;
                        has_call_line = 1;
                    } else {
                        call_column = (uint32_t)v;
                        has_call_column = 1;
                    }
                    continue;
                }
                if (attr_name == DW_AT_abstract_origin || attr_name == DW_AT_specification) {
                    char *tmp_str = NULL;
                    uint64_t ref = 0;
                    if (implicit) {
                        ref = (uint64_t)implicit_value;
                    } else {
                        if (parse_line_form_value(img,
                                                  &p,
                                                  unit_end,
                                                  offset_size,
                                                  addr_size,
                                                  form,
                                                  &tmp_str,
                                                  &ref) != 0) {
                            free(tmp_str);
                            if (skip_form_value(img,
                                                &p,
                                                unit_end,
                                                offset_size,
                                                addr_size,
                                                version,
                                                form) != 0) {
                                free(name);
                                free(linkage_name);
                                dwarf_abbrev_table_free(&abbrev);
                                return -1;
                            }
                            continue;
                        }
                    }
                    free(tmp_str);
                    if (dwarf_ref_is_unit_relative(form, implicit)) {
                        ref += unit_offset;
                    }
                    if (attr_name == DW_AT_abstract_origin) {
                        abstract_origin = ref;
                        has_abstract_origin = 1;
                    } else {
                        specification = ref;
                        has_specification = 1;
                    }
                    continue;
                }

                if (implicit) {
                    continue;
                }

                if (skip_form_value(img, &p, unit_end, offset_size, addr_size, version, form) !=
                    0) {
                    free(name);
                    free(linkage_name);
                    dwarf_abbrev_table_free(&abbrev);
                    return -1;
                }
            }

            if (ab->tag == DW_TAG_compile_unit && has_low_pc) {
                cu_low_pc = low_pc;
                cu_has_low_pc = 1;
            }
            if (ab->tag == DW_TAG_compile_unit && has_stmt_list) {
                cu_line_unit_index = find_line_unit_by_stmt_list(img, stmt_list);
            }
            if (image_add_die_name(img,
                                   die_offset,
                                   name,
                                   linkage_name,
                                   has_abstract_origin ? abstract_origin : 0,
                                   has_specification ? specification : 0) != 0) {
                free(name);
                free(linkage_name);
                dwarf_abbrev_table_free(&abbrev);
                return -1;
            }

            if (ab->tag == DW_TAG_subprogram) {
                dwarf_subprogram_t sp;
                memset(&sp, 0, sizeof(sp));
                sp.name = name;
                sp.linkage_name = linkage_name;
                sp.abstract_origin = has_abstract_origin ? abstract_origin : 0;
                sp.specification = has_specification ? specification : 0;
                name = NULL;
                linkage_name = NULL;

                if (has_ranges) {
                    uint64_t base = has_low_pc ? low_pc : (cu_has_low_pc ? cu_low_pc : 0);
                    if (version >= 5u) {
                        (void)parse_rnglists_ranges(img, ranges_off, addr_size, base, &sp);
                    } else {
                        (void)parse_legacy_ranges(img, ranges_off, addr_size, base, &sp);
                    }
                } else if (has_low_pc && has_high_pc) {
                    uint64_t high = high_pc;
                    if (high_form != DW_FORM_addr) {
                        high = low_pc + high_pc;
                    }
                    if (subprogram_add_range(&sp, low_pc, high) != 0) {
                        dwarf_subprogram_free(&sp);
                        dwarf_abbrev_table_free(&abbrev);
                        return -1;
                    }
                }

                if (sp.range_count > 0u && image_append_subprogram(img, &sp) != 0) {
                    dwarf_subprogram_free(&sp);
                    dwarf_abbrev_table_free(&abbrev);
                    return -1;
                }
                dwarf_subprogram_free(&sp);
            } else if (ab->tag == DW_TAG_inlined_subroutine) {
                dwarf_inline_t inl;
                memset(&inl, 0, sizeof(inl));
                inl.die_offset = die_offset;
                inl.abstract_origin = has_abstract_origin ? abstract_origin : 0;
                inl.specification = has_specification ? specification : 0;
                inl.name = name;
                inl.linkage_name = linkage_name;
                inl.call_file = has_call_file ? call_file : 0;
                inl.call_line = has_call_line ? call_line : 0;
                inl.call_column = has_call_column ? call_column : 0;
                inl.parent_inline = parent_inline;
                inl.line_unit_index = cu_line_unit_index;
                name = NULL;
                linkage_name = NULL;

                if (has_ranges) {
                    uint64_t base = has_low_pc ? low_pc : (cu_has_low_pc ? cu_low_pc : 0);
                    if (version >= 5u) {
                        (void)parse_rnglists_ranges_inline(img, ranges_off, addr_size, base, &inl);
                    } else {
                        (void)parse_legacy_ranges_inline(img, ranges_off, addr_size, base, &inl);
                    }
                } else if (has_low_pc && has_high_pc) {
                    uint64_t high = high_pc;
                    if (high_form != DW_FORM_addr) {
                        high = low_pc + high_pc;
                    }
                    if (inline_add_range(&inl, low_pc, high) != 0) {
                        dwarf_inline_free(&inl);
                        dwarf_abbrev_table_free(&abbrev);
                        return -1;
                    }
                }

                if (inl.range_count > 0u) {
                    if (image_append_inline(img, &inl) != 0) {
                        dwarf_inline_free(&inl);
                        dwarf_abbrev_table_free(&abbrev);
                        return -1;
                    }
                    this_inline = img->inline_count - 1u;
                }
                dwarf_inline_free(&inl);
            } else {
                free(name);
                free(linkage_name);
            }

            if (ab->has_children) {
                if (depth >= 256u) {
                    dwarf_abbrev_table_free(&abbrev);
                    return -1;
                }
                depth++;
                inline_stack[depth] = this_inline;
            }
        }

        dwarf_abbrev_table_free(&abbrev);
        p = unit_end;
    }

    return 0;
}

static const char *resolve_die_name_ref(const addr2line_image_t *img, uint64_t die_offset);

/**
 * subprogram_name - name a DW_TAG_subprogram the way GNU addr2line does
 * @img: image whose die_names table holds every named DIE
 * @sp:  subprogram with code
 *
 * Its own DW_AT_linkage_name or DW_AT_name wins.  A subprogram that has
 * neither (an out-of-line C++ member definition, a concrete copy of an
 * inline function) takes the name of the DIE its DW_AT_abstract_origin
 * or DW_AT_specification refers to.
 *
 * Return: the name, or NULL when none can be found.
 */
static const char *subprogram_name(const addr2line_image_t *img, const dwarf_subprogram_t *sp) {
    const char *resolved;
    if (sp->linkage_name != NULL && sp->linkage_name[0] != '\0') {
        return sp->linkage_name;
    }
    if (sp->name != NULL && sp->name[0] != '\0') {
        return sp->name;
    }
    if (sp->abstract_origin != 0) {
        resolved = resolve_die_name_ref(img, sp->abstract_origin);
        if (resolved != NULL) {
            return resolved;
        }
    }
    if (sp->specification != 0) {
        resolved = resolve_die_name_ref(img, sp->specification);
        if (resolved != NULL) {
            return resolved;
        }
    }
    return NULL;
}

static const char *lookup_function_name(const addr2line_image_t *img, uint64_t addr) {
    size_t i;
    const char *best_name = NULL;
    uint64_t best_span = UINT64_MAX;

    for (i = 0; i < img->subprogram_count; ++i) {
        const dwarf_subprogram_t *sp = &img->subprograms[i];
        size_t r;
        for (r = 0; r < sp->range_count; ++r) {
            uint64_t low = sp->ranges[r].low;
            uint64_t high = sp->ranges[r].high;
            if (addr >= low && addr < high && high - low <= best_span) {
                const char *name = subprogram_name(img, sp);
                if (name != NULL) {
                    best_span = high - low;
                    best_name = name;
                }
            }
        }
    }
    if (best_name != NULL) {
        return best_name;
    }

    if (img->func_count > 0u) {
        size_t lo = 0;
        size_t hi = img->func_count;
        while (lo < hi) {
            size_t mid = lo + ((hi - lo) / 2u);
            if (img->func_syms[mid].value <= addr) {
                lo = mid + 1u;
            } else {
                hi = mid;
            }
        }
        if (lo > 0u) {
            const func_symbol_t *sym = &img->func_syms[lo - 1u];
            if (sym->size > 0u && addr >= sym->value && addr < sym->value + sym->size) {
                return sym->name;
            }
        }
    }

    return "??";
}

static const char *resolve_die_name_ref(const addr2line_image_t *img, uint64_t die_offset) {
    const dwarf_die_name_t *ref;
    size_t depth = 0;
    while (die_offset != 0 && depth < 16u) {
        ref = find_die_name(img, die_offset);
        if (ref == NULL) {
            break;
        }
        if (ref->linkage_name != NULL && ref->linkage_name[0] != '\0') {
            return ref->linkage_name;
        }
        if (ref->name != NULL && ref->name[0] != '\0') {
            return ref->name;
        }
        if (ref->abstract_origin != 0) {
            die_offset = ref->abstract_origin;
        } else if (ref->specification != 0) {
            die_offset = ref->specification;
        } else {
            break;
        }
        depth++;
    }
    return NULL;
}

static const char *inline_function_name(const addr2line_image_t *img, const dwarf_inline_t *inl) {
    const char *resolved;
    if (inl->linkage_name != NULL && inl->linkage_name[0] != '\0') {
        return inl->linkage_name;
    }
    if (inl->name != NULL && inl->name[0] != '\0') {
        return inl->name;
    }
    if (inl->abstract_origin != 0) {
        resolved = resolve_die_name_ref(img, inl->abstract_origin);
        if (resolved != NULL) {
            return resolved;
        }
    }
    if (inl->specification != 0) {
        resolved = resolve_die_name_ref(img, inl->specification);
        if (resolved != NULL) {
            return resolved;
        }
    }
    return "??";
}

static size_t find_innermost_inline(const addr2line_image_t *img, uint64_t addr) {
    size_t i;
    size_t best = SIZE_MAX;
    uint64_t best_span = UINT64_MAX;
    size_t best_depth = 0;

    for (i = 0; i < img->inline_count; ++i) {
        const dwarf_inline_t *inl = &img->inlines[i];
        size_t r;
        for (r = 0; r < inl->range_count; ++r) {
            uint64_t low = inl->ranges[r].low;
            uint64_t high = inl->ranges[r].high;
            if (addr >= low && addr < high) {
                uint64_t span = high - low;
                size_t depth = 0;
                size_t p = inl->parent_inline;
                while (p != SIZE_MAX && depth < 256u) {
                    depth++;
                    p = img->inlines[p].parent_inline;
                }
                if (span < best_span || (span == best_span && depth > best_depth)) {
                    best = i;
                    best_span = span;
                    best_depth = depth;
                }
            }
        }
    }

    return best;
}

/**
 * @brief Collect the inlined-subroutine chain covering an address.
 *
 * @param img   Loaded image.
 * @param addr  Address to resolve.
 * @param chain Receives indexes into img->inlines, innermost first.
 * @param cap   Capacity of @p chain.
 * @return Number of entries stored (0 when no inlined subroutine covers
 *         @p addr).
 */
static size_t collect_inline_chain(const addr2line_image_t *img,
                                   uint64_t addr,
                                   size_t *chain,
                                   size_t cap) {
    size_t n = 0;
    size_t idx = find_innermost_inline(img, addr);

    while (idx != SIZE_MAX && n < cap) {
        chain[n++] = idx;
        idx = img->inlines[idx].parent_inline;
    }
    return n;
}

static void print_open_error(const char *path, elf_err_t err) {
    if (err == ELF_ERR_FORMAT) {
        fprintf(stderr, "%s: %s: file format not recognized\n", g_progname, path);
        return;
    }
    fprintf(stderr, "%s: %s: %s\n", g_progname, path, elf_errstr(err));
}

static int image_open(addr2line_image_t *img, const char *path) {
    elf_err_t err;

    memset(img, 0, sizeof(*img));

    img->path = strdup(path);
    if (img->path == NULL) {
        fprintf(stderr, "%s: out of memory\n", g_progname);
        return -1;
    }

    err = elf_open(path, &img->elf);
    if (err != ELF_OK) {
        print_open_error(path, err);
        image_reset(img);
        return -1;
    }

    img->elf_class = elf_class(img->elf);
    img->elf_endian = elf_endian(img->elf);
    img->elf_type = elf_type(img->elf);
    if (parse_elf_metadata(img) != 0) {
        fprintf(stderr, "%s: out of memory\n", g_progname);
        image_reset(img);
        return -1;
    }

    if (load_debug_blob(img, ".debug_line", &img->debug_line) != 0 ||
        load_debug_blob(img, ".debug_info", &img->debug_info) != 0 ||
        load_debug_blob(img, ".debug_abbrev", &img->debug_abbrev) != 0 ||
        load_debug_blob(img, ".debug_str", &img->debug_str) != 0 ||
        load_debug_blob(img, ".debug_line_str", &img->debug_line_str) != 0 ||
        load_debug_blob(img, ".debug_ranges", &img->debug_ranges) != 0 ||
        load_debug_blob(img, ".debug_rnglists", &img->debug_rnglists) != 0) {
        image_reset(img);
        return -1;
    }

    if (image_collect_symbols(img) != 0) {
        fprintf(stderr, "%s: out of memory\n", g_progname);
        image_reset(img);
        return -1;
    }

    if (img->debug_line.present && parse_debug_line_units(img) != 0) {
        warnf("malformed .debug_line section in %s", path);
        image_clear_line_cache(img);
    }
    if (img->debug_line.present && build_line_entries(img) != 0) {
        fprintf(stderr, "%s: out of memory\n", g_progname);
        image_reset(img);
        return -1;
    }
    if (img->debug_info.present && parse_debug_info_subprograms(img) != 0) {
        warnf("malformed .debug_info section in %s", path);
        image_clear_debug_info_cache(img);
    }

    return 0;
}

static int parse_hex_address(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long v;

    if (text == NULL || out == NULL) {
        return -1;
    }

    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }

    if (*text == '\0') {
        return -1;
    }

    errno = 0;
    v = strtoull(text, &end, 16);
    if (errno != 0 || end == text) {
        return -1;
    }
    while (*end != '\0') {
        if (!isspace((unsigned char)*end)) {
            return -1;
        }
        end++;
    }

    *out = (uint64_t)v;
    return 0;
}

/**
 * @brief Print the queried address, as -a asks, ahead of its frames.
 *
 * On a line of its own, or with -p as a "0xADDR: " prefix on the
 * first frame.
 *
 * @param opts  Output options.
 * @param query Address being resolved.
 */
static void emit_query_address(const addr2line_opts_t *opts, uint64_t query) {
    if (opts->show_addresses) {
        printf(opts->pretty ? "0x%llx: " : "0x%llx\n", (unsigned long long)query);
    }
}

/**
 * @brief Print the result for an address argument that does not parse.
 *
 * The output is that of an address that resolves to nothing, in the
 * form the options ask for, so every query still produces the same
 * number of lines.  As GNU addr2line does, the address -a shows is 0.
 *
 * @param opts Output options.
 */
static void output_unresolved(const addr2line_opts_t *opts) {
    emit_query_address(opts, 0);
    emit_unresolved_frame(opts);
}

static const char *path_basename_view(const char *path) {
    const char *slash;
    if (path == NULL) {
        return "??";
    }
    slash = strrchr(path, '/');
    if (slash != NULL && slash[1] != '\0') {
        return slash + 1;
    }
    return path;
}

/**
 * @brief Print one frame of a lookup result.
 *
 * The queried address (-a) and the " (inlined by) " marker are not
 * printed here: output_lookup() prints them ahead of the frame.
 *
 * @param opts          Output options.
 * @param function_name Function to show for this frame ("??" if NULL).
 * @param path          Source file ("??" if NULL).
 * @param line          Source line (0 when unknown).
 * @param column        Source column (0 when unknown).
 */
static void emit_frame_result(const addr2line_opts_t *opts,
                              const char *function_name,
                              const char *path,
                              uint32_t line,
                              uint32_t column) {
    const char *out_path = path != NULL ? path : "??";
    const char *func = function_name != NULL ? function_name : "??";
    const char *display_func = func;
    const char *display_path;
    char *dem = NULL;

    if (opts->basenames) {
        out_path = path_basename_view(out_path);
    }
    if (opts->demangle != 0 && strcmp(func, "??") != 0) {
        dem = demangle(func, opts->demangle |
                                 (opts->no_recurse_limit ? DEMANGLE_NO_RECURSE_LIMIT : 0));
        if (dem != NULL && dem[0] != '\0') {
            display_func = dem;
        }
    }
    display_path = out_path;

    /*
     * As in GNU addr2line, the function is shown only with -f; -p joins
     * it to the location with " at " instead of a newline.
     */
    if (opts->show_functions) {
        fputs(display_func, stdout);
        fputs(opts->pretty ? " at " : "\n", stdout);
    }
    printf("%s:%u", display_path, (unsigned)line);
    if (opts->show_column) {
        printf(":%u", (unsigned)column);
    }
    putchar('\n');

    demangle_free(dem);
}

/**
 * @brief Print the frame of an address that resolves to nothing.
 *
 * GNU addr2line's form for an address it finds no function or line
 * for: "??:0", preceded with -f by a "??" function, on a line of its
 * own or, with -p, followed by a space.  There is no " at ".
 *
 * @param opts Output options.
 */
static void emit_unresolved_frame(const addr2line_opts_t *opts) {
    if (opts->show_functions) {
        fputs(opts->pretty ? "?? " : "??\n", stdout);
    }
    fputs("??:0", stdout);
    if (opts->show_column) {
        fputs(":0", stdout);
    }
    putchar('\n');
}

static uint64_t adjust_query_address(const addr2line_image_t *img,
                                     const addr2line_opts_t *opts,
                                     uint64_t query) {
    uint64_t addr = query;

    if (opts->section_name != NULL) {
        uint64_t sec_addr = 0;
        if (find_section_addr(img, opts->section_name, &sec_addr) == 0) {
            addr += sec_addr;
        }
    }

    if (img->elf_type == ET_DYN) {
        const line_entry_t *direct = lookup_line_entry(img, addr);
        const char *fn = lookup_function_name(img, addr);
        if (direct == NULL && strcmp(fn, "??") == 0) {
            uint64_t page_mask = ~0xfffull;
            uint64_t ref_addr = img->min_load_vaddr;
            uint64_t text_addr = 0;
            if (find_section_addr(img, ".text", &text_addr) == 0 && text_addr != 0) {
                ref_addr = text_addr;
            }
            uint64_t base = (addr & page_mask) - (ref_addr & page_mask);
            if (base <= addr) {
                uint64_t cand = addr - base;
                if (lookup_line_entry(img, cand) != NULL ||
                    strcmp(lookup_function_name(img, cand), "??") != 0) {
                    addr = cand;
                }
            }
        }
    }

    return addr;
}

static char *line_path_for_unit_file(const addr2line_image_t *img,
                                     size_t unit_index,
                                     uint32_t file_index) {
    const line_unit_t *u;
    const line_file_t *f;
    const char *dir = NULL;
    size_t dir_len = 0;
    size_t file_len;
    uint32_t first;
    char *full;

    if (unit_index >= img->line_unit_count) {
        return NULL;
    }
    u = &img->line_units[unit_index];
    /*
     * DWARF 5 numbers both tables from 0: file 0 is the primary source
     * file and directory 0 the compilation directory, and both are
     * stored in the tables.  DWARF 2-4 number them from 1, with 0
     * meaning "no file" and "the compilation directory" (which the
     * header does not list), so the tables start at entry 1.
     */
    first = (u->version >= 5u) ? 0u : 1u;
    if (file_index < first || (size_t)(file_index - first) >= u->file_count) {
        return NULL;
    }
    f = &u->files[file_index - first];
    if (f->name == NULL) {
        return NULL;
    }
    file_len = strlen(f->name);

    if (f->dir_index >= first &&
        f->dir_index - first < (uint64_t)u->include_dir_count &&
        f->name[0] != '/') {
        dir = u->include_dirs[f->dir_index - first];
        if (dir != NULL && dir[0] != '\0') {
            dir_len = strlen(dir);
        }
    }

    if (dir_len == 0u) {
        return strdup(f->name);
    }

    full = (char *)malloc(dir_len + 1u + file_len + 1u);
    if (full == NULL) {
        return NULL;
    }
    memcpy(full, dir, dir_len);
    full[dir_len] = '/';
    memcpy(full + dir_len + 1u, f->name, file_len + 1u);
    return full;
}

static char *line_entry_path(const addr2line_image_t *img, const line_entry_t *entry) {
    if (entry == NULL) {
        return NULL;
    }
    return line_path_for_unit_file(img, entry->unit_index, entry->file);
}

static void output_lookup(const addr2line_image_t *img,
                          uint64_t query,
                          const addr2line_opts_t *opts) {
    const line_entry_t *entry = lookup_line_entry(img, query);
    const char *func = lookup_function_name(img, query);
    size_t chain[256];
    size_t n = collect_inline_chain(img, query, chain, 256u);
    const char *inner;
    char *path = NULL;
    size_t k;

    emit_query_address(opts, query);

    /*
     * Frame 0 is the innermost function (the deepest inlined subroutine,
     * or the subprogram itself) at the .debug_line location.
     */
    inner = n > 0u ? inline_function_name(img, &img->inlines[chain[0]]) : func;
    if (entry != NULL) {
        path = line_entry_path(img, entry);
    }
    if ((path == NULL || path[0] == '\0') && n == 0u && strcmp(inner, "??") == 0) {
        /* Neither a line nor a function: the address resolves to nothing. */
        free(path);
        emit_unresolved_frame(opts);
        return;
    }
    if (path == NULL || path[0] == '\0') {
        emit_frame_result(opts, inner, "??", 0, 0);
    } else {
        emit_frame_result(opts, inner, path, entry->line, entry->column);
    }
    free(path);
    if (!opts->show_inlines) {
        return;
    }

    /*
     * Each outer frame k is the function that frame k-1 was inlined
     * into, at the call site frame k-1 records; frame n is the
     * enclosing subprogram.
     */
    for (k = 1; k <= n; ++k) {
        const dwarf_inline_t *callee = &img->inlines[chain[k - 1u]];
        const char *name = k < n ? inline_function_name(img, &img->inlines[chain[k]]) : func;

        path = line_path_for_unit_file(img, callee->line_unit_index, callee->call_file);
        if (opts->pretty) {
            fputs(" (inlined by) ", stdout);
        }
        emit_frame_result(opts, name, path != NULL ? path : "??",
                          callee->call_line, callee->call_column);
        free(path);
    }
}

static int resolve_stdin(const addr2line_image_t *img, const addr2line_opts_t *opts) {
    char line[256];

    while (fgets(line, sizeof(line), stdin) != NULL) {
        uint64_t query;
        if (parse_hex_address(line, &query) != 0) {
            warnf("invalid address: %s", line);
            output_unresolved(opts);
            continue;
        }
        output_lookup(img, adjust_query_address(img, opts, query), opts);
    }

    return 0;
}

static int resolve_argv(const addr2line_image_t *img,
                        const addr2line_opts_t *opts,
                        const char **args,
                        size_t n) {
    size_t i;

    for (i = 0; i < n; ++i) {
        uint64_t query;
        if (parse_hex_address(args[i], &query) != 0) {
            warnf("invalid address: %s", args[i]);
            output_unresolved(opts);
            continue;
        }
        output_lookup(img, adjust_query_address(img, opts, query), opts);
    }

    return 0;
}

/* Whether a long option takes an argument. */
enum {
    A2L_ARG_NONE,
    A2L_ARG_REQUIRED,
    A2L_ARG_OPTIONAL
};

/* A long option, and the short option it is the same as. */
typedef struct {
    const char *name;
    int has_arg;
    int key;
} a2l_longopt_t;

static const a2l_longopt_t g_longopts[] = {
    { "addresses", A2L_ARG_NONE, 'a' },
    { "basenames", A2L_ARG_NONE, 's' },
    { "demangle", A2L_ARG_OPTIONAL, 'C' },
    { "exe", A2L_ARG_REQUIRED, 'e' },
    { "functions", A2L_ARG_NONE, 'f' },
    { "help", A2L_ARG_NONE, 'h' },
    { "inlines", A2L_ARG_NONE, 'i' },
    { "pretty-print", A2L_ARG_NONE, 'p' },
    { "recurse-limit", A2L_ARG_NONE, 'R' },
    { "recursion-limit", A2L_ARG_NONE, 'R' },
    { "no-recurse-limit", A2L_ARG_NONE, 'r' },
    { "no-recursion-limit", A2L_ARG_NONE, 'r' },
    { "section", A2L_ARG_REQUIRED, 'j' },
    { "target", A2L_ARG_REQUIRED, 'b' },
    { "version", A2L_ARG_NONE, 'V' },
    { NULL, A2L_ARG_NONE, 0 }
};

/* Short options; a ':' follows each that takes an argument. */
static const char g_shortopts[] = "ab:cCe:fhHij:prRsvV";

/**
 * @brief Map a --demangle=STYLE name to demangle(3) flags.
 *
 * The names are those GNU addr2line accepts.  "none" turns demangling
 * off; "gnat" is accepted, but there is no Ada demangler, so it too
 * leaves names as they are.
 *
 * @param style Style name.
 * @param flags Set to the demangle(3) flags, 0 for no demangling.
 * @return 0 on success, -1 if @p style is not a known style.
 */
static int demangle_style_flags(const char *style, int *flags) {
    static const struct {
        const char *name;
        int flags;
    } styles[] = {
        { "none", 0 },
        { "auto", DEMANGLE_AUTO },
        { "gnu-v3", DEMANGLE_ITANIUM },
        { "java", DEMANGLE_ITANIUM },
        { "gnat", 0 },
        { "dlang", DEMANGLE_DLANG },
        { "rust", DEMANGLE_RUST },
    };
    size_t k;

    for (k = 0; k < sizeof(styles) / sizeof(styles[0]); ++k) {
        if (strcmp(style, styles[k].name) == 0) {
            *flags = styles[k].flags;
            return 0;
        }
    }
    return -1;
}

/**
 * @brief Apply one parsed option.
 *
 * -h and -V print their text and exit here.
 *
 * @param opts  Options being filled in.
 * @param key   Short option letter (a long option's short equivalent).
 * @param value Option argument, or NULL when there is none.
 * @return 0 on success, -1 on a bad argument (already reported).
 */
static int apply_option(addr2line_opts_t *opts, int key, const char *value) {
    switch (key) {
    case 'a':
        opts->show_addresses = 1;
        break;
    case 'b':
        opts->target = value;
        break;
    case 'c':
        opts->show_column = 1;
        break;
    case 'C':
        if (value == NULL) {
            opts->demangle = DEMANGLE_AUTO;
        } else if (demangle_style_flags(value, &opts->demangle) != 0) {
            fprintf(stderr, "%s: unknown demangling style `%s'\n", g_progname, value);
            return -1;
        }
        break;
    case 'e':
        opts->exe_path = value;
        break;
    case 'f':
        opts->show_functions = 1;
        break;
    case 'h':
    case 'H':
        usage(stdout);
        exit(0);
    case 'i':
        opts->show_inlines = 1;
        break;
    case 'j':
        opts->section_name = value;
        break;
    case 'p':
        opts->pretty = 1;
        break;
    case 'r':
        opts->no_recurse_limit = 1;
        break;
    case 'R':
        opts->no_recurse_limit = 0;
        break;
    case 's':
        opts->basenames = 1;
        break;
    case 'v':
    case 'V':
        print_version();
        exit(0);
    default:
        return -1;
    }
    return 0;
}

/**
 * @brief Parse one "--name[=value]" argument.
 *
 * A name may be abbreviated to any prefix that names one option only;
 * as with getopt_long(3), a prefix of several names that all mean the
 * same option (--rec for --recurse-limit and --recursion-limit) is not
 * ambiguous.  An option that requires an argument takes it after '=' or, failing
 * that, from the next argument; an optional argument is only taken
 * after '='.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param i    Index of the argument; advanced past a separate value.
 * @param opts Options being filled in.
 * @return 0 on success, -1 on error (already reported).
 */
static int parse_long_option(int argc, char **argv, int *i, addr2line_opts_t *opts) {
    const char *arg = argv[*i];
    const char *name = arg + 2;
    const char *eq = strchr(name, '=');
    size_t len = eq != NULL ? (size_t)(eq - name) : strlen(name);
    const a2l_longopt_t *match = NULL;
    const char *value = NULL;
    size_t k;
    int ambiguous = 0;

    for (k = 0; g_longopts[k].name != NULL; ++k) {
        if (strncmp(g_longopts[k].name, name, len) != 0) {
            continue;
        }
        if (g_longopts[k].name[len] == '\0') {
            match = &g_longopts[k];
            ambiguous = 0;
            break;
        }
        if (match == NULL) {
            match = &g_longopts[k];
        } else if (match->key != g_longopts[k].key ||
                   match->has_arg != g_longopts[k].has_arg) {
            ambiguous = 1;
        }
    }
    if (match == NULL || len == 0u) {
        fprintf(stderr, "%s: unknown option: %s\n", g_progname, arg);
        return -1;
    }
    if (ambiguous) {
        fprintf(stderr, "%s: option '%.*s' is ambiguous\n", g_progname,
                (int)(len + 2u), arg);
        return -1;
    }

    if (eq != NULL) {
        if (match->has_arg == A2L_ARG_NONE) {
            fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n",
                    g_progname, match->name);
            return -1;
        }
        value = eq + 1;
    } else if (match->has_arg == A2L_ARG_REQUIRED) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "%s: option '--%s' requires an argument\n",
                    g_progname, match->name);
            return -1;
        }
        (*i)++;
        value = argv[*i];
    }
    return apply_option(opts, match->key, value);
}

/**
 * @brief Parse one argument of bundled short options, such as "-fpCe".
 *
 * -b, -e and -j take the rest of the argument as their value ("-ea.out")
 * or, when nothing follows them, the next argument.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param i    Index of the argument; advanced past a separate value.
 * @param opts Options being filled in.
 * @return 0 on success, -1 on error (already reported).
 */
static int parse_short_options(int argc, char **argv, int *i, addr2line_opts_t *opts) {
    const char *p;

    for (p = argv[*i] + 1; *p != '\0'; ++p) {
        const char *spec = *p != ':' ? strchr(g_shortopts, *p) : NULL;

        if (spec == NULL) {
            fprintf(stderr, "%s: unknown option: -%c\n", g_progname, *p);
            return -1;
        }
        if (spec[1] == ':') {
            const char *value = p + 1;
            if (*value == '\0') {
                if (*i + 1 >= argc) {
                    fprintf(stderr, "%s: option requires an argument -- '%c'\n",
                            g_progname, *p);
                    return -1;
                }
                (*i)++;
                value = argv[*i];
            }
            return apply_option(opts, *p, value);
        }
        if (apply_option(opts, *p, NULL) != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief Parse the command line into @p opts.
 *
 * As GNU getopt_long(3) does, options and addresses may be mixed in
 * any order ("-e a.out 0x401000 -f"): every argument that is not an
 * option is an address, in the order given.  "--" ends the options,
 * and so, when POSIXLY_CORRECT is set, does the first address.  A lone
 * "-" is an address, not an option.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param opts Filled in; @c addr_args is allocated and must be freed.
 * @return 0 on success, -1 on a bad option or no memory (already
 *         reported).
 */
static int parse_options(int argc, char **argv, addr2line_opts_t *opts) {
    const char **addrs;
    size_t n = 0;
    int posix = getenv("POSIXLY_CORRECT") != NULL;
    int i;

    memset(opts, 0, sizeof(*opts));
    opts->exe_path = "a.out";

    addrs = (const char **)calloc(argc > 0 ? (size_t)argc : 1u, sizeof(*addrs));
    if (addrs == NULL) {
        fprintf(stderr, "%s: out of memory\n", g_progname);
        return -1;
    }

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        }
        if (arg[0] != '-' || arg[1] == '\0') {
            if (posix) {
                break;
            }
            addrs[n++] = arg;
            continue;
        }
        if (arg[1] == '-') {
            if (parse_long_option(argc, argv, &i, opts) != 0) {
                free(addrs);
                return -1;
            }
        } else if (parse_short_options(argc, argv, &i, opts) != 0) {
            free(addrs);
            return -1;
        }
    }
    for (; i < argc; ++i) {
        addrs[n++] = argv[i];
    }

    opts->addr_args = addrs;
    opts->addr_argc = n;
    return 0;
}

/**
 * @brief Check the file against the format named with -b.
 *
 * The names are GNU's BFD target names for ELF: a named machine must
 * match e_machine, and every name fixes the class and byte order.
 * "default" accepts any file.  A name that is not an ELF target this
 * tool reads is reported as GNU reports an unknown one.
 *
 * @param img    Open image.
 * @param target Target name from -b or --target.
 * @return 0 if the file is in that format, -1 if not (already
 *         reported).
 */
static int check_target(const addr2line_image_t *img, const char *target) {
    static const struct {
        const char *name;
        elfobj_class_t elf_class;
        elfobj_endian_t endian;
        uint16_t machine;   /* 0: any machine */
    } targets[] = {
        { "elf64-x86-64", ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE, EM_X86_64 },
        { "elf32-x86-64", ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE, EM_X86_64 },
        { "elf32-i386", ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE, EM_386 },
        { "elf64-little", ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE, 0 },
        { "elf64-big", ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_BE, 0 },
        { "elf32-little", ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE, 0 },
        { "elf32-big", ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_BE, 0 },
    };
    size_t k;

    if (strcmp(target, "default") == 0) {
        return 0;
    }
    for (k = 0; k < sizeof(targets) / sizeof(targets[0]); ++k) {
        if (strcmp(target, targets[k].name) != 0) {
            continue;
        }
        if (img->elf_class != targets[k].elf_class ||
            img->elf_endian != targets[k].endian ||
            (targets[k].machine != 0 && elf_machine(img->elf) != targets[k].machine)) {
            fprintf(stderr, "%s: %s: file format not recognized\n", g_progname, img->path);
            return -1;
        }
        return 0;
    }
    fprintf(stderr, "%s: %s: invalid bfd target\n", g_progname, img->path);
    return -1;
}

int main(int argc, char **argv) {
    addr2line_opts_t opts;
    addr2line_image_t img;
    int rc;

    if (argc > 0 && argv[0] != NULL && argv[0][0] != '\0') {
        g_progname = argv[0];
    }

    if (parse_options(argc, argv, &opts) != 0) {
        usage(stderr);
        return 1;
    }

    if (image_open(&img, opts.exe_path) != 0) {
        free(opts.addr_args);
        return 1;
    }
    if (opts.target != NULL && check_target(&img, opts.target) != 0) {
        image_reset(&img);
        free(opts.addr_args);
        return 1;
    }
    if (opts.section_name != NULL) {
        uint64_t sec_addr = 0;
        if (find_section_addr(&img, opts.section_name, &sec_addr) != 0) {
            fprintf(stderr, "%s: unknown section: %s\n", g_progname, opts.section_name);
            image_reset(&img);
            free(opts.addr_args);
            return 1;
        }
    }

    if (opts.addr_argc == 0) {
        rc = resolve_stdin(&img, &opts);
    } else {
        rc = resolve_argv(&img, &opts, opts.addr_args, opts.addr_argc);
    }

    image_reset(&img);
    free(opts.addr_args);
    return rc;
}
