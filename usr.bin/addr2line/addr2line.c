#include <elfobj.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ADDR2LINE_HAVE_ZLIB)
#include <zlib.h>
#endif

#define ADDR2LINE_VERSION "0.1.0"

#define DW_FORM_addr 0x01u
#define DW_FORM_data1 0x0bu
#define DW_FORM_data2 0x05u
#define DW_FORM_data4 0x06u
#define DW_FORM_data8 0x07u
#define DW_FORM_string 0x08u
#define DW_FORM_strp 0x0eu
#define DW_FORM_udata 0x0fu
#define DW_FORM_sec_offset 0x17u
#define DW_FORM_line_strp 0x1fu

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

    line_file_t *files;
    size_t file_count;

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
} addr2line_image_t;

static const char *g_progname = "addr2line";

static void usage(FILE *out) {
    fprintf(out,
            "usage: %s [-e file] [addr ...]\n"
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
        value |= ((uint64_t)(byte & 0x7fu)) << shift;
        if ((byte & 0x80u) == 0) {
            *pp = p;
            *out = value;
            return 0;
        }
        shift += 7u;
        if (shift >= 64u) {
            return -1;
        }
    }

    return -1;
}

static int read_sleb128(const uint8_t **pp, const uint8_t *end, int64_t *out) {
    int64_t value = 0;
    unsigned shift = 0;
    uint8_t byte = 0;
    const uint8_t *p = *pp;

    while (p < end) {
        byte = *p++;
        value |= ((int64_t)(byte & 0x7fu)) << shift;
        shift += 7u;
        if ((byte & 0x80u) == 0u) {
            break;
        }
        if (shift >= 64u) {
            return -1;
        }
    }

    if ((byte & 0x80u) != 0u) {
        return -1;
    }
    if (shift < 64u && (byte & 0x40u) != 0u) {
        value |= -((int64_t)1 << shift);
    }
    *pp = p;
    *out = value;
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
    char **next = (char **)realloc(u->include_dirs,
                                   (u->include_dir_count + 1u) * sizeof(*next));
    if (next == NULL) {
        free(dir);
        return -1;
    }
    u->include_dirs = next;
    u->include_dirs[u->include_dir_count++] = dir;
    return 0;
}

static int line_unit_add_file(line_unit_t *u, line_file_t file) {
    line_file_t *next = (line_file_t *)realloc(u->files,
                                               (u->file_count + 1u) * sizeof(*next));
    if (next == NULL) {
        free(file.name);
        return -1;
    }
    u->files = next;
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
    case DW_FORM_data4:
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
        if (read_u64_cursor(pp, end, img->elf_endian, &v) != 0) {
            return -1;
        }
        *out_u64 = v;
        return 0;
    case DW_FORM_udata:
        return read_uleb128(pp, end, out_u64);
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
    free(img->line_rows);
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
        image_reset(img);
        return -1;
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

static void output_unresolved(void) {
    puts("??:0");
}

static int resolve_stdin(void) {
    char line[256];

    while (fgets(line, sizeof(line), stdin) != NULL) {
        uint64_t query;
        if (parse_hex_address(line, &query) != 0) {
            warnf("invalid address: %s", line);
            output_unresolved();
            continue;
        }
        (void)query;
        output_unresolved();
    }

    return 0;
}

static int resolve_argv(const char **args, size_t n) {
    size_t i;

    for (i = 0; i < n; ++i) {
        uint64_t query;
        if (parse_hex_address(args[i], &query) != 0) {
            warnf("invalid address: %s", args[i]);
            output_unresolved();
            continue;
        }
        (void)query;
        output_unresolved();
    }

    return 0;
}

static int parse_options(int argc, char **argv, addr2line_opts_t *opts) {
    int i;

    memset(opts, 0, sizeof(*opts));
    opts->exe_path = "a.out";

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        }

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            exit(0);
        }
        if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            exit(0);
        }

        if (strcmp(arg, "-e") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: -e requires a file path\n", g_progname);
                return -1;
            }
            i++;
            opts->exe_path = argv[i];
            continue;
        }
        if (strncmp(arg, "--exe=", 6) == 0) {
            opts->exe_path = arg + 6;
            continue;
        }

        if (arg[0] == '-') {
            fprintf(stderr, "%s: unknown option: %s\n", g_progname, arg);
            return -1;
        }

        break;
    }

    opts->addr_args = (const char **)&argv[i];
    opts->addr_argc = (size_t)(argc - i);
    return 0;
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
        return 1;
    }

    if (opts.addr_argc == 0) {
        rc = resolve_stdin();
    } else {
        rc = resolve_argv(opts.addr_args, opts.addr_argc);
    }

    image_reset(&img);
    return rc;
}
