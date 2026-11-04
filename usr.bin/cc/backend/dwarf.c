/*
 * dwarf.c: DWARF debug information for -g (see cc_dwarf.h).
 *
 * The information is written as assembler directives: each DIE an
 * abbreviation code (.uleb128) and its attributes (.byte, .long, .quad,
 * .uleb128, .sleb128), references between DIEs as differences of labels
 * in .debug_info, strings as references to their labels in .debug_str, and
 * addresses as relocations against the code's own labels.  gcc writes its
 * debug information the same way, and the assembler and linker apply
 * relocations to sections that are not loaded as to any other.
 *
 * Types are emitted on demand: a DIE that names a type asks for its label
 * (type_ref()), which queues the type the first time; the queue is
 * drained into the types' part of the unit, each type's own references
 * queueing more.  So only the types the unit's functions and variables
 * reach are described, as gcc does by default.
 *
 * Locations: a function's frame base is its canonical frame address
 * (DW_OP_call_frame_cfa), which the CFI the backends write describes
 * wherever the code is; a variable in a stack slot is at a constant
 * offset from it (DW_OP_fbreg).  The backends report each slot's offset
 * (cc_dwarf_func_slot()); a variable whose storage the optimizer took
 * apart, or that lives in a register, has no location -- omitted rather
 * than wrong.
 */

#include "cc_dwarf.h"
#include "cc_ctype.h"
#include "cc_diag.h"
#include "cc_dtype.h"
#include "cc_lang.h"
#include "cc_lexer.h"
#include "cc_middle.h"
#include "cc_options.h"
#include "cc_target.h"
#include "cc_util.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- DWARF constants (DWARF 5, 7.5) ---- */

enum {
    DW_TAG_array_type = 0x01,
    DW_TAG_enumeration_type = 0x04,
    DW_TAG_formal_parameter = 0x05,
    DW_TAG_member = 0x0d,
    DW_TAG_pointer_type = 0x0f,
    DW_TAG_compile_unit = 0x11,
    DW_TAG_structure_type = 0x13,
    DW_TAG_subroutine_type = 0x15,
    DW_TAG_typedef = 0x16,
    DW_TAG_union_type = 0x17,
    DW_TAG_unspecified_parameters = 0x18,
    DW_TAG_subrange_type = 0x21,
    DW_TAG_base_type = 0x24,
    DW_TAG_const_type = 0x26,
    DW_TAG_enumerator = 0x28,
    DW_TAG_subprogram = 0x2e,
    DW_TAG_variable = 0x34,
    DW_TAG_volatile_type = 0x35,
    DW_TAG_restrict_type = 0x37,
    DW_TAG_unspecified_type = 0x3b,
    DW_TAG_atomic_type = 0x47
};

enum {
    DW_AT_location = 0x02,
    DW_AT_name = 0x03,
    DW_AT_byte_size = 0x0b,
    DW_AT_bit_size = 0x0d,
    DW_AT_stmt_list = 0x10,
    DW_AT_low_pc = 0x11,
    DW_AT_high_pc = 0x12,
    DW_AT_language = 0x13,
    DW_AT_comp_dir = 0x1b,
    DW_AT_const_value = 0x1c,
    DW_AT_inline = 0x20,
    DW_AT_producer = 0x25,
    DW_AT_prototyped = 0x27,
    DW_AT_upper_bound = 0x2f,
    DW_AT_count = 0x37,
    DW_AT_data_member_location = 0x38,
    DW_AT_decl_file = 0x3a,
    DW_AT_decl_line = 0x3b,
    DW_AT_declaration = 0x3c,
    DW_AT_encoding = 0x3e,
    DW_AT_external = 0x3f,
    DW_AT_frame_base = 0x40,
    DW_AT_type = 0x49,
    DW_AT_ranges = 0x55,
    DW_AT_data_bit_offset = 0x6b,
    DW_AT_linkage_name = 0x6e,
    DW_AT_noreturn = 0x87,
    DW_AT_alignment = 0x88,
    DW_AT_GNU_vector = 0x2107
};

enum {
    DW_FORM_addr = 0x01,
    DW_FORM_data2 = 0x05,
    DW_FORM_data4 = 0x06,
    DW_FORM_data8 = 0x07,
    DW_FORM_data1 = 0x0b,
    DW_FORM_sdata = 0x0d,
    DW_FORM_strp = 0x0e,
    DW_FORM_udata = 0x0f,
    DW_FORM_ref4 = 0x13,
    DW_FORM_sec_offset = 0x17,
    DW_FORM_exprloc = 0x18,
    DW_FORM_flag_present = 0x19
};

enum {
    DW_ATE_boolean = 0x02,
    DW_ATE_complex_float = 0x03,
    DW_ATE_float = 0x04,
    DW_ATE_signed = 0x05,
    DW_ATE_signed_char = 0x06,
    DW_ATE_unsigned = 0x07,
    DW_ATE_unsigned_char = 0x08,
    DW_ATE_imaginary_float = 0x09,
    DW_ATE_decimal_float = 0x0f
};

enum {
    DW_OP_addr = 0x03,
    DW_OP_fbreg = 0x91,
    DW_OP_call_frame_cfa = 0x9c
};

enum {
    DW_LANG_C89 = 0x01,
    DW_LANG_C99 = 0x0c,
    DW_LANG_C11 = 0x1d
};

enum {
    DW_UT_compile = 0x01,
    DW_RLE_end_of_list = 0x00,
    DW_RLE_start_end = 0x06
};

/** The qualifiers DWARF describes; an address space or nocf_check is the code's business alone. */
#define DW_QUALS (CC_CQ_CONST | CC_CQ_VOLATILE | CC_CQ_RESTRICT | CC_CQ_ATOMIC)

/* ---- growable text ---- */

/** Text being built: a section's directives. */
typedef struct {
    char *p;    /**< the text, NUL-terminated */
    size_t len; /**< its length */
    size_t cap; /**< room for */
    int oom;    /**< an append failed: the text is incomplete */
} dbuf_t;

/**
 * Append formatted text to @p b.
 *
 * @param b    the text
 * @param fmt  printf's format
 */
static void db_printf(dbuf_t *b, const char *fmt, ...) {
    va_list ap;
    int n;

    if (b->oom) {
        return;
    }
    for (;;) {
        size_t room = b->cap - b->len;

        va_start(ap, fmt);
        n = vsnprintf(b->p != NULL ? b->p + b->len : NULL, room, fmt, ap);
        va_end(ap);
        if (n < 0) {
            b->oom = 1;
            return;
        }
        if ((size_t)n < room) {
            b->len += (size_t)n;
            return;
        }
        {
            size_t ncap = b->cap == 0 ? 4096 : b->cap;
            char *next;

            while (ncap - b->len <= (size_t)n) {
                ncap *= 2;
            }
            next = (char *)realloc(b->p, ncap);
            if (next == NULL) {
                b->oom = 1;
                return;
            }
            b->p = next;
            b->cap = ncap;
        }
    }
}

/**
 * Append the text of @p src to @p dst.
 *
 * @param dst  the text appended to
 * @param src  the text appended
 */
static void db_append(dbuf_t *dst, const dbuf_t *src) {
    if (src->oom) {
        dst->oom = 1;
    }
    if (src->len > 0) {
        db_printf(dst, "%s", src->p);
    }
}

/**
 * Free @p b's text and empty it.
 *
 * @param b  the text
 */
static void db_free(dbuf_t *b) {
    free(b->p);
    memset(b, 0, sizeof(*b));
}

/* ---- pointer-keyed map ---- */

/** A map from pointers (or small integers cast to them) to positive ints. */
typedef struct {
    const void **keys;
    int *vals;
    size_t cap;
    size_t count;
} pmap_t;

/**
 * Hash a pointer.
 *
 * @param k  the key
 * @return   its hash
 */
static size_t pm_hash(const void *k) {
    uint64_t h = (uint64_t)(uintptr_t)k;

    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    return (size_t)h;
}

/**
 * The value @p m maps @p k to.
 *
 * @param m  the map
 * @param k  the key
 * @return   the value, or 0 for none
 */
static int pm_get(const pmap_t *m, const void *k) {
    size_t i;

    if (m->cap == 0) {
        return 0;
    }
    i = pm_hash(k) & (m->cap - 1);
    while (m->keys[i] != NULL) {
        if (m->keys[i] == k) {
            return m->vals[i];
        }
        i = (i + 1) & (m->cap - 1);
    }
    return 0;
}

/**
 * Map @p k to @p v.
 *
 * @param m  the map
 * @param k  the key, not NULL
 * @param v  the value, positive
 * @return   0, or -1 when out of memory
 */
static int pm_put(pmap_t *m, const void *k, int v) {
    size_t i;

    if ((m->count + 1) * 2 > m->cap) {
        size_t ncap = m->cap == 0 ? 256 : m->cap * 2;
        const void **nk = (const void **)calloc(ncap, sizeof(*nk));
        int *nv = (int *)calloc(ncap, sizeof(*nv));

        if (nk == NULL || nv == NULL) {
            free(nk);
            free(nv);
            return -1;
        }
        for (i = 0; i < m->cap; ++i) {
            if (m->keys[i] != NULL) {
                size_t j = pm_hash(m->keys[i]) & (ncap - 1);

                while (nk[j] != NULL) {
                    j = (j + 1) & (ncap - 1);
                }
                nk[j] = m->keys[i];
                nv[j] = m->vals[i];
            }
        }
        free(m->keys);
        free(m->vals);
        m->keys = nk;
        m->vals = nv;
        m->cap = ncap;
    }
    i = pm_hash(k) & (m->cap - 1);
    while (m->keys[i] != NULL && m->keys[i] != k) {
        i = (i + 1) & (m->cap - 1);
    }
    if (m->keys[i] == NULL) {
        m->count++;
    }
    m->keys[i] = k;
    m->vals[i] = v;
    return 0;
}

/**
 * Free @p m and empty it.
 *
 * @param m  the map
 */
static void pm_free(pmap_t *m) {
    free(m->keys);
    free(m->vals);
    memset(m, 0, sizeof(*m));
}

/* ---- the state of the module being described ---- */

/** A stack slot the backend placed: a STACKALLOC's value and its offset from the CFA. */
typedef struct {
    int value;
    long cfa_off;
} dw_slot_t;

/** A variable's location in a function: its dbg_vars index and its offset from the CFA. */
typedef struct {
    int var;
    long cfa_off;
} dw_loc_t;

/** A function the backend wrote. */
typedef struct {
    const cc_ssa_function_t *f;
    int label;         /**< .LdfbN and .LdfeN: its start and end */
    char *section;     /**< the section its code is in (owned) */
    dw_loc_t *locs;    /**< where its variables' slots are (owned) */
    size_t loc_count;
} dw_func_t;

/** The strings of .debug_str, each labelled .Ldstr<index>. */
typedef struct {
    char **items;   /**< in order (owned) */
    size_t count;
    size_t cap;
    /* by content: open addressing over indexes + 1 */
    int *table;
    size_t table_cap;
} dw_strs_t;

/** A DIE's abbreviation: its tag, whether it has children, and its attributes' names and forms. */
#define DW_MAX_ATTRS 20
typedef struct {
    unsigned tag;
    unsigned char children;
    unsigned char n;
    unsigned short at[DW_MAX_ATTRS];
    unsigned char form[DW_MAX_ATTRS];
} dw_abbrev_t;

static struct {
    int active;
    int version;
    int ptr;                   /**< address size: 4 or 8 */
    const cc_ssa_module_t *m;
    const cc_translation_unit_t *tu;
    char *src_path;
    /* functions written */
    dw_func_t *funcs;
    size_t func_count;
    size_t func_cap;
    int next_label;
    dw_slot_t *slots;          /**< the function being written's */
    size_t slot_count;
    size_t slot_cap;
    /* line-table files: per src_file (index + 1 in the unit's), its number; next to give */
    unsigned *file_nos;
    size_t file_nos_cap;
    unsigned next_file_no;
    /* the last .loc row printed in the function being written */
    unsigned loc_line;
    unsigned loc_col;
    int loc_file;
    /* the output */
    dbuf_t types;              /**< type DIEs */
    dbuf_t main;               /**< variable and subprogram DIEs */
    dbuf_t die;                /**< the attributes of the DIE being built */
    dw_abbrev_t cur;           /**< and its abbreviation */
    dw_abbrev_t *abbrevs;
    size_t abbrev_count;
    size_t abbrev_cap;
    dw_strs_t strs;
    /* types */
    pmap_t type_ids;           /**< debug type -> DIE label */
    const cc_dtype_t **queue;  /**< types asked for, not yet written */
    int *queue_ids;
    size_t queue_count;
    size_t queue_cap;
    int next_die;
    unsigned char *rec_ua;     /**< per structure: 0 not known, 1 no, 2 its alignment is the user's, 3 being found */
    pmap_t var_head;           /**< function -> one more than its first variable's dbg_vars index */
    int *var_next;             /**< per variable: one more than the next of its function's, or 0 */
    pmap_t call_head;          /**< function -> one more than its first call's index in g_calls */
    int *call_next;            /**< per call: one more than the next of its function's, or 0 */
    int oom;
} g_dw;

/* The calls the module made before inlining (cc_dwarf_note_calls()): kept apart from g_dw, which
 * cc_dwarf_begin() starts afresh. */
static struct {
    const cc_function_t **caller; /**< each call's function */
    char **callee;                /**< and the name it calls (owned) */
    size_t count;
    size_t cap;
} g_calls;

/**
 * Forget the calls cc_dwarf_note_calls() recorded.
 */
static void calls_free(void) {
    size_t i;

    for (i = 0; i < g_calls.count; ++i) {
        free(g_calls.callee[i]);
    }
    free(g_calls.caller);
    free(g_calls.callee);
    memset(&g_calls, 0, sizeof(g_calls));
}

/**
 * Record the calls of the module, before inlining (see cc_dwarf.h).
 *
 * @param m  the module
 * @return   0, or -1 when out of memory
 */
int cc_dwarf_note_calls(const cc_ssa_module_t *m) {
    size_t i;
    size_t j;

    calls_free();
    for (i = 0; m != NULL && i < m->func_count; ++i) {
        const cc_ssa_function_t *f = &m->funcs[i];

        if (f->src_fn == NULL) {
            continue;
        }
        for (j = 0; j < f->instr_count; ++j) {
            const cc_ssa_instr_t *in = &f->instrs[j];

            if ((in->op != CC_SSA_CALL && in->op != CC_SSA_GADDR) || in->sym == NULL) {
                continue;
            }
            if (g_calls.count == g_calls.cap) {
                size_t ncap = g_calls.cap == 0 ? 256 : g_calls.cap * 2;
                const cc_function_t **nc = (const cc_function_t **)realloc(g_calls.caller, ncap * sizeof(*nc));
                char **ne;

                if (nc == NULL) {
                    return -1;
                }
                g_calls.caller = nc;
                ne = (char **)realloc(g_calls.callee, ncap * sizeof(*ne));
                if (ne == NULL) {
                    return -1;
                }
                g_calls.callee = ne;
                g_calls.cap = ncap;
            }
            g_calls.callee[g_calls.count] = cc_strdup(in->sym);
            if (g_calls.callee[g_calls.count] == NULL) {
                return -1;
            }
            g_calls.caller[g_calls.count++] = f->src_fn;
        }
    }
    return 0;
}

/* ---- strings ---- */

/**
 * Hash a string.
 *
 * @param s  the string
 * @return   its hash
 */
static size_t dw_hash_str(const char *s) {
    uint64_t h = 1469598103934665603ULL;

    while (*s != '\0') {
        h = (h ^ (unsigned char)*s++) * 1099511628211ULL;
    }
    return (size_t)h;
}

/**
 * The index of @p s among the .debug_str strings, adding it the first
 * time.
 *
 * @param s  the string
 * @return   its index
 */
static size_t dw_str(const char *s) {
    dw_strs_t *t = &g_dw.strs;
    size_t i;

    if ((t->count + 1) * 2 > t->table_cap) {
        size_t ncap = t->table_cap == 0 ? 1024 : t->table_cap * 2;
        int *nt = (int *)calloc(ncap, sizeof(*nt));

        if (nt == NULL) {
            g_dw.oom = 1;
            return 0;
        }
        for (i = 0; i < t->count; ++i) {
            size_t j = dw_hash_str(t->items[i]) & (ncap - 1);

            while (nt[j] != 0) {
                j = (j + 1) & (ncap - 1);
            }
            nt[j] = (int)i + 1;
        }
        free(t->table);
        t->table = nt;
        t->table_cap = ncap;
    }
    i = dw_hash_str(s) & (t->table_cap - 1);
    while (t->table[i] != 0) {
        if (strcmp(t->items[t->table[i] - 1], s) == 0) {
            return (size_t)t->table[i] - 1;
        }
        i = (i + 1) & (t->table_cap - 1);
    }
    if (t->count == t->cap) {
        size_t ncap = t->cap == 0 ? 256 : t->cap * 2;
        char **ni = (char **)realloc(t->items, ncap * sizeof(*ni));

        if (ni == NULL) {
            g_dw.oom = 1;
            return 0;
        }
        t->items = ni;
        t->cap = ncap;
    }
    t->items[t->count] = cc_strdup(s);
    if (t->items[t->count] == NULL) {
        g_dw.oom = 1;
        return 0;
    }
    t->table[i] = (int)t->count + 1;
    return t->count++;
}

/**
 * Write @p s as the operand of a .string directive: quoted, with what
 * the assembler would read otherwise escaped.
 *
 * @param fp  the assembly
 * @param s   the string
 */
static void dw_print_quoted(FILE *fp, const char *s) {
    fputc('"', fp);
    for (; *s != '\0'; ++s) {
        unsigned char c = (unsigned char)*s;

        if (c == '"' || c == '\\') {
            fputc('\\', fp);
            fputc(c, fp);
        } else if (c < 0x20 || c >= 0x7f) {
            fprintf(fp, "\\%03o", c);
        } else {
            fputc(c, fp);
        }
    }
    fputc('"', fp);
}

/* ---- DIEs ---- */

/**
 * Start a DIE.
 *
 * @param tag       its tag
 * @param children  it has children (ended by dw_children_end())
 */
static void die_start(unsigned tag, int children) {
    memset(&g_dw.cur, 0, sizeof(g_dw.cur));
    g_dw.cur.tag = tag;
    g_dw.cur.children = (unsigned char)(children != 0);
    g_dw.die.len = 0;
    if (g_dw.die.p != NULL) {
        g_dw.die.p[0] = '\0';
    }
}

/**
 * Add an attribute to the DIE being built; its value is what the caller
 * appends to g_dw.die next (nothing for DW_FORM_flag_present).
 *
 * @param at    the attribute
 * @param form  its form
 */
static void die_attr(unsigned at, unsigned form) {
    if (g_dw.cur.n >= DW_MAX_ATTRS) {
        g_dw.oom = 1;
        return;
    }
    g_dw.cur.at[g_dw.cur.n] = (unsigned short)at;
    g_dw.cur.form[g_dw.cur.n] = (unsigned char)form;
    g_dw.cur.n++;
}

/**
 * Add a string attribute (DW_FORM_strp): a reference to the string's own
 * label, which the assembler makes the section and the string's offset.
 * cc's .debug_str is not mergeable: in a section that is, the in-tree
 * assembler keeps each such label as a local symbol (a linker finds a
 * merged string by its symbol), and the thousands of a unit's strings
 * made thousands of symbols an object -- millions in a Linux vmlinux.o,
 * past what the linker's link map holds.
 */
static void die_str(unsigned at, const char *s) {
    die_attr(at, DW_FORM_strp);
    db_printf(&g_dw.die, "\t.long .Ldstr%lu\n", (unsigned long)dw_str(s));
}

/** Add an unsigned constant attribute (DW_FORM_udata). */
static void die_udata(unsigned at, unsigned long long v) {
    die_attr(at, DW_FORM_udata);
    db_printf(&g_dw.die, "\t.uleb128 %llu\n", v);
}

/** Add a signed constant attribute (DW_FORM_sdata). */
static void die_sdata(unsigned at, long long v) {
    die_attr(at, DW_FORM_sdata);
    db_printf(&g_dw.die, "\t.sleb128 %lld\n", v);
}

/** Add a one-byte constant attribute (DW_FORM_data1). */
static void die_data1(unsigned at, unsigned v) {
    die_attr(at, DW_FORM_data1);
    db_printf(&g_dw.die, "\t.byte %u\n", v & 0xffu);
}

/** Add a flag that is present (DW_FORM_flag_present). */
static void die_flag(unsigned at) {
    die_attr(at, DW_FORM_flag_present);
}

/** Add a reference to the DIE labelled @p id (DW_FORM_ref4); none for 0 (void). */
static void die_ref(unsigned at, int id) {
    if (id <= 0) {
        return;
    }
    die_attr(at, DW_FORM_ref4);
    db_printf(&g_dw.die, "\t.long .Ldi%d-.Ldebug_info0\n", id);
}

/** Add an address attribute (DW_FORM_addr): label @p label. */
static void die_addr(unsigned at, const char *label) {
    die_attr(at, DW_FORM_addr);
    db_printf(&g_dw.die, g_dw.ptr == 8 ? "\t.quad %s\n" : "\t.long %s\n", label);
}

/**
 * The number of bytes the SLEB128 encoding of @p v takes.
 *
 * @param v  the value
 * @return   its length
 */
static int sleb_len(long long v) {
    int n = 0;

    for (;;) {
        unsigned char b = (unsigned char)(v & 0x7f);

        v >>= 7;
        ++n;
        if ((v == 0 && (b & 0x40) == 0) || (v == -1 && (b & 0x40) != 0)) {
            return n;
        }
    }
}

/** Add a location: DW_OP_fbreg @p off. */
static void die_loc_fbreg(long off) {
    die_attr(DW_AT_location, DW_FORM_exprloc);
    db_printf(&g_dw.die, "\t.uleb128 %d\n\t.byte 0x%x\n\t.sleb128 %ld\n", 1 + sleb_len(off), DW_OP_fbreg, off);
}

/** Add a location: DW_OP_addr @p sym. */
static void die_loc_addr(const char *sym) {
    die_attr(DW_AT_location, DW_FORM_exprloc);
    db_printf(&g_dw.die, "\t.uleb128 %d\n\t.byte 0x%x\n", 1 + g_dw.ptr, DW_OP_addr);
    db_printf(&g_dw.die, g_dw.ptr == 8 ? "\t.quad %s\n" : "\t.long %s\n", sym);
}

/**
 * The code of abbreviation @p a, added the first time.
 *
 * @param a  the abbreviation
 * @return   its code
 */
static unsigned abbrev_code(const dw_abbrev_t *a) {
    size_t i;

    for (i = 0; i < g_dw.abbrev_count; ++i) {
        const dw_abbrev_t *b = &g_dw.abbrevs[i];

        if (b->tag == a->tag && b->children == a->children && b->n == a->n &&
            memcmp(b->at, a->at, a->n * sizeof(a->at[0])) == 0 && memcmp(b->form, a->form, a->n) == 0) {
            return (unsigned)i + 1;
        }
    }
    if (g_dw.abbrev_count == g_dw.abbrev_cap) {
        size_t ncap = g_dw.abbrev_cap == 0 ? 64 : g_dw.abbrev_cap * 2;
        dw_abbrev_t *next = (dw_abbrev_t *)realloc(g_dw.abbrevs, ncap * sizeof(*next));

        if (next == NULL) {
            g_dw.oom = 1;
            return 1;
        }
        g_dw.abbrevs = next;
        g_dw.abbrev_cap = ncap;
    }
    g_dw.abbrevs[g_dw.abbrev_count] = *a;
    return (unsigned)++g_dw.abbrev_count;
}

/**
 * Finish the DIE being built: append it to @p out, labelled .Ldi<label>
 * when @p label is positive.
 *
 * @param out    the section text
 * @param label  its label, or 0
 */
static void die_end(dbuf_t *out, int label) {
    if (label > 0) {
        db_printf(out, ".Ldi%d:\n", label);
    }
    db_printf(out, "\t.uleb128 %u\n", abbrev_code(&g_dw.cur));
    if (g_dw.die.len > 0) {
        db_append(out, &g_dw.die);
    }
}

/**
 * End the children of a DIE.
 *
 * @param out  the section text
 */
static void die_children_end(dbuf_t *out) {
    db_printf(out, "\t.byte 0\n");
}

/* ---- types ---- */

/**
 * The outermost of qualifiers @p q, as DWARF nests them: const outside
 * volatile outside restrict outside _Atomic.
 *
 * @param q  CC_CQ_* (DW_QUALS of them), not 0
 * @return   the one bit
 */
static unsigned qual_outer(unsigned q) {
    if ((q & CC_CQ_CONST) != 0) {
        return CC_CQ_CONST;
    }
    if ((q & CC_CQ_VOLATILE) != 0) {
        return CC_CQ_VOLATILE;
    }
    if ((q & CC_CQ_RESTRICT) != 0) {
        return CC_CQ_RESTRICT;
    }
    return CC_CQ_ATOMIC;
}

/**
 * The tag of the DIE for qualifier @p bit.
 *
 * @param bit  one CC_CQ_* bit of DW_QUALS
 * @return     its DW_TAG
 */
static unsigned qual_tag(unsigned bit) {
    switch (bit) {
    case CC_CQ_CONST:
        return DW_TAG_const_type;
    case CC_CQ_VOLATILE:
        return DW_TAG_volatile_type;
    case CC_CQ_RESTRICT:
        return DW_TAG_restrict_type;
    default:
        return DW_TAG_atomic_type;
    }
}

/**
 * The label of the DIE describing debug type @p t, queued for writing the
 * first time; 0 for void, which has none.
 *
 * @param t  the type, or NULL (void)
 * @return   its DIE's label number, or 0
 */
static int type_ref(const cc_dtype_t *t) {
    int id;

    while (t != NULL) {
        const cc_ctype_t *ct = t->ct;

        if (t->kind == CC_DT_PLAIN) {
            if ((ct->quals & DW_QUALS) == 0 && ct->quals != 0 && ct->kind != CC_CT_ARRAY) {
                /* an address space or nocf_check only */
                t = cc_dt_plain(ct->unqual);
                continue;
            }
            if (ct->kind == CC_CT_VOID && (ct->quals & DW_QUALS) == 0) {
                return 0;
            }
        } else if (t->kind == CC_DT_QUAL && (t->quals & DW_QUALS) == 0) {
            t = t->of;
            continue;
        } else if (t->kind == CC_DT_POINTER && (t->quals & DW_QUALS) == 0 && t->quals != 0) {
            t = cc_dt_pointer(t->of, 0);
            continue;
        }
        break;
    }
    if (t == NULL) {
        return 0;
    }
    id = pm_get(&g_dw.type_ids, t);
    if (id > 0) {
        return id;
    }
    id = ++g_dw.next_die;
    if (pm_put(&g_dw.type_ids, t, id) != 0) {
        g_dw.oom = 1;
        return 0;
    }
    if (g_dw.queue_count == g_dw.queue_cap) {
        size_t ncap = g_dw.queue_cap == 0 ? 256 : g_dw.queue_cap * 2;
        const cc_dtype_t **nq = (const cc_dtype_t **)realloc(g_dw.queue, ncap * sizeof(*nq));
        int *ni;

        if (nq == NULL) {
            g_dw.oom = 1;
            return 0;
        }
        g_dw.queue = nq;
        ni = (int *)realloc(g_dw.queue_ids, ncap * sizeof(*ni));
        if (ni == NULL) {
            g_dw.oom = 1;
            return 0;
        }
        g_dw.queue_ids = ni;
        g_dw.queue_cap = ncap;
    }
    g_dw.queue[g_dw.queue_count] = t;
    g_dw.queue_ids[g_dw.queue_count] = id;
    g_dw.queue_count++;
    return id;
}

/**
 * The label of the DIE describing canonical type @p ct.
 *
 * @param ct  the type, or NULL (void)
 * @return    its label number, or 0
 */
static int ctype_ref(const cc_ctype_t *ct) {
    return ct == NULL ? 0 : type_ref(cc_dt_plain(ct));
}

/**
 * The label of the DIE of the unsigned or signed integer type of @p size
 * bytes: an enumeration's underlying type, an array's index type.
 *
 * @param size         1, 2, 4 or 8
 * @param is_unsigned  unsigned
 * @return             its label number
 */
static int int_type_ref(long size, int is_unsigned) {
    cc_ctype_kind_t k;

    switch (size) {
    case 1:
        k = is_unsigned ? CC_CT_UCHAR : CC_CT_SCHAR;
        break;
    case 2:
        k = is_unsigned ? CC_CT_USHORT : CC_CT_SHORT;
        break;
    case 8:
        k = g_dw.ptr == 8 ? (is_unsigned ? CC_CT_ULONG : CC_CT_LONG) : (is_unsigned ? CC_CT_ULLONG : CC_CT_LLONG);
        break;
    default:
        k = is_unsigned ? CC_CT_UINT : CC_CT_INT;
        break;
    }
    return ctype_ref(cc_ct_basic(k));
}

/**
 * Write a base type DIE.
 *
 * @param id        its label
 * @param name      its name
 * @param size      its size in bytes
 * @param encoding  its DW_ATE
 * @param bits      its width in bits where that is not size * 8 (_BitInt), else 0
 */
static void emit_base(int id, const char *name, long size, unsigned encoding, long bits) {
    die_start(DW_TAG_base_type, 0);
    die_udata(DW_AT_byte_size, (unsigned long long)(size > 0 ? size : 0));
    die_data1(DW_AT_encoding, encoding);
    die_str(DW_AT_name, name);
    if (bits > 0) {
        die_udata(DW_AT_bit_size, (unsigned long long)bits);
    }
    die_end(&g_dw.types, id);
}

/**
 * Write a qualifier DIE: @p bit over the type labelled @p inner.
 *
 * @param id     its label
 * @param bit    the qualifier
 * @param inner  the qualified type's label, 0 for void
 */
static void emit_qual(int id, unsigned bit, int inner) {
    die_start(qual_tag(bit), 0);
    die_ref(DW_AT_type, inner);
    die_end(&g_dw.types, id);
}

/**
 * The name and encoding of an arithmetic type with no parts.
 *
 * @param ct        the type, unqualified
 * @param name      receives its name
 * @param cap       the size of @p name
 * @param encoding  receives its DW_ATE
 * @return          1, or 0 if it is not one
 */
static int base_name(const cc_ctype_t *ct, char *name, size_t cap, unsigned *encoding) {
    const char *n = NULL;
    int variant = cc_ct_float_variant(ct);

    *encoding = DW_ATE_signed;
    switch (ct->kind) {
    case CC_CT_BOOL:
        n = cc_lang_at_least(2023) ? "bool" : "_Bool";
        *encoding = DW_ATE_boolean;
        break;
    case CC_CT_CHAR:
        n = "char";
        *encoding = cc_options_current()->unsigned_char ? DW_ATE_unsigned_char : DW_ATE_signed_char;
        break;
    case CC_CT_SCHAR:
        n = "signed char";
        *encoding = DW_ATE_signed_char;
        break;
    case CC_CT_UCHAR:
        n = "unsigned char";
        *encoding = DW_ATE_unsigned_char;
        break;
    case CC_CT_SHORT:
        n = "short int";
        break;
    case CC_CT_USHORT:
        n = "short unsigned int";
        *encoding = DW_ATE_unsigned;
        break;
    case CC_CT_INT:
    case CC_CT_ENUM:
        n = "int";
        break;
    case CC_CT_UINT:
        n = "unsigned int";
        *encoding = DW_ATE_unsigned;
        break;
    case CC_CT_LONG:
        n = "long int";
        break;
    case CC_CT_ULONG:
        n = "long unsigned int";
        *encoding = DW_ATE_unsigned;
        break;
    case CC_CT_LLONG:
        n = "long long int";
        break;
    case CC_CT_ULLONG:
        n = "long long unsigned int";
        *encoding = DW_ATE_unsigned;
        break;
    case CC_CT_BITINT:
    case CC_CT_UBITINT:
        snprintf(name, cap, "%s_BitInt(%ld)", ct->kind == CC_CT_UBITINT ? "unsigned " : "", ct->count);
        *encoding = ct->kind == CC_CT_UBITINT ? DW_ATE_unsigned : DW_ATE_signed;
        return 1;
    case CC_CT_FLOAT16:
        n = "_Float16";
        *encoding = DW_ATE_float;
        break;
    case CC_CT_FLOAT:
        n = variant == CC_FLOATN_32 ? "_Float32" : "float";
        *encoding = DW_ATE_float;
        break;
    case CC_CT_DOUBLE:
        n = variant == CC_FLOATN_64 ? "_Float64" : variant == CC_FLOATN_32X ? "_Float32x" : "double";
        *encoding = DW_ATE_float;
        break;
    case CC_CT_LDOUBLE:
        n = variant == CC_FLOATN_64X ? "_Float64x" : "long double";
        *encoding = DW_ATE_float;
        break;
    case CC_CT_DECIMAL32:
        n = "_Decimal32";
        *encoding = DW_ATE_decimal_float;
        break;
    case CC_CT_DECIMAL64:
        n = "_Decimal64";
        *encoding = DW_ATE_decimal_float;
        break;
    case CC_CT_DECIMAL128:
        n = "_Decimal128";
        *encoding = DW_ATE_decimal_float;
        break;
    default:
        return 0;
    }
    snprintf(name, cap, "%s", n);
    return 1;
}

/**
 * The name of the complex or imaginary type of a real floating type.
 *
 * @param elem       the real type
 * @param imaginary  _Imaginary rather than _Complex
 * @param name       receives the name
 * @param cap        its size
 */
static void complex_name(const cc_ctype_t *elem, int imaginary, char *name, size_t cap) {
    const char *e = elem == NULL               ? "double"
                    : elem->kind == CC_CT_FLOAT ? "float"
                    : elem->kind == CC_CT_LDOUBLE ? "long double"
                    : elem->kind == CC_CT_FLOAT16 ? "_Float16"
                                                 : "double";

    snprintf(name, cap, "%s %s", imaginary ? "imaginary" : "complex", e);
}

/**
 * The size in bytes of canonical type @p ct on the target.
 *
 * @param ct  the type
 * @return    its size, or 0 if not known
 */
static long dw_size(const cc_ctype_t *ct) {
    long s = cc_ct_size(ct, g_dw.tu);

    return s > 0 ? s : 0;
}

/**
 * Write the DIE of an array type: its element type and a subrange for
 * each dimension, as gcc folds the dimensions of `T a[2][3]` into one
 * DIE.
 *
 * @param id      its label
 * @param t       the type (CC_DT_ARRAY, or a CC_DT_PLAIN array)
 * @param vector  a GNU vector type: the element count; else 0
 */
static void emit_array(int id, const cc_dtype_t *t, long vector) {
    long dims[64];
    int ndims = 0;
    int k;
    int index_type = int_type_ref(g_dw.ptr, 1);

    while (ndims < 64) {
        if (t->kind == CC_DT_ARRAY) {
            dims[ndims++] = t->count;
            t = t->of;
        } else if (t->kind == CC_DT_PLAIN && t->ct->kind == CC_CT_ARRAY) {
            dims[ndims++] = t->ct->count;
            t = cc_dt_plain(t->ct->of);
        } else {
            break;
        }
        if (vector > 0) {
            break;
        }
    }
    die_start(DW_TAG_array_type, 1);
    if (vector > 0) {
        die_flag(DW_AT_GNU_vector);
    }
    die_ref(DW_AT_type, type_ref(t));
    die_end(&g_dw.types, id);
    for (k = 0; k < ndims; ++k) {
        die_start(DW_TAG_subrange_type, 0);
        die_ref(DW_AT_type, index_type);
        if (dims[k] > 0) {
            die_udata(DW_AT_upper_bound, (unsigned long long)(dims[k] - 1));
        } else if (dims[k] == 0) {
            die_udata(DW_AT_count, 0);
        }
        die_end(&g_dw.types, 0);
    }
    die_children_end(&g_dw.types);
}

/**
 * Write the DIE of a function type.
 *
 * @param id          its label
 * @param ret         its return type
 * @param params      its parameters' types (as spelled; NULL to take @p ct's)
 * @param ct          the canonical type
 */
static void emit_subroutine(int id, const cc_dtype_t *ret, const cc_dtype_t *const *params, const cc_ctype_t *ct) {
    size_t i;
    int children = ct->param_count > 0 || ct->variadic || !ct->prototyped;

    die_start(DW_TAG_subroutine_type, children);
    if (ct->prototyped) {
        die_flag(DW_AT_prototyped);
    }
    die_ref(DW_AT_type, type_ref(ret));
    die_end(&g_dw.types, id);
    if (!children) {
        return;
    }
    for (i = 0; i < ct->param_count; ++i) {
        die_start(DW_TAG_formal_parameter, 0);
        die_ref(DW_AT_type, params != NULL ? type_ref(params[i]) : ctype_ref(ct->params[i]));
        die_end(&g_dw.types, 0);
    }
    if (ct->variadic || !ct->prototyped) {
        die_start(DW_TAG_unspecified_parameters, 0);
        die_end(&g_dw.types, 0);
    }
    die_children_end(&g_dw.types);
}

/**
 * The alignment a member declarator of structure @p sd asks for with
 * `aligned` or _Alignas.
 *
 * @param sd     the structure
 * @param index  the member's index
 * @return       the alignment, or 0 for none
 */
static long member_user_align(const cc_struct_def_t *sd, size_t index) {
    size_t i;

    for (i = 0; i < sd->layout_count; ++i) {
        if (sd->layout[i].member == (int)index) {
            return sd->layout[i].user_align;
        }
    }
    return 0;
}

static int record_user_aligned(int sid);

/**
 * The alignment a member declarator of structure @p sd was laid out
 * with: its type's, or 1 where the declarator is `packed`.
 *
 * @param sd     the structure
 * @param index  the member's index
 * @return       the alignment, or 0 if not known
 */
static long member_layout_align(const cc_struct_def_t *sd, size_t index) {
    size_t i;

    for (i = 0; i < sd->layout_count; ++i) {
        if (sd->layout[i].member == (int)index) {
            return sd->layout[i].align;
        }
    }
    return 0;
}

/**
 * A member's type as its declaration spelled it -- an anonymous
 * structure member's with the qualifiers its declaration gave it.
 *
 * @param m  the member, with a ctype
 * @return   the debug type
 */
static const cc_dtype_t *member_dtype(const cc_struct_member_t *m) {
    if (m->name == NULL && m->dtype != NULL && m->dtype->ct->unqual == m->ctype->unqual) {
        return m->dtype;
    }
    return cc_dt_fit(m->dtype, m->ctype);
}

/**
 * Whether a type's alignment is one the program asked for, as gcc's
 * TYPE_USER_ALIGN has it: a typedef name's `aligned`, or a structure's
 * (record_user_aligned()), through arrays and qualifiers.  gcc gives such
 * a type, and a member or typedef of one, DW_AT_alignment, and pahole
 * shows it as an __aligned__ attribute.
 *
 * @param t  the type
 * @return   1 or 0
 */
static int type_user_aligned(const cc_dtype_t *t) {
    while (t != NULL) {
        switch (t->kind) {
        case CC_DT_TYPEDEF:
            if (t->count > 0) {
                return 1;
            }
            t = t->of;
            break;
        case CC_DT_QUAL:
        case CC_DT_ARRAY:
            t = t->of;
            break;
        case CC_DT_PLAIN:
            if (t->ct->kind == CC_CT_ARRAY) {
                t = cc_dt_plain(t->ct->of);
                break;
            }
            return t->ct->kind == CC_CT_RECORD && record_user_aligned(t->ct->id);
        default:
            return 0;
        }
    }
    return 0;
}

/**
 * Whether structure @p sid's alignment is one the program asked for: its
 * own `aligned`, or a member's -- the member's `aligned` or _Alignas, or
 * its type's (gcc's TYPE_USER_ALIGN, which a structure takes from its
 * fields).
 *
 * @param sid  the structure's index
 * @return     1 or 0
 */
static int record_user_aligned(int sid) {
    const cc_struct_def_t *sd;
    size_t i;
    int ua = 0;

    if (g_dw.tu == NULL || sid < 0 || (size_t)sid >= g_dw.tu->struct_count) {
        return 0;
    }
    if (g_dw.rec_ua == NULL) {
        g_dw.rec_ua = (unsigned char *)calloc(g_dw.tu->struct_count, 1);
        if (g_dw.rec_ua == NULL) {
            g_dw.oom = 1;
            return 0;
        }
    }
    if (g_dw.rec_ua[sid] != 0) {
        return g_dw.rec_ua[sid] == 2;
    }
    g_dw.rec_ua[sid] = 3;
    sd = &g_dw.tu->structs[sid];
    if ((sd->attr_flags & CC_ATTR_ALIGNED) != 0 && sd->attr_align > 0) {
        ua = 1;
    }
    for (i = 0; !ua && sd->complete && i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];

        ua = member_user_align(sd, i) > 0 || (m->ctype != NULL && type_user_aligned(member_dtype(m)));
    }
    g_dw.rec_ua[sid] = (unsigned char)(ua ? 2 : 1);
    return ua;
}

/**
 * Write the DIE of a structure or union, or of the type a structure of
 * the compiler's own stands for (complex, wide integer, vector, decimal
 * or binary128 floating).
 *
 * @param id   its label
 * @param sid  its index in the unit's structures
 */
static void emit_record(int id, int sid) {
    const cc_struct_def_t *sd;
    char name[64];
    size_t i;

    if (g_dw.tu == NULL || sid < 0 || (size_t)sid >= g_dw.tu->struct_count) {
        emit_base(id, "int", 4, DW_ATE_signed, 0);
        return;
    }
    sd = &g_dw.tu->structs[sid];
    if (sd->complex_elem != NULL) {
        complex_name(sd->complex_elem->unqual, 0, name, sizeof(name));
        emit_base(id, name, sd->size, DW_ATE_complex_float, 0);
        return;
    }
    if (sd->wide_bits > 0) {
        if (sd->wide_is_int128) {
            snprintf(name, sizeof(name), "%s", sd->wide_unsigned ? "__int128 unsigned" : "__int128");
            emit_base(id, name, sd->size, sd->wide_unsigned ? DW_ATE_unsigned : DW_ATE_signed, 0);
        } else {
            snprintf(name, sizeof(name), "%s_BitInt(%ld)", sd->wide_unsigned ? "unsigned " : "", sd->wide_bits);
            emit_base(id, name, sd->size, sd->wide_unsigned ? DW_ATE_unsigned : DW_ATE_signed, sd->wide_bits);
        }
        return;
    }
    if (sd->vector_count > 0 && sd->vector_elem != NULL) {
        emit_array(id, cc_dt_array(cc_dt_plain(sd->vector_elem), sd->vector_count), sd->vector_count);
        return;
    }
    if (sd->decimal_bits > 0) {
        snprintf(name, sizeof(name), "_Decimal%d", sd->decimal_bits);
        emit_base(id, name, sd->size, DW_ATE_decimal_float, 0);
        return;
    }
    if (sd->binary_bits > 0) {
        emit_base(id, "_Float128", sd->size, DW_ATE_float, 0);
        return;
    }
    die_start(sd->is_union ? DW_TAG_union_type : DW_TAG_structure_type, sd->complete && sd->member_count > 0);
    if (sd->tag != NULL && sd->tag[0] != '\0') {
        die_str(DW_AT_name, sd->tag);
    }
    if (!sd->complete) {
        die_flag(DW_AT_declaration);
        die_end(&g_dw.types, id);
        return;
    }
    die_udata(DW_AT_byte_size, (unsigned long long)(sd->size > 0 ? sd->size : 0));
    if (record_user_aligned(sid) && sd->align > 0) {
        /* (as gcc gives it, under -gdwarf-4 too: its DWARF is not strict) */
        die_udata(DW_AT_alignment, (unsigned long long)sd->align);
    }
    die_end(&g_dw.types, id);
    if (sd->member_count == 0) {
        return;
    }
    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        const cc_dtype_t *mt;
        long ua;

        if (m->ctype == NULL) {
            continue;
        }
        mt = member_dtype(m);
        die_start(DW_TAG_member, 0);
        if (m->name != NULL) {
            die_str(DW_AT_name, m->name);
        }
        die_ref(DW_AT_type, type_ref(mt));
        if (m->is_bitfield) {
            die_udata(DW_AT_bit_size, (unsigned long long)(m->bit_width > 0 ? m->bit_width : 0));
            die_udata(DW_AT_data_bit_offset, (unsigned long long)(m->offset * 8 + m->bit_offset));
        } else if (!sd->is_union) {
            die_udata(DW_AT_data_member_location, (unsigned long long)(m->offset > 0 ? m->offset : 0));
        }
        ua = member_user_align(sd, i);
        if (ua > 0 || type_user_aligned(mt)) {
            /* the member's alignment: what it asks, or its type's, the greater -- in a packed structure 1 for its
             * type's, as gcc's packed field has it */
            long ta = (sd->attr_flags & CC_ATTR_PACKED) != 0 || member_layout_align(sd, i) == 1
                          ? 1
                          : cc_ct_align(m->ctype, g_dw.tu);

            die_udata(DW_AT_alignment, (unsigned long long)(ta > ua ? ta : ua));
        }
        die_end(&g_dw.types, 0);
    }
    die_children_end(&g_dw.types);
}

/**
 * Write the DIE of an enumeration.
 *
 * @param id  its label
 * @param t   the type (CC_DT_ENUM)
 */
static void emit_enum(int id, const cc_dtype_t *t) {
    const cc_dt_enum_def_t *d = cc_dt_enum_def(t->id);
    long size = dw_size(t->ct);
    size_t i;
    int is_signed = d != NULL && d->has_negative;
    int children = d != NULL && d->complete && d->count > 0;

    die_start(DW_TAG_enumeration_type, children);
    if (d != NULL && d->tag != NULL) {
        die_str(DW_AT_name, d->tag);
    }
    if (d == NULL || !d->complete) {
        die_flag(DW_AT_declaration);
        die_end(&g_dw.types, id);
        return;
    }
    die_udata(DW_AT_byte_size, (unsigned long long)size);
    /*
     * the standard integer type of its size and of the signedness of the type it is compatible with (its
     * canonical type), as gcc names it: `enum : unsigned long long` is `long unsigned int` on x86-64
     */
    die_ref(DW_AT_type, int_type_ref(size, t->ct != NULL && cc_ct_is_integer(t->ct) && t->ct->kind != CC_CT_ENUM
                                               ? cc_ct_is_unsigned(t->ct)
                                               : !is_signed));
    die_end(&g_dw.types, id);
    if (!children) {
        return;
    }
    for (i = 0; i < d->count; ++i) {
        die_start(DW_TAG_enumerator, 0);
        die_str(DW_AT_name, d->items[i].name);
        if (is_signed) {
            die_sdata(DW_AT_const_value, (long long)d->items[i].bits);
        } else {
            die_udata(DW_AT_const_value, (unsigned long long)d->items[i].bits);
        }
        die_end(&g_dw.types, 0);
    }
    die_children_end(&g_dw.types);
}

/**
 * Write the DIE of canonical type @p ct, with no sugar.
 *
 * @param id  its label
 * @param t   the type (CC_DT_PLAIN)
 */
static void emit_plain(int id, const cc_dtype_t *t) {
    const cc_ctype_t *ct = t->ct;
    unsigned q = ct->quals & DW_QUALS;
    char name[64];
    unsigned enc;

    if (q != 0 && ct->kind != CC_CT_ARRAY) {
        unsigned bit = qual_outer(q);

        emit_qual(id, bit, ctype_ref(cc_ct_qualified(ct->unqual, q & ~bit)));
        return;
    }
    if (base_name(ct, name, sizeof(name), &enc)) {
        long bits = (ct->kind == CC_CT_BITINT || ct->kind == CC_CT_UBITINT) ? ct->count : 0;

        emit_base(id, name, dw_size(ct), enc, bits);
        return;
    }
    switch (ct->kind) {
    case CC_CT_COMPLEX:
    case CC_CT_IMAGINARY:
        complex_name(ct->of, ct->kind == CC_CT_IMAGINARY, name, sizeof(name));
        emit_base(id, name, dw_size(ct), ct->kind == CC_CT_IMAGINARY ? DW_ATE_imaginary_float : DW_ATE_complex_float,
                  0);
        return;
    case CC_CT_RECORD:
        emit_record(id, ct->id);
        return;
    case CC_CT_POINTER:
    case CC_CT_NULLPTR:
        if (ct->kind == CC_CT_NULLPTR) {
            die_start(DW_TAG_unspecified_type, 0);
            die_str(DW_AT_name, "nullptr_t");
            die_end(&g_dw.types, id);
            return;
        }
        die_start(DW_TAG_pointer_type, 0);
        die_data1(DW_AT_byte_size, (unsigned)g_dw.ptr);
        die_ref(DW_AT_type, ctype_ref(ct->of));
        die_end(&g_dw.types, id);
        return;
    case CC_CT_ARRAY:
        emit_array(id, t, 0);
        return;
    case CC_CT_FUNCTION:
        emit_subroutine(id, ct->of != NULL ? cc_dt_plain(ct->of) : NULL, NULL, ct);
        return;
    default:
        emit_base(id, "int", 4, DW_ATE_signed, 0);
        return;
    }
}

/**
 * Write the DIE of a queued type.
 *
 * @param id  its label
 * @param t   the type
 */
static void emit_type(int id, const cc_dtype_t *t) {
    switch (t->kind) {
    case CC_DT_PLAIN:
        emit_plain(id, t);
        return;
    case CC_DT_TYPEDEF:
        die_start(DW_TAG_typedef, 0);
        die_str(DW_AT_name, t->name);
        die_ref(DW_AT_type, type_ref(t->of));
        if (type_user_aligned(t)) {
            long ta = cc_ct_align(t->ct, g_dw.tu);

            die_udata(DW_AT_alignment, (unsigned long long)(ta > t->count ? ta : t->count));
        }
        die_end(&g_dw.types, id);
        return;
    case CC_DT_ENUM:
        emit_enum(id, t);
        return;
    case CC_DT_QUAL: {
        unsigned q = t->quals & DW_QUALS;
        unsigned bit = qual_outer(q);

        emit_qual(id, bit, type_ref((q & ~bit) != 0 ? cc_dt_qualified(t->of, q & ~bit) : t->of));
        return;
    }
    case CC_DT_POINTER: {
        unsigned q = t->quals & DW_QUALS;

        if (q != 0) {
            unsigned bit = qual_outer(q);

            emit_qual(id, bit, type_ref(cc_dt_pointer(t->of, q & ~bit)));
            return;
        }
        die_start(DW_TAG_pointer_type, 0);
        die_data1(DW_AT_byte_size, (unsigned)g_dw.ptr);
        die_ref(DW_AT_type, type_ref(t->of));
        die_end(&g_dw.types, id);
        return;
    }
    case CC_DT_ARRAY:
        emit_array(id, t, 0);
        return;
    case CC_DT_FUNCTION:
        emit_subroutine(id, t->of, t->params, t->ct);
        return;
    }
}

/**
 * Write every queued type, and those they queue.
 */
static void drain_types(void) {
    size_t i;

    for (i = 0; i < g_dw.queue_count && !g_dw.oom; ++i) {
        emit_type(g_dw.queue_ids[i], g_dw.queue[i]);
    }
}

/* ---- files ---- */

/** A unit file's line-table number, its .file printed the first time (see cc_dwarf.h). @return the number */
unsigned cc_dwarf_file_number(FILE *fp, int src_file) {
    const cc_src_files_t *files = g_dw.tu != NULL ? g_dw.tu->files : NULL;
    const char *name;

    if (!g_dw.active || src_file <= 0 || files == NULL || (size_t)src_file > files->count) {
        return 1;
    }
    name = files->items[src_file - 1].name;
    if (name == NULL || (g_dw.src_path != NULL && strcmp(name, g_dw.src_path) == 0)) {
        return 1;
    }
    if ((size_t)src_file >= g_dw.file_nos_cap) {
        size_t ncap = g_dw.file_nos_cap == 0 ? 64 : g_dw.file_nos_cap;
        unsigned *next;

        while (ncap <= (size_t)src_file) {
            ncap *= 2;
        }
        next = (unsigned *)realloc(g_dw.file_nos, ncap * sizeof(*next));
        if (next == NULL) {
            g_dw.oom = 1;
            return 1;
        }
        memset(next + g_dw.file_nos_cap, 0, (ncap - g_dw.file_nos_cap) * sizeof(*next));
        g_dw.file_nos = next;
        g_dw.file_nos_cap = ncap;
    }
    if (g_dw.file_nos[src_file] == 0) {
        g_dw.file_nos[src_file] = g_dw.next_file_no++;
        fprintf(fp, "\t.file %u ", g_dw.file_nos[src_file]);
        dw_print_quoted(fp, name);
        fputc('\n', fp);
    }
    return g_dw.file_nos[src_file];
}

/* ---- the module ---- */

/** Start a module's debug information (see cc_dwarf.h). @return 0, or -1 when out of memory */
int cc_dwarf_begin(FILE *fp, const cc_ssa_module_t *m, const char *src_path, int version) {
    cc_dwarf_abort();
    g_dw.active = 1;
    g_dw.version = version == 4 ? 4 : 5;
    g_dw.ptr = (int)cc_target_pointer_size();
    g_dw.m = m;
    g_dw.tu = m != NULL ? m->src_tu : NULL;
    g_dw.src_path = cc_strdup(src_path != NULL ? src_path : "");
    g_dw.next_file_no = 2;
    if (g_dw.src_path == NULL) {
        g_dw.active = 0;
        return -1;
    }
    if (g_dw.version >= 5) {
        /* DWARF 5's line table: file 0 is the unit's own, with the directory it was compiled in */
        char cwd[4096];

        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            snprintf(cwd, sizeof(cwd), ".");
        }
        fputs("\t.file 0 ", fp);
        dw_print_quoted(fp, cwd);
        fputc(' ', fp);
        dw_print_quoted(fp, g_dw.src_path);
        fputc('\n', fp);
    }
    fputs("\t.file 1 ", fp);
    dw_print_quoted(fp, g_dw.src_path);
    fputs("\n\t.text\n.Ltext0:\n", fp);
    return 0;
}

/** Whether debug information is being written (see cc_dwarf.h). @return 1 or 0 */
int cc_dwarf_active(void) {
    return g_dw.active;
}

/**
 * Print the code's first line-table row, at its label: the line of the
 * function's name, as gcc's first row is (see cc_dwarf.h).
 *
 * @param fp  the assembly
 * @param f   the function
 */
void cc_dwarf_func_begin(FILE *fp, const cc_ssa_function_t *f) {
    const cc_function_t *af;

    if (!g_dw.active) {
        return;
    }
    g_dw.slot_count = 0;
    g_dw.loc_line = 0;
    g_dw.loc_col = 0;
    g_dw.loc_file = 0;
    fprintf(fp, ".Ldfb%d:\n", g_dw.next_label + 1);
    af = f->src_fn;
    if (af != NULL) {
        cc_dwarf_loc_at(fp, (unsigned)(af->name_line != 0 ? af->name_line : af->line),
                        (unsigned)(af->name_line != 0 ? af->name_col : af->col), af->src_file);
    } else if (f->src_line != 0) {
        cc_dwarf_loc_at(fp, (unsigned)f->src_line, (unsigned)f->src_col, 0);
    }
}

/**
 * Print a .loc row for a source position, unless it is the last one's
 * (see cc_dwarf.h).
 *
 * @param fp    the assembly
 * @param line  the line; 0 for none
 * @param col   the column
 * @param file  the file (one more than its index); 0 the source file
 */
void cc_dwarf_loc_at(FILE *fp, unsigned line, unsigned col, int file) {
    unsigned n;

    if (!g_dw.active || line == 0 || (line == g_dw.loc_line && col == g_dw.loc_col && file == g_dw.loc_file)) {
        return;
    }
    n = cc_dwarf_file_number(fp, file);
    fprintf(fp, "\t.loc %u %u %u\n", n, line, col);
    g_dw.loc_line = line;
    g_dw.loc_col = col;
    g_dw.loc_file = file;
}

/**
 * Print the .loc row of an IR instruction's position (see cc_dwarf.h).
 *
 * @param fp  the assembly
 * @param in  the instruction
 */
void cc_dwarf_loc(FILE *fp, const cc_ssa_instr_t *in) {
    if (in != NULL) {
        cc_dwarf_loc_at(fp, in->dbg_line, in->dbg_col, in->dbg_file);
    }
}

/** Note where a stack allocation of the function being written is (see cc_dwarf.h). */
void cc_dwarf_func_slot(int value, long cfa_off) {
    if (!g_dw.active) {
        return;
    }
    if (g_dw.slot_count == g_dw.slot_cap) {
        size_t ncap = g_dw.slot_cap == 0 ? 64 : g_dw.slot_cap * 2;
        dw_slot_t *next = (dw_slot_t *)realloc(g_dw.slots, ncap * sizeof(*next));

        if (next == NULL) {
            g_dw.oom = 1;
            return;
        }
        g_dw.slots = next;
        g_dw.slot_cap = ncap;
    }
    g_dw.slots[g_dw.slot_count].value = value;
    g_dw.slots[g_dw.slot_count].cfa_off = cfa_off;
    g_dw.slot_count++;
}

/** cc_ssa_for_each_use() callback: mark a value read. */
static void mark_used(void *ctx, int v, int role) {
    unsigned char *used = (unsigned char *)ctx;

    (void)role;
    if (v >= 0) {
        used[v] = 1;
    }
}

/**
 * Where function @p f's variables are: each variable of its own whose
 * one stack allocation the backend placed, and that something reads or
 * writes -- one the optimizer split, merged or emptied has none.
 *
 * @param f    the function
 * @param out  receives the locations (owned)
 * @param n    receives how many
 */
static void func_locations(const cc_ssa_function_t *f, dw_loc_t **out, size_t *n) {
    unsigned char *used;
    int *seen;
    size_t i;
    size_t k;
    size_t count = 0;
    dw_loc_t *locs;

    *out = NULL;
    *n = 0;
    if (g_dw.m == NULL || g_dw.m->dbg_var_count == 0 || f->value_count <= 0) {
        return;
    }
    used = (unsigned char *)calloc((size_t)f->value_count, 1);
    seen = (int *)calloc(g_dw.m->dbg_var_count, sizeof(*seen));
    locs = (dw_loc_t *)calloc(g_dw.slot_count > 0 ? g_dw.slot_count : 1, sizeof(*locs));
    if (used == NULL || seen == NULL || locs == NULL) {
        free(used);
        free(seen);
        free(locs);
        g_dw.oom = 1;
        return;
    }
    for (i = 0; i < f->instr_count; ++i) {
        if (f->instrs[i].op != CC_SSA_STACKALLOC) {
            cc_ssa_for_each_use(&f->instrs[i], mark_used, used);
        }
    }
    /* a variable's storage is one allocation: two (a copy an inlining made) and it has none */
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (in->op == CC_SSA_STACKALLOC && in->dbg_var > 0 && (size_t)in->dbg_var <= g_dw.m->dbg_var_count) {
            seen[in->dbg_var - 1]++;
        }
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        const cc_ssa_dbg_var_t *v;

        if (in->op != CC_SSA_STACKALLOC || in->dbg_var <= 0 || (size_t)in->dbg_var > g_dw.m->dbg_var_count ||
            seen[in->dbg_var - 1] != 1 || in->dst < 0 || in->dst >= f->value_count || !used[in->dst]) {
            continue;
        }
        v = &g_dw.m->dbg_vars[in->dbg_var - 1];
        if (v->fn != f->src_fn) {
            continue;
        }
        for (k = 0; k < g_dw.slot_count; ++k) {
            if (g_dw.slots[k].value == in->dst) {
                locs[count].var = in->dbg_var - 1;
                locs[count].cfa_off = g_dw.slots[k].cfa_off;
                count++;
                break;
            }
        }
    }
    free(used);
    free(seen);
    *out = locs;
    *n = count;
}

/** End a function's code and record it with its variables' slots (see cc_dwarf.h). */
void cc_dwarf_func_end(FILE *fp, const cc_ssa_function_t *f, const char *section) {
    dw_func_t *df;

    if (!g_dw.active) {
        return;
    }
    if (g_dw.func_count == g_dw.func_cap) {
        size_t ncap = g_dw.func_cap == 0 ? 64 : g_dw.func_cap * 2;
        dw_func_t *next = (dw_func_t *)realloc(g_dw.funcs, ncap * sizeof(*next));

        if (next == NULL) {
            g_dw.oom = 1;
            return;
        }
        g_dw.funcs = next;
        g_dw.func_cap = ncap;
    }
    df = &g_dw.funcs[g_dw.func_count];
    memset(df, 0, sizeof(*df));
    df->f = f;
    df->label = ++g_dw.next_label;
    df->section = cc_strdup(section != NULL ? section : ".text");
    if (df->section == NULL) {
        g_dw.oom = 1;
        return;
    }
    func_locations(f, &df->locs, &df->loc_count);
    g_dw.func_count++;
    g_dw.slot_count = 0;
    fprintf(fp, ".Ldfe%d:\n", df->label);
}

/**
 * The symbol file-scope object or function @p name has: its asm label, or
 * the name.
 *
 * @param name  the name in the source
 * @return      the symbol
 */
static const char *symbol_of(const char *name) {
    size_t i;

    for (i = 0; g_dw.m != NULL && i < g_dw.m->asm_label_count; ++i) {
        if (strcmp(g_dw.m->asm_label_names[i], name) == 0) {
            const char *l = g_dw.m->asm_labels[i];

            return l[0] == '*' ? l + 1 : l;
        }
    }
    return name;
}

/**
 * The module's object named @p sym, if it defines it: one with storage
 * here, not only declared.
 *
 * @param sym  the symbol
 * @return     the object, or NULL
 */
static const cc_ssa_global_t *defined_global(const char *sym) {
    size_t i;

    for (i = 0; g_dw.m != NULL && i < g_dw.m->global_count; ++i) {
        const cc_ssa_global_t *g = &g_dw.m->globals[i];

        if (g->name != NULL && strcmp(g->name, sym) == 0 && (g->storage & CC_STORAGE_EXTERN) == 0) {
            return g;
        }
    }
    return NULL;
}

/** A set of symbols: their names in order, and a hash index of them. */
typedef struct {
    cc_strvec_t names; /**< in the order added */
    int *table;        /**< open addressing over indexes + 1 */
    size_t cap;
} dw_symset_t;

/**
 * Whether set @p s holds @p sym.
 *
 * @param s    the set
 * @param sym  the symbol
 * @return     1 or 0
 */
static int sym_referenced(const dw_symset_t *s, const char *sym) {
    size_t i;

    if (s->cap == 0) {
        return 0;
    }
    i = dw_hash_str(sym) & (s->cap - 1);
    while (s->table[i] != 0) {
        if (strcmp(s->names.items[s->table[i] - 1], sym) == 0) {
            return 1;
        }
        i = (i + 1) & (s->cap - 1);
    }
    return 0;
}

/**
 * Add @p sym to set @p s, unless it holds it.
 *
 * @param s    the set
 * @param sym  the symbol
 */
static void symset_add(dw_symset_t *s, const char *sym) {
    size_t i;

    if (sym_referenced(s, sym)) {
        return;
    }
    if ((s->names.count + 1) * 2 > s->cap) {
        size_t ncap = s->cap == 0 ? 256 : s->cap * 2;
        int *nt = (int *)calloc(ncap, sizeof(*nt));

        if (nt == NULL) {
            g_dw.oom = 1;
            return;
        }
        for (i = 0; i < s->names.count; ++i) {
            size_t j = dw_hash_str(s->names.items[i]) & (ncap - 1);

            while (nt[j] != 0) {
                j = (j + 1) & (ncap - 1);
            }
            nt[j] = (int)i + 1;
        }
        free(s->table);
        s->table = nt;
        s->cap = ncap;
    }
    if (cc_strvec_push(&s->names, sym) != 0) {
        g_dw.oom = 1;
        return;
    }
    i = dw_hash_str(sym) & (s->cap - 1);
    while (s->table[i] != 0) {
        i = (i + 1) & (s->cap - 1);
    }
    s->table[i] = (int)s->names.count;
}

/**
 * Free set @p s.
 *
 * @param s  the set
 */
static void symset_free(dw_symset_t *s) {
    cc_strvec_free(&s->names);
    free(s->table);
    memset(s, 0, sizeof(*s));
}

/**
 * Add the declaration attributes of a DIE: its file and line.
 *
 * @param fp        the assembly (for a file's .file directive)
 * @param src_file  one more than its file's index; 0 unknown
 * @param line      its line; 0 unknown
 */
static void die_decl(FILE *fp, int src_file, size_t line) {
    if (line == 0) {
        return;
    }
    die_udata(DW_AT_decl_file, cc_dwarf_file_number(fp, src_file));
    die_udata(DW_AT_decl_line, (unsigned long long)line);
}

/**
 * Write the DIEs of the file-scope objects: the ones the module defines,
 * with their addresses, and the ones only declared here that its code
 * uses.
 *
 * @param fp    the assembly
 * @param refs  the symbols the module's code names
 */
static void emit_globals(FILE *fp, const dw_symset_t *refs) {
    const cc_translation_unit_t *tu = g_dw.tu;
    size_t i;

    for (i = 0; tu != NULL && i < tu->global_count; ++i) {
        const cc_global_t *g = &tu->globals[i];
        const char *sym;
        const cc_ssa_global_t *sg;
        int is_static;

        if (g->name == NULL || g->ctype == NULL || g->ctype->kind == CC_CT_FUNCTION || g->asm_register != NULL) {
            continue;
        }
        if (i > 0 && cc_tu_global_first(tu, g->name) != (long)i) {
            /* (one DIE per name: the first declaration, which the others merged into) */
            continue;
        }
        sym = g->asm_label != NULL ? (g->asm_label[0] == '*' ? g->asm_label + 1 : g->asm_label) : symbol_of(g->name);
        sg = defined_global(sym);
        is_static = (g->storage & CC_STORAGE_STATIC) != 0;
        if (sg == NULL && (is_static || (!sym_referenced(refs, sym) && !sym_referenced(refs, g->name)))) {
            continue;
        }
        die_start(DW_TAG_variable, 0);
        die_str(DW_AT_name, g->name);
        die_decl(fp, g->src_file, g->line);
        die_ref(DW_AT_type, type_ref(cc_dt_fit(g->dtype, g->ctype)));
        if (!is_static) {
            die_flag(DW_AT_external);
        }
        if (sg == NULL) {
            die_flag(DW_AT_declaration);
        } else if ((g->storage & CC_STORAGE_THREAD_LOCAL) == 0) {
            die_loc_addr(sym);
        }
        die_end(&g_dw.main, 0);
    }
}

/**
 * The definition of function @p f in the unit: its source function, or
 * failing that the best of its name.
 *
 * @param f  the function
 * @return   the definition, or NULL
 */
static const cc_function_t *func_source(const cc_ssa_function_t *f) {
    if (f->src_fn != NULL) {
        return f->src_fn;
    }
    return g_dw.tu != NULL && f->name != NULL ? cc_tu_function_best(g_dw.tu, f->name) : NULL;
}

static void emit_vars(FILE *fp, const cc_function_t *af, const dw_loc_t *locs, size_t loc_count);

/**
 * Index the module's variables and the recorded calls by function, so
 * that a function's are found without a walk of them all: a list per
 * function, in order.
 */
static void build_indexes(void) {
    size_t i;

    if (g_dw.m != NULL && g_dw.m->dbg_var_count > 0) {
        g_dw.var_next = (int *)calloc(g_dw.m->dbg_var_count, sizeof(int));
        if (g_dw.var_next == NULL) {
            g_dw.oom = 1;
            return;
        }
        /* backwards, each pushed on its function's list: the lists come out in order */
        for (i = g_dw.m->dbg_var_count; i > 0; --i) {
            const cc_function_t *fn = g_dw.m->dbg_vars[i - 1].fn;

            if (fn == NULL) {
                continue;
            }
            g_dw.var_next[i - 1] = pm_get(&g_dw.var_head, fn);
            if (pm_put(&g_dw.var_head, fn, (int)i) != 0) {
                g_dw.oom = 1;
                return;
            }
        }
    }
    if (g_calls.count > 0) {
        g_dw.call_next = (int *)calloc(g_calls.count, sizeof(int));
        if (g_dw.call_next == NULL) {
            g_dw.oom = 1;
            return;
        }
        for (i = g_calls.count; i > 0; --i) {
            g_dw.call_next[i - 1] = pm_get(&g_dw.call_head, g_calls.caller[i - 1]);
            if (pm_put(&g_dw.call_head, g_calls.caller[i - 1], (int)i) != 0) {
                g_dw.oom = 1;
                return;
            }
        }
    }
}

/**
 * Whether a function's DIE has children: parameters, `...`, or variables
 * lowering recorded.
 *
 * @param af  the function
 * @return    1 or 0
 */
static int subprogram_has_children(const cc_function_t *af) {
    return af->param_count > 0 || af->is_variadic || pm_get(&g_dw.var_head, af) != 0;
}

/**
 * Write the DIE of a function the module defines, with its parameters and
 * variables.
 *
 * @param fp  the assembly
 * @param df  the function, as the backend wrote it
 */
static void emit_subprogram(FILE *fp, const dw_func_t *df) {
    const cc_ssa_function_t *f = df->f;
    const cc_function_t *af = func_source(f);
    char label[32];
    int has_children;

    if (af == NULL) {
        return;
    }
    has_children = subprogram_has_children(af);
    die_start(DW_TAG_subprogram, has_children);
    if ((f->storage & CC_STORAGE_STATIC) == 0) {
        die_flag(DW_AT_external);
    }
    die_str(DW_AT_name, af->name != NULL ? af->name : f->name);
    if (af->name != NULL && f->name != NULL && strcmp(af->name, f->name) != 0) {
        die_str(DW_AT_linkage_name, f->name);
    }
    die_decl(fp, af->src_file, af->name_line != 0 ? af->name_line : af->line);
    if (af->has_prototype) {
        die_flag(DW_AT_prototyped);
    }
    die_ref(DW_AT_type, type_ref(cc_dt_fit(af->ret_dtype, af->ret_ctype)));
    if ((af->attr_flags & CC_ATTR_NORETURN) != 0) {
        die_flag(DW_AT_noreturn);
    }
    snprintf(label, sizeof(label), ".Ldfb%d", df->label);
    die_addr(DW_AT_low_pc, label);
    die_attr(DW_AT_high_pc, DW_FORM_data4);
    db_printf(&g_dw.die, "\t.long .Ldfe%d-.Ldfb%d\n", df->label, df->label);
    die_attr(DW_AT_frame_base, DW_FORM_exprloc);
    db_printf(&g_dw.die, "\t.uleb128 1\n\t.byte 0x%x\n", DW_OP_call_frame_cfa);
    die_end(&g_dw.main, 0);
    if (has_children) {
        emit_vars(fp, af, df->locs, df->loc_count);
    }
}

/**
 * Write the DIEs of a function's parameters and variables, and end its
 * children.
 *
 * @param fp         the assembly
 * @param af         the function
 * @param locs       where its stack slots are, or NULL (an abstract instance: none)
 * @param loc_count  how many
 */
static void emit_vars(FILE *fp, const cc_function_t *af, const dw_loc_t *locs, size_t loc_count) {
    int at;
    size_t k;

    for (at = pm_get(&g_dw.var_head, af); at > 0; at = g_dw.var_next[at - 1]) {
        size_t i = (size_t)at - 1;
        const cc_ssa_dbg_var_t *v = &g_dw.m->dbg_vars[i];
        int anon;

        anon = v->is_param && strncmp(v->name, "__anon_param_", 13) == 0;
        die_start(v->is_param ? DW_TAG_formal_parameter : DW_TAG_variable, 0);
        if (!anon) {
            die_str(DW_AT_name, v->name);
        }
        die_decl(fp, v->src_file, v->line);
        die_ref(DW_AT_type, type_ref(cc_dt_fit(v->dtype, v->ctype)));
        if (v->is_extern) {
            die_flag(DW_AT_external);
            die_flag(DW_AT_declaration);
        } else if (v->sym != NULL) {
            if (defined_global(v->sym) != NULL) {
                die_loc_addr(v->sym);
            }
        } else {
            for (k = 0; k < loc_count; ++k) {
                if (locs[k].var == (int)i) {
                    die_loc_fbreg(locs[k].cfa_off);
                    break;
                }
            }
        }
        die_end(&g_dw.main, 0);
    }
    if (af->is_variadic) {
        die_start(DW_TAG_unspecified_parameters, 0);
        die_end(&g_dw.main, 0);
    }
    die_children_end(&g_dw.main);
}

/**
 * Write the abstract instance of a function inlined into the code written
 * and not written itself, as gcc does: its declaration's attributes,
 * DW_AT_inline, and its parameters and variables without locations.
 *
 * @param fp  the assembly
 * @param af  the function
 */
static void emit_abstract(FILE *fp, const cc_function_t *af) {
    int has_children = subprogram_has_children(af);

    die_start(DW_TAG_subprogram, has_children);
    if ((af->storage & CC_STORAGE_STATIC) == 0) {
        die_flag(DW_AT_external);
    }
    die_str(DW_AT_name, af->name);
    die_decl(fp, af->src_file, af->name_line != 0 ? af->name_line : af->line);
    if (af->has_prototype) {
        die_flag(DW_AT_prototyped);
    }
    die_ref(DW_AT_type, type_ref(cc_dt_fit(af->ret_dtype, af->ret_ctype)));
    if ((af->attr_flags & CC_ATTR_NORETURN) != 0) {
        die_flag(DW_AT_noreturn);
    }
    /* DW_INL_declared_inlined, or DW_INL_inlined for one inlined though not declared inline */
    die_data1(DW_AT_inline, (af->storage & CC_STORAGE_INLINE) != 0 ? 3 : 1);
    die_end(&g_dw.main, 0);
    if (has_children) {
        emit_vars(fp, af, NULL, 0);
    }
}

/**
 * Find the functions inlined into the code written: those the functions
 * written called before inlining, and those they called, that were not
 * written themselves (cc_dwarf_note_calls()).  What all of them named
 * before the optimizer saw them goes into @p refs, as gcc describes the
 * objects the source uses, not only the code: a per-CPU variable an
 * inlined accessor read is declared in the unit.
 *
 * @param refs       the symbols named, which this adds to
 * @param out        receives the inlined functions (owned), after the written ones' own: [*first, *n)
 * @param first      receives where the inlined ones start in @p out
 * @param n          receives how many there are in all
 */
static void find_inlined(dw_symset_t *refs, const cc_function_t ***out, size_t *first, size_t *n) {
    const cc_function_t **work;
    size_t nwork = 0;
    size_t cap;
    size_t i;
    size_t k;
    pmap_t seen;

    *out = NULL;
    *first = 0;
    *n = 0;
    if (g_calls.count == 0 || g_dw.tu == NULL) {
        return;
    }
    memset(&seen, 0, sizeof(seen));
    cap = g_dw.func_count + 64;
    work = (const cc_function_t **)malloc(cap * sizeof(*work));
    if (work == NULL) {
        g_dw.oom = 1;
        return;
    }
    for (i = 0; i < g_dw.func_count; ++i) {
        const cc_function_t *af = func_source(g_dw.funcs[i].f);

        if (af != NULL && pm_get(&seen, af) == 0) {
            if (pm_put(&seen, af, 1) != 0) {
                g_dw.oom = 1;
                break;
            }
            work[nwork++] = af;
        }
    }
    *first = nwork;
    for (i = 0; i < nwork && !g_dw.oom; ++i) {
        int at;

        for (at = pm_get(&g_dw.call_head, work[i]); at > 0; at = g_dw.call_next[at - 1]) {
            const cc_function_t *callee;

            k = (size_t)at - 1;
            symset_add(refs, g_calls.callee[k]);
            callee = cc_tu_function_best(g_dw.tu, g_calls.callee[k]);
            if (callee == NULL || !callee->has_body || pm_get(&seen, callee) != 0) {
                continue;
            }
            if (pm_put(&seen, callee, 2) != 0) {
                g_dw.oom = 1;
                break;
            }
            if (nwork == cap) {
                const cc_function_t **next = (const cc_function_t **)realloc(work, cap * 2 * sizeof(*work));

                if (next == NULL) {
                    g_dw.oom = 1;
                    break;
                }
                work = next;
                cap *= 2;
            }
            work[nwork++] = callee;
        }
    }
    pm_free(&seen);
    *out = work;
    *n = nwork;
}

/**
 * Write the DIEs of the functions the module's code calls but does not
 * define, as declarations.
 *
 * @param fp    the assembly
 * @param refs  the symbols the code names
 */
static void emit_func_decls(FILE *fp, const dw_symset_t *refs) {
    const cc_translation_unit_t *tu = g_dw.tu;
    size_t i;
    size_t k;

    for (i = 0; tu != NULL && i < refs->names.count; ++i) {
        const char *sym = refs->names.items[i];
        const cc_function_t *af = NULL;
        int defined = 0;

        for (k = 0; k < g_dw.func_count; ++k) {
            if (g_dw.funcs[k].f->name != NULL && strcmp(g_dw.funcs[k].f->name, sym) == 0) {
                defined = 1;
                break;
            }
        }
        if (defined) {
            continue;
        }
        {
            long at;

            for (at = cc_tu_function_first(tu, sym); at >= 0; at = cc_tu_function_next(tu, at)) {
                const cc_function_t *c = &tu->funcs[at];

                if (c->ret_ctype != NULL && (af == NULL || (c->has_prototype && !af->has_prototype))) {
                    af = c;
                }
            }
        }
        if (af == NULL || af->has_body || (af->storage & CC_STORAGE_STATIC) != 0) {
            continue;
        }
        die_start(DW_TAG_subprogram, af->param_count > 0 || af->is_variadic);
        die_flag(DW_AT_external);
        die_str(DW_AT_name, af->name);
        die_decl(fp, af->src_file, af->line);
        if (af->has_prototype) {
            die_flag(DW_AT_prototyped);
        }
        die_ref(DW_AT_type, type_ref(cc_dt_fit(af->ret_dtype, af->ret_ctype)));
        die_flag(DW_AT_declaration);
        die_end(&g_dw.main, 0);
        if (af->param_count == 0 && !af->is_variadic) {
            continue;
        }
        for (k = 0; k < af->param_count; ++k) {
            die_start(DW_TAG_formal_parameter, 0);
            die_ref(DW_AT_type, type_ref(cc_dt_fit(af->params[k].dtype, af->params[k].ctype)));
            die_end(&g_dw.main, 0);
        }
        if (af->is_variadic) {
            die_start(DW_TAG_unspecified_parameters, 0);
            die_end(&g_dw.main, 0);
        }
        die_children_end(&g_dw.main);
    }
}

/**
 * The symbols the functions written name: what they take the address of
 * and what they call.
 *
 * @param out  receives the symbols, once each
 */
static void build_refs(dw_symset_t *out) {
    size_t i;
    size_t j;

    for (i = 0; i < g_dw.func_count; ++i) {
        const cc_ssa_function_t *f = g_dw.funcs[i].f;

        for (j = 0; j < f->instr_count; ++j) {
            const cc_ssa_instr_t *in = &f->instrs[j];

            if ((in->op == CC_SSA_GADDR || in->op == CC_SSA_CALL) && in->sym != NULL) {
                symset_add(out, in->sym);
            }
        }
    }
}

/**
 * The C standard's DW_LANG for the language mode.
 *
 * @return the DW_LANG
 */
static unsigned dw_language(void) {
    if (cc_lang_at_least(2011)) {
        return DW_LANG_C11;
    }
    if (cc_lang_at_least(1999)) {
        return DW_LANG_C99;
    }
    return DW_LANG_C89;
}

/**
 * Whether every function written is in .text, so that the unit's code is
 * one range from .Ltext0 to .Letext0.
 *
 * @return 1 or 0
 */
static int all_in_text(void) {
    size_t i;

    for (i = 0; i < g_dw.func_count; ++i) {
        if (strcmp(g_dw.funcs[i].section, ".text") != 0) {
            return 0;
        }
    }
    return 1;
}

/**
 * Write the unit's code ranges, for a unit whose functions are in more
 * than one section: .debug_rnglists (DWARF 5) or .debug_ranges (4).
 *
 * @param fp  the assembly
 */
static void emit_ranges(FILE *fp) {
    const char *addr = g_dw.ptr == 8 ? ".quad" : ".long";
    int any_text = 0;
    size_t i;

    for (i = 0; i < g_dw.func_count; ++i) {
        any_text |= strcmp(g_dw.funcs[i].section, ".text") == 0;
    }
    if (g_dw.version >= 5) {
        fputs("\t.section .debug_rnglists,\"\",@progbits\n.Ldebug_rnglists0:\n"
              "\t.long .Ldebug_rnglists_end0-.Ldebug_rnglists_start0\n.Ldebug_rnglists_start0:\n"
              "\t.value 5\n",
              fp);
        fprintf(fp, "\t.byte %d\n\t.byte 0\n\t.long 0\n.Ldebug_ranges0:\n", g_dw.ptr);
        if (any_text) {
            fprintf(fp, "\t.byte %d\n\t%s .Ltext0\n\t%s .Letext0\n", DW_RLE_start_end, addr, addr);
        }
        for (i = 0; i < g_dw.func_count; ++i) {
            if (strcmp(g_dw.funcs[i].section, ".text") != 0) {
                fprintf(fp, "\t.byte %d\n\t%s .Ldfb%d\n\t%s .Ldfe%d\n", DW_RLE_start_end, addr, g_dw.funcs[i].label,
                        addr, g_dw.funcs[i].label);
            }
        }
        fprintf(fp, "\t.byte %d\n.Ldebug_rnglists_end0:\n", DW_RLE_end_of_list);
        return;
    }
    fputs("\t.section .debug_ranges,\"\",@progbits\n.Ldebug_ranges0:\n", fp);
    if (any_text) {
        fprintf(fp, "\t%s .Ltext0\n\t%s .Letext0\n", addr, addr);
    }
    for (i = 0; i < g_dw.func_count; ++i) {
        if (strcmp(g_dw.funcs[i].section, ".text") != 0) {
            fprintf(fp, "\t%s .Ldfb%d\n\t%s .Ldfe%d\n", addr, g_dw.funcs[i].label, addr, g_dw.funcs[i].label);
        }
    }
    fprintf(fp, "\t%s 0\n\t%s 0\n", addr, addr);
}

/** Write the debug sections and forget the module (see cc_dwarf.h). @return 0, or -1 when out of memory */
int cc_dwarf_finish(FILE *fp, cc_diag_t *diag) {
    dw_symset_t refs;
    const cc_function_t **inl = NULL;
    size_t inl_first = 0;
    size_t inl_count = 0;
    size_t i;
    int ranges;
    char cwd[4096];

    if (!g_dw.active) {
        return 0;
    }
    memset(&refs, 0, sizeof(refs));
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        snprintf(cwd, sizeof(cwd), ".");
    }
    fputs("\t.text\n.Letext0:\n", fp);
    build_refs(&refs);
    ranges = g_dw.func_count > 0 && !all_in_text();

    /* the unit */
    die_start(DW_TAG_compile_unit, 1);
    die_str(DW_AT_producer, "Substrate C Compiler v0.1");
    die_data1(DW_AT_language, dw_language());
    die_str(DW_AT_name, g_dw.src_path);
    die_str(DW_AT_comp_dir, cwd);
    if (ranges) {
        die_addr(DW_AT_low_pc, "0");
        die_attr(DW_AT_ranges, DW_FORM_sec_offset);
        db_printf(&g_dw.die, "\t.long .Ldebug_ranges0\n");
    } else if (g_dw.func_count > 0) {
        die_addr(DW_AT_low_pc, ".Ltext0");
        die_attr(DW_AT_high_pc, DW_FORM_data4);
        db_printf(&g_dw.die, "\t.long .Letext0-.Ltext0\n");
    }
    if (g_dw.func_count > 0) {
        /* (the assembler's line table, made of the .loc rows: there are none without code) */
        die_attr(DW_AT_stmt_list, DW_FORM_sec_offset);
        db_printf(&g_dw.die, "\t.long .Ldebug_line0\n");
    }
    {
        dbuf_t cu;

        memset(&cu, 0, sizeof(cu));
        die_end(&cu, 0);
        build_indexes();
        find_inlined(&refs, &inl, &inl_first, &inl_count);
        emit_globals(fp, &refs);
        for (i = 0; i < g_dw.func_count; ++i) {
            emit_subprogram(fp, &g_dw.funcs[i]);
        }
        for (i = inl_first; i < inl_count; ++i) {
            emit_abstract(fp, inl[i]);
        }
        free(inl);
        emit_func_decls(fp, &refs);
        drain_types();
        if (g_dw.oom || cu.oom || g_dw.types.oom || g_dw.main.oom || g_dw.die.oom) {
            db_free(&cu);
            symset_free(&refs);
            calls_free();
            cc_dwarf_abort();
            if (diag != NULL) {
                snprintf(diag->message, sizeof(diag->message), "out of memory writing debug information");
            }
            return -1;
        }
        fputs("\t.section .debug_info,\"\",@progbits\n.Ldebug_info0:\n"
              "\t.long .Ldebug_info_end0-.Ldebug_info_start0\n.Ldebug_info_start0:\n",
              fp);
        if (g_dw.version >= 5) {
            fprintf(fp, "\t.value 5\n\t.byte %d\n\t.byte %d\n\t.long .Ldebug_abbrev0\n", DW_UT_compile, g_dw.ptr);
        } else {
            fprintf(fp, "\t.value 4\n\t.long .Ldebug_abbrev0\n\t.byte %d\n", g_dw.ptr);
        }
        fputs(cu.p != NULL ? cu.p : "", fp);
        fputs(g_dw.types.p != NULL ? g_dw.types.p : "", fp);
        fputs(g_dw.main.p != NULL ? g_dw.main.p : "", fp);
        fputs("\t.byte 0\n.Ldebug_info_end0:\n", fp);
        db_free(&cu);
    }

    /* the abbreviations */
    fputs("\t.section .debug_abbrev,\"\",@progbits\n.Ldebug_abbrev0:\n", fp);
    for (i = 0; i < g_dw.abbrev_count; ++i) {
        const dw_abbrev_t *a = &g_dw.abbrevs[i];
        unsigned k;

        fprintf(fp, "\t.uleb128 %lu\n\t.uleb128 0x%x\n\t.byte %d\n", (unsigned long)i + 1, a->tag, a->children);
        for (k = 0; k < a->n; ++k) {
            fprintf(fp, "\t.uleb128 0x%x\n\t.uleb128 0x%x\n", a->at[k], a->form[k]);
        }
        fputs("\t.byte 0\n\t.byte 0\n", fp);
    }
    fputs("\t.byte 0\n", fp);

    if (ranges) {
        emit_ranges(fp);
    }
    if (g_dw.func_count > 0) {
        fputs("\t.section .debug_line,\"\",@progbits\n.Ldebug_line0:\n", fp);
    }
    fputs("\t.section .debug_str,\"\",@progbits\n", fp);
    for (i = 0; i < g_dw.strs.count; ++i) {
        fprintf(fp, ".Ldstr%lu:\n\t.string ", (unsigned long)i);
        dw_print_quoted(fp, g_dw.strs.items[i]);
        fputc('\n', fp);
    }
    symset_free(&refs);
    calls_free();
    cc_dwarf_abort();
    return 0;
}

/** Forget the module without writing anything (see cc_dwarf.h). */
void cc_dwarf_abort(void) {
    size_t i;

    for (i = 0; i < g_dw.func_count; ++i) {
        free(g_dw.funcs[i].section);
        free(g_dw.funcs[i].locs);
    }
    free(g_dw.funcs);
    free(g_dw.slots);
    free(g_dw.src_path);
    free(g_dw.file_nos);
    db_free(&g_dw.types);
    db_free(&g_dw.main);
    db_free(&g_dw.die);
    free(g_dw.abbrevs);
    for (i = 0; i < g_dw.strs.count; ++i) {
        free(g_dw.strs.items[i]);
    }
    free(g_dw.strs.items);
    free(g_dw.strs.table);
    pm_free(&g_dw.type_ids);
    pm_free(&g_dw.var_head);
    pm_free(&g_dw.call_head);
    free(g_dw.var_next);
    free(g_dw.call_next);
    free(g_dw.rec_ua);
    free(g_dw.queue);
    free(g_dw.queue_ids);
    memset(&g_dw, 0, sizeof(g_dw));
}
