/*
 * ld.h -- what the files of the linker share: its types, and the
 * functions that one of them has and another uses.
 */
#ifndef LD_H
#define LD_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "elfobj.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define LD_MAX_SCRIPT_INCLUDE_DEPTH 16
/* How deeply a script expression may nest: parentheses, unary operators
 * and the arguments of builtins each count one. */
#define LD_MAX_SCRIPT_EXPR_DEPTH 256
#define LD_MAX_TRACKED_SYMBOLS 262144U
#define LD_MAX_INPUT_OBJECTS 131072U
#define LD_MAX_ARCHIVE_SCAN_PASSES 1024

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} strvec_t;

typedef enum {
    LD_INPUT_FILE = 0,
    LD_INPUT_LIB = 1,
    LD_INPUT_GROUP_START = 2,
    LD_INPUT_GROUP_END = 3
} ld_input_kind_t;

typedef enum {
    LD_LIBMODE_DYNAMIC = 0,
    LD_LIBMODE_STATIC = 1
} ld_lib_mode_t;

typedef enum {
    LD_COMPAT_GNU = 0,
    LD_COMPAT_LLD = 1
} ld_compat_mode_t;

typedef enum {
    LD_HASH_BOTH = 0,
    LD_HASH_SYSV = 1,
    LD_HASH_GNU = 2
} ld_hash_style_t;

typedef struct {
    ld_input_kind_t kind;
    ld_lib_mode_t lib_mode;
    int whole_archive;
    int as_needed;
    char *text;
} ld_input_t;

typedef struct {
    ld_input_t *items;
    size_t count;
    size_t cap;
} inputvec_t;

typedef struct {
    char *name;
    int need_plt;
    int need_got;
    int need_tls_gd;
    int need_tls_ie;
    size_t plt_slot;
    size_t got_slot;
    size_t tls_gd_slot;
    size_t tls_ie_slot;
    int canonical;              /* its PLT entry is its address: see
                                 * plan_copy_relocs() */
} dyn_import_t;

typedef struct {
    dyn_import_t *items;
    size_t count;
    size_t cap;
    /* The data of shared objects that the executable has a copy of
     * (R_*_COPY): each symbol the copy is of.  Its aliases, defined at
     * the same place, are in copy_aliases. */
    elf_symbol_t **copies;
    size_t copy_count;
    size_t copy_cap;
    elf_symbol_t **copy_aliases;
    size_t alias_count;
    size_t alias_cap;
} dyn_import_vec_t;

typedef struct {
    elfobj_t **objs;
    char **names;
    size_t count;
    size_t cap;
} objvec_t;

typedef struct {
    char *name;
    uint64_t value;
} defsym_t;

typedef struct {
    defsym_t *items;
    size_t count;
    size_t cap;
} defsymvec_t;

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} symset_t;

typedef struct {
    symset_t defined;
    symset_t unresolved;
} symstate_t;

typedef struct {
    int mode; /* 32 or 64 */
    int explicit_mode;
    int mode_settled;           /* an input has said which machine */
    int explicit_unresolved_policy;
    uint16_t expect_type;
    int allow_undefined;
    int warn_common;
    int fatal_warnings;
    int warning_count;
    int query_version;
    int trace_inputs;
    int export_dynamic;
    int gc_sections;
    int gc_print_sections;
    int icf_mode; /* 0=off, 1=safe, 2=all */
    int z_text_mode; /* 0=default, 1=text, 2=notext */
    int z_execstack; /* -1=auto, 0=noexecstack, 1=execstack */
    int z_relro; /* 0=norelro, 1=relro */
    int z_now;   /* -z now: the dynamic linker binds everything at once */
    ld_hash_style_t hash_style;
    const char *out_path;
    const char *self_path;
    const char *script_path;
    struct lds_script *script;  /* script_path, parsed */
    const char *plugin_path;
    const char *plugin_opts[32];
    size_t plugin_opt_count;
    int plugin_checked;
    int plugin_unusable;        /* one was named that cannot be run */
    int eh_frame_hdr;           /* --eh-frame-hdr */
    int copy_dt_needed;         /* --copy-dt-needed-entries */
    const char *sysroot;        /* --sysroot */
    const char *entry_symbol;
    const char *interp_path;
    const char *soname;         /* -soname, -h: the output's DT_SONAME */
    uint64_t image_base;        /* -Ttext-segment, when have_image_base */
    int have_image_base;
    int pie;                    /* -pie: ET_DYN, and a program */
    int strip_debug;            /* -S, -s: no debugging information */
    int emit_relocs;            /* -q: the output keeps its relocations */
    strvec_t rpaths;            /* -rpath: the output's DT_RUNPATH */
    const char *map_path;
    const char *reproduce_path;
    ld_compat_mode_t compat_mode;
    ld_lib_mode_t current_lib_mode;
    int explicit_lib_mode;
    int current_whole_archive;
    int current_as_needed;
    strvec_t lib_paths;
    strvec_t trace_symbols;
    strvec_t force_undefined;
    defsymvec_t defsyms;
    strvec_t dso_inputs;
    strvec_t dso_names;         /* what each is needed as: its DT_SONAME, or
                                 * failing that the name it was found by */
    symset_t dso_wants;         /* the symbols they refer to and do not define */
    dyn_import_vec_t dyn_imports;
    /* The symbols of the output itself that have a slot in .got
     * (collect_local_got), in slot order. */
    const elf_symbol_t **local_got;
    size_t local_got_count;
    size_t local_got_cap;
    int local_got_owned;        /* the slots exist */
    size_t local_got_base;      /* the first of them: after the imports' */
    size_t local_got_relative;  /* how many have a RELATIVE relocation */
    int local_got_need_base;    /* i386: something is GOT-relative */
    inputvec_t inputs;
} ld_ctx_t;

typedef enum {
    LDS_TOK_EOF = 0,
    LDS_TOK_IDENT,
    LDS_TOK_NUMBER,
    LDS_TOK_STRING,
    LDS_TOK_LBRACE,
    LDS_TOK_RBRACE,
    LDS_TOK_LPAREN,
    LDS_TOK_RPAREN,
    LDS_TOK_SEMI,
    LDS_TOK_COLON,
    LDS_TOK_COMMA,
    LDS_TOK_EQUAL,
    LDS_TOK_OTHER
} lds_tok_kind_t;

typedef struct {
    lds_tok_kind_t kind;
    char *text;
    const char *path;
    size_t line;
    size_t col;
} lds_tok_t;

typedef struct {
    const unsigned char *buf;
    size_t len;
    size_t pos;
    const char *path;
    size_t line;
    size_t col;
    /*
     * What a word is depends on where it stands.  In an expression a name
     * is letters, digits, '_', '.' and '$', so that a-b is a subtraction;
     * where a file or section name is expected ("names") it also takes
     * the characters of a path and of a wildcard, so that
     * .note.gnu.build-id, /DISCARD/ and *crt?.o are each one word.
     */
    int names;
} lds_lexer_t;

typedef struct {
    lds_tok_t *items;
    size_t count;
    size_t cap;
} lds_tokvec_t;

typedef struct {
    ld_ctx_t *ctx;
    const elfobj_t *obj;
    const lds_tok_t *err_tok;
    const char *err_msg;
    unsigned depth;             /* nesting, bounded by LD_MAX_SCRIPT_EXPR_DEPTH */
    int have_dot;               /* inside SECTIONS: "." has a value */
    uint64_t dot;
    char err_buf[160];          /* for an err_msg that names something */
    unsigned skip;              /* in the arm of a ?: that is not taken:
                                 * read, and nothing in it is an error */
} lds_eval_ctx_t;

typedef struct {
    char *name;
    uint32_t type;
    uint32_t flags;
    uint64_t align;
    int has_flags;              /* FLAGS(...) given, else from the sections */
} lds_phdr_entry_t;

typedef struct {
    lds_phdr_entry_t *items;
    size_t count;
    size_t cap;
} lds_phdr_vec_t;

/*
 * A linker script, parsed.  The file is read once, into this, and the
 * link then consults it at the points where it has something to say:
 * while the inputs are merged (which output section an input section goes
 * to), when the sections are ordered, when they are given addresses (the
 * location counter), and when the program headers are made.
 */
typedef enum {
    LDS_ST_ASSIGN = 0,          /* sym = expr, sym += expr, PROVIDE(...) */
    LDS_ST_ASSERT,              /* ASSERT(expr, "message") */
    LDS_ST_OUTSEC,              /* name [addr] : { body } [>region] [:phdr] */
    LDS_ST_INPUT,               /* file(sections...), KEEP(...) */
    LDS_ST_DATA                 /* BYTE(expr) and its kind: not implemented */
} lds_stmt_kind_t;

typedef enum {
    LDS_IN_TOP = 0,             /* outside SECTIONS: no location counter */
    LDS_IN_SECTIONS,            /* between output sections */
    LDS_IN_BODY                 /* inside an output section's braces */
} lds_where_t;

typedef struct lds_stmt lds_stmt_t;

typedef struct {
    lds_stmt_t *items;
    size_t count;
    size_t cap;
} lds_stmtvec_t;

struct lds_stmt {
    lds_stmt_kind_t kind;
    lds_where_t where;
    lds_tok_t at;               /* where it is; text: the symbol, the output
                                 * section, the file pattern, the keyword */
    char op;                    /* ASSIGN: '=' or the operator of op= */
    int provide;                /* ASSIGN: only if referenced and undefined */
    int hidden;
    int active;                 /* ASSIGN: it defines its symbol in this link */
    lds_tokvec_t expr;          /* ASSIGN value, ASSERT condition, OUTSEC address */
    char *message;              /* ASSERT */
    int discard;                /* OUTSEC: /DISCARD/ */
    lds_stmtvec_t body;         /* OUTSEC */
    strvec_t phdrs;             /* OUTSEC: :phdr ... */
    lds_tokvec_t align;         /* OUTSEC: ALIGN(expr) after the colon */
    char *region;               /* OUTSEC: >region */
    uint64_t pad;               /* OUTSEC: what its body adds to its end */
    int keep;                   /* INPUT: KEEP() */
    strvec_t patterns;          /* INPUT: section name patterns */
    strvec_t excludes;          /* INPUT: EXCLUDE_FILE() patterns */
};

typedef struct {
    char *name;
    lds_tokvec_t origin;
    lds_tokvec_t length;
    int evaluated;
    uint64_t org;
    uint64_t len;
    uint64_t cursor;            /* where its next section goes */
} lds_region_t;

typedef struct lds_script {
    lds_stmtvec_t stmts;        /* in order; SECTIONS' own statements inline */
    lds_phdr_vec_t phdrs;
    lds_region_t *regions;
    size_t region_count;
    size_t region_cap;
    char *entry;                /* ENTRY(sym) */
    int has_sections;
    defsymvec_t locals;         /* what PROVIDE names and nothing refers to:
                                 * no symbol, but the script may use it */
    strvec_t files;             /* the script and what it INCLUDEs: tokens
                                 * point at these names */
} lds_script_t;

#define LD_MAX_SCRIPT_TOKEN_LEN (1u << 20)  /* 1 MiB per token */

/* Segment permissions (p_flags). */
enum {
    LD_PF_X = 0x1u,
    LD_PF_W = 0x2u,
    LD_PF_R = 0x4u
};

/*
 * The parser of linker scripts: one token of lookahead, read either as a
 * name or as part of an expression according to what the grammar expects
 * there.  When the two readings would differ the token is read again.
 * Expressions are kept as their tokens and evaluated when their values
 * exist.
 */
#define LD_MAX_SCRIPT_EXPR_TOKENS 4096

typedef struct {
    lds_lexer_t lx;
    lds_lexer_t before;         /* lx as it was before the token in hand */
    lds_tok_t tok;
    int have;
    lds_script_t *sc;
    const ld_ctx_t *ctx;
    strvec_t *include_stack;
    int depth;
} lds_parser_t;

typedef struct {
    uint16_t index;
    char *name;
} verdef_name_t;

typedef struct {
    verdef_name_t *items;
    size_t count;
    size_t cap;
} verdef_table_t;

typedef struct {
    char *name;
    uint16_t index;
    uint32_t name_off;
} dyn_verdef_t;

typedef struct {
    char *file;
    char *name;
    uint16_t index;
    uint32_t file_off;
    uint32_t name_off;
} dyn_verneed_t;

typedef struct {
    dyn_verdef_t *defs;
    size_t def_count;
    size_t def_cap;
    dyn_verneed_t *needs;
    size_t need_count;
    size_t need_cap;
    uint16_t next_index;
} dyn_ver_plan_t;

/* How a shared object of the link defines a symbol. */
typedef struct {
    int type;                   /* STT_* */
    uint64_t size;
    uint64_t value;
    uint16_t shndx;
    size_t dso;                 /* which of ctx->dso_inputs */
} dso_def_t;

/*
 * .eh_frame_hdr: the table the unwinder looks a program counter up in.
 *
 * .eh_frame is a run of records, each a length and then either a CIE
 * (what a group of functions has in common) or an FDE (one function: where
 * it begins, how long it is, how to unwind out of it).  To find the FDE
 * for an address the unwinder would have to read them all; .eh_frame_hdr
 * is a sorted table of (function start, FDE) that it searches instead,
 * and PT_GNU_EH_FRAME is how it finds the table.  Without it the unwinder
 * of a dynamically linked program finds nothing and every throw ends in
 * terminate().
 *
 * The section is made before the layout (plan_eh_frame_hdr), sized for one
 * entry per FDE, and filled once everything has an address and .eh_frame
 * has been relocated (fill_eh_frame_hdr).
 */
#define DW_EH_PE_absptr 0x00
#define DW_EH_PE_udata4 0x03
#define DW_EH_PE_udata8 0x04
#define DW_EH_PE_sdata4 0x0b
#define DW_EH_PE_sdata8 0x0c
#define DW_EH_PE_pcrel 0x10
#define DW_EH_PE_datarel 0x30
#define DW_EH_PE_omit 0xff

typedef struct {
    int64_t start;              /* of the function, from the table */
    int64_t fde;                /* of its FDE, from the table */
} eh_entry_t;

typedef struct {
    char *name;
    const char *strong_src;
    const char *weak_src;
    const char *common_src;
    uint64_t common_size;
} symrule_entry_t;

typedef struct {
    symrule_entry_t *items;
    size_t count;
    size_t cap;
} symrule_vec_t;

typedef struct {
    char *name;
    const char *source;
} symref_entry_t;

typedef struct {
    symref_entry_t *items;
    size_t count;
    size_t cap;
} symref_map_t;

typedef struct {
    ld_ctx_t *ctx;
    elfobj_t *out;
    lds_script_t *sc;
    uint64_t dot;
    uint64_t high;              /* the end of what has an address so far */
    int moved;                  /* a statement set the counter since the last section */
    int final;                  /* values are settled: define, assert */
} lds_walk_t;

/* ld_util.c */
char *xstrdup(const char *s);
int strvec_push(strvec_t *v, const char *s);
void strvec_free(strvec_t *v);
void strvec_pop(strvec_t *v);
int defsymvec_push(defsymvec_t *v, const char *name, uint64_t value);
int defsymvec_find(const defsymvec_t *v, const char *name);
int defsymvec_get(const defsymvec_t *v, const char *name, uint64_t *out_value);
int defsymvec_set(defsymvec_t *v, const char *name, uint64_t value);
void defsymvec_free(defsymvec_t *v);
int inputvec_push(inputvec_t *v, ld_input_kind_t kind, ld_lib_mode_t lib_mode, int whole_archive,
                         int as_needed, const char *text);
void inputvec_free(inputvec_t *v);
int dyn_import_find(const dyn_import_vec_t *v, const char *name);
dyn_import_t *dyn_import_get_or_add(dyn_import_vec_t *v, const char *name);
void dyn_import_vec_free(dyn_import_vec_t *v);
int objvec_push(objvec_t *v, elfobj_t *obj, const char *name);
void objvec_free(objvec_t *v);
int symset_contains(const symset_t *set, const char *sym);
int symset_add(symset_t *set, const char *sym);
void symset_remove(symset_t *set, const char *sym);
void symset_free(symset_t *set);
void symstate_free(symstate_t *state);
char *path_join(const char *dir, const char *leaf);
int default_mode(void);
int parse_mode_token(const char *tok);
int parse_compat_mode(const char *tok, ld_compat_mode_t *out_mode);
const char *canonical_mode_name(int mode);
int parse_z_option(ld_ctx_t *ctx, const char *val);
int parse_hash_style_option(const char *val, ld_hash_style_t *out_style);
void ld_diag_note(const char *category, const char *source, const char *hint);
int ld_warn(ld_ctx_t *ctx, const char *fmt, ...);
int set_explicit_mode(ld_ctx_t *ctx, int mode, const char *opt_text);
int align_up_u64_checked(uint64_t v, uint64_t a, uint64_t *out);
int add_u64_checked(uint64_t a, uint64_t b, uint64_t *out);
int mul_u64_checked(uint64_t a, uint64_t b, uint64_t *out);
int has_suffix(const char *s, const char *suffix);
int read_file(const char *path, unsigned char **out, size_t *out_sz);
int parse_u64_dec(const char *s, size_t n, uint64_t *out);
int parse_u64_auto(const char *s, uint64_t *out);
uint16_t read_u16_endian(const uint8_t *p, elfobj_endian_t endian);
uint32_t read_u32_endian(const uint8_t *p, elfobj_endian_t endian);
uint64_t read_u64_endian(const uint8_t *p, elfobj_endian_t endian);
void write_u16_endian(uint8_t *p, elfobj_endian_t endian, uint16_t v);
void write_u32_endian(uint8_t *p, elfobj_endian_t endian, uint32_t v);
void write_u64_endian(uint8_t *p, elfobj_endian_t endian, uint64_t v);

/* ld_script.c */
int add_script_segments(elfobj_t *obj, const ld_ctx_t *ctx);
void lds_script_free(lds_script_t *sc);
lds_script_t *lds_script_parse(const char *path, const ld_ctx_t *ctx);
const char *script_output_name(const char *section, const char *file, void *user);
int script_declare_symbols(ld_ctx_t *ctx, elfobj_t *out);
int script_apply_sections(ld_ctx_t *ctx, elfobj_t *out);
int script_assign_addresses(ld_ctx_t *ctx, elfobj_t *out);

/* ld_plugin.c */
int plugin_discover_and_handshake(ld_ctx_t *ctx);
int plugin_materialize_object(const ld_ctx_t *ctx, const char *in_path, char *out_path, size_t out_path_sz);

/* ld_input.c */
int obj_matches_mode(const elfobj_t *obj, int mode);
void maybe_autoswitch_mode(ld_ctx_t *ctx, const elfobj_t *obj, size_t loaded_count, const char *path);
char *resolve_library_path_exact(const ld_ctx_t *ctx, const char *leaf);
int load_all_inputs(ld_ctx_t *ctx, objvec_t *objs);

/* ld_dso.c */
void split_symbol_version(const char *name, const char **base, size_t *base_len, const char **ver_name,
                                 int *is_default);
int shared_object_matches_unresolved(const char *path, ld_ctx_t *ctx, const symstate_t *state,
                                            int *out_match);
int register_dso_provider(ld_ctx_t *ctx, const char *path, symstate_t *state);
int unresolved_symbol_has_dso_provider(ld_ctx_t *ctx, const char *name, int *out_has_provider);
const char *dso_needed_name(const ld_ctx_t *ctx, size_t i);
int note_dso_names(ld_ctx_t *ctx);
int plan_symbol_version_sections(ld_ctx_t *ctx, elfobj_t *out, uint8_t **dynstr_buf, size_t *dynstr_len,
                                        size_t *dynstr_cap, const uint8_t *dynsym_buf, size_t dynsym_len, size_t entsz,
                                        uint8_t *versym_buf, size_t versym_len, size_t *out_verdef_count,
                                        size_t *out_verneed_count);

/* ld_dynamic.c */
int dynstr_append_cstr(uint8_t **buf, size_t *len, size_t *cap, const char *name, uint32_t *out_off);
int is_runtime_import_symbol(const elf_symbol_t *sym);
int reloc_is_x64_plt_ref(uint32_t type);
int reloc_is_x64_got_ref(uint32_t type);
int reloc_is_x64_tls_gd_ref(uint32_t type);
int reloc_is_x64_tls_ie_ref(uint32_t type);
int reloc_is_x64_runtime_data_ref(uint32_t type);
int reloc_is_i386_plt_ref(uint32_t type);
int reloc_is_i386_got_ref(uint32_t type);
int reloc_is_i386_tls_gd_ref(uint32_t type);
int reloc_is_i386_tls_ie_ref(uint32_t type);
int reloc_is_i386_runtime_data_ref(uint32_t type);
int reloc_is_direct_ref(uint16_t machine, uint32_t type, int data);
int settle_undefined_weak(const ld_ctx_t *ctx, elfobj_t *out);
int set_section_zero_data(elf_section_t *sec, size_t sz);
int plan_dynamic_imports(ld_ctx_t *ctx, elfobj_t *out);
int finalize_dynamic_imports_x64(elfobj_t *out, const dyn_import_vec_t *imports);
int finalize_dynamic_imports_i386(elfobj_t *out, const dyn_import_vec_t *imports);
const char *text_relocation_section(const ld_ctx_t *ctx, elfobj_t *out);
char *dso_soname(const char *path);
int dso_dynamic_strings(const char *path, uint64_t want, strvec_t *out);
int plan_dynamic_needed(ld_ctx_t *ctx, elfobj_t *out);
int patch_dynamic_tag_values(elfobj_t *out);
int finalize_symbol_values_for_output(elfobj_t *out);
int patch_dynsym_symbol_values(const ld_ctx_t *ctx, elfobj_t *out);

/* ld_ehframe.c */
int plan_eh_frame_hdr(const ld_ctx_t *ctx, elfobj_t *out);
int fill_eh_frame_hdr(elfobj_t *out);

/* ld_resolve.c */
void emit_trace_inputs(const ld_ctx_t *ctx, const objvec_t *inputs);
void emit_trace_symbols(const ld_ctx_t *ctx, const objvec_t *inputs);
int emit_common_symbol_warnings(ld_ctx_t *ctx, const objvec_t *inputs);
int check_symbol_precedence(ld_ctx_t *ctx, const objvec_t *inputs);
void symref_map_free(symref_map_t *m);
const char *symref_map_get(const symref_map_t *m, const char *name);
int collect_undefined_refs(const objvec_t *inputs, symref_map_t *out);
const char *find_symbol_source_input(const objvec_t *inputs, const char *sym_name);

/* ld_map.c */
int write_reproduce_bundle(const ld_ctx_t *ctx, const objvec_t *inputs);
int write_map_file(const ld_ctx_t *ctx, const objvec_t *inputs, elfobj_t *out);

/* ld_reloc.c */
int apply_defsyms(ld_ctx_t *ctx, elfobj_t *out);
int resolve_symbol_addr(elfobj_t *obj, const elf_symbol_t *sym, int allow_undef,
                               uint64_t *out_addr, const char **undef_name);
const dyn_import_t *find_planned_import(const ld_ctx_t *ctx, const char *name);
int collect_local_got(ld_ctx_t *ctx, elfobj_t *out);
int plan_local_got(ld_ctx_t *ctx, elfobj_t *out);
int fill_local_got(const ld_ctx_t *ctx, elfobj_t *out);
int apply_all_relocations(elfobj_t *obj, const ld_ctx_t *ctx, int allow_undefined);
int relax_tls_dynamic_in_program(elfobj_t *out);

/* ld_layout.c */
int alloc_section_class(uint64_t flags);
int assign_section_addresses(elfobj_t *obj, uint64_t base_vaddr);
int tls_extent(const elfobj_t *obj, uint64_t *start, uint64_t *memsz, uint64_t *filesz, uint64_t *align);
int64_t tls_tpoff(const elfobj_t *obj, uint64_t addr);
int reorder_sections_default_policy(elfobj_t *obj);
int is_relro_candidate_name(const char *name);
int add_default_segments(elfobj_t *obj, const ld_ctx_t *ctx);
int strip_group_sections_for_final(elfobj_t *obj);
int enforce_wx_policy(const elfobj_t *obj);

/* ld_gc.c */
int gc_sections_by_reachability(elfobj_t *obj, const ld_ctx_t *ctx);
int apply_identical_code_folding(elfobj_t *obj, const ld_ctx_t *ctx);

#endif /* LD_H */
