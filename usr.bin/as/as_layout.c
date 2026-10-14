#include "as_elf_emit.h"

#include "as_arm.h"

#include "as_x86_encode.h"

#include "as_x86_regs.h"

#include "as_reloc_op.h"

#include "as_cfi.h"

#include "as_dwline.h"

#include "as_x86_vec.h"



#include "as_data.h"

#include "as_strmap.h"

#include "as_util.h"

#include "elfobj.h"

#include <ctype.h>

#include <errno.h>

#include <limits.h>

#include <stdarg.h>

#include <stdint.h>

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <strings.h>

#include <sys/stat.h>

#include <unistd.h>

#include "as_emit_int.h"

/* The table: the run's, and not an emitter's alone -- an instruction is
 * encoded as it is read (as_x86_precode()), before there is an emitter. */
static struct {
    struct text_code *slots;
    size_t cap;
    size_t count;
} text_codes;

int parse_symbol_addend_arg(const char *arg, char **sym_out, int64_t *add_out);

static int stmt_code_find(emit_ctx_t *ctx, const as_stmt_t *st, uint64_t sec_off, unsigned x86_code_bits, int anywhere,
                          unsigned char *code, size_t code_cap, size_t *code_len);

static void stmt_code_keep(emit_ctx_t *ctx, const as_stmt_t *st, uint64_t sec_off, unsigned x86_code_bits,
                           const unsigned char *code, size_t code_len);

static int stmt_code_is_anywhere(emit_ctx_t *ctx, const as_stmt_t *st);

static int encode_x86_stmt_by_text(emit_ctx_t *ctx, const as_elf_cfg_t *cfg, unsigned x86_code_bits,
                                   const as_stmt_t *st, unsigned char *code, size_t code_cap, size_t *code_len,
                                   char *encerr, size_t encerr_sz);

int append_incbin_to_bytebuf(emit_ctx_t *ctx, const as_stmt_t *st, bytebuf_t *buf, const as_directive_t *d);

static int parsed_stmt_index(emit_ctx_t *ctx, const as_stmt_t *needle, size_t *idx_out);

static int label_stmt_index(emit_ctx_t *ctx, const char *name, size_t *idx_out);

static int stmt_range_size_in_section(emit_ctx_t *ctx, const char *section_name, size_t start_idx, size_t end_idx,
                                      uint64_t sec_off, unsigned x86_code_bits, uint64_t *size_out);

static int linux_alt_original_range_size(emit_ctx_t *ctx, const char *section_name, size_t start_idx, size_t end_idx,
                                         unsigned x86_code_bits, uint64_t *size_out);

static int stmt_declared_label_section(emit_ctx_t *ctx, const as_stmt_t *target_st, const char **section_out);

static int eval_direct_local_branch_target(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *base_st,
                                           uint64_t base_off, unsigned x86_code_bits, const as_expr_t *rel_expr,
                                           size_t branch_len, long long *abs_target_out);

static int x86_branch_may_be_short(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st,
                                   const as_expr_t *e, unsigned x86_code_bits);

static int build_stmt_section_at(emit_ctx_t *ctx);

static const as_stmt_t *stmt_fold_label_diffs(emit_ctx_t *ctx, const as_stmt_t *st, unsigned x86_code_bits,
                                              as_stmt_t *tmp, as_operand_t ops[4], as_expr_t consts[4]);

static const uint32_t *get_section_prefix_sums(emit_ctx_t *ctx, const char *section_name,
                                               unsigned x86_code_bits);

int is_local_temp_symbol_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    return name[0] == '.' && name[1] == 'L';
}

/*
 * The labels of the source that are its own: defined in it, and named
 * by no .globl, .global or .weak.  A branch to one in the branch's own
 * section goes a distance that is known here, as a branch to a .L label
 * does -- `jmp l` / `l: ret` is EB 00 and has no relocation -- where it
 * had been the long form with a relocation for the linker to work out
 * what the assembler knew.  Sorted; made by as_elf_mark_far_branches().
 */
static char **near_names;

static size_t near_name_count;

static int near_name_cmp(const void *a, const void *b) {
    return as_strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int name_is_near_label(const char *name) {
    return name != NULL && near_name_count != 0 &&
           bsearch(&name, near_names, near_name_count, sizeof(near_names[0]), near_name_cmp) != NULL;
}

static void near_names_free(void) {
    size_t i;

    for (i = 0; i < near_name_count; ++i) {
        free(near_names[i]);
    }
    free(near_names);
    near_names = NULL;
    near_name_count = 0;
}

/* Whether what a branch names is a label whose place the assembler
 * knows: a compiler's .L label, or one of the source's own. */
int expr_is_local_temp_symbol(const as_expr_t *e) {
    /* (With a relocation operator on it, it is the linker's to find,
     * wherever it is.) */
    return e != NULL && e->kind == AS_EXPR_SYMBOL && e->reloc_op == NULL &&
           (is_local_temp_symbol_name(e->symbol) || name_is_near_label(e->symbol));
}

int asm_var_lookup(const emit_ctx_t *ctx, const char *name, long long *out) {
    size_t i;

    if (ctx == NULL || name == NULL || out == NULL) {
        return -1;
    }
    for (i = ctx->asm_var_count; i > 0; --i) {
        const asm_var_t *v = &ctx->asm_vars[i - 1];
        if (v->name != NULL && as_strcmp(v->name, name) == 0) {
            *out = v->value;
            return 0;
        }
    }
    return -1;
}

size_t hash_name(const char *name) {
    size_t h;
    size_t prime;

#if UINTPTR_MAX > 0xFFFFFFFFu
    h = 1469598103934665603ull;
    prime = 1099511628211ull;
#else
    h = 2166136261u;
    prime = 16777619u;
#endif

    while (name != NULL && *name != '\0') {
        h ^= (unsigned char)*name++;
        h *= prime;
    }
    return h;
}

static int bytebuf_reserve(bytebuf_t *b, size_t extra) {
    unsigned char *next;

    /* Reject before either compare uses b->len + extra: if the sum
     * wraps a small value, both the early-return below ("already
     * fits") and the doubling loop ("ncap < need") silently succeed
     * without growing the buffer, and the next memcpy writes past
     * the allocation.  Reachable on 32-bit hosts via large .fill /
     * .skip / sized data directives where the size came from a
     * parsed expression. */
    if (extra > SIZE_MAX - b->len) {
        return -1;
    }
    if (b->len + extra <= b->cap) {
        return 0;
    }
    {
        size_t ncap = b->cap == 0 ? 256 : b->cap;
        while (ncap < b->len + extra) {
            if (ncap > SIZE_MAX / 2) {
                return -1;
            }
            ncap *= 2;
        }
        next = (unsigned char *)realloc(b->data, ncap);
        if (next == NULL) {
            return -1;
        }
        b->data = next;
        b->cap = ncap;
    }
    return 0;
}

int bytebuf_append(bytebuf_t *b, const void *p, size_t n) {
    if (bytebuf_reserve(b, n) != 0) {
        return -1;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
    return 0;
}

int bytebuf_append_zeros(bytebuf_t *b, size_t n) {
    /* (None is none: a buffer that has had nothing has no storage to
     * hand to memset, which may not be given NULL even for no bytes.) */
    if (n == 0) {
        return 0;
    }
    if (bytebuf_reserve(b, n) != 0) {
        return -1;
    }
    memset(b->data + b->len, 0, n);
    b->len += n;
    return 0;
}

int bytebuf_append_u64_le(bytebuf_t *b, uint64_t v, unsigned width) {
    unsigned i;

    if (width < 1 || width > 8) {
        return -1;
    }
    if (bytebuf_reserve(b, width) != 0) {
        return -1;
    }
    for (i = 0; i < width; ++i) {
        b->data[b->len + i] = (unsigned char)((v >> (i * 8)) & 0xffu);
    }
    b->len += width;
    return 0;
}

/* Whether a directive has an argument at this place that is not empty
 * (`.p2align 4,,10` has an empty second). */
int directive_has_arg(const as_directive_t *d, size_t i) {
    return d != NULL && i < d->arg_count && d->args[i] != NULL && d->args[i][0] != '\0';
}

/*
 * The third argument of .align, .balign and .p2align: the most padding
 * that is wanted.  If reaching the alignment takes more, there is to be
 * none.  Returns 1 if `need` bytes are too many, 0 if not or if there is
 * no limit, -1 if the limit is not a number.
 */
int align_exceeds_max_skip(const as_directive_t *d, size_t need) {
    long long max_skip;

    if (!directive_has_arg(d, 2)) {
        return 0;
    }
    if (as_expr_eval_string(d->args[2], NULL, NULL, &max_skip) != AS_EXPR_EVAL_OK || max_skip < 0) {
        return -1;
    }
    return (unsigned long long)need > (unsigned long long)max_skip ? 1 : 0;
}

/*
 * The padding of an alignment: `need` bytes of `fill`.  .balignw and
 * .balignl, and the two of .p2align, pad with a pattern of two bytes or
 * four where one is written, and then the padding must be a whole number
 * of patterns: GNU as refuses "alignment padding (7 bytes) not a multiple
 * of 2", and so does this (-2).  -1 for no memory.
 */
int append_align_fill(bytebuf_t *buf, const as_directive_t *d, size_t need, uint64_t fill) {
    size_t width = d->fill_width > 1 && directive_has_arg(d, 1) ? d->fill_width : 1;

    if (need % width != 0) {
        return -2;
    }
    for (; need > 0; need -= width) {
        if (bytebuf_append_u64_le(buf, fill, width) != 0) {
            return -1;
        }
    }
    return 0;
}

/*
 * Expressions are as_expr.c's.  These give its three answers the 0 or -1
 * the rest of this file tests for.
 */


/* One integer literal. */
int parse_int64(const char *s, long long *out) {
    return as_expr_parse_number(s, out);
}

/* Text that is an expression of numbers alone. */
int parse_const_expr_string(const char *s, long long *out) {
    if (s == NULL || out == NULL) {
        return -1;
    }
    return as_expr_eval_string(s, NULL, NULL, out) == AS_EXPR_EVAL_OK ? 0 : -1;
}

/* An expression of numbers alone. */
int eval_expr_const(const as_expr_t *e, long long *out) {
    return as_expr_eval(e, NULL, NULL, out) == AS_EXPR_EVAL_OK ? 0 : -1;
}

/* What .set and = have given a value to; the location counter is not
 * among them. */
static int asm_var_expr_lookup(void *cookie, const char *name, long long *out) {
    if (as_strcmp(name, ".") == 0) {
        return 0;
    }
    return asm_var_lookup((const emit_ctx_t *)cookie, name, out) == 0;
}

int eval_arg_asm_vars(emit_ctx_t *ctx, const as_stmt_t *st, const char *arg, long long *out) {
    (void)st;
    if (ctx == NULL || arg == NULL || out == NULL) {
        return -1;
    }
    if (parse_int64(arg, out) == 0) {
        return 0;
    }
    /* One reading of it, with the variables to hand: an expression of
     * numbers alone asks for none.  (It was evaluated as numbers alone
     * and then, a copy of it made for the purpose, with the variables;
     * where the statement stands is nothing to either.) */
    return as_expr_eval_string(arg, asm_var_expr_lookup, ctx, out) == AS_EXPR_EVAL_OK ? 0 : -1;
}

int expr_has_symbol(const as_expr_t *e) {
    if (e == NULL) {
        return 0;
    }
    if (e->kind == AS_EXPR_SYMBOL || e->kind == AS_EXPR_LOCAL_REF) {
        return 1;
    }
    if (expr_has_symbol(e->lhs) || expr_has_symbol(e->rhs)) {
        return 1;
    }
    return 0;
}

int expr_has_local_ref(const as_expr_t *e) {
    if (e == NULL) {
        return 0;
    }
    if (e->kind == AS_EXPR_LOCAL_REF) {
        return 1;
    }
    return expr_has_local_ref(e->lhs) || expr_has_local_ref(e->rhs);
}

int raw_is_numeric_local_ref(const char *raw) {
    size_t i = 0;

    if (raw == NULL || raw[0] == '\0') {
        return 0;
    }
    while (raw[i] >= '0' && raw[i] <= '9') {
        i++;
    }
    return i > 0 && (raw[i] == 'f' || raw[i] == 'b') && raw[i + 1] == '\0';
}

/* How long one is: two bytes, and the prefix where the mode wants it. */
static size_t fixed_short_rel_size(const char *mn, unsigned code_bits) {
    int need67 = 0;

    return x86_fixed_short_branch(mn, code_bits, &need67, NULL) == 0 && need67 ? 3u : 2u;
}

/*
 * The strings: .ascii, .asciz and .string.  1 with the bytes in `buf`, 0
 * if the directive is none of them, -1 if a literal cannot be read.
 *
 * (This was a second emitter of every data directive -- the numbers,
 * the space, .fill, the alignments, .org and the floats -- behind the
 * one that has the context, append_directive_data_ctx(), which has
 * each of those itself and came here for the strings alone; the floats
 * are numbers by the time anything is emitted.)
 */
static int append_string_directive(bytebuf_t *buf, const as_directive_t *d) {
    size_t i;

    if (d == NULL || d->name == NULL) {
        return 0;
    }
    if (as_strcmp(d->name, ".ascii") == 0 || as_strcmp(d->name, ".asciz") == 0 || as_strcmp(d->name, ".string") == 0) {
        int nul = (as_strcmp(d->name, ".ascii") == 0) ? 0 : 1;
        for (i = 0; i < d->arg_count; ++i) {
            const char *s = d->args[i] != NULL ? d->args[i] : "";
            char *bytes = NULL;
            size_t len = 0;
            if (as_decode_string_literal(s, &bytes, &len) != 0) {
                return -1;
            }
            if (bytebuf_append(buf, bytes, len) != 0) {
                free(bytes);
                return -1;
            }
            free(bytes);
            if (nul && bytebuf_append_zeros(buf, 1) != 0) {
                return -1;
            }
        }
        return 1;
    }
    return 0;
}

void sec_buf_vec_free(sec_buf_vec_t *v) {
    size_t i;

    if (v == NULL) {
        return;
    }
    for (i = 0; i < v->count; ++i) {
        free(v->items[i].name);
        free(v->items[i].buf.data);
    }
    free(v->items);
    as_strmap_free(&v->by_name);
    memset(v, 0, sizeof(*v));
}

static sec_buf_t *sec_buf_find(sec_buf_vec_t *v, const char *name) {
    size_t i;

    if (v == NULL || name == NULL) {
        return NULL;
    }
    return as_strmap_get(&v->by_name, name, &i) ? &v->items[i] : NULL;
}

sec_buf_t *sec_buf_get_or_add(sec_buf_vec_t *v, const char *name) {
    sec_buf_t *next;

    if (v == NULL || name == NULL || name[0] == '\0') {
        return NULL;
    }
    next = sec_buf_find(v, name);
    if (next != NULL) {
        return next;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 8 : v->cap * 2;
        sec_buf_t *grow = (sec_buf_t *)realloc(v->items, ncap * sizeof(*grow));
        if (grow == NULL) {
            return NULL;
        }
        v->items = grow;
        v->cap = ncap;
    }
    memset(&v->items[v->count], 0, sizeof(v->items[v->count]));
    v->items[v->count].name = xstrdup(name);
    if (v->items[v->count].name == NULL) {
        return NULL;
    }
    if (as_strmap_put(&v->by_name, v->items[v->count].name, v->count) != 0) {
        free(v->items[v->count].name);
        return NULL;
    }
    v->count++;
    return &v->items[v->count - 1];
}

/*
 * A section of the object, kept by its name for section_for_name(): as
 * it is made, so that it is never looked for in the object, whose
 * search goes through every section -- for a section to a function,
 * every function for every function.  (If it cannot be kept it is
 * looked for.)
 */
void section_remember(emit_ctx_t *ctx, const char *name, elf_section_t *sec) {
    const char *kept;
    size_t at;

    if (ctx == NULL || name == NULL || sec == NULL || as_strmap_get(&ctx->sec_by_name, name, &at)) {
        return;
    }
    kept = section_name_keep(ctx, name);
    if (ctx->sec_found_count == ctx->sec_found_cap) {
        size_t cap = ctx->sec_found_cap != 0 ? ctx->sec_found_cap * 2 : 16;
        elf_section_t **grown = (elf_section_t **)realloc(ctx->sec_found, cap * sizeof(*grown));

        if (grown == NULL) {
            return;
        }
        ctx->sec_found = grown;
        ctx->sec_found_cap = cap;
    }
    if (kept[0] != '\0' && as_strmap_put(&ctx->sec_by_name, kept, ctx->sec_found_count) == 0) {
        ctx->sec_found[ctx->sec_found_count++] = sec;
    }
}

elf_section_t *section_for_name(emit_ctx_t *ctx, const char *name) {
    if (ctx == NULL || name == NULL) {
        return NULL;
    }
    if (as_strcmp(name, ".text") == 0) {
        return ctx->text_sec;
    }
    if (as_strcmp(name, ".data") == 0) {
        return ctx->data_sec;
    }
    /* Looked for in the object once, and after that by its name's
     * number: the object's own search goes through every section. */
    {
        elf_section_t *sec;
        size_t at;

        if (as_strmap_get(&ctx->sec_by_name, name, &at)) {
            return ctx->sec_found[at];
        }
        sec = elf_find_section(ctx->obj, name);
        if (sec != NULL) {
            section_remember(ctx, name, sec);
        }
        return sec;
    }
}

/* Whether the section is flagged as code: it is padded with NOPs. */
static int section_name_has_exec_flag(emit_ctx_t *ctx, const char *name) {
    elf_section_t *sec;

    /* (A flat image has no object to ask: its writer says.) */
    if (ctx != NULL && ctx->flat_image) {
        return ctx->flat_section_exec;
    }
    sec = section_for_name(ctx, name);
    if (sec == NULL) {
        return 0;
    }
    return (elf_section_flags(sec) & SHF_EXECINSTR) != 0;
}

/*
 * The names of the sections an instruction is written in, whatever their
 * flags: found once, by following the source.
 */
static int find_sections_with_instructions(emit_ctx_t *ctx) {
    section_track_t track;
    size_t i;

    ctx->code_sections_found = 1;
    if (section_track_init(&track, 32u) != 0) {
        return -1;
    }
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        size_t k;

        if (st->kind == AS_STMT_DIRECTIVE) {
            if (section_track_apply_directive(&track, &st->u.directive) < 0) {
                section_track_free(&track);
                return -1;
            }
            continue;
        }
        if (st->kind != AS_STMT_INSTRUCTION || track.current == NULL) {
            continue;
        }
        for (k = 0; k < ctx->code_section_count; ++k) {
            if (as_strcmp(ctx->code_sections[k], track.current) == 0) {
                break;
            }
        }
        if (k == ctx->code_section_count) {
            char **next = (char **)realloc(ctx->code_sections, (k + 1) * sizeof(*next));

            if (next == NULL) {
                section_track_free(&track);
                return -1;
            }
            ctx->code_sections = next;
            ctx->code_sections[k] = xstrdup(track.current);
            if (ctx->code_sections[k] == NULL) {
                section_track_free(&track);
                return -1;
            }
            ctx->code_section_count++;
        }
    }
    section_track_free(&track);
    return 0;
}

/*
 * Whether the section's statements are assembled as a section of code
 * is: its instructions encoded, its data and its labels laid out among
 * them.  One flagged as code is, and so is any other an instruction is
 * written in -- .init and .fini opened without flags, a section "a", or
 * .data.  It went by the flag alone, and an instruction anywhere else
 * was left out of the object without a word: `call f` in .init was not
 * there.
 */
int section_name_is_executable(emit_ctx_t *ctx, const char *name) {
    size_t k;

    if (ctx == NULL || name == NULL) {
        return 0;
    }
    if (section_name_has_exec_flag(ctx, name)) {
        return 1;
    }
    if (!ctx->code_sections_found && ctx->parsed != NULL) {
        (void)find_sections_with_instructions(ctx);
    }
    for (k = 0; k < ctx->code_section_count; ++k) {
        if (as_strcmp(ctx->code_sections[k], name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The width in bytes of each value a data directive lays down, or 0 if
 * the directive is not one of those: the one place that says. */
unsigned directive_fixed_scalar_width(const as_directive_t *d) {
    if (d == NULL || d->name == NULL) {
        return 0;
    }
    if (as_strcmp(d->name, ".byte") == 0) return 1;
    if (as_strcmp(d->name, ".word") == 0 || as_strcmp(d->name, ".short") == 0 ||
        as_strcmp(d->name, ".hword") == 0 || as_strcmp(d->name, ".2byte") == 0) return 2;
    if (as_strcmp(d->name, ".long") == 0 || as_strcmp(d->name, ".4byte") == 0) return 4;
    if (as_strcmp(d->name, ".quad") == 0 || as_strcmp(d->name, ".8byte") == 0) return 8;
    return 0;
}

/*
 * A section's name, kept: the same pointer for the same name, good until
 * the object is made.  What is found out about where a label is is
 * handed about as one of these, where it had been copied into arrays of
 * 128 bytes -- and a section whose name was longer was another section,
 * or none.
 */
const char *section_name_keep(emit_ctx_t *ctx, const char *name) {
    size_t i;
    char *copy;

    if (name == NULL || name[0] == '\0' || ctx == NULL) {
        return "";
    }
    if (as_strmap_get(&ctx->kept_by_name, name, &i)) {
        return ctx->kept_names[i];
    }
    if (ctx->kept_name_count == ctx->kept_name_cap) {
        size_t cap = ctx->kept_name_cap != 0 ? ctx->kept_name_cap * 2 : 16;
        char **grown = (char **)realloc(ctx->kept_names, cap * sizeof(*grown));

        if (grown == NULL) {
            return "";
        }
        ctx->kept_names = grown;
        ctx->kept_name_cap = cap;
    }
    copy = xstrdup(name);
    if (copy == NULL || as_strmap_put(&ctx->kept_by_name, copy, ctx->kept_name_count) != 0) {
        free(copy);
        return "";
    }
    ctx->kept_names[ctx->kept_name_count++] = copy;
    return copy;
}

/* The keys the cache of labels is found by: a place in the source, and
 * a statement.  0, or -1 for a file's name too long to make one of. */
#define VLABEL_KEY_MAX 1100

static int vlabel_place_key(char *key, size_t key_sz, const char *file, unsigned line, int digit) {
    int n = snprintf(key, key_sz, "%u %d %s", line, digit, file);

    return n < 0 || (size_t)n >= key_sz ? -1 : 0;
}

static int virtual_label_cache_lookup(emit_ctx_t *ctx, const char *file, unsigned line, int digit,
                                      const char *sym_name, const char **section_out, uint64_t *off_out) {
    size_t i;

    if (ctx == NULL || section_out == NULL || off_out == NULL) {
        return -1;
    }
    /* sym_name == NULL && digit >= 0 used to short-circuit here, which
     * forced every numbered local-label query (e.g. `1f`, `2b`) to redo
     * the full virtual-layout walk even after the first call had already
     * populated the cache. The push path stores (file, line, digit) for
     * numbered labels, so the loop below can find them. */
    if (sym_name == NULL && digit < 0) {
        return -1;
    }
    {
        virtual_label_cache_t *c;
        char key[VLABEL_KEY_MAX];

        if (sym_name != NULL) {
            if (!as_strmap_get(&ctx->vlabel_by_name, sym_name, &i)) {
                return -1;
            }
        } else if (file == NULL || vlabel_place_key(key, sizeof(key), file, line, digit) != 0 ||
                   !as_strmap_get(&ctx->vlabel_by_place, key, &i)) {
            return -1;
        }
        c = &ctx->vlabel_cache[i];
        if (c->off == UINT64_MAX) {
            return -1;
        }
        *section_out = section_name_keep(ctx, c->section);
        *off_out = c->off;
        return 0;
    }
    return -1;
}

static int virtual_label_cache_lookup_stmt(emit_ctx_t *ctx, const as_stmt_t *stmt, const char **section_out,
                                           uint64_t *off_out) {
    size_t i;

    if (ctx == NULL || stmt == NULL || section_out == NULL || off_out == NULL) {
        return -1;
    }
    {
        virtual_label_cache_t *c;

        if (ctx->vlabel_of_stmt == NULL || ctx->parsed == NULL || stmt < ctx->parsed->items ||
            stmt >= ctx->parsed->items + ctx->parsed->count ||
            ctx->vlabel_of_stmt[stmt - ctx->parsed->items] == 0) {
            return -1;
        }
        i = ctx->vlabel_of_stmt[stmt - ctx->parsed->items] - 1;
        c = &ctx->vlabel_cache[i];
        if (c->off == UINT64_MAX) {
            return -1;
        }
        *section_out = section_name_keep(ctx, c->section);
        *off_out = c->off;
        return 0;
    }
}

static int virtual_label_cache_push(emit_ctx_t *ctx, const as_stmt_t *st, const as_label_def_t *label,
                                    const char *section, uint64_t off) {
    virtual_label_cache_t *next;
    virtual_label_cache_t *c;
    int digit = -1;

    if (ctx == NULL || label == NULL || label->name == NULL || section == NULL) {
        return -1;
    }
    (void)numeric_local_label_number(label->name, &digit);
    if (digit < 0 && virtual_label_cache_lookup(ctx, label->unit, label->ordinal, digit, label->name,
                                                &(const char *){NULL}, &(uint64_t){0}) == 0) {
        return 0;
    }
    if (ctx->vlabel_count == ctx->vlabel_cap) {
        size_t ncap = ctx->vlabel_cap == 0 ? 256 : ctx->vlabel_cap * 2;
        next = (virtual_label_cache_t *)realloc(ctx->vlabel_cache, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        ctx->vlabel_cache = next;
        ctx->vlabel_cap = ncap;
    }
    c = &ctx->vlabel_cache[ctx->vlabel_count++];
    memset(c, 0, sizeof(*c));
    /* (Nothing of an entry is a copy but the key of a numbered label's
     * place.  Each had its name, its file and its section's name copied
     * and two keys printed for it, which was most of the time of
     * finding where a label is.) */
    c->name = label->name;
    c->file = label->unit != NULL ? label->unit : "";
    c->section = section_name_keep(ctx, section);
    c->line = label->ordinal;
    c->digit = digit;
    c->off = off;
    c->stmt = st;
    {
        char key[VLABEL_KEY_MAX];
        size_t at = ctx->vlabel_count - 1;
        size_t was;

        /* By name the first of the name is found, by place the first
         * at the place, by statement the last. */
        if (!as_strmap_get(&ctx->vlabel_by_name, c->name, &was) &&
            as_strmap_put(&ctx->vlabel_by_name, c->name, at) != 0) {
            return -1;
        }
        /* (By its place a numbered label is looked for, and no other.) */
        if (digit >= 0 && vlabel_place_key(key, sizeof(key), c->file, c->line, c->digit) == 0 &&
            !as_strmap_get(&ctx->vlabel_by_place, key, &was)) {
            c->place_key = xstrdup(key);
            if (c->place_key == NULL || as_strmap_put(&ctx->vlabel_by_place, c->place_key, at) != 0) {
                return -1;
            }
        }
        if (ctx->parsed != NULL && st >= ctx->parsed->items && st < ctx->parsed->items + ctx->parsed->count) {
            if (ctx->vlabel_of_stmt == NULL) {
                ctx->vlabel_of_stmt = (size_t *)calloc(ctx->parsed->count, sizeof(*ctx->vlabel_of_stmt));
                if (ctx->vlabel_of_stmt == NULL) {
                    return -1;
                }
            }
            ctx->vlabel_of_stmt[st - ctx->parsed->items] = at + 1;
        }
    }
    return 0;
}

void virtual_label_cache_free(emit_ctx_t *ctx) {
    size_t i;

    if (ctx == NULL) {
        return;
    }
    /* (Where the rows of the line table stand is of the layout too.) */
    ctx->line_rows_valid = 0;
    for (i = 0; i < ctx->vlabel_count; ++i) {
        free(ctx->vlabel_cache[i].place_key);
    }
    as_strmap_free(&ctx->vlabel_by_name);
    as_strmap_free(&ctx->vlabel_by_place);
    free(ctx->vlabel_of_stmt);
    ctx->vlabel_of_stmt = NULL;
    free(ctx->vlabel_cache);
    ctx->vlabel_cache = NULL;
    ctx->vlabel_count = 0;
    ctx->vlabel_cap = 0;
}

int find_label_virtual_location(emit_ctx_t *ctx, const as_stmt_t *base_st,
                                       const char *file, unsigned line, int digit, const char *sym_name,
                                       const char **section_out, uint64_t *off_out) {
    size_t i;
    sec_buf_vec_t secbufs;
    section_track_t track;

    if (ctx == NULL || section_out == NULL || off_out == NULL || (sym_name == NULL && file == NULL)) {
        return -1;
    }
    *section_out = "";
    *off_out = 0;
    if (virtual_label_cache_lookup(ctx, file, line, digit, sym_name, section_out, off_out) == 0) {
        return 0;
    }
    /* Fast path: look up the target stmt by label name (O(N) once via the
     * parsed array, then re-used) and read its virtual offset from the
     * per-section prefix-sum table that get_section_prefix_sums builds
     * on demand. This avoids the expensive section_track + encoder walk
     * below for the common case of resolving a named or numbered local
     * label that exists in the parse. */
    if (build_stmt_section_at(ctx) == 0 && ctx->parsed != NULL) {
        const as_stmt_t *target_st = NULL;
        size_t target_idx = 0;
        /* A name is looked up; a numeric label is found by going
         * through them. */
        if (sym_name != NULL && label_stmt_index(ctx, sym_name, &target_idx) == 0) {
            target_st = &ctx->parsed->items[target_idx];
        }
        for (i = 0; sym_name == NULL && i < ctx->parsed->count; ++i) {
            const as_stmt_t *st = &ctx->parsed->items[i];
            size_t j;
            for (j = 0; j < st->label_count; ++j) {
                int matches;
                if (sym_name != NULL) {
                    matches = (st->labels[j].name != NULL && as_strcmp(st->labels[j].name, sym_name) == 0);
                } else {
                    int label_digit = -1;
                    matches = (numeric_local_label_number(st->labels[j].name, &label_digit) == 0 &&
                               label_digit == digit && st->labels[j].ordinal == line &&
                               st->labels[j].unit != NULL && file != NULL &&
                               as_strcmp(st->labels[j].unit, file) == 0);
                }
                if (matches) {
                    target_st = st;
                    target_idx = i;
                    break;
                }
            }
            if (target_st != NULL) {
                break;
            }
        }
        if (target_st != NULL && target_idx < ctx->stmt_section_at_count &&
            ctx->stmt_section_at[target_idx] != NULL) {
            const char *target_section = ctx->stmt_section_at[target_idx];
            const uint32_t *pfx;
            unsigned bits = ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                            (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u);
            pfx = get_section_prefix_sums(ctx, target_section, bits);
            if (pfx != NULL && target_idx < ctx->section_prefix_count + ctx->parsed->count) {
                *section_out = section_name_keep(ctx, target_section);
                *off_out = pfx[target_idx];
                /* Also push into vlabel_cache so future lookups hit the
                 * existing fast path. */
                {
                    size_t k;
                    for (k = 0; k < target_st->label_count; ++k) {
                        (void)virtual_label_cache_push(ctx, target_st, &target_st->labels[k],
                                                       target_section, pfx[target_idx]);
                    }
                }
                return 0;
            }
        }
        /* The label doesn't exist in this translation unit. Don't walk
         * the entire parse just to confirm an external symbol can't be
         * resolved virtually — return failure so the caller emits a
         * relocation instead. This is critical for files with thousands
         * of `.long external_sym` directives (Linux's syscall tables),
         * each of which used to drive a full O(N) encoder walk. */
        if (target_st == NULL) {
            return -1;
        }
    }
    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track, ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                                   (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        return -1;
    }

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        sec_buf_t *sb;
        size_t j;

        sb = sec_buf_get_or_add(&secbufs, track.current);
        if (sb == NULL) {
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        for (j = 0; j < st->label_count; ++j) {
            int matches = 0;
            if (virtual_label_cache_push(ctx, st, &st->labels[j], track.current != NULL ? track.current : "",
                                         (uint64_t)sb->buf.len) != 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }

            if (sym_name != NULL) {
                matches = (st->labels[j].name != NULL && as_strcmp(st->labels[j].name, sym_name) == 0);
            } else {
                int label_digit = -1;
                matches = (numeric_local_label_number(st->labels[j].name, &label_digit) == 0 &&
                       label_digit == digit &&
                       st->labels[j].ordinal == line &&
                       st->labels[j].unit != NULL &&
                       as_strcmp(st->labels[j].unit, file) == 0);
            }
            if (matches) {
                *section_out = section_name_keep(ctx, track.current);
                *off_out = (uint64_t)sb->buf.len;
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return 0;
            }
        }
        if (st == base_st && st->kind == AS_STMT_DIRECTIVE) {
            continue;
        }
        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            if (trc > 0) {
                continue;
            }
            {
                unsigned scalar_width = directive_fixed_scalar_width(&st->u.directive);
                if (scalar_width != 0) {
                    if (bytebuf_append_zeros(&sb->buf, st->u.directive.arg_count * (size_t)scalar_width) != 0) {
                        section_track_free(&track);
                        sec_buf_vec_free(&secbufs);
                        return -1;
                    }
                    continue;
                }
            }
            ctx->virtual_scanning++;
            {
                int arc = append_directive_data_ctx(ctx, &sb->buf, track.current, st, (uint64_t)sb->buf.len,
                                                    track.x86_code_bits, &st->u.directive);
                ctx->virtual_scanning--;
                if (arc < 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
                }
            }
            continue;
        }
        if (st->kind == AS_STMT_INSTRUCTION && section_name_is_executable(ctx, track.current)) {
            if (append_virtual_instruction_bytes(ctx, &sb->buf, track.current, st, track.x86_code_bits) != 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
        }
    }

    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    return -1;
}

static int resolve_local_ref_target_line(emit_ctx_t *ctx, const char *file, unsigned ref_line,
                                         int digit, int forward, unsigned *line_out) {
    size_t i;
    int found = 0;
    unsigned best = 0;

    if (ctx == NULL || file == NULL || line_out == NULL) {
        return -1;
    }
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        size_t j;
        for (j = 0; j < st->label_count; ++j) {
            int label_digit = -1;
            if (numeric_local_label_number(st->labels[j].name, &label_digit) != 0 ||
                label_digit != digit || st->labels[j].unit == NULL ||
                as_strcmp(st->labels[j].unit, file) != 0) {
                continue;
            }
            if (forward) {
                if (st->labels[j].ordinal > ref_line && (!found || st->labels[j].ordinal < best)) {
                    best = st->labels[j].ordinal;
                    found = 1;
                }
            } else {
                if (st->labels[j].ordinal <= ref_line && (!found || st->labels[j].ordinal > best)) {
                    best = st->labels[j].ordinal;
                    found = 1;
                }
            }
        }
    }
    if (!found) {
        return -1;
    }
    *line_out = best;
    return 0;
}

static const as_stmt_t *resolve_local_ref_target_stmt(emit_ctx_t *ctx, const as_stmt_t *base_st,
                                                      const char *file, int digit, int forward) {
    size_t i;
    size_t base_idx = 0;
    int have_base = 0;

    if (ctx == NULL || base_st == NULL || file == NULL) {
        return NULL;
    }
    for (i = 0; i < ctx->parsed->count; ++i) {
        if (&ctx->parsed->items[i] == base_st) {
            base_idx = i;
            have_base = 1;
            break;
        }
    }
    if (!have_base) {
        return NULL;
    }
    if (base_st->source_seq != 0) {
        /* The statements were reordered: nearest in the source, not in
         * the order they are assembled in. */
        const as_stmt_t *best = NULL;

        for (i = 0; i < ctx->parsed->count; ++i) {
            const as_stmt_t *st = &ctx->parsed->items[i];
            size_t j;

            if (st->source_seq == 0 ||
                (forward ? st->source_seq <= base_st->source_seq : st->source_seq > base_st->source_seq) ||
                (best != NULL &&
                 (forward ? st->source_seq > best->source_seq : st->source_seq < best->source_seq))) {
                continue;
            }
            for (j = 0; j < st->label_count; ++j) {
                int label_digit = -1;

                if (numeric_local_label_number(st->labels[j].name, &label_digit) == 0 && label_digit == digit &&
                    st->labels[j].unit != NULL && as_strcmp(st->labels[j].unit, file) == 0) {
                    best = st;
                    break;
                }
            }
        }
        return best;
    }
    if (forward) {
        for (i = base_idx + 1; i < ctx->parsed->count; ++i) {
            const as_stmt_t *st = &ctx->parsed->items[i];
            size_t j;
            for (j = 0; j < st->label_count; ++j) {
                int label_digit = -1;
                if (numeric_local_label_number(st->labels[j].name, &label_digit) == 0 &&
                    label_digit == digit &&
                    st->labels[j].unit != NULL &&
                    as_strcmp(st->labels[j].unit, file) == 0) {
                    return st;
                }
            }
        }
    } else {
        for (i = base_idx + 1; i > 0; --i) {
            const as_stmt_t *st = &ctx->parsed->items[i - 1];
            size_t j;
            for (j = st->label_count; j > 0; --j) {
                int label_digit = -1;
                if (numeric_local_label_number(st->labels[j - 1].name, &label_digit) == 0 &&
                    label_digit == digit &&
                    st->labels[j - 1].unit != NULL &&
                    as_strcmp(st->labels[j - 1].unit, file) == 0) {
                    return st;
                }
            }
        }
    }
    return NULL;
}

int find_stmt_virtual_location(emit_ctx_t *ctx, const as_stmt_t *target_st,
                                      const char **section_out, uint64_t *off_out) {
    size_t i;
    sec_buf_vec_t secbufs;
    section_track_t track;
    int found = 0;

    if (ctx == NULL || target_st == NULL || section_out == NULL || off_out == NULL) {
        return -1;
    }
    *section_out = "";
    *off_out = 0;
    if (virtual_label_cache_lookup_stmt(ctx, target_st, section_out, off_out) == 0) {
        return 0;
    }
    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track, ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                                   (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        return -1;
    }

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        sec_buf_t *sb;
        size_t j;

        sb = sec_buf_get_or_add(&secbufs, track.current);
        if (sb == NULL) {
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        for (j = 0; j < st->label_count; ++j) {
            if (virtual_label_cache_push(ctx, st, &st->labels[j], track.current != NULL ? track.current : "",
                                         (uint64_t)sb->buf.len) != 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
        }
        /* A statement without a label has a place too: a .uleb128 asks
         * for its own, to take `.` from. */
        if (st == target_st && !found) {
            *section_out = section_name_keep(ctx, track.current);
            *off_out = (uint64_t)sb->buf.len;
            found = 1;
        }
        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            if (trc > 0) {
                continue;
            }
            {
                unsigned scalar_width = directive_fixed_scalar_width(&st->u.directive);
                if (scalar_width != 0) {
                    if (bytebuf_append_zeros(&sb->buf, st->u.directive.arg_count * (size_t)scalar_width) != 0) {
                        section_track_free(&track);
                        sec_buf_vec_free(&secbufs);
                        return -1;
                    }
                    continue;
                }
            }
            ctx->virtual_scanning++;
            {
                int arc = append_directive_data_ctx(ctx, &sb->buf, track.current, st, (uint64_t)sb->buf.len,
                                                    track.x86_code_bits, &st->u.directive);
                ctx->virtual_scanning--;
                if (arc < 0) {
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
            }
            continue;
        }
        if (st->kind == AS_STMT_INSTRUCTION && section_name_is_executable(ctx, track.current)) {
            if (append_virtual_instruction_bytes(ctx, &sb->buf, track.current, st, track.x86_code_bits) != 0) {
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
        }
    }

    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    return found ? 0 : -1;
}

int eval_local_rel_expr_virtual(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *base_st, uint64_t base_off,
                                       unsigned x86_code_bits, const as_expr_t *e, long long *out) {
    long long l;
    long long r;
    uint64_t target_off;
    virtual_addr_value_t target_loc;

    if (ctx == NULL || section_name == NULL || e == NULL || out == NULL) {
        return -1;
    }
    switch (e->kind) {
    case AS_EXPR_CONST:
        *out = e->value;
        return 0;
    case AS_EXPR_SYMBOL:
        if (e->symbol != NULL && as_strcmp(e->symbol, ".") == 0) {
            *out = 0;
            return 0;
        }
        if (e->symbol == NULL || e->reloc_op != NULL ||
            find_label_virtual_location(ctx, base_st, NULL, 0, 0, e->symbol,
                                        &target_loc.section,
                                        &target_off) != 0 ||
            as_strcmp(target_loc.section, section_name) != 0) {
            return -1;
        }
        *out = (long long)target_off - (long long)base_off;
        return 0;
    case AS_EXPR_LOCAL_REF:
        if (e->src_file == NULL) {
            return -1;
        }
        {
            const as_stmt_t *target_st = resolve_local_ref_target_stmt(ctx, base_st, e->src_file,
                                                                       e->local_digit, e->local_forward);
            if (target_st != NULL &&
                find_stmt_virtual_location(ctx, target_st, &target_loc.section,
                                           &target_off) == 0 &&
                as_strcmp(target_loc.section, section_name) == 0) {
                *out = (long long)target_off - (long long)base_off;
                return 0;
            }
        }
        {
            unsigned target_line = 0;
            if (e->local_resolved) {
                target_line = e->local_target_line;
            } else if (resolve_local_ref_target_line(ctx, e->src_file, e->src_line,
                                                     e->local_digit, e->local_forward,
                                                     &target_line) != 0) {
                return -1;
            }
            if (find_label_virtual_location(ctx, base_st,
                                            e->src_file, target_line, e->local_digit,
                                            NULL, &target_loc.section,
                                            &target_off) != 0 ||
                as_strcmp(target_loc.section, section_name) != 0) {
                return -1;
            }
        }
        *out = (long long)target_off - (long long)base_off;
        return 0;
    case AS_EXPR_UNARY:
        if (eval_local_rel_expr_virtual(ctx, section_name, base_st, base_off, x86_code_bits, e->lhs, &l) != 0) {
            return -1;
        }
        if (e->op == AS_EXPR_OP_NEG) {
            *out = -l;
            return 0;
        }
        if (e->op == AS_EXPR_OP_BNOT) {
            *out = ~l;
            return 0;
        }
        return -1;
    case AS_EXPR_BINARY:
        if (eval_local_rel_expr_virtual(ctx, section_name, base_st, base_off, x86_code_bits, e->lhs, &l) != 0 ||
            eval_local_rel_expr_virtual(ctx, section_name, base_st, base_off, x86_code_bits, e->rhs, &r) != 0) {
            return -1;
        }
        switch (e->op) {
        case AS_EXPR_OP_ADD: *out = l + r; return 0;
        case AS_EXPR_OP_SUB: *out = l - r; return 0;
        case AS_EXPR_OP_MUL: *out = l * r; return 0;
        case AS_EXPR_OP_DIV: if (r == 0) return -1; *out = l / r; return 0;
        case AS_EXPR_OP_MOD: if (r == 0) return -1; *out = l % r; return 0;
        case AS_EXPR_OP_OR: *out = l | r; return 0;
        case AS_EXPR_OP_AND: *out = l & r; return 0;
        case AS_EXPR_OP_XOR: *out = l ^ r; return 0;
        case AS_EXPR_OP_SHL: *out = l << (r & 63); return 0;
        case AS_EXPR_OP_SHR: *out = l >> (r & 63); return 0;
        case AS_EXPR_OP_EQ: *out = (l == r) ? -1 : 0; return 0;
        case AS_EXPR_OP_NE: *out = (l != r) ? -1 : 0; return 0;
        case AS_EXPR_OP_LT: *out = (l < r) ? -1 : 0; return 0;
        case AS_EXPR_OP_LE: *out = (l <= r) ? -1 : 0; return 0;
        case AS_EXPR_OP_GT: *out = (l > r) ? -1 : 0; return 0;
        case AS_EXPR_OP_GE: *out = (l >= r) ? -1 : 0; return 0;
        default: return -1;
        }
    default:
        return -1;
    }
}

int local_ref_virtual_location(emit_ctx_t *ctx, const as_stmt_t *base_st,
                                      const as_expr_t *e, virtual_addr_value_t *out) {
    unsigned target_line = 0;
    uint64_t off = 0;

    if (ctx == NULL || e == NULL || e->kind != AS_EXPR_LOCAL_REF || out == NULL || e->src_file == NULL) {
        return -1;
    }
    {
        const as_stmt_t *target_st = resolve_local_ref_target_stmt(ctx, base_st, e->src_file,
                                                                   e->local_digit, e->local_forward);
        if (target_st != NULL) {
            memset(out, 0, sizeof(*out));
            if (find_stmt_virtual_location(ctx, target_st, &out->section, &off) != 0) {
                return -1;
            }
            out->has_section = 1;
            out->value = (long long)off;
            return 0;
        }
    }
    if (e->local_resolved) {
        target_line = e->local_target_line;
    } else if (resolve_local_ref_target_line(ctx, e->src_file, e->src_line,
                                             e->local_digit, e->local_forward, &target_line) != 0) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (find_label_virtual_location(ctx, base_st, e->src_file, target_line, e->local_digit, NULL,
                                    &out->section, &off) != 0) {
        return -1;
    }
    out->has_section = 1;
    out->value = (long long)off;
    return 0;
}

static int eval_virtual_addr_expr(emit_ctx_t *ctx, const as_stmt_t *base_st,
                                  const as_expr_t *e, virtual_addr_value_t *out) {
    virtual_addr_value_t l;
    virtual_addr_value_t r;

    if (ctx == NULL || e == NULL || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    switch (e->kind) {
    case AS_EXPR_CONST:
        out->value = e->value;
        return 0;
    case AS_EXPR_SYMBOL: {
        uint64_t off = 0;

        if (e->symbol == NULL || e->reloc_op != NULL || as_strcmp(e->symbol, ".") == 0) {
            return -1;
        }
        if (find_label_virtual_location(ctx, base_st, NULL, 0, 0, e->symbol,
                                        &out->section, &off) != 0) {
            return -1;
        }
        out->has_section = 1;
        out->value = (long long)off;
        return 0;
    }
    case AS_EXPR_LOCAL_REF:
        return local_ref_virtual_location(ctx, base_st, e, out);
    case AS_EXPR_UNARY:
        if (eval_virtual_addr_expr(ctx, base_st, e->lhs, &l) != 0 || l.has_section) {
            return -1;
        }
        if (e->op == AS_EXPR_OP_NEG) {
            out->value = -l.value;
            return 0;
        }
        if (e->op == AS_EXPR_OP_BNOT) {
            out->value = ~l.value;
            return 0;
        }
        return -1;
    case AS_EXPR_BINARY:
        if (eval_virtual_addr_expr(ctx, base_st, e->lhs, &l) != 0 ||
            eval_virtual_addr_expr(ctx, base_st, e->rhs, &r) != 0) {
            return -1;
        }
        switch (e->op) {
        case AS_EXPR_OP_ADD:
            if (l.has_section && r.has_section) {
                return -1;
            }
            *out = l.has_section ? l : r;
            out->value = l.value + r.value;
            return 0;
        case AS_EXPR_OP_SUB:
            if (l.has_section && r.has_section) {
                if (as_strcmp(l.section, r.section) != 0) {
                    return -1;
                }
                out->has_section = 0;
                out->value = l.value - r.value;
                return 0;
            }
            if (!l.has_section && !r.has_section) {
                out->value = l.value - r.value;
                return 0;
            }
            if (l.has_section) {
                *out = l;
                out->value = l.value - r.value;
                return 0;
            }
            return -1;
        case AS_EXPR_OP_MUL:
        case AS_EXPR_OP_DIV:
        case AS_EXPR_OP_MOD:
        case AS_EXPR_OP_OR:
        case AS_EXPR_OP_AND:
        case AS_EXPR_OP_XOR:
        case AS_EXPR_OP_SHL:
        case AS_EXPR_OP_SHR:
        case AS_EXPR_OP_EQ:
        case AS_EXPR_OP_NE:
        case AS_EXPR_OP_LT:
        case AS_EXPR_OP_LE:
        case AS_EXPR_OP_GT:
        case AS_EXPR_OP_GE:
            if (l.has_section || r.has_section) {
                return -1;
            }
            switch (e->op) {
            case AS_EXPR_OP_MUL: out->value = l.value * r.value; return 0;
            case AS_EXPR_OP_DIV: if (r.value == 0) return -1; out->value = l.value / r.value; return 0;
            case AS_EXPR_OP_MOD: if (r.value == 0) return -1; out->value = l.value % r.value; return 0;
            case AS_EXPR_OP_OR: out->value = l.value | r.value; return 0;
            case AS_EXPR_OP_AND: out->value = l.value & r.value; return 0;
            case AS_EXPR_OP_XOR: out->value = l.value ^ r.value; return 0;
            case AS_EXPR_OP_SHL: out->value = l.value << (r.value & 63); return 0;
            case AS_EXPR_OP_SHR: out->value = l.value >> (r.value & 63); return 0;
            case AS_EXPR_OP_EQ: out->value = (l.value == r.value) ? -1 : 0; return 0;
            case AS_EXPR_OP_NE: out->value = (l.value != r.value) ? -1 : 0; return 0;
            case AS_EXPR_OP_LT: out->value = (l.value < r.value) ? -1 : 0; return 0;
            case AS_EXPR_OP_LE: out->value = (l.value <= r.value) ? -1 : 0; return 0;
            case AS_EXPR_OP_GT: out->value = (l.value > r.value) ? -1 : 0; return 0;
            case AS_EXPR_OP_GE: out->value = (l.value >= r.value) ? -1 : 0; return 0;
            default: return -1;
            }
        default:
            return -1;
        }
    default:
        return -1;
    }
}

int eval_abs_local_diff_expr_virtual(emit_ctx_t *ctx, const as_stmt_t *base_st,
                                            const as_expr_t *e, long long *out) {
    virtual_addr_value_t v;

    if (out == NULL || eval_virtual_addr_expr(ctx, base_st, e, &v) != 0 || v.has_section) {
        return -1;
    }
    *out = v.value;
    return 0;
}

static int numeric_local_virtual_location(emit_ctx_t *ctx, const as_stmt_t *base_st,
                                          int digit, int forward, virtual_addr_value_t *out) {
    unsigned target_line = 0;
    uint64_t off = 0;

    if (ctx == NULL || base_st == NULL || base_st->unit == NULL || out == NULL) {
        return -1;
    }
    {
        const as_stmt_t *target_st = resolve_local_ref_target_stmt(ctx, base_st, base_st->unit, digit, forward);
        if (target_st != NULL) {
            memset(out, 0, sizeof(*out));
            if (find_stmt_virtual_location(ctx, target_st, &out->section, &off) != 0) {
                return -1;
            }
            out->has_section = 1;
            out->value = (long long)off;
            return 0;
        }
    }
    if (resolve_local_ref_target_line(ctx, base_st->unit, base_st->ordinal, digit, forward, &target_line) != 0) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (find_label_virtual_location(ctx, base_st, base_st->unit, target_line, digit, NULL,
                                    &out->section, &off) != 0) {
        return -1;
    }
    out->has_section = 1;
    out->value = (long long)off;
    return 0;
}

int eval_linux_alt_pad_expr(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st,
                                   uint64_t sec_off, unsigned x86_code_bits, const char *arg, long long *out) {
    virtual_addr_value_t v744f;
    virtual_addr_value_t v743f;
    virtual_addr_value_t v741b;
    virtual_addr_value_t v740b;
    long long repl_len;
    long long orig_len;
    long long delta;

    if (ctx == NULL || st == NULL || arg == NULL || out == NULL ||
        strstr(arg, "744f-743f") == NULL || strstr(arg, "741b-740b") == NULL) {
        return -1;
    }
    {
        const as_stmt_t *s744 = resolve_local_ref_target_stmt(ctx, st, st->unit,744, 1);
        const as_stmt_t *s743 = resolve_local_ref_target_stmt(ctx, st, st->unit,743, 1);
        const as_stmt_t *s740 = resolve_local_ref_target_stmt(ctx, st, st->unit,740, 0);
        size_t i744;
        size_t i743;
        size_t i740;
        size_t ibase;
        uint64_t repl = 0;
        uint64_t orig = 0;

        if (s744 != NULL && s743 != NULL && s740 != NULL &&
            parsed_stmt_index(ctx, s744, &i744) == 0 &&
            parsed_stmt_index(ctx, s743, &i743) == 0 &&
            parsed_stmt_index(ctx, s740, &i740) == 0 &&
            parsed_stmt_index(ctx, st, &ibase) == 0 &&
            i743 <= i744 && i740 <= ibase &&
            stmt_range_size_in_section(ctx, ".altinstr_replacement", i743, i744,
                                       0, x86_code_bits, &repl) == 0 &&
            section_name != NULL &&
            linux_alt_original_range_size(ctx, section_name, i740, ibase,
                                          x86_code_bits, &orig) == 0) {
            *out = repl > orig ? (long long)(repl - orig) : 0;
            return 0;
        }
    }
    if (numeric_local_virtual_location(ctx, st, 744, 1, &v744f) != 0 ||
        numeric_local_virtual_location(ctx, st, 743, 1, &v743f) != 0 ||
        numeric_local_virtual_location(ctx, st, 740, 0, &v740b) != 0 ||
        !v744f.has_section || !v743f.has_section || !v740b.has_section ||
        as_strcmp(v744f.section, v743f.section) != 0 ||
        section_name == NULL || as_strcmp(section_name, v740b.section) != 0) {
        return -1;
    }
    memset(&v741b, 0, sizeof(v741b));
    v741b.has_section = 1;
    v741b.section = section_name_keep(ctx, section_name);
    v741b.value = (long long)sec_off;
    repl_len = v744f.value - v743f.value;
    orig_len = v741b.value - v740b.value;
    delta = repl_len - orig_len;
    *out = delta > 0 ? delta : 0;
    return 0;
}

int eval_linux_alt_len_expr(emit_ctx_t *ctx, const as_stmt_t *st, const char *arg, long long *out) {
    virtual_addr_value_t a;
    virtual_addr_value_t b;

    if (ctx == NULL || st == NULL || arg == NULL || out == NULL) {
        return -1;
    }
    if (as_strcmp(arg, "742b-740b") == 0) {
        const as_stmt_t *s742 = resolve_local_ref_target_stmt(ctx, st, st->unit,742, 0);
        const as_stmt_t *s740 = resolve_local_ref_target_stmt(ctx, st, st->unit,740, 0);
        size_t i742;
        size_t i740;
        uint64_t orig = 0;
        const char *section = "";

        if (s742 != NULL && s740 != NULL &&
            parsed_stmt_index(ctx, s742, &i742) == 0 &&
            parsed_stmt_index(ctx, s740, &i740) == 0 &&
            i740 <= i742 &&
            stmt_declared_label_section(ctx, s740, &section) == 0 &&
            linux_alt_original_range_size(ctx, section, i740, i742,
                                          ctx->cfg != NULL ? ctx->cfg->x86_code_bits : 64u,
                                          &orig) == 0) {
            *out = (long long)orig;
            return 0;
        }
        if (numeric_local_virtual_location(ctx, st, 742, 0, &a) != 0 ||
            numeric_local_virtual_location(ctx, st, 740, 0, &b) != 0 ||
            !a.has_section || !b.has_section || as_strcmp(a.section, b.section) != 0) {
            return -1;
        }
        *out = a.value - b.value;
        return 0;
    }
    if (as_strcmp(arg, "744f-743f") == 0) {
        if (numeric_local_virtual_location(ctx, st, 744, 1, &a) != 0 ||
            numeric_local_virtual_location(ctx, st, 743, 1, &b) != 0 ||
            !a.has_section || !b.has_section || as_strcmp(a.section, b.section) != 0) {
            return -1;
        }
        *out = a.value - b.value;
        return 0;
    }
    return -1;
}

static int eval_local_align_fill_expr(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st,
                                      uint64_t sec_off, unsigned x86_code_bits, const char *arg,
                                      long long *out) {
    const char *plus;
    const char *minus_dot;
    char *align_text;
    long long align = 0;
    long long rel = 0;
    as_expr_t *local;
    int rc = -1;

    if (arg == NULL || strstr(arg, "0b") == NULL || (minus_dot = strstr(arg, "- .")) == NULL ||
        (plus = strchr(arg, '+')) == NULL || plus >= minus_dot) {
        return -1;
    }
    align_text = (char *)malloc((size_t)(minus_dot - plus));
    if (align_text == NULL) {
        return -1;
    }
    memcpy(align_text, plus + 1, (size_t)(minus_dot - plus - 1));
    align_text[minus_dot - plus - 1] = '\0';
    if (parse_const_expr_string(align_text, &align) != 0 && parse_int64(align_text, &align) != 0) {
        goto out;
    }
    local = as_parse_expr_string("0b", st != NULL ? st->unit : NULL, st != NULL ? st->ordinal : 0);
    if (local == NULL) {
        goto out;
    }
    if (eval_local_rel_expr_virtual(ctx, section_name, st, sec_off, x86_code_bits, local, &rel) == 0) {
        *out = align + rel;
        if (*out < 0) {
            *out = 0;
        }
        rc = 0;
    }
    as_expr_free(local);

out:
    free(align_text);
    return rc;
}

static int parsed_stmt_index(emit_ctx_t *ctx, const as_stmt_t *needle, size_t *idx_out) {
    if (ctx == NULL || needle == NULL || idx_out == NULL || ctx->parsed == NULL ||
        ctx->parsed->items == NULL) {
        return -1;
    }
    /* Stmts live in a contiguous array, so index is direct pointer math. */
    if (needle < ctx->parsed->items || needle >= ctx->parsed->items + ctx->parsed->count) {
        return -1;
    }
    *idx_out = (size_t)(needle - ctx->parsed->items);
    return 0;
}

static int local_temp_branch_target_within(emit_ctx_t *ctx, const as_stmt_t *base_st,
                                           const as_expr_t *rel_expr, size_t max_stmt_distance) {
    const as_stmt_t *target_st = NULL;
    size_t base_idx;
    size_t target_idx;
    size_t i;

    if (ctx == NULL || base_st == NULL || rel_expr == NULL ||
        rel_expr->kind != AS_EXPR_SYMBOL ||
        !expr_is_local_temp_symbol(rel_expr) ||
        parsed_stmt_index(ctx, base_st, &base_idx) != 0) {
        return 0;
    }
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        size_t j;

        for (j = 0; j < st->label_count; ++j) {
            if (st->labels[j].name != NULL && as_strcmp(st->labels[j].name, rel_expr->symbol) == 0) {
                target_st = st;
                break;
            }
        }
        if (target_st != NULL) {
            break;
        }
    }
    if (target_st == NULL || parsed_stmt_index(ctx, target_st, &target_idx) != 0) {
        return 0;
    }
    if (target_idx > base_idx) {
        return target_idx - base_idx <= max_stmt_distance;
    }
    return base_idx - target_idx <= max_stmt_distance;
}

/* An alignment or an .org is as long as it takes to get from where it is
 * to where it leads: its size is not the statement's alone and must not
 * be remembered as if it were. */
static int stmt_size_depends_on_offset(const as_stmt_t *st) {
    const char *name;

    if (st == NULL || st->kind != AS_STMT_DIRECTIVE || st->u.directive.name == NULL) {
        return 0;
    }
    name = st->u.directive.name;
    return as_strcmp(name, ".align") == 0 || as_strcmp(name, ".balign") == 0 ||
           as_strcmp(name, ".p2align") == 0 || as_strcmp(name, ".org") == 0;
}

static int stmt_virtual_size_in_section(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st,
                                        uint64_t sec_off, unsigned x86_code_bits, size_t *size_out) {
    bytebuf_t tmp;
    size_t cache_idx = (size_t)-1;
    int cache_eligible = 0;

    if (ctx == NULL || section_name == NULL || st == NULL || size_out == NULL) {
        return -1;
    }
    *size_out = 0;
    memset(&tmp, 0, sizeof(tmp));
    /* Cache hit path: when called below the top-level scan (virtual_scanning > 0),
     * branches degrade to a conservative size that does not depend on `sec_off`,
     * so the value is fully determined by the statement. Memoise. */
    if (ctx->virtual_scanning > 0 && ctx->parsed != NULL && parsed_stmt_index(ctx, st, &cache_idx) == 0 &&
        !stmt_size_depends_on_offset(st)) {
        cache_eligible = 1;
        if (cache_idx < ctx->stmt_size_cache_count && ctx->stmt_size_cached != NULL &&
            ctx->stmt_size_cached[cache_idx] == ctx->stmt_size_stamp + 1u) {
            *size_out = ctx->stmt_size_cache[cache_idx];
            return 0;
        }
    }
    if (st->kind == AS_STMT_INSTRUCTION) {
        /* Branch sizing is recursive: computing one branch's exact size may
         * require sizing a range that contains other branches. Two branches
         * straddling each other can drive the recursion to stack overflow on
         * large inputs (the Linux kernel hits >25k frames). Past the top
         * level we fall back to the conservative virtual-byte path, which
         * produces a safe upper bound (rel32) and never recurses further.
         * The top-level call still gets the precise size; only nested
         * sizing of *other* branches is conservatized. */
        if (st->u.instr.operand_count == 1 && is_rel_mnemonic(st->u.instr.mnemonic) &&
            ctx->virtual_scanning == 0) {
            const as_operand_t *op = &st->u.instr.operands[0];
            const as_expr_t *e = op->u.expr;
            if ((ctx->virtual_scanning == 0 ||
                 (ctx->virtual_scanning == 1 && local_temp_branch_target_within(ctx, st, e, 64))) &&
                (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
                e != NULL && e->kind == AS_EXPR_SYMBOL && expr_is_local_temp_symbol(e)) {
                as_elf_cfg_t local_cfg = *ctx->cfg;
                long long abs_target;
                int may_short = x86_branch_may_be_short(ctx, section_name, st, e, x86_code_bits);
                unsigned char code[32];
                size_t code_len = 0;
                char encerr[128];

                local_cfg.x86_code_bits = x86_code_bits;
                local_cfg.is_64 = x86_code_bits == 64u;
                if (eval_direct_local_branch_target(ctx, section_name, st, sec_off, x86_code_bits, e,
                                                    2, &abs_target) == 0 &&
                    emit_resolved_x86_branch_ex(&local_cfg, st, sec_off, abs_target, may_short, code, sizeof(code),
                                             &code_len, encerr, sizeof(encerr)) == 0) {
                    if (code_len != 2 &&
                        eval_direct_local_branch_target(ctx, section_name, st, sec_off, x86_code_bits, e,
                                                        code_len, &abs_target) == 0 &&
                        emit_resolved_x86_branch_ex(&local_cfg, st, sec_off, abs_target, may_short, code, sizeof(code),
                                                 &code_len, encerr, sizeof(encerr)) != 0) {
                        return -1;
                    }
                    *size_out = code_len;
                    return 0;
                }
            }
        }
        if (!section_name_is_executable(ctx, section_name) ||
            append_virtual_instruction_bytes(ctx, &tmp, section_name, st, x86_code_bits) != 0) {
            free(tmp.data);
            return -1;
        }
        *size_out = tmp.len;
        free(tmp.data);
        if (cache_eligible && ctx->parsed != NULL) {
            if (ctx->stmt_size_cache_count < ctx->parsed->count) {
                size_t want = ctx->parsed->count;
                size_t *new_cache = (size_t *)realloc(ctx->stmt_size_cache, want * sizeof(*new_cache));
                unsigned *new_flags = (unsigned *)realloc(ctx->stmt_size_cached, want * sizeof(*new_flags));
                if (new_cache != NULL && new_flags != NULL) {
                    if (ctx->stmt_size_cache_count < want) {
                        memset(new_cache + ctx->stmt_size_cache_count, 0,
                               (want - ctx->stmt_size_cache_count) * sizeof(*new_cache));
                        memset(new_flags + ctx->stmt_size_cache_count, 0,
                               (want - ctx->stmt_size_cache_count) * sizeof(*new_flags));
                    }
                    ctx->stmt_size_cache = new_cache;
                    ctx->stmt_size_cached = new_flags;
                    ctx->stmt_size_cache_count = want;
                }
            }
            if (cache_idx < ctx->stmt_size_cache_count && ctx->stmt_size_cached != NULL) {
                ctx->stmt_size_cache[cache_idx] = *size_out;
                ctx->stmt_size_cached[cache_idx] = ctx->stmt_size_stamp + 1u;
            }
        }
        return 0;
    }
    if (st->kind == AS_STMT_DIRECTIVE) {
        section_track_t track;
        int trc;
        unsigned data_width = directive_fixed_scalar_width(&st->u.directive);

        /*
         * A directive that lays down values of a fixed width is that
         * width times their number, whatever the values are.  Measuring
         * it by assembling it evaluated them: a jump table's
         * `.long .L15` asked where .L15 is, which measured the branches
         * before it, which measured what lay between each and its label
         * -- this directive among it -- without end.
         */
        if (data_width != 0) {
            *size_out = (size_t)data_width * st->u.directive.arg_count;
            return 0;
        }

        if (section_track_init(&track, x86_code_bits) != 0) {
            return -1;
        }
        free(track.current);
        track.current = xstrdup(section_name);
        if (track.current == NULL) {
            section_track_free(&track);
            return -1;
        }
        trc = section_track_apply_directive(&track, &st->u.directive);
        if (trc < 0) {
            section_track_free(&track);
            return -1;
        }
        if (trc > 0) {
            section_track_free(&track);
            return 0;
        }
        if (append_directive_data_ctx(ctx, &tmp, section_name, st, sec_off, x86_code_bits, &st->u.directive) < 0) {
            free(tmp.data);
            section_track_free(&track);
            return -1;
        }
        *size_out = tmp.len;
        free(tmp.data);
        section_track_free(&track);
        if (cache_eligible && ctx->parsed != NULL) {
            if (ctx->stmt_size_cache_count < ctx->parsed->count) {
                size_t want = ctx->parsed->count;
                size_t *new_cache = (size_t *)realloc(ctx->stmt_size_cache, want * sizeof(*new_cache));
                unsigned *new_flags = (unsigned *)realloc(ctx->stmt_size_cached, want * sizeof(*new_flags));
                if (new_cache != NULL && new_flags != NULL) {
                    if (ctx->stmt_size_cache_count < want) {
                        memset(new_cache + ctx->stmt_size_cache_count, 0,
                               (want - ctx->stmt_size_cache_count) * sizeof(*new_cache));
                        memset(new_flags + ctx->stmt_size_cache_count, 0,
                               (want - ctx->stmt_size_cache_count) * sizeof(*new_flags));
                    }
                    ctx->stmt_size_cache = new_cache;
                    ctx->stmt_size_cached = new_flags;
                    ctx->stmt_size_cache_count = want;
                }
            }
            if (cache_idx < ctx->stmt_size_cache_count && ctx->stmt_size_cached != NULL) {
                ctx->stmt_size_cache[cache_idx] = *size_out;
                ctx->stmt_size_cached[cache_idx] = ctx->stmt_size_stamp + 1u;
            }
        }
        return 0;
    }
    return 0;
}

/* Find or build the prefix-sum array for (section_name, x86_code_bits).
 * Returns NULL on allocation failure. The returned array has parsed->count + 1
 * entries; prefix[i] is the cumulative size of stmts j < i in the named
 * section (under the conservative virtual sizing, i.e. with virtual_scanning
 * incremented so branches degrade to a fixed length). */
static const uint32_t *get_section_prefix_sums(emit_ctx_t *ctx, const char *section_name,
                                               unsigned x86_code_bits) {
    size_t i;
    section_track_t track;
    uint32_t *prefix;
    uint64_t total;
    if (ctx == NULL || ctx->parsed == NULL || section_name == NULL) {
        return NULL;
    }
    for (i = 0; i < ctx->section_prefix_count; ++i) {
        if (ctx->section_prefixes[i].name != NULL &&
            ctx->section_prefixes[i].x86_code_bits == x86_code_bits &&
            as_strcmp(ctx->section_prefixes[i].name, section_name) == 0) {
            return ctx->section_prefixes[i].prefix;
        }
    }
    /* Avoid re-entering during build: the size computation calls back into
     * stmt_virtual_size_in_section which calls back here. The build pass
     * sets section_prefix_building so the recursive call falls through to
     * the per-stmt cache + linear walk and does not try to build another
     * prefix table for a different section. */
    if (ctx->section_prefix_building) {
        return NULL;
    }
    if (ctx->section_prefix_count == ctx->section_prefix_cap) {
        size_t ncap = ctx->section_prefix_cap == 0 ? 4 : ctx->section_prefix_cap * 2;
        void *next = realloc(ctx->section_prefixes, ncap * sizeof(*ctx->section_prefixes));
        if (next == NULL) {
            return NULL;
        }
        ctx->section_prefixes = next;
        ctx->section_prefix_cap = ncap;
    }
    prefix = (uint32_t *)calloc(ctx->parsed->count + 1, sizeof(*prefix));
    if (prefix == NULL) {
        return NULL;
    }
    if (section_track_init(&track, x86_code_bits) != 0) {
        free(prefix);
        return NULL;
    }
    free(track.current);
    track.current = xstrdup(".text");
    if (track.current == NULL) {
        section_track_free(&track);
        free(prefix);
        return NULL;
    }
    total = 0;
    ctx->virtual_scanning++;
    ctx->section_prefix_building = 1;
    prefix[0] = 0;
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                ctx->section_prefix_building = 0;
                ctx->virtual_scanning--;
                section_track_free(&track);
                free(prefix);
                return NULL;
            }
            if (trc > 0) {
                prefix[i + 1] = (uint32_t)total;
                continue;
            }
        }
        if (track.current != NULL && as_strcmp(track.current, section_name) == 0) {
            size_t n = 0;
            /* (In the mode the statement is in, which a .code16 before
             * it may have made another than the file began in.) */
            if (stmt_virtual_size_in_section(ctx, section_name, st, total, track.x86_code_bits, &n) == 0) {
                total += (uint64_t)n;
            }
        }
        /* (A section of more than 4 GiB has no table: it is walked.) */
        if (total > UINT32_MAX) {
            ctx->section_prefix_building = 0;
            ctx->virtual_scanning--;
            section_track_free(&track);
            free(prefix);
            return NULL;
        }
        prefix[i + 1] = (uint32_t)total;
    }
    ctx->section_prefix_building = 0;
    ctx->virtual_scanning--;
    section_track_free(&track);
    {
        struct as_section_prefix_s *slot = &ctx->section_prefixes[ctx->section_prefix_count];
        slot->name = xstrdup(section_name);
        slot->x86_code_bits = x86_code_bits;
        slot->prefix = prefix;
        slot->prefix_count = ctx->parsed->count + 1;
        if (slot->name == NULL) {
            free(prefix);
            return NULL;
        }
        ctx->section_prefix_count++;
        return prefix;
    }
}

static int stmt_range_size_in_section(emit_ctx_t *ctx, const char *section_name, size_t start_idx, size_t end_idx,
                                      uint64_t sec_off, unsigned x86_code_bits, uint64_t *size_out) {
    section_track_t track;
    uint64_t total = 0;
    size_t i;
    const uint32_t *prefix;

    (void)sec_off;
    if (ctx == NULL || section_name == NULL || size_out == NULL || start_idx > end_idx ||
        end_idx > ctx->parsed->count) {
        return -1;
    }
    /* Fast path: prefix-sum O(1) lookup. Only safe when not currently
     * building the table itself (which calls us recursively per stmt). */
    if (!ctx->section_prefix_building) {
        prefix = get_section_prefix_sums(ctx, section_name, x86_code_bits);
        if (prefix != NULL) {
            *size_out = prefix[end_idx] - prefix[start_idx];
            return 0;
        }
    }
    if (section_track_init(&track, x86_code_bits) != 0) {
        return -1;
    }
    free(track.current);
    track.current = xstrdup(section_name);
    if (track.current == NULL) {
        section_track_free(&track);
        return -1;
    }
    ctx->virtual_scanning++;
    for (i = start_idx; i < end_idx; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];

        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                ctx->virtual_scanning--;
                section_track_free(&track);
                return -1;
            }
            if (trc > 0) {
                continue;
            }
        }
        if (track.current == NULL || as_strcmp(track.current, section_name) != 0) {
            continue;
        }
        {
            size_t n = 0;
            if (stmt_virtual_size_in_section(ctx, section_name, st, total, x86_code_bits, &n) != 0) {
                ctx->virtual_scanning--;
                section_track_free(&track);
                return -1;
            }
            total += (uint64_t)n;
        }
    }
    ctx->virtual_scanning--;
    section_track_free(&track);
    *size_out = total;
    return 0;
}

static int linux_alt_original_range_size(emit_ctx_t *ctx, const char *section_name, size_t start_idx, size_t end_idx,
                                         unsigned x86_code_bits, uint64_t *size_out) {
    section_track_t track;
    uint64_t total = 0;
    size_t i;

    if (ctx == NULL || section_name == NULL || size_out == NULL || start_idx > end_idx ||
        end_idx > ctx->parsed->count) {
        return -1;
    }
    if (section_track_init(&track, x86_code_bits) != 0) {
        return -1;
    }
    free(track.current);
    track.current = xstrdup(section_name);
    if (track.current == NULL) {
        section_track_free(&track);
        return -1;
    }
    ctx->virtual_scanning++;
    for (i = start_idx; i < end_idx; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];

        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                ctx->virtual_scanning--;
                section_track_free(&track);
                return -1;
            }
            if (trc > 0) {
                continue;
            }
        }
        if (track.current == NULL || as_strcmp(track.current, section_name) != 0) {
            continue;
        }
        if (st->kind == AS_STMT_DIRECTIVE &&
            st->u.directive.name != NULL &&
            (as_strcmp(st->u.directive.name, ".skip") == 0 ||
             as_strcmp(st->u.directive.name, ".space") == 0) &&
            st->u.directive.arg_count >= 1 &&
            st->u.directive.args[0] != NULL &&
            strstr(st->u.directive.args[0], "744f-743f") != NULL &&
            strstr(st->u.directive.args[0], "741b-740b") != NULL) {
            const as_stmt_t *s744 = resolve_local_ref_target_stmt(ctx, st, st->unit,744, 1);
            const as_stmt_t *s743 = resolve_local_ref_target_stmt(ctx, st, st->unit,743, 1);
            size_t i744;
            size_t i743;
            uint64_t repl = 0;

            if (s744 == NULL || s743 == NULL ||
                parsed_stmt_index(ctx, s744, &i744) != 0 ||
                parsed_stmt_index(ctx, s743, &i743) != 0 ||
                i743 > i744 ||
                stmt_range_size_in_section(ctx, ".altinstr_replacement", i743, i744,
                                           0, x86_code_bits, &repl) != 0) {
                ctx->virtual_scanning--;
                section_track_free(&track);
                return -1;
            }
            if (repl > total) {
                total += repl - total;
            }
            continue;
        }
        {
            size_t n = 0;

            if (stmt_virtual_size_in_section(ctx, section_name, st, total, x86_code_bits, &n) != 0) {
                ctx->virtual_scanning--;
                section_track_free(&track);
                return -1;
            }
            total += (uint64_t)n;
        }
    }
    ctx->virtual_scanning--;
    section_track_free(&track);
    *size_out = total;
    return 0;
}

/* Lazy build of the per-stmt section table. Walks parsed once and
 * records track.current at each stmt position. Subsequent
 * stmt_declared_label_section calls are O(1). */
static int build_stmt_section_at(emit_ctx_t *ctx) {
    section_track_t track;
    size_t i;
    char **table;
    if (ctx == NULL || ctx->parsed == NULL) {
        return -1;
    }
    if (ctx->stmt_section_at != NULL && ctx->stmt_section_at_count == ctx->parsed->count) {
        return 0;
    }
    table = (char **)calloc(ctx->parsed->count, sizeof(*table));
    if (table == NULL) {
        return -1;
    }
    if (section_track_init(&track, ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                                   (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        free(table);
        return -1;
    }
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        table[i] = xstrdup(track.current != NULL ? track.current : "");
        if (table[i] == NULL) {
            section_track_free(&track);
            for (i = 0; i < ctx->parsed->count; ++i) {
                free(table[i]);
            }
            free(table);
            return -1;
        }
        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                section_track_free(&track);
                for (i = 0; i < ctx->parsed->count; ++i) {
                    free(table[i]);
                }
                free(table);
                return -1;
            }
        }
    }
    section_track_free(&track);
    ctx->stmt_section_at = table;
    ctx->stmt_section_at_count = ctx->parsed->count;
    return 0;
}

static int stmt_declared_label_section(emit_ctx_t *ctx, const as_stmt_t *target_st, const char **section_out) {
    size_t idx;

    if (ctx == NULL || target_st == NULL || section_out == NULL) {
        return -1;
    }
    *section_out = "";
    if (build_stmt_section_at(ctx) != 0) {
        return -1;
    }
    if (parsed_stmt_index(ctx, target_st, &idx) != 0 || idx >= ctx->stmt_section_at_count) {
        return -1;
    }
    if (ctx->stmt_section_at[idx] != NULL) {
        /* The table's own copy: it lasts as long as the context. */
        *section_out = ctx->stmt_section_at[idx];
    }
    return 0;
}

/*
 * The statement a label is on: the first, in the order of the
 * statements, that has it.  Asked for every branch on every pass --
 * by going through every label of the source, once; then by halving a
 * sorted list of them, seventeen names compared for one of a hundred
 * thousand, which was a sixth of the time of assembling branches; and
 * by one hash of the name now.
 */
static int label_stmt_index(emit_ctx_t *ctx, const char *name, size_t *idx_out) {
    if (ctx == NULL || ctx->parsed == NULL || name == NULL) {
        return -1;
    }
    if (!ctx->label_index_made) {
        size_t i, j;

        for (i = 0; i < ctx->parsed->count; ++i) {
            const as_stmt_t *st = &ctx->parsed->items[i];

            for (j = 0; j < st->label_count; ++j) {
                size_t had;

                /* (The first of a name that is defined twice.) */
                if (st->labels[j].name != NULL && !as_strmap_get(&ctx->label_index, st->labels[j].name, &had) &&
                    as_strmap_put(&ctx->label_index, st->labels[j].name, i) != 0) {
                    return -1;
                }
            }
        }
        ctx->label_index_made = 1;
    }
    return as_strmap_get(&ctx->label_index, name, idx_out) ? 0 : -1;
}

static int eval_direct_local_branch_target(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *base_st,
                                           uint64_t base_off, unsigned x86_code_bits, const as_expr_t *rel_expr,
                                           size_t branch_len, long long *abs_target_out) {
    const as_stmt_t *target_st;
    size_t base_idx;
    size_t target_idx;
    size_t i;
    uint64_t cur;

    if (ctx == NULL || section_name == NULL || base_st == NULL || rel_expr == NULL ||
        abs_target_out == NULL ||
        parsed_stmt_index(ctx, base_st, &base_idx) != 0) {
        return -1;
    }
    if (rel_expr->kind == AS_EXPR_LOCAL_REF) {
        target_st = resolve_local_ref_target_stmt(ctx, base_st, rel_expr->src_file,
                                                  rel_expr->local_digit, rel_expr->local_forward);
    } else if (rel_expr->kind == AS_EXPR_SYMBOL && rel_expr->symbol != NULL) {
        target_st = label_stmt_index(ctx, rel_expr->symbol, &i) == 0 ? &ctx->parsed->items[i] : NULL;
    } else {
        return -1;
    }
    if (target_st == NULL || parsed_stmt_index(ctx, target_st, &target_idx) != 0 || target_idx == base_idx) {
        return -1;
    }
    {
        const char *target_section = "";

        if (stmt_declared_label_section(ctx, target_st, &target_section) != 0 ||
            as_strcmp(target_section, section_name) != 0) {
            return -1;
        }
    }
    (void)i;
    if (target_idx > base_idx) {
        if (stmt_range_size_in_section(ctx, section_name, base_idx + 1, target_idx,
                                       base_off + (uint64_t)branch_len, x86_code_bits, &cur) != 0) {
            return -1;
        }
        *abs_target_out = (long long)(base_off + (uint64_t)branch_len + cur);
        return 0;
    }
    if (stmt_range_size_in_section(ctx, section_name, target_idx, base_idx, base_off,
                                   x86_code_bits, &cur) != 0) {
        return -1;
    }
    *abs_target_out = (long long)base_off - (long long)cur;
    return 0;
}

/*
 * What a jump names, where that is one place and a number of bytes from
 * it: a label, numbered or not, or `.`, alone or with a number added or
 * taken away.  0 with the place's node and the number, -1 for anything
 * else.
 */
static int relax_branch_parts(const as_expr_t *e, const as_expr_t **place, long long *addend) {
    const as_expr_t *other = NULL;

    *addend = 0;
    if (e == NULL) {
        return -1;
    }
    if (e->kind == AS_EXPR_BINARY && e->lhs != NULL && e->rhs != NULL &&
        (e->op == AS_EXPR_OP_ADD || e->op == AS_EXPR_OP_SUB)) {
        if (e->rhs->kind == AS_EXPR_CONST) {
            other = e->lhs;
            *addend = e->op == AS_EXPR_OP_ADD ? e->rhs->value : (long long)(0ull - (unsigned long long)e->rhs->value);
        } else if (e->lhs->kind == AS_EXPR_CONST && e->op == AS_EXPR_OP_ADD) {
            other = e->rhs;
            *addend = e->lhs->value;
        } else {
            return -1;
        }
        e = other;
    }
    if (e->kind != AS_EXPR_LOCAL_REF && !(e->kind == AS_EXPR_SYMBOL && e->symbol != NULL)) {
        return -1;
    }
    *place = e;
    return 0;
}

/*
 * The statement a branch that is sized here goes to, or -1 if it is not
 * one: a `jmp` or a conditional jump of 32- or 64-bit code, to one label
 * of the source -- a numbered one, a compiler's, or any that no other
 * file sees -- in the section the branch is in.
 */
static int relax_branch_target(emit_ctx_t *ctx, size_t idx, const char *section, unsigned code_bits,
                               size_t *target_out) {
    const as_stmt_t *st = &ctx->parsed->items[idx];
    const as_stmt_t *target_st = NULL;
    const as_operand_t *op;
    const as_expr_t *e;
    long long addend;
    char mnbuf[32];
    char suffix = '\0';
    unsigned cc;
    size_t target;

    (void)code_bits;
    if (st->kind != AS_STMT_INSTRUCTION || st->u.instr.operand_count != 1 ||
        st->u.instr.target_in_other_section || !is_rel_mnemonic(st->u.instr.mnemonic) ||
        is_call_mnemonic(st->u.instr.mnemonic) || is_fixed_short_rel_mnemonic(st->u.instr.mnemonic) ||
        normalize_x86_mnemonic(st->u.instr.mnemonic, mnbuf, sizeof(mnbuf), &suffix) != 0 ||
        (!streq_ci(mnbuf, "jmp") && x86_cond_code(mnbuf, &cc) != 0)) {
        return -1;
    }
    op = &st->u.instr.operands[0];
    if ((op->kind != AS_OPERAND_LABEL_REF && op->kind != AS_OPERAND_IMMEDIATE) ||
        (op->raw != NULL && op->raw[0] == '*') || relax_branch_parts(op->u.expr, &e, &addend) != 0) {
        return -1;
    }
    /* `.`: the jump itself. */
    if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL && as_strcmp(e->symbol, ".") == 0) {
        *target_out = idx;
        return 0;
    }
    if (e->kind == AS_EXPR_LOCAL_REF) {
        target_st = resolve_local_ref_target_stmt(ctx, st, e->src_file, e->local_digit, e->local_forward);
    } else if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL && expr_is_local_temp_symbol(e) &&
               label_stmt_index(ctx, e->symbol, &target) == 0) {
        target_st = &ctx->parsed->items[target];
    }
    if (target_st == NULL || parsed_stmt_index(ctx, target_st, &target) != 0 ||
        target >= ctx->stmt_section_at_count || ctx->stmt_section_at[target] == NULL ||
        as_strcmp(ctx->stmt_section_at[target], section) != 0) {
        return -1;
    }
    *target_out = target;
    return 0;
}

/* A jump that does not reach in two bytes: `jmp` is five, a conditional
 * one six. */
/* The long form of a jump: one opcode byte or two, a distance of 32
 * bits or -- in 16-bit code, or with a `w` -- of 16, and the operand-
 * size prefix where that is not the code's. */
static size_t relax_long_size(const as_stmt_t *st, unsigned code_bits) {
    char mnbuf[32];
    char suffix = '\0';
    int is_jmp = normalize_x86_mnemonic(st->u.instr.mnemonic, mnbuf, sizeof(mnbuf), &suffix) == 0 &&
                 streq_ci(mnbuf, "jmp");
    int code16 = code_bits == 16u;
    int wide16;

    suffix = x86_branch_size_letter(st, suffix, code_bits);
    wide16 = suffix == 'w' || (code16 && suffix != 'l');
    return (is_jmp ? 1u : 2u) + (wide16 ? 2u : 4u) + (wide16 != code16 ? 1u : 0u);
}

/* The sizes kept for the statements, forgotten: the next stamp, under
 * which none has been kept.  (When the stamps run out, which is after
 * four thousand million of them, the table is cleared.) */
void stmt_sizes_forget(emit_ctx_t *ctx) {
    if (++ctx->stmt_size_stamp == UINT_MAX) {
        ctx->stmt_size_stamp = 0;
        if (ctx->stmt_size_cached != NULL) {
            memset(ctx->stmt_size_cached, 0, ctx->stmt_size_cache_count * sizeof(*ctx->stmt_size_cached));
        }
    }
}

/* What is remembered of where statements are, forgotten: it was worked
 * out with branches of other sizes. */
static void relax_forget_layout(emit_ctx_t *ctx) {
    size_t i;

    ctx->layout_epoch++;
    virtual_label_cache_free(ctx);
    stmt_sizes_forget(ctx);
    for (i = 0; i < ctx->section_prefix_count; ++i) {
        free(ctx->section_prefixes[i].name);
        free(ctx->section_prefixes[i].prefix);
    }
    ctx->section_prefix_count = 0;
}

/*
 * Size the branches.
 *
 * A jump to a label of its own section is two bytes if the label is
 * within 127 bytes ahead or 128 behind, and five or six if not; and how
 * far the label is depends on the sizes of the jumps between.  So every
 * one starts short, the statements are laid out, each jump that does not
 * reach is made long, and that is done again until none changes -- which
 * it does, a jump only ever growing.  Then each is as short as it can
 * be, which is what GNU as makes it.
 *
 * Before this there were two guesses.  A jump to a compiler's label was
 * short if the statements between came to 127 bytes with every jump
 * among them counted long, and none were more than 64: right, but long
 * where it need not be.  A jump to a numbered label was counted as two
 * bytes whatever it was written as: after `jmp 9f` over 200 bytes, which
 * is five, every label was taken to be three bytes before its place by
 * whatever worked a distance out -- `jmp a` further down went three
 * bytes short of `a`, and `.long b - a` was three less than it is.
 *
 * 0, or -1 for no memory.  While it runs (relax_state 1) what asks is
 * given the sizes as they stand.
 */
static int relax_branches(emit_ctx_t *ctx) {
    size_t count = ctx->parsed != NULL ? ctx->parsed->count : 0;
    size_t *target = NULL;
    size_t *fixed = NULL;
    size_t *sid = NULL;
    unsigned *region = NULL;
    uint64_t *off = NULL;
    struct relax_at {
        const char *name;
        uint64_t off;
        unsigned region;
    } *at = NULL;
    size_t at_count = 0, at_cap = 0;
    section_track_t track;
    int tracking = 0;
    int changed = 0;
    int moved = 0;
    unsigned round = 0;
    int rc = -1;
    size_t i;

    if (ctx->relax_state != 0) {
        return 0;
    }
    if (count == 0 || ctx->cfg == NULL || ctx->cfg->target != 0 ||
        (ctx->cfg->machine != EM_386 && ctx->cfg->machine != EM_X86_64)) {
        ctx->relax_state = 2;
        return 0;
    }
    if (ctx->branch_short_count < count) {
        unsigned char *next = (unsigned char *)realloc(ctx->branch_short, count);

        if (next == NULL) {
            return -1;
        }
        ctx->branch_short = next;
        ctx->branch_short_count = count;
    }
    memset(ctx->branch_short, 0, ctx->branch_short_count);
    target = (size_t *)malloc(count * sizeof(*target));
    fixed = (size_t *)malloc(count * sizeof(*fixed));
    off = (uint64_t *)malloc(count * sizeof(*off));
    if (target == NULL || fixed == NULL || off == NULL || build_stmt_section_at(ctx) != 0) {
        goto out;
    }
    /* Which statements are the jumps, each of them short to begin with. */
    if (section_track_init(&track, ctx->cfg->is_64 ? 64u : (ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        goto out;
    }
    tracking = 1;
    for (i = 0; i < count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];

        fixed[i] = (size_t)-1;
        target[i] = (size_t)-1;
        if (st->kind == AS_STMT_DIRECTIVE) {
            if (section_track_apply_directive(&track, &st->u.directive) < 0) {
                goto out;
            }
        } else if (relax_branch_target(ctx, i, track.current != NULL ? track.current : "", track.x86_code_bits,
                                       &target[i]) == 0) {
            ctx->branch_short[i] = 1;
        }
    }
    /*
     * Which section each statement is in, by number: the sections are
     * numbered as they are met, through a table of their names.  (The
     * name was looked for among all the sections at every statement of
     * every round, which for a section to a function is every function
     * for every function.)
     */
    {
        size_t cap = 64;
        size_t *slots;

        while (cap < count * 2) {
            cap *= 2;
        }
        slots = (size_t *)calloc(cap, sizeof(*slots));
        sid = (size_t *)malloc(count * sizeof(*sid));
        region = (unsigned *)malloc(count * sizeof(*region));
        if (slots == NULL || sid == NULL || region == NULL) {
            free(slots);
            goto out;
        }
        for (i = 0; i < count; ++i) {
            const char *name = ctx->stmt_section_at[i];
            size_t h = 2166136261u;
            const char *p;
            size_t s;

            for (p = name; *p != '\0'; ++p) {
                h = (h ^ (unsigned char)*p) * 16777619u;
            }
            for (s = h & (cap - 1); slots[s] != 0 && as_strcmp(at[slots[s] - 1].name, name) != 0; s = (s + 1) & (cap - 1)) {
            }
            if (slots[s] == 0) {
                if (at_count == at_cap) {
                    size_t ncap = at_cap != 0 ? at_cap * 2 : 16;
                    struct relax_at *grown = (struct relax_at *)realloc(at, ncap * sizeof(*grown));

                    if (grown == NULL) {
                        free(slots);
                        goto out;
                    }
                    at = grown;
                    at_cap = ncap;
                }
                at[at_count].name = name;
                at[at_count].off = 0;
                at[at_count].region = 0;
                slots[s] = ++at_count;
            }
            sid[i] = slots[s] - 1;
            /* (And which stretch of its section between alignments,
             * for the rounds below: an alignment is the last of one.) */
            region[i] = at[sid[i]].region;
            {
                const as_stmt_t *st = &ctx->parsed->items[i];

                if (st->kind == AS_STMT_DIRECTIVE && st->u.directive.name != NULL &&
                    (as_strcmp(st->u.directive.name, ".p2align") == 0 || as_strcmp(st->u.directive.name, ".balign") == 0 ||
                     as_strcmp(st->u.directive.name, ".align") == 0 || as_strcmp(st->u.directive.name, ".org") == 0)) {
                    at[sid[i]].region++;
                }
            }
        }
        free(slots);
    }
    ctx->relax_state = 1;
    ctx->virtual_scanning++;
    /*
     * A round goes down the source once.  Each statement is where the
     * ones before it now put it; a short jump is asked whether it still
     * reaches -- a label behind it is where this round put it, and one
     * ahead where the last round did, moved by as much as the jump
     * itself has been -- and if it does not it is long from there on,
     * in the round that found it.  The rounds end when one changes no
     * jump and moves no statement, so that every distance asked about
     * in it was the true one.
     *
     * (Every jump of a round was asked against the layout of the round
     * before, all at once: a jump that did not reach by the width of
     * some padding was made long though another, made long in the same
     * round, had taken the padding away.  GNU as walks as this does, and
     * what a compiler makes at -O2 came out some bytes different from
     * it in one function in eight.)
     */
    /* (The first round lays the statements out with every jump short,
     * and asks nothing; the second is the first that asks.) */
    while (changed || moved || round < 2) {
        changed = 0;
        moved = 0;
        relax_forget_layout(ctx);
        if (tracking) {
            section_track_free(&track);
        }
        if (section_track_init(&track, ctx->cfg->is_64 ? 64u : (ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
            tracking = 0;
            goto unwind;
        }
        tracking = 1;
        for (i = 0; i < at_count; ++i) {
            at[i].off = 0;
        }
        for (i = 0; i < count; ++i) {
            const as_stmt_t *st = &ctx->parsed->items[i];
            const char *section = track.current != NULL ? track.current : "";
            size_t k = sid[i];
            size_t n = 0;
            long long stretch = round == 0 ? 0 : (long long)at[k].off - (long long)off[i];

            moved |= stretch != 0;
            off[i] = at[k].off;
            if (round != 0 && ctx->branch_short[i] == 1) {
                const as_expr_t *place;
                long long addend;
                long long there;
                long long disp;
                long long pre = (long long)x86_branch_flip16(st) + (long long)x86_branch_hint(st);

                (void)relax_branch_parts(st->u.instr.operands[0].u.expr, &place, &addend);
                there = (long long)off[target[i]];
                /* A label ahead has yet to be moved in this round.  It
                 * will move as this jump has, unless an alignment lies
                 * between, whose padding takes up what came before it:
                 * then it is taken to stay, and if that puts it behind
                 * the jump the question is left for the next round. */
                if (target[i] > i && (stretch < 0 || region[target[i]] == region[i])) {
                    there += stretch;
                }
                disp = there + addend - ((long long)off[i] + 2 + pre);
                if (target[i] > i && there < (long long)off[i]) {
                    disp = 0;
                }
                if (disp < -128 || disp > 127) {
                    ctx->branch_short[i] = 2;
                    changed = 1;
                    /* (What has been remembered of sizes and places in
                     * this round was worked out with the jump short.) */
                    relax_forget_layout(ctx);
                }
            }
            if (st->kind == AS_STMT_DIRECTIVE) {
                int trc = section_track_apply_directive(&track, &st->u.directive);

                if (trc < 0) {
                    goto unwind;
                }
                if (trc > 0) {
                    continue;
                }
                (void)stmt_virtual_size_in_section(ctx, section, st, off[i], track.x86_code_bits, &n);
            } else if (st->kind == AS_STMT_INSTRUCTION) {
                if (ctx->branch_short[i] == 1) {
                    n = 2u + (size_t)x86_branch_flip16(st) + x86_branch_hint(st);
                } else if (ctx->branch_short[i] == 2) {
                    n = relax_long_size(st, track.x86_code_bits) + x86_branch_hint(st);
                } else {
                    /* Any other instruction is as long as it is,
                     * wherever it is: found once. */
                    if (fixed[i] == (size_t)-1 &&
                        stmt_virtual_size_in_section(ctx, section, st, off[i], track.x86_code_bits, &fixed[i]) != 0) {
                        fixed[i] = 0;
                    }
                    n = fixed[i];
                }
            }
            at[k].off += n;
        }
        round++;
    }
    rc = 0;
unwind:
    ctx->virtual_scanning--;
    relax_forget_layout(ctx);
    if (rc != 0) {
        memset(ctx->branch_short, 0, ctx->branch_short_count);
    }
    ctx->relax_state = rc == 0 ? 2 : 0;
out:
    if (tracking) {
        section_track_free(&track);
    }
    free(target);
    free(fixed);
    free(sid);
    free(region);
    free(off);
    free(at);
    return rc;
}

/*
 * Whether the branch `st` to the compiler's local label `e` takes the
 * short form, two bytes with a displacement of one.
 *
 * This is asked in two places that must be given the same answer: where
 * the branch is encoded, and where it is one of the statements another
 * branch measures its own distance across.  They used to be answered
 * differently.  Measured across, a branch within 64 statements of its
 * label was counted as short; encoded, it was short only if the distance
 * came to no more than 127 bytes, which 64 statements easily exceed.  A
 * loop compiled by the system's own cc has a jump back to its top of
 * about 170 bytes and a jump forward over that one to its end: the
 * forward jump counted the backward one as two bytes, it was written as
 * five, and the forward jump landed three bytes short of its label, in
 * the middle of the other.  Every program with a loop died of an illegal
 * instruction.
 *
 * So it is decided once, by relax_branches(), and both are told what it
 * decided.
 */
static int local_temp_branch_is_short(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st,
                                      const as_expr_t *e, unsigned x86_code_bits) {
    size_t base_idx;

    (void)section_name;
    (void)e;
    (void)x86_code_bits;
    if (ctx == NULL || ctx->parsed == NULL || st == NULL || parsed_stmt_index(ctx, st, &base_idx) != 0 ||
        relax_branches(ctx) != 0) {
        return 0;
    }
    return base_idx < ctx->branch_short_count && ctx->branch_short[base_idx] == 1;
}

/* Whether the statement is a branch relax_branches() has sized, and
 * then *is_short for which form it has. */
static int branch_is_relaxed(emit_ctx_t *ctx, const as_stmt_t *st, int *is_short) {
    size_t idx;

    if (ctx == NULL || ctx->parsed == NULL || st == NULL || parsed_stmt_index(ctx, st, &idx) != 0 ||
        relax_branches(ctx) != 0 || idx >= ctx->branch_short_count || ctx->branch_short[idx] == 0) {
        return 0;
    }
    *is_short = ctx->branch_short[idx] == 1;
    return 1;
}

/*
 * Where a sized jump goes that names `.`, or a label of the source that
 * is not a numbered one, with a number added: 0 with the place in its
 * section and which form the jump has, -1 if `st` is not such a jump.
 * (A label alone, and a numbered one with a number, were resolved
 * before there was this, and still are where they were.)
 */
/*
 * A call of where it stands, or so many bytes from there: `call .`,
 * `call .+5`.  It is as far as it says, like a jump of the same, and
 * was given a relocation against the section -- with the distance of
 * the wrong end of the instruction in it.
 */
int call_to_place_target(const as_stmt_t *st, uint64_t sec_off, long long *abs_target) {
    const as_expr_t *place = NULL;
    long long addend = 0;
    const as_operand_t *op;

    if (st == NULL || st->kind != AS_STMT_INSTRUCTION || st->u.instr.operand_count != 1 ||
        !is_call_mnemonic(st->u.instr.mnemonic) || strncasecmp(st->u.instr.mnemonic, "lcall", 5) == 0) {
        return -1;
    }
    op = &st->u.instr.operands[0];
    if ((op->kind != AS_OPERAND_LABEL_REF && op->kind != AS_OPERAND_IMMEDIATE) ||
        (op->raw != NULL && op->raw[0] == '*') || relax_branch_parts(op->u.expr, &place, &addend) != 0 ||
        place->kind != AS_EXPR_SYMBOL || as_strcmp(place->symbol, ".") != 0) {
        return -1;
    }
    *abs_target = (long long)sec_off + addend;
    return 0;
}

int relaxed_place_branch_target(emit_ctx_t *ctx, const as_stmt_t *st, const char *section_name,
                                       uint64_t sec_off, unsigned x86_code_bits, long long *abs_target,
                                       int *is_short) {
    const as_expr_t *place = NULL;
    long long addend = 0;

    if (st == NULL || st->kind != AS_STMT_INSTRUCTION || st->u.instr.operand_count != 1 ||
        !branch_is_relaxed(ctx, st, is_short) ||
        relax_branch_parts(st->u.instr.operands[0].u.expr, &place, &addend) != 0 || place->kind != AS_EXPR_SYMBOL) {
        return -1;
    }
    if (as_strcmp(place->symbol, ".") == 0) {
        *abs_target = (long long)sec_off + addend;
        return 0;
    }
    if (addend == 0 ||
        eval_direct_local_branch_target(ctx, section_name, st, sec_off, x86_code_bits, place,
                                        (*is_short ? 2 : relax_long_size(st, x86_code_bits)) + x86_branch_hint(st),
                                        abs_target) != 0) {
        return -1;
    }
    *abs_target += addend;
    return 0;
}

/*
 * Whether a branch may be encoded short if its displacement fits.  For
 * one to a compiler's local label that is the decision above; for the
 * others it is as it was.  So is 16-bit code, where the long forms are
 * shorter and the sizes assumed elsewhere are not these.
 */
static int x86_branch_may_be_short(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st,
                                   const as_expr_t *e, unsigned x86_code_bits) {
    /* To another section: how far is not known here. */
    int is_short;

    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.target_in_other_section) {
        return 0;
    }
    /* One that was sized is the size it was given. */
    if (branch_is_relaxed(ctx, st, &is_short)) {
        return is_short;
    }
    if (e == NULL || e->kind != AS_EXPR_SYMBOL || !expr_is_local_temp_symbol(e) || x86_code_bits == 16u) {
        return 1;
    }
    return local_temp_branch_is_short(ctx, section_name, st, e, x86_code_bits);
}

static unsigned stmt_local_rel_virtual_len(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st,
                                           unsigned x86_code_bits) {
    const as_operand_t *op;
    const as_expr_t *e;
    char mnbuf[32];
    char suffix = '\0';
    unsigned cc;
    int is_local;

    if (st == NULL || st->kind != AS_STMT_INSTRUCTION ||
        st->u.instr.operand_count != 1 ||
        !is_rel_mnemonic(st->u.instr.mnemonic) ||
        normalize_x86_mnemonic(st->u.instr.mnemonic, mnbuf, sizeof(mnbuf), &suffix) != 0) {
        return 0;
    }
    op = &st->u.instr.operands[0];
    if (op->kind != AS_OPERAND_LABEL_REF && op->kind != AS_OPERAND_IMMEDIATE) {
        return 0;
    }
    /* A jump that was sized is the size it was given. */
    if (branch_is_relaxed(ctx, st, &is_local)) {
        return is_local ? 2u + (unsigned)x86_branch_flip16(st) : (unsigned)relax_long_size(st, x86_code_bits);
    }
    e = op->u.expr;
    is_local = ((e != NULL && expr_has_local_ref(e)) ||
                (section_name != NULL && as_strcmp(section_name, ".altinstr_replacement") != 0 &&
                 e != NULL && expr_is_local_temp_symbol(e) &&
                 (x86_code_bits == 16u ? local_temp_branch_target_within(ctx, st, e, 64)
                                       : (e->kind == AS_EXPR_SYMBOL &&
                                          local_temp_branch_is_short(ctx, section_name, st, e, x86_code_bits)))) ||
                raw_is_numeric_local_ref(op->raw));
    /*
     * The distance is of 16 bits in 16-bit code and with a `w`, of 32
     * otherwise, and has the operand-size prefix where it is not the
     * code's.  A call was five bytes and a jump five or six whatever
     * the code: in 16-bit code every label behind a call or a long
     * jump was thought two bytes further on than it is, and a branch
     * over one went two bytes past where it was sent.
     */
    {
        int code16 = x86_code_bits == 16u;
        int wide16 = (suffix = x86_branch_size_letter(st, suffix, x86_code_bits)) == 'w' || (code16 && suffix != 'l');
        unsigned field = wide16 ? 2u : 4u;
        unsigned prefix = (wide16 != code16) ? 1u : 0u;

        if (is_call_mnemonic(st->u.instr.mnemonic)) {
            return 1u + field + prefix;
        }
        if (is_fixed_short_rel_mnemonic(st->u.instr.mnemonic)) {
            /* (And the address-size prefix, written: `addr16 loop`
             * counts in %cx.) */
            return (unsigned)fixed_short_rel_size(st->u.instr.mnemonic, x86_code_bits) +
                   ((st->u.instr.prefixes & AS_PREFIX_ADDR16) != 0 ? 1u : 0u);
        }
        if (is_local) {
            return 2u + (unsigned)x86_branch_flip16(st);
        }
        if (streq_ci(mnbuf, "jmp")) {
            return 1u + field + prefix;
        }
        if (x86_cond_code(mnbuf, &cc) == 0) {
            return 2u + field + prefix;
        }
    }
    return 0;
}

/*
 * ARM and AArch64.
 *
 * An instruction of theirs is as long as it is wherever it stands, so
 * there is nothing to relax: it is measured by encoding it with no
 * labels known, and encoded again once they are.
 */
typedef struct {
    emit_ctx_t *ctx;
    const char *section;
} arm_label_ctx_t;

static int arm_label(void *cookie, const char *name, uint64_t *offset) {
    arm_label_ctx_t *lc = (arm_label_ctx_t *)cookie;
    const char *sec = "";
    uint64_t off = 0;

    if (find_label_virtual_location(lc->ctx, NULL, NULL, 0, 0, name, &sec, &off) != 0 || sec == NULL ||
        lc->section == NULL || as_strcmp(sec, lc->section) != 0) {
        return 1;
    }
    *offset = off;
    return 0;
}

static int arm_encode_stmt(emit_ctx_t *ctx, const char *section_name, uint64_t sec_off, int placed,
                           unsigned code_bits, const as_stmt_t *st, unsigned char *code, size_t code_cap,
                           size_t *code_len, char *encerr, size_t encerr_sz) {
    const char *ops[16];
    arm_label_ctx_t lc;
    as_arm_req_t rq;
    size_t i;
    int rc;

    if (st->u.instr.operand_count > sizeof(ops) / sizeof(ops[0])) {
        snprintf(encerr, encerr_sz, "'%s' has too many operands", st->u.instr.mnemonic);
        return -1;
    }
    for (i = 0; i < st->u.instr.operand_count; ++i) {
        ops[i] = st->u.instr.operands[i].raw != NULL ? st->u.instr.operands[i].raw : "";
    }
    lc.ctx = ctx;
    lc.section = section_name;
    memset(&rq, 0, sizeof(rq));
    rq.mnemonic = st->u.instr.mnemonic;
    rq.ops = ops;
    rq.op_count = st->u.instr.operand_count;
    rq.pc = sec_off;
    rq.thumb = code_bits == 16u;
    rq.in_it = (int)st->u.instr.arm_in_it;
    rq.label = placed ? arm_label : NULL;
    rq.cookie = &lc;
    rc = ctx->cfg->target == EM_AARCH64 ? as_a64_encode(&rq, &ctx->arm_out, encerr, encerr_sz)
                                        : as_arm_encode(&rq, &ctx->arm_out, encerr, encerr_sz);
    if (rc != 0) {
        return -1;
    }
    if (ctx->arm_out.len > code_cap) {
        return -1;
    }
    memcpy(code, ctx->arm_out.bytes, ctx->arm_out.len);
    *code_len = ctx->arm_out.len;
    return 0;
}

/* Padding where instructions go: the instruction that does nothing,
 * behind as many zeros as it takes to stand where one may. */
int append_arm_nops(bytebuf_t *buf, size_t need, unsigned target, unsigned code_bits) {
    static const unsigned char arm_nop[4] = {0x00, 0xf0, 0x20, 0xe3};
    static const unsigned char thumb_nop[2] = {0x00, 0xbf};
    static const unsigned char a64_nop[4] = {0x1f, 0x20, 0x03, 0xd5};
    const unsigned char *nop = target == EM_AARCH64 ? a64_nop : (code_bits == 16u ? thumb_nop : arm_nop);
    size_t unit = target != EM_AARCH64 && code_bits == 16u ? 2 : 4;

    if (need % unit != 0 && bytebuf_append_zeros(buf, need % unit) != 0) {
        return -1;
    }
    for (need -= need % unit; need != 0; need -= unit) {
        if (bytebuf_append(buf, nop, unit) != 0) {
            return -1;
        }
    }
    return 0;
}

/* A statement between two labels that may be one size or another, so
 * that how far apart the labels are is not known until all are laid
 * out: a jump that is sized, an alignment, a change of code. */
static int stmt_is_of_variable_size(const as_stmt_t *st) {
    if (st->kind == AS_STMT_INSTRUCTION) {
        const as_instruction_t *in = &st->u.instr;

        return in->mnemonic != NULL && in->operand_count == 1 && is_rel_mnemonic(in->mnemonic) &&
               !is_call_mnemonic(in->mnemonic) && !is_fixed_short_rel_mnemonic(in->mnemonic) &&
               (in->operands[0].kind == AS_OPERAND_LABEL_REF || in->operands[0].kind == AS_OPERAND_IMMEDIATE) &&
               !(in->operands[0].raw != NULL && in->operands[0].raw[0] == '*');
    }
    if (st->kind == AS_STMT_DIRECTIVE && st->u.directive.name != NULL) {
        static const char *const always[] = {".p2align", ".balign", ".align",  ".org",    ".uleb128",
                                             ".sleb128", ".incbin", ".code16", ".code32", ".code64"};
        static const char *const counted[] = {".skip", ".space", ".zero", ".fill"};
        const as_directive_t *d = &st->u.directive;
        size_t i;

        for (i = 0; i < sizeof(always) / sizeof(always[0]); ++i) {
            if (as_strcmp(d->name, always[i]) == 0) {
                return 1;
            }
        }
        for (i = 0; i < sizeof(counted) / sizeof(counted[0]); ++i) {
            if (as_strcmp(d->name, counted[i]) == 0) {
                long long n;

                return d->arg_count == 0 || d->args[0] == NULL || parse_int64(d->args[0], &n) != 0;
            }
        }
    }
    return 0;
}

/*
 * A label less a label, where both stand before the statement, in one
 * section, with nothing of variable size between: how far apart they
 * are is known where the statement is read, and it is a number like any
 * other.  0 and the number (with whatever else the expression adds), or
 * -1 if the expression is not such a one.
 */
int fixed_label_diff(emit_ctx_t *ctx, const as_stmt_t *st, unsigned x86_code_bits, const as_expr_t *e,
                            long long *out) {
    as_expr_linear_t lin;
    const char *sec;
    uint64_t size = 0;
    size_t is, ia, ib, lo, hi, k;

    if (e == NULL || e->kind == AS_EXPR_CONST || first_symbol_op_in_expr(e) != NULL ||
        as_expr_eval_linear(e, NULL, NULL, &lin) != AS_EXPR_EVAL_OK || lin.add_symbol == NULL ||
        lin.sub_symbol == NULL || lin.add_local != NULL || lin.sub_local != NULL ||
        parsed_stmt_index(ctx, st, &is) != 0 || label_stmt_index(ctx, lin.add_symbol, &ib) != 0 ||
        label_stmt_index(ctx, lin.sub_symbol, &ia) != 0 || ia > is || ib > is || build_stmt_section_at(ctx) != 0 ||
        ia >= ctx->stmt_section_at_count || ib >= ctx->stmt_section_at_count) {
        return -1;
    }
    sec = ctx->stmt_section_at[ia];
    if (sec == NULL || ctx->stmt_section_at[ib] == NULL || as_strcmp(sec, ctx->stmt_section_at[ib]) != 0) {
        return -1;
    }
    lo = ia < ib ? ia : ib;
    hi = ia < ib ? ib : ia;
    for (k = lo; k < hi; ++k) {
        const as_stmt_t *between = &ctx->parsed->items[k];

        if (stmt_is_of_variable_size(between) &&
            (between->kind == AS_STMT_DIRECTIVE ||
             (ctx->stmt_section_at[k] != NULL && as_strcmp(ctx->stmt_section_at[k], sec) == 0))) {
            return -1;
        }
    }
    if (stmt_range_size_in_section(ctx, sec, lo, hi, 0, x86_code_bits, &size) != 0) {
        return -1;
    }
    *out = lin.value + (ib >= ia ? (long long)size : -(long long)size);
    return 0;
}

/*
 * The statement with each such difference in its operands made the
 * number it is: `st` itself if it has none, else `tmp`, which has `ops`
 * for its operands and `consts` for the numbers.
 *
 * `addl $b-a, %eax` was encoded as an instruction with a symbol in it,
 * in the long form whatever the distance -- 81 and four bytes where GNU
 * as has 83 and one -- and `ret $b-a`, `shl $b-a, %eax` and `bt $b-a,
 * %eax`, which have no field a symbol could go in, were refused.  A
 * label ahead of the statement, or a jump or an alignment between the
 * two, leaves it the long form, as it does GNU as's.
 */
static const as_stmt_t *stmt_fold_label_diffs(emit_ctx_t *ctx, const as_stmt_t *st, unsigned x86_code_bits,
                                              as_stmt_t *tmp, as_operand_t ops[4], as_expr_t consts[4]) {
    const as_instruction_t *in;
    int any = 0;
    size_t i;

    if (ctx == NULL || ctx->cfg == NULL || ctx->cfg->target != 0 || st == NULL || st->kind != AS_STMT_INSTRUCTION) {
        return st;
    }
    in = &st->u.instr;
    if (in->operand_count == 0 || in->operand_count > 4 || in->mnemonic == NULL || is_rel_mnemonic(in->mnemonic)) {
        return st;
    }
    for (i = 0; i < in->operand_count; ++i) {
        const as_operand_t *op = &in->operands[i];
        const as_expr_t *e = op->kind == AS_OPERAND_MEMORY                                         ? op->u.mem.disp
                             : (op->kind == AS_OPERAND_IMMEDIATE || op->kind == AS_OPERAND_LABEL_REF) ? op->u.expr
                                                                                                    : NULL;
        long long v;

        ops[i] = *op;
        if (e == NULL || fixed_label_diff(ctx, st, x86_code_bits, e, &v) != 0) {
            continue;
        }
        memset(&consts[i], 0, sizeof(consts[i]));
        consts[i].kind = AS_EXPR_CONST;
        consts[i].value = v;
        if (op->kind == AS_OPERAND_MEMORY) {
            ops[i].u.mem.disp = &consts[i];
        } else {
            ops[i].u.expr = &consts[i];
        }
        any = 1;
    }
    if (!any) {
        return st;
    }
    *tmp = *st;
    tmp->u.instr.operands = ops;
    return tmp;
}

int append_virtual_instruction_bytes(emit_ctx_t *ctx, bytebuf_t *buf, const char *section_name,
                                            const as_stmt_t *st, unsigned x86_code_bits) {
    unsigned char code[32];
    size_t code_len = 0;
    char encerr[256];
    as_elf_cfg_t stmt_cfg;

    if (ctx == NULL || ctx->cfg == NULL || buf == NULL || st == NULL) {
        return -1;
    }
    if (ctx->cfg->target != 0 && st->kind == AS_STMT_INSTRUCTION) {
        if (arm_encode_stmt(ctx, section_name, (uint64_t)buf->len, 0, x86_code_bits, st, code, sizeof(code), &code_len,
                            encerr, sizeof(encerr)) != 0) {
            return -1;
        }
        return bytebuf_append(buf, code, code_len);
    }
    /* See note in stmt_virtual_size_in_section: precise branch sizing recurses
     * through this function, so cap recursion at the top level. */
    if (st->kind == AS_STMT_INSTRUCTION &&
        st->u.instr.operand_count == 1 &&
        is_rel_mnemonic(st->u.instr.mnemonic) &&
        ctx->virtual_scanning == 0) {
        const as_operand_t *op = &st->u.instr.operands[0];
        const as_expr_t *e = op->u.expr;
        if ((ctx->virtual_scanning == 0 ||
             (ctx->virtual_scanning == 1 && local_temp_branch_target_within(ctx, st, e, 64))) &&
            (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
            e != NULL && e->kind == AS_EXPR_SYMBOL && expr_is_local_temp_symbol(e)) {
            as_elf_cfg_t local_cfg = *ctx->cfg;
            long long abs_target;
            int may_short = x86_branch_may_be_short(ctx, section_name, st, e, x86_code_bits);

            local_cfg.x86_code_bits = x86_code_bits;
            local_cfg.is_64 = x86_code_bits == 64u;
            if (eval_direct_local_branch_target(ctx, section_name, st, (uint64_t)buf->len, x86_code_bits, e,
                                                2, &abs_target) == 0 &&
                emit_resolved_x86_branch_ex(&local_cfg, st, (uint64_t)buf->len, abs_target, may_short, code, sizeof(code),
                                         &code_len, encerr, sizeof(encerr)) == 0) {
                if (code_len != 2 &&
                    eval_direct_local_branch_target(ctx, section_name, st, (uint64_t)buf->len, x86_code_bits, e,
                                                    code_len, &abs_target) == 0 &&
                    emit_resolved_x86_branch_ex(&local_cfg, st, (uint64_t)buf->len, abs_target, may_short, code, sizeof(code),
                                             &code_len, encerr, sizeof(encerr)) != 0) {
                    return -1;
                }
                return bytebuf_append(buf, code, code_len);
            }
        }
    }
    {
        unsigned local_rel_len = stmt_local_rel_virtual_len(ctx, section_name, st, x86_code_bits);
        if (local_rel_len != 0) {
            return bytebuf_append_zeros(buf, local_rel_len + x86_branch_hint(st));
        }
    }
    /* The mode is what .code16, .code32 or .code64 last said, whatever
     * the object is: is_64 here is the code's, and ctx->cfg's the
     * object's. */
    stmt_cfg = *ctx->cfg;
    stmt_cfg.x86_code_bits = x86_code_bits;
    stmt_cfg.is_64 = x86_code_bits == 64u;
    stmt_cfg.have_current_text_offset = 1u;
    stmt_cfg.current_text_offset = (uint64_t)buf->len;
    if (stmt_code_find(ctx, st, 0, x86_code_bits, 1, code, sizeof(code), &code_len)) {
        return bytebuf_append(buf, code, code_len);
    }
    {
        as_stmt_t folded;
        as_operand_t fold_ops[4];
        as_expr_t fold_consts[4];
        const as_stmt_t *as = stmt_fold_label_diffs(ctx, st, x86_code_bits, &folded, fold_ops, fold_consts);
        int rc = as == st ? encode_x86_stmt_by_text(ctx, &stmt_cfg, x86_code_bits, st, code, sizeof(code), &code_len,
                                                    encerr, sizeof(encerr))
                          : 1;

        if (rc < 0 ||
            (rc == 1 && encode_x86_stmt(ctx, &stmt_cfg, as, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0)) {
            return -1;
        }
    }
    if (stmt_code_is_anywhere(ctx, st)) {
        stmt_code_keep(ctx, st, (uint64_t)buf->len, x86_code_bits, code, code_len);
    }
    return bytebuf_append(buf, code, code_len);
}

/*
 * .uleb128 and .sleb128 of the distance between two labels.
 *
 * (Of numbers they are .byte by now: as_directives_lower.)  How many
 * bytes one takes depends on a distance, and the distance on how many
 * bytes this and its like take, if any lies between the labels -- as in
 * a compiler's exception table, whose length is such a number and whose
 * entries are too.  So the sizes are settled before anything is laid
 * out for good: settle_leb128_sizes() starts each at one byte and grows
 * it until none wants more.  Wherever the layout is being worked out the
 * directive is that many bytes and nothing is evaluated; where the bytes
 * are written for the object, the value is, and is padded to the size.
 */
static struct leb_stmt *find_leb_stmt(emit_ctx_t *ctx, const as_stmt_t *st) {
    size_t lo = 0;
    size_t hi = ctx != NULL ? ctx->leb_count : 0;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (ctx->leb[mid].st == st) {
            return &ctx->leb[mid];
        }
        if (ctx->leb[mid].st < st) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

/*
 * Where every statement stands in its section, as the layout now is:
 * one walk of the source for all of them.  (To ask it of each statement
 * is a walk of its section for each, and the rows of a line table and
 * the directives of call frame information are many: a compiler's
 * 2,400 lines of C++ with debugging information took 81 seconds.)
 * The offsets are the caller's to free; a statement's section is
 * ctx->stmt_section_at[] of it.  0, or -1.
 */
int layout_offsets(emit_ctx_t *ctx, uint64_t **off_out) {
    size_t count = ctx->parsed->count;
    uint64_t *off = (uint64_t *)malloc((count != 0 ? count : 1) * sizeof(*off));
    uint64_t *sizes = NULL;
    size_t size_count = 0;
    size_t size_cap = 0;
    as_strmap_t by_name;
    section_track_t track;
    size_t i;
    int rc = -1;

    memset(&by_name, 0, sizeof(by_name));
    if (off == NULL || build_stmt_section_at(ctx) != 0 ||
        section_track_init(&track, ctx->cfg->is_64 ? 64u : (ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        free(off);
        return -1;
    }
    ctx->virtual_scanning++;
    for (i = 0; i < count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        const char *section = track.current != NULL ? track.current : "";
        const char *in = i < ctx->stmt_section_at_count && ctx->stmt_section_at[i] != NULL ? ctx->stmt_section_at[i]
                                                                                           : "";
        size_t k;
        size_t n = 0;

        if (!as_strmap_get(&by_name, in, &k)) {
            if (size_count == size_cap) {
                size_t cap = size_cap != 0 ? size_cap * 2 : 16;
                uint64_t *grown = (uint64_t *)realloc(sizes, cap * sizeof(*grown));

                if (grown == NULL) {
                    goto out;
                }
                sizes = grown;
                size_cap = cap;
            }
            k = size_count++;
            sizes[k] = 0;
            if (as_strmap_put(&by_name, in, k) != 0) {
                goto out;
            }
        }
        off[i] = sizes[k];
        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc = section_track_apply_directive(&track, &st->u.directive);

            if (trc < 0) {
                goto out;
            }
            if (trc > 0) {
                continue;
            }
        } else if (st->kind != AS_STMT_INSTRUCTION) {
            continue;
        }
        (void)stmt_virtual_size_in_section(ctx, section, st, off[i], track.x86_code_bits, &n);
        sizes[k] += n;
    }
    *off_out = off;
    off = NULL;
    rc = 0;
out:
    ctx->virtual_scanning--;
    section_track_free(&track);
    as_strmap_free(&by_name);
    free(sizes);
    free(off);
    return rc;
}

/* The words of a directive, which may have come as one argument or as
 * several: the next one from *at, in word[0 .. word_sz).  0 at the end. */
int next_word(const as_directive_t *d, size_t *arg, const char **at, char *word, size_t word_sz) {
    for (;;) {
        const char *p = *at;
        size_t n = 0;

        if (p == NULL) {
            if (*arg >= d->arg_count) {
                return 0;
            }
            p = d->args[*arg] != NULL ? d->args[*arg] : "";
        }
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            (*arg)++;
            *at = NULL;
            continue;
        }
        if (*p == '"') {
            /* A string is one word, quotes and all. */
            word[n++] = *p++;
            while (*p != '\0' && *p != '"' && n + 2 < word_sz) {
                word[n++] = *p++;
            }
            if (*p == '"') {
                word[n++] = *p++;
            }
        } else {
            while (*p != '\0' && !isspace((unsigned char)*p) && n + 1 < word_sz) {
                word[n++] = *p++;
            }
        }
        word[n] = '\0';
        *at = p;
        return 1;
    }
}

/*
 * What a `.loc` says, into *r: the file and the line, a column, and
 * then by name what is so of the place.  `is_stmt` and `isa` stay as
 * they are set until set again -- *sticky_stmt, *sticky_isa -- and the
 * rest are of this row alone.  0, or -1 with *why.
 */
static int parse_loc(const as_directive_t *d, struct line_row *r, unsigned *sticky_stmt, unsigned *sticky_isa,
                     const char **why) {
    char word[512];
    size_t arg = 0;
    const char *at = NULL;
    unsigned long numbers[3] = {0, 0, 0};
    size_t have = 0;
    char *end;

    memset(r, 0, sizeof(*r));
    *why = "a file's number and a line";
    while (next_word(d, &arg, &at, word, sizeof(word))) {
        if (isdigit((unsigned char)word[0]) && have < 3) {
            numbers[have++] = strtoul(word, &end, 0);
            if (*end != '\0') {
                return -1;
            }
            continue;
        }
        if (have < 2) {
            return -1;
        }
        if (as_strcmp(word, "basic_block") == 0) {
            r->row.flags |= AS_DWLINE_BASIC_BLOCK;
        } else if (as_strcmp(word, "prologue_end") == 0) {
            r->row.flags |= AS_DWLINE_PROLOGUE_END;
        } else if (as_strcmp(word, "epilogue_begin") == 0) {
            r->row.flags |= AS_DWLINE_EPILOGUE_BEGIN;
        } else if (as_strcmp(word, "is_stmt") == 0 || as_strcmp(word, "isa") == 0 || as_strcmp(word, "discriminator") == 0) {
            char value[64];
            unsigned long n;

            *why = "a number after is_stmt, isa or discriminator";
            if (!next_word(d, &arg, &at, value, sizeof(value))) {
                return -1;
            }
            n = strtoul(value, &end, 0);
            if (*end != '\0' || (word[1] == 's' && word[2] == '_' && n > 1)) {
                return -1;
            }
            if (word[0] == 'd') {
                r->row.discriminator = (unsigned)n;
            } else if (word[2] == 'a') {
                *sticky_isa = (unsigned)n;
            } else {
                *sticky_stmt = (unsigned)n;
            }
        } else if (as_strcmp(word, "view") == 0) {
            *why = "a name after view";
            if (!next_word(d, &arg, &at, word, sizeof(word)) || strlen(word) >= sizeof(r->view)) {
                return -1;
            }
            memcpy(r->view, word, strlen(word) + 1);
            if (as_strcmp(word, "-0") == 0) {
                r->row.flags |= AS_DWLINE_VIEW_RESET;
            }
        } else {
            *why = "what is not a thing .loc says of a place";
            return -1;
        }
    }
    if (have < 2) {
        return -1;
    }
    r->row.file = (unsigned)numbers[0];
    r->row.line = (unsigned)numbers[1];
    r->row.column = (unsigned)numbers[2];
    r->row.isa = *sticky_isa;
    if (*sticky_stmt) {
        r->row.flags |= AS_DWLINE_IS_STMT;
    }
    return 0;
}

/*
 * The rows of the line table, as the layout now is.
 *
 * A `.loc` is a row where it takes effect, and that is where GNU as
 * has it: at the instruction after it, wherever padding or data has
 * put that, or -- two with nothing between -- at the `.loc` after it;
 * one with a `view` at once, where it stands; and one that nothing
 * follows is no row.  With `own_source` there are no `.loc` and each
 * instruction is a row, of its own line in the source (-g).
 */
static int line_rows_from(emit_ctx_t *ctx, const uint64_t *offsets);

int line_rows(emit_ctx_t *ctx) {
    uint64_t *offsets = NULL;
    int rc;

    if (ctx->line_rows_valid) {
        return 0;
    }
    /* (Laying the source out may ask after a view, which asks for
     * this: it is told there is none yet.) */
    if (ctx->line_rows_busy) {
        return -1;
    }
    ctx->line_rows_busy = 1;
    rc = layout_offsets(ctx, &offsets);
    ctx->line_rows_busy = 0;
    if (rc != 0) {
        set_err(ctx, "out of memory");
        return -1;
    }
    rc = line_rows_from(ctx, offsets);
    free(offsets);
    return rc;
}

static int line_rows_from(emit_ctx_t *ctx, const uint64_t *offsets) {
    struct line_row pending;
    int have_pending = 0;
    unsigned stmt = 1;
    unsigned isa = 0;
    size_t cap = 0;
    size_t i;

    ctx->line_row_count = 0;
    as_strmap_free(&ctx->line_views);
    memset(&ctx->line_views, 0, sizeof(ctx->line_views));
    cap = 0;
    free(ctx->line_rows);
    ctx->line_rows = NULL;
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        int is_loc = st->kind == AS_STMT_DIRECTIVE && st->u.directive.name != NULL &&
                     as_strcmp(st->u.directive.name, ".loc") == 0;
        struct line_row now;
        const char *why = "";
        const char *section = NULL;
        uint64_t off = 0;
        int emit = 0;

        if (!is_loc && !(st->kind == AS_STMT_INSTRUCTION && (have_pending || ctx->line_own))) {
            continue;
        }
        if (is_loc && parse_loc(&st->u.directive, &now, &stmt, &isa, &why) != 0) {
            set_err(ctx, "%s:%u: .loc: %s", st->file != NULL ? st->file : "<input>", st->line, why);
            return -1;
        }
        if (i >= ctx->stmt_section_at_count || ctx->stmt_section_at[i] == NULL) {
            continue;
        }
        section = ctx->stmt_section_at[i];
        off = offsets[i];
        if (!is_loc && ctx->line_own) {
            memset(&pending, 0, sizeof(pending));
            pending.row.file = 1;
            pending.row.line = st->line;
            pending.row.flags = AS_DWLINE_IS_STMT;
            have_pending = 1;
        }
        /* The row before takes effect here, if it has not yet. */
        if (have_pending) {
            pending.row.section = section;
            pending.row.offset = off;
            emit = 1;
        }
        for (;;) {
            if (emit) {
                if (ctx->line_row_count == cap) {
                    size_t ncap = cap != 0 ? cap * 2 : 256;
                    struct line_row *grown = (struct line_row *)realloc(ctx->line_rows, ncap * sizeof(*grown));

                    if (grown == NULL) {
                        set_err(ctx, "out of memory");
                        return -1;
                    }
                    ctx->line_rows = grown;
                    cap = ncap;
                }
                ctx->line_rows[ctx->line_row_count] = pending;
                ctx->line_row_count++;
                have_pending = 0;
                emit = 0;
            }
            if (!is_loc) {
                break;
            }
            /* And this one waits for what follows it, unless it has a
             * view, which is of where it stands. */
            pending = now;
            have_pending = 1;
            is_loc = 0;
            if (now.view[0] != '\0') {
                pending.row.section = section;
                pending.row.offset = off;
                emit = 1;
                continue;
            }
            break;
        }
    }
    /* The rows that have names, by them. */
    for (i = 0; i < ctx->line_row_count; ++i) {
        if (ctx->line_rows[i].view[0] != '\0' && as_strcmp(ctx->line_rows[i].view, "-0") != 0 &&
            as_strmap_put(&ctx->line_views, ctx->line_rows[i].view, i) != 0) {
            set_err(ctx, "out of memory");
            return -1;
        }
    }
    ctx->line_rows_valid = 1;
    return 0;
}

/* The names `.loc ... view NAME` gives, each with the statement that
 * gives it: found once. */
int line_view_names(emit_ctx_t *ctx) {
    size_t i;

    if (ctx->line_view_names_made) {
        return 0;
    }
    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        const as_directive_t *d = &st->u.directive;
        char word[512];
        size_t arg = 0;
        const char *at = NULL;

        if (st->kind != AS_STMT_DIRECTIVE || d->name == NULL || as_strcmp(d->name, ".loc") != 0) {
            continue;
        }
        while (next_word(d, &arg, &at, word, sizeof(word))) {
            if (as_strcmp(word, "view") == 0 && next_word(d, &arg, &at, word, sizeof(word)) &&
                as_strcmp(word, "-0") != 0) {
                char *kept = xstrdup(word);
                void *grown = kept != NULL
                                  ? realloc(ctx->line_view_name_list,
                                            (ctx->line_view_name_count + 1) * sizeof(ctx->line_view_name_list[0]))
                                  : NULL;

                if (grown == NULL) {
                    free(kept);
                    return -1;
                }
                ctx->line_view_name_list = (char **)grown;
                ctx->line_view_name_list[ctx->line_view_name_count++] = kept;
                if (as_strmap_put(&ctx->line_view_names, kept, i) != 0) {
                    return -1;
                }
            }
        }
    }
    ctx->line_view_names_made = 1;
    return 0;
}

/*
 * The number of a view that `.loc ... view NAME` named: how many rows
 * stand before it at the same address of the same section, with none
 * between at another.  0 and the number, or -1 if NAME is no view's.
 */
int line_view_value(emit_ctx_t *ctx, const char *name, long long *out) {
    size_t at = 0;
    size_t i;
    long long n = 0;

    while (isspace((unsigned char)*name)) {
        name++;
    }
    if (name[0] != '.' && !isalpha((unsigned char)name[0]) && name[0] != '_') {
        return -1;
    }
    /* (The names there are do not change, and most that are asked of
     * are none of them: the rows are worked out only for one that is.) */
    if (line_view_names(ctx) != 0 || !as_strmap_get(&ctx->line_view_names, name, &at)) {
        return -1;
    }
    if (line_rows(ctx) != 0 || !as_strmap_get(&ctx->line_views, name, &at)) {
        return -1;
    }
    for (i = at; i > 0; --i) {
        const struct line_row *before = &ctx->line_rows[i - 1];

        if (as_strcmp(before->row.section, ctx->line_rows[at].row.section) != 0) {
            continue;
        }
        if (before->row.offset != ctx->line_rows[at].row.offset) {
            break;
        }
        n++;
        /* (`view -0` is where the count begins again.) */
        if ((before->row.flags & AS_DWLINE_VIEW_RESET) != 0) {
            break;
        }
    }
    *out = n;
    return 0;
}

int eval_leb128_arg(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st, uint64_t sec_off,
                           unsigned x86_code_bits, const char *arg, long long *out) {
    as_expr_t *expr;
    int rc;

    if (parse_int64(arg, out) == 0 || parse_const_expr_string(arg, out) == 0) {
        return 0;
    }
    /* A view of the line table, by its name. */
    if (ctx != NULL && line_view_value(ctx, arg, out) == 0) {
        return 0;
    }
    expr = as_parse_expr_string(arg, st != NULL ? st->unit : NULL, st != NULL ? st->ordinal : 0);
    if (expr == NULL) {
        return -1;
    }
    /* Two labels of another section, or of this one. */
    rc = eval_abs_local_diff_expr_virtual(ctx, st, expr, out);
    if (rc != 0) {
        rc = eval_local_rel_expr_virtual(ctx, section_name, st, sec_off, x86_code_bits, expr, out);
    }
    as_expr_free(expr);
    return rc;
}

static int append_leb128_directive(emit_ctx_t *ctx, bytebuf_t *buf, const char *section_name, const as_stmt_t *st,
                                   uint64_t sec_off, unsigned x86_code_bits, const as_directive_t *d) {
    struct leb_stmt *leb = find_leb_stmt(ctx, st);
    int is_signed = d->name[1] == 's';
    uint64_t done = 0;
    size_t i;

    if (ctx == NULL || d->arg_count == 0) {
        return -1;
    }
    for (i = 0; i < d->arg_count; ++i) {
        unsigned char bytes[LEB128_MAX_BYTES];
        size_t want = leb != NULL ? leb->arg_bytes[i] : 1;
        size_t n;
        long long v = 0;

        if (ctx->virtual_scanning) {
            if (bytebuf_append_zeros(buf, want) != 0) {
                return -1;
            }
            continue;
        }
        /* `.` is where this argument's bytes begin. */
        if (d->args[i] == NULL ||
            eval_leb128_arg(ctx, section_name, st, sec_off + done, x86_code_bits, d->args[i], &v) != 0) {
            set_err(ctx, "%s:%u: %s: '%s' is neither a number nor the distance between two labels",
                    st != NULL && st->file != NULL ? st->file : "<input>", st != NULL ? st->line : 0, d->name,
                    d->args[i] != NULL ? d->args[i] : "");
            return -1;
        }
        n = as_leb128_encode(v, is_signed, bytes);
        if (n > want) {
            set_err(ctx, "%s:%u: %s: the size of '%s' did not settle",
                    st != NULL && st->file != NULL ? st->file : "<input>", st != NULL ? st->line : 0, d->name,
                    d->args[i]);
            return -1;
        }
        /* Padded: every byte but the last says that more follow, and
         * those added carry the sign and nothing else. */
        for (; n < want; ++n) {
            bytes[n - 1] |= 0x80;
            bytes[n] = is_signed && v < 0 ? 0x7f : 0x00;
        }
        if (bytebuf_append(buf, bytes, n) != 0) {
            return -1;
        }
        done += n;
    }
    return 1;
}

int append_directive_data_ctx(emit_ctx_t *ctx, bytebuf_t *buf, const char *section_name,
                                     const as_stmt_t *st, uint64_t sec_off, unsigned x86_code_bits,
                                     const as_directive_t *d) {
    size_t i;
    unsigned width = 0;

    (void)section_name;
    (void)st;
    (void)sec_off;
    (void)x86_code_bits;
    if (d == NULL || d->name == NULL) {
        return 0;
    }
    if (as_strcmp(d->name, ".uleb128") == 0 || as_strcmp(d->name, ".sleb128") == 0) {
        return append_leb128_directive(ctx, buf, section_name, st, sec_off, x86_code_bits, d);
    }
    if (ctx != NULL && ctx->virtual_scanning &&
        (as_strcmp(d->name, ".skip") == 0 || as_strcmp(d->name, ".space") == 0)) {
        long long tmp = 0;
        if (ctx->virtual_scanning > 1) {
            return 1;
        }
        if (d->arg_count >= 1 && d->args[0] != NULL &&
            parse_int64(d->args[0], &tmp) != 0 &&
            parse_const_expr_string(d->args[0], &tmp) != 0 &&
            eval_linux_alt_pad_expr(ctx, section_name, st, sec_off, x86_code_bits, d->args[0], &tmp) != 0) {
            return 1;
        }
    }
    if (as_strcmp(d->name, ".incbin") == 0) {
        return append_incbin_to_bytebuf(ctx, st, buf, d);
    }
    if (as_strcmp(d->name, ".org") == 0) {
        long long target = 0;
        as_expr_t *expr;

        if (d->arg_count < 1 || d->args[0] == NULL) {
            return -1;
        }
        if (parse_int64(d->args[0], &target) != 0) {
            expr = as_parse_expr_string(d->args[0], st != NULL ? st->unit : NULL,
                                        st != NULL ? st->ordinal : 0);
            if (expr == NULL) {
                return -1;
            }
            /* The distance from here, where `.` is here; it was measured
             * from the section's start, and `.org .+4` went to 4. */
            if (eval_local_rel_expr_virtual(ctx, section_name, st, sec_off, x86_code_bits, expr, &target) != 0) {
                as_expr_free(expr);
                return -1;
            }
            target += (long long)sec_off;
            as_expr_free(expr);
        }
        if (target < 0 || (uint64_t)target < sec_off) {
            set_err(ctx, "%s:%u: attempt to move .org backwards",
                    st != NULL && st->file != NULL ? st->file : "<input>", st != NULL ? st->line : 0);
            return -1;
        }
        if (d->arg_count >= 2 && d->args[1] != NULL && d->args[1][0] != '\0') {
            long long fill = 0;
            uint64_t n;

            /* `.org place, fill`: the byte the gap is filled with. */
            if (parse_int64(d->args[1], &fill) != 0 && parse_const_expr_string(d->args[1], &fill) != 0) {
                return -1;
            }
            for (n = sec_off; n < (uint64_t)target; ++n) {
                if (bytebuf_append_u64_le(buf, (uint64_t)fill, 1) != 0) {
                    return -1;
                }
            }
            return 1;
        }
        if ((uint64_t)target > sec_off &&
            bytebuf_append_zeros(buf, (size_t)((uint64_t)target - sec_off)) != 0) {
            return -1;
        }
        return 1;
    }
    if (as_strcmp(d->name, ".align") == 0 || as_strcmp(d->name, ".balign") == 0 || as_strcmp(d->name, ".p2align") == 0) {
        long long raw = 0;
        long long fill = section_name_has_exec_flag(ctx, section_name) ? 0x90 : 0;
        size_t align;
        size_t need;

        if (d->arg_count < 1 || d->args[0] == NULL ||
            (parse_int64(d->args[0], &raw) != 0 && parse_const_expr_string(d->args[0], &raw) != 0) ||
            raw < 0) {
            return -1;
        }
        if (d->arg_count >= 2 && d->args[1] != NULL && d->args[1][0] != '\0' &&
            (parse_int64(d->args[1], &fill) != 0 && parse_const_expr_string(d->args[1], &fill) != 0)) {
            return -1;
        }
        if (as_strcmp(d->name, ".p2align") == 0) {
            if (raw > 31) {
                return -1;
            }
            align = (size_t)1u << (unsigned)raw;
        } else {
            align = (size_t)raw;
        }
        if (align == 0 || (align & (align - 1)) != 0) {
            return -1;
        }
        /*
         * From where the directive is in its section, which is sec_off,
         * and not from the length of the buffer written to: when a
         * statement is being measured that buffer is a scratch one and
         * empty, so every alignment measured 0 bytes, and a branch across
         * one was aimed short by its padding -- into the padding.
         */
        need = (align - ((size_t)sec_off & (align - 1))) & (align - 1);
        switch (align_exceeds_max_skip(d, need)) {
        case 1: need = 0; break;
        case 0: break;
        default: return -1;
        }
        /* Where instructions go, padding that is NOPs -- none named, or
         * 0x90 named -- is the long NOPs GNU as pads with. */
        if (ctx != NULL && ctx->cfg != NULL && ctx->cfg->target != 0 && !directive_has_arg(d, 1) &&
            section_name_has_exec_flag(ctx, section_name)) {
            return append_arm_nops(buf, need, ctx->cfg->target, x86_code_bits) == 0 ? 1 : -1;
        }
        if (fill == 0x90 && !(d->fill_width > 1 && directive_has_arg(d, 1)) && ctx != NULL && ctx->cfg != NULL &&
            ctx->cfg->target == 0 && (ctx->cfg->machine == EM_386 || ctx->cfg->machine == EM_X86_64) &&
            section_name_has_exec_flag(ctx, section_name)) {
            /* Behind data GNU as writes one NOP of a byte, and pads
             * what is left. */
            if (ctx->pad_after_data && need != 0) {
                if (bytebuf_append_u64_le(buf, 0x90, 1) != 0) {
                    return -1;
                }
                need--;
            }
            return append_x86_nops(buf, need, 0, x86_code_bits, ctx->cfg->machine == EM_X86_64) == 0 ? 1 : -1;
        }
        if (append_align_fill(buf, d, need, (uint64_t)fill) != 0) {
            set_err(ctx, "%s:%u: alignment padding (%lu bytes) is not a multiple of %u",
                    st != NULL && st->file != NULL ? st->file : "<input>", st != NULL ? st->line : 0,
                    (unsigned long)need, d->fill_width);
            return -1;
        }
        return 1;
    }
    if (as_strcmp(d->name, ".fill") == 0) {
        long long repeat = 0;
        long long size = 1;
        long long value = 0;

        if (d->arg_count < 1 || d->args[0] == NULL) {
            return -1;
        }
        if (parse_int64(d->args[0], &repeat) != 0 && parse_const_expr_string(d->args[0], &repeat) != 0) {
            as_expr_t *expr = as_parse_expr_string(d->args[0], st != NULL ? st->unit : NULL,
                                                  st != NULL ? st->ordinal : 0);
            if (expr == NULL ||
                (eval_abs_local_diff_expr_virtual(ctx, st, expr, &repeat) != 0 &&
                 eval_local_rel_expr_virtual(ctx, section_name, st, sec_off, x86_code_bits, expr, &repeat) != 0 &&
                 eval_local_align_fill_expr(ctx, section_name, st, sec_off, x86_code_bits, d->args[0], &repeat) != 0)) {
                as_expr_free(expr);
                return -1;
            }
            as_expr_free(expr);
        }
        if (d->arg_count >= 2 &&
            ((parse_int64(d->args[1], &size) != 0 && parse_const_expr_string(d->args[1], &size) != 0) ||
             size <= 0 || size > 8)) {
            return -1;
        }
        if (d->arg_count >= 3 &&
            (parse_int64(d->args[2], &value) != 0 && parse_const_expr_string(d->args[2], &value) != 0)) {
            return -1;
        }
        if (repeat < 0 || repeat > AS_ELF_FILL_MAX / size) {
            return -1;
        }
        /* The value is of 32 bits, as GNU as has it, whatever the size
         * of what it fills. */
        if (size > 4) {
            value &= 0xffffffffll;
        }
        for (i = 0; i < (size_t)repeat; ++i) {
            if (bytebuf_append_u64_le(buf, (uint64_t)value, (unsigned)size) != 0) {
                return -1;
            }
        }
        return 1;
    }
    width = directive_fixed_scalar_width(d);
    /* Whoever wants only to know where things are wants no values: a
     * number of a width is that many bytes whatever it is. */
    if (width != 0 && ctx != NULL && ctx->data_sizes_only) {
        return bytebuf_append_zeros(buf, (size_t)width * d->arg_count) == 0 ? 1 : -1;
    }
    if (width != 0) {
        for (i = 0; i < d->arg_count; ++i) {
            long long v;
            char *sym = NULL;
            int64_t add = 0;
            as_expr_t *expr;

            /* (A number, or an expression of numbers and of variables:
             * one reading, where it was read for numbers alone and then
             * again with the variables, which give the same where there
             * are none.) */
            if (eval_linux_alt_len_expr(ctx, st, d->args[i], &v) == 0 ||
                (ctx != NULL ? eval_arg_asm_vars(ctx, st, d->args[i], &v)
                             : parse_int64(d->args[i], &v) == 0 ? 0 : parse_const_expr_string(d->args[i], &v)) == 0) {
                if (bytebuf_append_u64_le(buf, (uint64_t)v, width) != 0) {
                    return -1;
                }
                continue;
            }
            /* For x86 what is not a number as it stands is worked out
             * where the relocations are made (emit_data_field), which
             * writes the field: here it is its width and no more. */
            if (ctx != NULL && ctx->cfg != NULL &&
                (ctx->cfg->machine == EM_386 || ctx->cfg->machine == EM_X86_64)) {
                if (bytebuf_append_zeros(buf, width) != 0) {
                    return -1;
                }
                continue;
            }
            expr = as_parse_expr_string(d->args[i], st != NULL ? st->unit : NULL,
                                        st != NULL ? st->ordinal : 0);
            if (expr != NULL) {
                long long ev = 0;
                if (eval_abs_local_diff_expr_virtual(ctx, st, expr, &ev) == 0 ||
                    eval_local_rel_expr_virtual(ctx, section_name, st, sec_off + (uint64_t)(i * width),
                                                x86_code_bits, expr, &ev) == 0) {
                    as_expr_free(expr);
                    if (bytebuf_append_u64_le(buf, (uint64_t)ev, width) != 0) {
                        return -1;
                    }
                    continue;
                }
                as_expr_free(expr);
                expr = NULL;
            }
            if (parse_symbol_addend_arg(d->args[i], &sym, &add) == 0 && sym != NULL) {
                free(sym);
                if (bytebuf_append_zeros(buf, width) != 0) {
                    return -1;
                }
                continue;
            }
            free(sym);
            expr = as_parse_expr_string(d->args[i], st != NULL ? st->unit : NULL,
                                        st != NULL ? st->ordinal : 0);
            if (expr != NULL && expr_has_symbol(expr)) {
                as_expr_free(expr);
                if (bytebuf_append_zeros(buf, width) != 0) {
                    return -1;
                }
                continue;
            }
            as_expr_free(expr);
            return -1;
        }
        return 1;
    }
    if (as_strcmp(d->name, ".zero") == 0 || as_strcmp(d->name, ".space") == 0 || as_strcmp(d->name, ".skip") == 0) {
        long long count = 0;
        long long fill = 0;
        size_t j;
        if (d->arg_count < 1 || d->args[0] == NULL) {
            return -1;
        }
        if (eval_linux_alt_pad_expr(ctx, section_name, st, sec_off, x86_code_bits, d->args[0], &count) == 0) {
            /* Linux alternative macros use label-difference expressions that must be
             * evaluated before generic constant parsing can treat comparisons as
             * ordinary arithmetic. */
        } else if (parse_int64(d->args[0], &count) != 0 && parse_const_expr_string(d->args[0], &count) != 0) {
            as_expr_t *expr = as_parse_expr_string(d->args[0], st != NULL ? st->unit : NULL,
                                                  st != NULL ? st->ordinal : 0);
            if (expr == NULL ||
                (eval_abs_local_diff_expr_virtual(ctx, st, expr, &count) != 0 &&
                 eval_local_rel_expr_virtual(ctx, section_name, st, sec_off, x86_code_bits, expr, &count) != 0)) {
                /* While places are still being found a count may not be
                 * known yet; once they are, one that is no number was
                 * taken for none, and is refused as that of .zero is. */
                if ((as_strcmp(d->name, ".space") == 0 || as_strcmp(d->name, ".skip") == 0) && ctx != NULL &&
                    ctx->virtual_scanning) {
                    as_expr_free(expr);
                    expr = NULL;
                    count = 0;
                } else {
                    set_err(ctx, "%s:%u: cannot evaluate %s count expression '%s'",
                            st != NULL && st->file != NULL ? st->file : "<input>",
                            st != NULL ? st->line : 0,
                            d->name,
                            d->args[0]);
                    as_expr_free(expr);
                    return -1;
                }
            }
            as_expr_free(expr);
        }
        if (count < 0 && (as_strcmp(d->name, ".space") == 0 || as_strcmp(d->name, ".skip") == 0)) {
            count = 0;
        }
        if (count < 0) {
            return -1;
        }
        if (count > AS_ELF_FILL_MAX) {
            set_err(ctx, "%s:%u: %s count is unreasonably large: %lld",
                    st != NULL && st->file != NULL ? st->file : "<input>",
                    st != NULL ? st->line : 0,
                    d->name,
                    count);
            return -1;
        }
        if ((as_strcmp(d->name, ".space") == 0 || as_strcmp(d->name, ".skip") == 0) && d->arg_count >= 2) {
            if (parse_int64(d->args[1], &fill) != 0 && parse_const_expr_string(d->args[1], &fill) != 0) {
                return -1;
            }
            for (j = 0; j < (size_t)count; ++j) {
                if (bytebuf_append_u64_le(buf, (uint64_t)fill, 1) != 0) {
                    return -1;
                }
            }
            return 1;
        }
        if (bytebuf_append_zeros(buf, (size_t)count) != 0) {
            return -1;
        }
        return 1;
    }
    return append_string_directive(buf, d);
}

static int encode_x86_stmt_at(emit_ctx_t *ctx, const char *section_name, uint64_t sec_off, unsigned x86_code_bits,
                              const as_stmt_t *st, unsigned char *code, size_t code_cap, size_t *code_len,
                              char *encerr, size_t encerr_sz);

/* Is there no name in the expression: nothing but numbers? */
static int expr_is_numbers(const as_expr_t *e) {
    if (e == NULL) {
        return 1;
    }
    if (e->kind == AS_EXPR_CONST) {
        return 1;
    }
    if (e->kind == AS_EXPR_SYMBOL || e->kind == AS_EXPR_LOCAL_REF) {
        return 0;
    }
    return expr_is_numbers(e->lhs) && expr_is_numbers(e->rhs);
}

/*
 * An instruction of registers and numbers is the same bytes wherever
 * it stands and however the rest is laid out; one with a name in it, or
 * that goes somewhere, is not.
 */
int stmt_is_place_free(const as_stmt_t *st) {
    size_t i;

    if (st->u.instr.mnemonic == NULL || is_rel_mnemonic(st->u.instr.mnemonic)) {
        return 0;
    }
    for (i = 0; i < st->u.instr.operand_count; ++i) {
        const as_operand_t *op = &st->u.instr.operands[i];

        if (op->kind == AS_OPERAND_REGISTER) {
            continue;
        }
        if (op->kind == AS_OPERAND_IMMEDIATE) {
            if (!expr_is_numbers(op->u.expr)) {
                return 0;
            }
        } else if (op->kind == AS_OPERAND_MEMORY) {
            if (!expr_is_numbers(op->u.mem.disp) || x86_base_is_ip(op->u.mem.base_reg)) {
                return 0;
            }
        } else {
            return 0;
        }
    }
    return 1;
}

/* Is every name in the expression one the source does not define: a
 * symbol of another object, whose place is the linker's to put in? */
static int expr_names_are_external(emit_ctx_t *ctx, const as_expr_t *e) {
    if (e == NULL || e->kind == AS_EXPR_CONST) {
        return 1;
    }
    if (e->kind == AS_EXPR_LOCAL_REF) {
        return 0;
    }
    if (e->kind == AS_EXPR_SYMBOL) {
        const as_symbol_t *sym;
        long long value;

        if (e->symbol == NULL || as_strcmp(e->symbol, ".") == 0 || ctx->symtab == NULL ||
            asm_var_lookup(ctx, e->symbol, &value) == 0) {
            return 0;
        }
        sym = find_as_symbol_const(ctx, ctx->symtab, e->symbol);
        return sym != NULL && !sym->defined && !sym->is_absolute && sym->alias_target == NULL;
    }
    return expr_names_are_external(ctx, e->lhs) && expr_names_are_external(ctx, e->rhs);
}

/*
 * Are a statement's bytes the same wherever it stands in this object:
 * one of registers and numbers, or one whose names are all of symbols
 * defined elsewhere -- what is written for those is room for the linker
 * and nothing of where the statement is.  (Such a statement was worked
 * out once to see that it could be, and again to be written.)  Not one
 * that goes somewhere, nor one that reaches from the instruction
 * pointer.
 */
static int stmt_code_is_anywhere(emit_ctx_t *ctx, const as_stmt_t *st) {
    size_t i;

    if (stmt_is_place_free(st)) {
        return 1;
    }
    if (ctx == NULL || st->u.instr.mnemonic == NULL || is_rel_mnemonic(st->u.instr.mnemonic)) {
        return 0;
    }
    for (i = 0; i < st->u.instr.operand_count; ++i) {
        const as_operand_t *op = &st->u.instr.operands[i];

        if (op->kind == AS_OPERAND_REGISTER) {
            continue;
        }
        /* (A name alone is memory at that name: `movl ext, %eax`.) */
        if (op->kind == AS_OPERAND_IMMEDIATE || op->kind == AS_OPERAND_LABEL_REF) {
            if (!expr_names_are_external(ctx, op->u.expr)) {
                return 0;
            }
        } else if (op->kind == AS_OPERAND_MEMORY) {
            if (x86_base_is_ip(op->u.mem.base_reg) || !expr_names_are_external(ctx, op->u.mem.disp)) {
                return 0;
            }
        } else {
            return 0;
        }
    }
    return 1;
}

static unsigned char fields_keep(const as_stmt_t *st, struct kept_field *out);
static void fields_put(const as_stmt_t *st, const struct kept_field *in, unsigned char n);

static struct stmt_code *stmt_code_slot(emit_ctx_t *ctx, const as_stmt_t *st) {
    static int off = -1;
    size_t idx = 0;

    if (off < 0) {
        off = getenv("AS_NO_STMT_CODE") != NULL;
    }
    if (off || ctx == NULL || ctx->cfg == NULL || ctx->cfg->target != 0 || st == NULL ||
        st->kind != AS_STMT_INSTRUCTION || parsed_stmt_index(ctx, st, &idx) != 0) {
        return NULL;
    }
    if (ctx->stmt_code == NULL) {
        ctx->stmt_code = (struct stmt_code *)calloc(ctx->parsed->count, sizeof(*ctx->stmt_code));
        ctx->stmt_code_count = ctx->stmt_code != NULL ? ctx->parsed->count : 0;
    }
    return idx < ctx->stmt_code_count ? &ctx->stmt_code[idx] : NULL;
}

/* The statement's bytes, if they are kept for here (or, `anywhere`
 * asked, for anywhere): 1, and the bytes. */
static int stmt_code_find(emit_ctx_t *ctx, const as_stmt_t *st, uint64_t sec_off, unsigned x86_code_bits, int anywhere,
                          unsigned char *code, size_t code_cap, size_t *code_len) {
    const struct stmt_code *kept = stmt_code_slot(ctx, st);

    if (kept == NULL || kept->len == 0 || kept->bits != (unsigned char)x86_code_bits || kept->len > code_cap) {
        return 0;
    }
    if (!kept->anywhere && (anywhere || kept->off != sec_off || kept->epoch != ctx->layout_epoch)) {
        return 0;
    }
    memcpy(code, kept->bytes, kept->len);
    *code_len = kept->len;
    fields_put(st, kept->fields, kept->nfields);
    return 1;
}

/*
 * The key of a statement in the table of texts: every field of it that
 * an encoder reads, with the code's size before them.  0 if it has no
 * key -- it is not of registers and numbers, an operand has no text, a
 * register has been given another name (which a later statement may
 * give again), or the key is too long to be worth keeping.
 */
/* What a key is put together of: a string, and a number.  Each is ended
 * by a byte no name has, so that no two keys of different things are
 * the same.  0, or -1 where the key has no room for it. */
static int key_str(char *key, size_t key_sz, size_t *n, const char *s) {
    size_t len = strlen(s);

    if (*n + len + 2 > key_sz) {
        return -1;
    }
    memcpy(key + *n, s, len);
    *n += len;
    key[(*n)++] = '\x1f';
    return 0;
}

static int key_num(char *key, size_t key_sz, size_t *n, unsigned long long v) {
    if (*n + 18 > key_sz) {
        return -1;
    }
    do {
        key[(*n)++] = "0123456789abcdef"[v & 15u];
        v >>= 4;
    } while (v != 0);
    key[(*n)++] = '\x1e';
    return 0;
}

size_t text_code_key(emit_ctx_t *ctx, const as_stmt_t *st, unsigned x86_code_bits, char *key, size_t key_sz) {
    const as_instruction_t *in = &st->u.instr;
    size_t n;
    size_t i;
    int ok;

    /* (One that is its bytes already has no operands to be the key.) */
    if ((ctx != NULL && ctx->asm_reg_alias_count != 0) || in->code_len != 0 || !stmt_is_place_free(st)) {
        return 0;
    }
    /* (Put together by hand: it is made for every instruction of
     * registers and numbers that is read, and snprintf() was a fifth of
     * the time such a source took.) */
    n = 0;
    ok = key_num(key, key_sz, &n, x86_code_bits) == 0 && key_num(key, key_sz, &n, in->prefixes) == 0 &&
         key_num(key, key_sz, &n, in->pseudo) == 0 && key_num(key, key_sz, &n, in->rex_bits) == 0 &&
         key_num(key, key_sz, &n, in->opmask) == 0 &&
         key_num(key, key_sz, &n,
                 (unsigned long long)in->have_opmask | ((unsigned long long)in->zeroing << 1) |
                     ((unsigned long long)in->sae << 2) | ((unsigned long long)in->syntax_intel << 3) |
                         ((unsigned long long)in->from_intel << 4) | ((unsigned long long)in->have_dfv << 5) |
                         ((unsigned long long)in->dfv << 6)) == 0 &&
         key_num(key, key_sz, &n, in->broadcast) == 0 &&
         key_num(key, key_sz, &n, (unsigned long long)(long long)in->rounding_mode) == 0 &&
         key_str(key, key_sz, &n, in->segment_override != NULL ? in->segment_override : "-") == 0 &&
         key_str(key, key_sz, &n, in->mnemonic) == 0;
    /* What each operand is, not how it was written: a name that is a
     * number is written the same when the number has been set again. */
    for (i = 0; ok && i < in->operand_count; ++i) {
        const as_operand_t *op = &in->operands[i];
        char mark[3];
        long long v = 0;

        mark[0] = '\t';
        mark[1] = op->raw != NULL && (op->raw[0] == '$' || op->raw[0] == '*') ? op->raw[0] : '.';
        mark[2] = '\0';
        ok = key_str(key, key_sz, &n, mark) == 0;
        if (op->kind == AS_OPERAND_REGISTER) {
            ok = ok && key_str(key, key_sz, &n, "%") == 0 && key_str(key, key_sz, &n, op->u.reg) == 0;
        } else if (op->kind == AS_OPERAND_IMMEDIATE) {
            if (eval_expr_const(op->u.expr, &v) != 0) {
                return 0;
            }
            ok = ok && key_str(key, key_sz, &n, "$") == 0 && key_num(key, key_sz, &n, (unsigned long long)v) == 0;
        } else {
            const as_mem_operand_t *m = &op->u.mem;

            if (m->disp != NULL && eval_expr_const(m->disp, &v) != 0) {
                return 0;
            }
            ok = ok && key_str(key, key_sz, &n, m->segment_reg != NULL ? m->segment_reg : "") == 0 &&
                 key_num(key, key_sz, &n, m->disp != NULL) == 0 && key_num(key, key_sz, &n, (unsigned long long)v) == 0 &&
                 key_str(key, key_sz, &n, m->base_reg != NULL ? m->base_reg : "") == 0 &&
                 key_str(key, key_sz, &n, m->index_reg != NULL ? m->index_reg : "") == 0 &&
                 key_num(key, key_sz, &n, (unsigned long long)m->scale) == 0 &&
                 key_num(key, key_sz, &n, (unsigned long long)m->size_bits) == 0;
        }
    }
    if (!ok || n >= key_sz) {
        return 0;
    }
    key[n] = '\0';
    return n;
}

static uint32_t text_code_hash(const char *key, size_t n) {
    uint32_t h = 2166136261u;
    size_t i;

    for (i = 0; i < n; ++i) {
        h = (h ^ (unsigned char)key[i]) * 16777619u;
    }
    return h;
}

/* The expression an operand was written with, which the emitter's
 * fields are told by. */
static const void *operand_expr(const as_operand_t *op) {
    return op->kind == AS_OPERAND_MEMORY                                       ? (const void *)op->u.mem.disp
           : op->kind == AS_OPERAND_IMMEDIATE || op->kind == AS_OPERAND_LABEL_REF ? (const void *)op->u.expr
                                                                                : NULL;
}

/* The fields the emitter last said, kept by which of the statement's
 * operands each holds (one of none, a number folded in, is not kept). */
static unsigned char fields_keep(const as_stmt_t *st, struct kept_field *out) {
    const as_x86_fields_t *f = x86_stmt_fields();
    unsigned char n = 0;
    unsigned k;
    size_t i;

    for (k = 0; st != NULL && st->kind == AS_STMT_INSTRUCTION && k < f->n; ++k) {
        for (i = 0; i < st->u.instr.operand_count; ++i) {
            if (f->f[k].expr != NULL && operand_expr(&st->u.instr.operands[i]) == f->f[k].expr) {
                out[n].op = (unsigned char)i;
                out[n].off = f->f[k].off;
                out[n].width = f->f[k].width;
                out[n].kind = f->f[k].kind;
                out[n].flags = f->f[k].flags;
                n++;
                break;
            }
        }
    }
    return n;
}

/* And given back, as if the bytes had been encoded again. */
static void fields_put(const as_stmt_t *st, const struct kept_field *in, unsigned char n) {
    as_x86_fields_t f;
    unsigned k;

    f.n = 0;
    for (k = 0; st != NULL && st->kind == AS_STMT_INSTRUCTION && k < n; ++k) {
        if (in[k].op < st->u.instr.operand_count) {
            f.f[f.n].expr = operand_expr(&st->u.instr.operands[in[k].op]);
            f.f[f.n].off = in[k].off;
            f.f[f.n].width = in[k].width;
            f.f[f.n].kind = in[k].kind;
            f.f[f.n].flags = in[k].flags;
            f.n++;
        }
    }
    x86_stmt_fields_put(&f);
}

int text_code_find(const char *key, size_t n, unsigned char *code, size_t code_cap, size_t *code_len,
                   const as_stmt_t *st) {
    uint32_t h = text_code_hash(key, n);
    size_t at;

    if (text_codes.cap == 0) {
        return 0;
    }
    for (at = h & (text_codes.cap - 1); text_codes.slots[at].key != NULL; at = (at + 1) & (text_codes.cap - 1)) {
        const struct text_code *t = &text_codes.slots[at];

        if (t->hash == h && as_strcmp(t->key, key) == 0) {
            if (t->len > code_cap) {
                return 0;
            }
            memcpy(code, t->bytes, t->len);
            *code_len = t->len;
            fields_put(st, t->fields, t->nfields);
            return 1;
        }
    }
    return 0;
}

/* (For want of memory the statement is not kept, and is encoded again
 * when it is written again.) */
void text_code_keep(const char *key, size_t n, const unsigned char *code, size_t code_len, const as_stmt_t *st) {
    uint32_t h = text_code_hash(key, n);
    struct text_code *t;
    size_t at;

    if (code_len == 0 || code_len > sizeof(t->bytes)) {
        return;
    }
    if ((text_codes.count + 1) * 2 > text_codes.cap) {
        size_t ncap = text_codes.cap == 0 ? 1024 : text_codes.cap * 2;
        struct text_code *next = (struct text_code *)calloc(ncap, sizeof(*next));
        size_t i;

        if (next == NULL) {
            return;
        }
        for (i = 0; i < text_codes.cap; ++i) {
            if (text_codes.slots[i].key != NULL) {
                for (at = text_codes.slots[i].hash & (ncap - 1); next[at].key != NULL; at = (at + 1) & (ncap - 1)) {
                }
                next[at] = text_codes.slots[i];
            }
        }
        free(text_codes.slots);
        text_codes.slots = next;
        text_codes.cap = ncap;
    }
    for (at = h & (text_codes.cap - 1); text_codes.slots[at].key != NULL; at = (at + 1) & (text_codes.cap - 1)) {
    }
    t = &text_codes.slots[at];
    t->key = xstrdup(key);
    if (t->key == NULL) {
        return;
    }
    t->hash = h;
    t->len = (unsigned char)code_len;
    memcpy(t->bytes, code, code_len);
    t->nfields = fields_keep(st, t->fields);
    text_codes.count++;
}

void text_code_free(void) {
    size_t i;

    for (i = 0; i < text_codes.cap; ++i) {
        free(text_codes.slots[i].key);
    }
    free(text_codes.slots);
    text_codes.slots = NULL;
    text_codes.cap = 0;
    text_codes.count = 0;
}

/*
 * An instruction of registers and numbers, encoded -- once for each way
 * it is written.  1 if the statement is not one (and nothing is done),
 * else what encode_x86_stmt() returns.
 */
static int encode_x86_stmt_by_text(emit_ctx_t *ctx, const as_elf_cfg_t *cfg, unsigned x86_code_bits,
                                   const as_stmt_t *st, unsigned char *code, size_t code_cap, size_t *code_len,
                                   char *encerr, size_t encerr_sz) {
    char key[192];
    unsigned asked = cfg != NULL && cfg->warnings != NULL ? cfg->warnings->asked : 0;
    size_t n;
    int rc;

    if (stmt_code_slot(ctx, st) == NULL) {
        return 1;
    }
    n = text_code_key(ctx, st, x86_code_bits, key, sizeof(key));
    if (n == 0) {
        return 1;
    }
    if (text_code_find(key, n, code, code_cap, code_len, st)) {
        return 0;
    }
    rc = encode_x86_stmt(ctx, cfg, st, code, code_cap, code_len, encerr, encerr_sz);
    /* (One that has a warning to make is not kept: the next that is
     * written the same has the warning to make too.) */
    if (rc == 0 && (cfg == NULL || cfg->warnings == NULL || cfg->warnings->asked == asked)) {
        text_code_keep(key, n, code, *code_len, st);
    }
    return rc;
}

static void stmt_code_keep(emit_ctx_t *ctx, const as_stmt_t *st, uint64_t sec_off, unsigned x86_code_bits,
                           const unsigned char *code, size_t code_len) {
    struct stmt_code *kept = stmt_code_slot(ctx, st);

    if (kept == NULL || code_len == 0 || code_len > sizeof(kept->bytes)) {
        return;
    }
    kept->off = sec_off;
    kept->bits = (unsigned char)x86_code_bits;
    kept->epoch = ctx->layout_epoch;
    kept->anywhere = (unsigned char)stmt_code_is_anywhere(ctx, st);
    kept->len = (unsigned char)code_len;
    memcpy(kept->bytes, code, code_len);
    kept->nfields = fields_keep(st, kept->fields);
}

/*
 * A statement's bytes where it stands.  They are worked out once for a
 * layout and kept (struct stmt_code); AS_NO_STMT_CODE in the
 * environment has them worked out every time, for a test to hold the
 * two to the same object.
 */
int encode_x86_stmt_for_layout(emit_ctx_t *ctx, const char *section_name, uint64_t sec_off, unsigned x86_code_bits,
                                      const as_stmt_t *st, unsigned char *code, size_t code_cap,
                                      size_t *code_len, char *encerr, size_t encerr_sz) {
    int rc;

    if (code != NULL && code_len != NULL && stmt_code_find(ctx, st, sec_off, x86_code_bits, 0, code, code_cap, code_len)) {
        return 0;
    }
    rc = encode_x86_stmt_at(ctx, section_name, sec_off, x86_code_bits, st, code, code_cap, code_len, encerr, encerr_sz);
    if (rc == 0 && code != NULL && code_len != NULL) {
        stmt_code_keep(ctx, st, sec_off, x86_code_bits, code, *code_len);
    }
    return rc;
}

static int encode_x86_stmt_at(emit_ctx_t *ctx, const char *section_name, uint64_t sec_off, unsigned x86_code_bits,
                              const as_stmt_t *st, unsigned char *code, size_t code_cap, size_t *code_len,
                              char *encerr, size_t encerr_sz) {
    as_elf_cfg_t stmt_cfg;
    as_stmt_t folded;
    as_operand_t fold_ops[4];
    as_expr_t fold_consts[4];

    if (ctx == NULL || ctx->cfg == NULL || st == NULL) {
        return -1;
    }
    if (ctx->cfg->target != 0 && st->kind == AS_STMT_INSTRUCTION) {
        return arm_encode_stmt(ctx, section_name, sec_off, 1, x86_code_bits, st, code, code_cap, code_len, encerr,
                               encerr_sz);
    }
    st = stmt_fold_label_diffs(ctx, st, x86_code_bits, &folded, fold_ops, fold_consts);
    stmt_cfg = *ctx->cfg;
    stmt_cfg.x86_code_bits = x86_code_bits;
    stmt_cfg.is_64 = x86_code_bits == 64u;
    stmt_cfg.have_current_text_offset = 1u;
    stmt_cfg.current_text_offset = sec_off;

    if ((stmt_cfg.machine == EM_386 || stmt_cfg.machine == EM_X86_64) &&
        st->kind == AS_STMT_INSTRUCTION &&
        st->u.instr.operand_count == 1 &&
        is_rel_mnemonic(st->u.instr.mnemonic)) {
        const as_operand_t *op = &st->u.instr.operands[0];
        as_expr_t *raw_local_expr = NULL;
        const as_expr_t *rel_expr = NULL;
        long long disp;
        /* A local label of another section is no nearer than a symbol
         * of another file: the long form, and a relocation. */
        int far_label = st->u.instr.target_in_other_section && !is_fixed_short_rel_mnemonic(st->u.instr.mnemonic);

        if ((op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
            op->u.expr != NULL &&
            op->kind == AS_OPERAND_LABEL_REF &&
            !is_fixed_short_rel_mnemonic(st->u.instr.mnemonic) &&
            (far_label || (!expr_has_local_ref(op->u.expr) &&
                           !expr_is_local_temp_symbol(op->u.expr) &&
                           !raw_is_numeric_local_ref(op->raw)))) {
            stmt_cfg.have_current_text_offset = 0u;
            stmt_cfg.current_text_offset = 0u;
        }
        if (far_label) {
            goto unresolved_rel_target;
        }
        /* `jmp .`, `jmp .+2`, `jmp label+1`: a place and a number, which
         * is as far as it is and needs no relocation. */
        {
            long long abs_target;
            int is_short;

            if (relaxed_place_branch_target(ctx, st, section_name, sec_off, x86_code_bits, &abs_target, &is_short) ==
                0) {
                as_stmt_t tmp = *st;
                as_instruction_t tin = st->u.instr;
                as_operand_t tops[3];
                as_expr_t texpr;
                as_elf_cfg_t local_cfg = stmt_cfg;

                memcpy(tops, st->u.instr.operands, st->u.instr.operand_count * sizeof(*tops));
                memset(&texpr, 0, sizeof(texpr));
                texpr.kind = AS_EXPR_CONST;
                texpr.value = abs_target;
                tops[0].kind = AS_OPERAND_LABEL_REF;
                tops[0].u.expr = &texpr;
                tin.operands = tops;
                tmp.u.instr = tin;
                local_cfg.x86_rel_is_disp = 0u;
                local_cfg.have_current_text_offset = 1u;
                local_cfg.current_text_offset = sec_off;
                {
                    int rc = emit_resolved_x86_branch_ex(&local_cfg, &tmp, sec_off, abs_target, is_short, code,
                                                         code_cap, code_len, encerr, encerr_sz);

                    /* (The place stood in for what was written.) */
                    x86_stmt_fields_rename(&texpr, st->u.instr.operands[0].u.expr);
                    return rc;
                }
            }
            if (call_to_place_target(st, sec_off, &abs_target) == 0) {
                return emit_resolved_x86_branch_ex(&stmt_cfg, st, sec_off, abs_target, 0, code, code_cap, code_len,
                                                   encerr, encerr_sz);
            }
        }

        if (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) {
            rel_expr = op->u.expr;
            if ((rel_expr == NULL || (!expr_has_local_ref(rel_expr) && !expr_is_local_temp_symbol(rel_expr))) &&
                raw_is_numeric_local_ref(op->raw)) {
                raw_local_expr = as_parse_expr_string(op->raw, st->unit, st->ordinal);
                if (raw_local_expr != NULL) {
                    rel_expr = raw_local_expr;
                }
            }
        }
        if ((op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) &&
            rel_expr != NULL &&
            (expr_has_local_ref(rel_expr) || expr_is_local_temp_symbol(rel_expr) ||
             is_fixed_short_rel_mnemonic(st->u.instr.mnemonic))) {
            as_stmt_t tmp = *st;
            as_instruction_t tin = st->u.instr;
            as_operand_t tops[3];
            as_expr_t texpr;
            as_elf_cfg_t local_cfg = stmt_cfg;
            long long abs_target;
            int may_short = x86_branch_may_be_short(ctx, section_name, st, rel_expr, x86_code_bits);
            virtual_addr_value_t target;

            if ((rel_expr->kind == AS_EXPR_LOCAL_REF || expr_is_local_temp_symbol(rel_expr)) &&
                eval_direct_local_branch_target(ctx, section_name, st, sec_off, x86_code_bits, rel_expr,
                                                2, &abs_target) == 0) {
                size_t tmp_len = 0;
                if (emit_resolved_x86_branch_ex(&local_cfg, st, sec_off, abs_target, may_short, code, code_cap,
                                             &tmp_len, encerr, encerr_sz) != 0) {
                    as_expr_free(raw_local_expr);
                    return -1;
                }
                if (tmp_len != 2 &&
                    eval_direct_local_branch_target(ctx, section_name, st, sec_off, x86_code_bits, rel_expr,
                                                    tmp_len, &abs_target) != 0) {
                    as_expr_free(raw_local_expr);
                    return -1;
                }
            } else if (eval_local_rel_expr_virtual(ctx, section_name, st, sec_off, x86_code_bits, rel_expr, &disp) == 0) {
                abs_target = (long long)sec_off + disp;
            } else if ((expr_has_local_ref(rel_expr) || expr_is_local_temp_symbol(rel_expr)) &&
                       eval_virtual_addr_expr(ctx, st, rel_expr, &target) == 0 &&
                       target.has_section &&
                       as_strcmp(target.section, section_name) == 0) {
                abs_target = target.value;
            } else {
                as_expr_free(raw_local_expr);
                goto unresolved_rel_target;
            }

            memcpy(tops, st->u.instr.operands, st->u.instr.operand_count * sizeof(*tops));
            memset(&texpr, 0, sizeof(texpr));
            texpr.kind = AS_EXPR_CONST;
            texpr.value = abs_target;
            tops[0].kind = AS_OPERAND_LABEL_REF;
            tops[0].u.expr = &texpr;
            tin.operands = tops;
            tmp.u.instr = tin;
            local_cfg.x86_rel_is_disp = 0u;
            local_cfg.have_current_text_offset = 1u;
            local_cfg.current_text_offset = sec_off;
            {
                int rc = emit_resolved_x86_branch_ex(&local_cfg, &tmp, sec_off, abs_target, may_short, code, code_cap,
                                                  code_len, encerr, encerr_sz);

                x86_stmt_fields_rename(&texpr, st->u.instr.operands[0].u.expr);
                as_expr_free(raw_local_expr);
                return rc;
            }
        }
        as_expr_free(raw_local_expr);
unresolved_rel_target:
        ;
    }
    if (stmt_cfg.machine == EM_X86_64 &&
        st->kind == AS_STMT_INSTRUCTION &&
        st->u.instr.operand_count > 0) {
        size_t oi;

        for (oi = 0; oi < st->u.instr.operand_count; ++oi) {
            const as_operand_t *op = &st->u.instr.operands[oi];
            long long rel;

            if (op->kind != AS_OPERAND_MEMORY ||
                op->u.mem.base_reg == NULL ||
                !x86_base_is_ip(op->u.mem.base_reg) ||
                op->u.mem.disp == NULL ||
                !expr_has_local_ref(op->u.mem.disp) ||
                eval_local_rel_expr_virtual(ctx, section_name, NULL, 0,
                                            x86_code_bits, op->u.mem.disp, &rel) != 0) {
                continue;
            }
            {
                size_t probe_len = 0;
                as_stmt_t tmp = *st;
                as_instruction_t tin = st->u.instr;
                as_operand_t tops[8];
                as_expr_t texpr;
                long long disp;

                if (st->u.instr.operand_count > sizeof(tops) / sizeof(tops[0])) {
                    return -1;
                }
                if (encode_x86_stmt(ctx, &stmt_cfg, st, code, code_cap, &probe_len, encerr, encerr_sz) != 0) {
                    return -1;
                }
                memcpy(tops, st->u.instr.operands, st->u.instr.operand_count * sizeof(*tops));
                memset(&texpr, 0, sizeof(texpr));
                texpr.kind = AS_EXPR_CONST;
                disp = rel - ((long long)sec_off + (long long)probe_len);
                texpr.value = disp;
                tops[oi].u.mem.disp = &texpr;
                tin.operands = tops;
                tmp.u.instr = tin;
                return encode_x86_stmt(ctx, &stmt_cfg, &tmp, code, code_cap, code_len, encerr, encerr_sz);
            }
        }
    }

    {
        int rc = encode_x86_stmt_by_text(ctx, &stmt_cfg, x86_code_bits, st, code, code_cap, code_len, encerr, encerr_sz);

        if (rc != 1) {
            return rc;
        }
    }
    /*
     * A jump or a call to a name and nothing more, that was not
     * resolved above: its bytes are the long form with room for where
     * it goes, the same for every such statement of the mnemonic in
     * the mode -- so they are worked out once for each and kept.  (It
     * went through the whole of the general encoder every time, some
     * 23,000 machine instructions of names compared.)
     */
    {
        static struct far_form {
            char mn[16];
            unsigned bits;
            unsigned char machine64;
            unsigned char known_place;
            unsigned char kind;
            unsigned char len;
            unsigned char bytes[16];
            unsigned char nfields;
            struct kept_field fields[AS_X86_FIELDS_MAX];
        } kept[64];
        static size_t kept_count;
        static int off = -1;
        const as_expr_t *e = NULL;
        struct far_form want;
        size_t k;
        int rc;

        if (off < 0) {
            off = getenv("AS_NO_STMT_CODE") != NULL;
        }
        memset(&want, 0, sizeof(want));
        if (!off && st->kind == AS_STMT_INSTRUCTION && st->u.instr.operand_count == 1 && st->u.instr.prefixes == 0 &&
            st->u.instr.pseudo == 0 && st->u.instr.code_len == 0 && st->u.instr.mnemonic != NULL &&
            strlen(st->u.instr.mnemonic) < sizeof(want.mn) && is_rel_mnemonic(st->u.instr.mnemonic) &&
            st->u.instr.operands[0].kind == AS_OPERAND_LABEL_REF && ctx->asm_reg_alias_count == 0) {
            e = st->u.instr.operands[0].u.expr;
        }
        if (e == NULL || e->kind != AS_EXPR_SYMBOL || e->symbol == NULL || e->reloc_op != NULL ||
            code_cap < sizeof(want.bytes)) {
            return encode_x86_stmt(ctx, &stmt_cfg, st, code, code_cap, code_len, encerr, encerr_sz);
        }
        for (k = 0; st->u.instr.mnemonic[k] != '\0'; ++k) {
            want.mn[k] = (char)tolower((unsigned char)st->u.instr.mnemonic[k]);
        }
        want.bits = x86_code_bits;
        want.machine64 = stmt_cfg.machine == EM_X86_64;
        want.known_place = (unsigned char)stmt_cfg.have_current_text_offset;
        /* (What the name is of: a compiler's label, a label of the
         * source's own, or a symbol of elsewhere.) */
        want.kind = (unsigned char)(expr_is_local_temp_symbol(e) ? 1 : name_is_near_label(e->symbol) ? 2 : 3);
        want.kind = (unsigned char)(want.kind + (st->u.instr.target_in_other_section ? 8 : 0));
        for (k = 0; k < kept_count; ++k) {
            if (kept[k].bits == want.bits && kept[k].machine64 == want.machine64 &&
                kept[k].known_place == want.known_place && kept[k].kind == want.kind &&
                strcmp(kept[k].mn, want.mn) == 0) {
                if (getenv("AS_CHECK_FAR_FORMS") != NULL) {
                    unsigned char again[16];
                    size_t again_len = 0;

                    if (encode_x86_stmt(ctx, &stmt_cfg, st, again, sizeof(again), &again_len, encerr, encerr_sz) != 0 ||
                        again_len != kept[k].len || memcmp(again, kept[k].bytes, again_len) != 0) {
                        fprintf(stderr, "AS_CHECK_FAR_FORMS: %s %s differs from the form kept\n", want.mn, e->symbol);
                        abort();
                    }
                }
                memcpy(code, kept[k].bytes, kept[k].len);
                *code_len = kept[k].len;
                fields_put(st, kept[k].fields, kept[k].nfields);
                return 0;
            }
        }
        rc = encode_x86_stmt(ctx, &stmt_cfg, st, code, code_cap, code_len, encerr, encerr_sz);
        if (rc == 0 && *code_len <= sizeof(want.bytes) && kept_count < sizeof(kept) / sizeof(kept[0])) {
            want.len = (unsigned char)*code_len;
            memcpy(want.bytes, code, *code_len);
            want.nfields = fields_keep(st, want.fields);
            kept[kept_count++] = want;
        }
        return rc;
    }
}

elf_symbol_t *find_emit_symbol(emit_ctx_t *ctx, const char *name) {
    size_t pos;

    if (ctx == NULL || name == NULL || ctx->sym_index == NULL || ctx->sym_index_cap == 0) {
        return NULL;
    }
    pos = hash_name(name) & (ctx->sym_index_cap - 1);
    while (ctx->sym_index[pos] != 0) {
        size_t idx = ctx->sym_index[pos] - 1;
        if (as_strcmp(ctx->sym_map[idx].name, name) == 0) {
            return ctx->sym_map[idx].sym;
        }
        pos = (pos + 1) & (ctx->sym_index_cap - 1);
    }
    return NULL;
}

static int ensure_emit_symbol_index(emit_ctx_t *ctx) {
    size_t cap;
    size_t *slots;
    size_t i;

    if (ctx == NULL) {
        return -1;
    }
    if (ctx->sym_index_cap != 0 && (ctx->sym_count + 1) * 2 < ctx->sym_index_cap) {
        return 0;
    }
    cap = 64;
    while (cap <= (ctx->sym_count + 1) * 2) {
        cap <<= 1;
    }
    slots = (size_t *)calloc(cap, sizeof(*slots));
    if (slots == NULL) {
        return -1;
    }
    for (i = 0; i < ctx->sym_count; ++i) {
        size_t pos = hash_name(ctx->sym_map[i].name) & (cap - 1);
        while (slots[pos] != 0) {
            pos = (pos + 1) & (cap - 1);
        }
        slots[pos] = i + 1;
    }
    free(ctx->sym_index);
    ctx->sym_index = slots;
    ctx->sym_index_cap = cap;
    return 0;
}

elf_symbol_t *ensure_emit_symbol(emit_ctx_t *ctx, const char *name) {
    elf_symbol_t *sym;
    elf_section_t *sec;

    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    sym = find_emit_symbol(ctx, name);
    if (sym != NULL) {
        return sym;
    }

    sec = section_for_name(ctx, name);
    if (sec != NULL) {
        sym = elf_add_symbol(ctx->obj, name, 0, 0, STB_LOCAL, STT_SECTION);
    } else {
        /* (Weak if the source said so of it, though it was not
         * written with the source's symbols: see
         * symbol_is_not_emitted().) */
        const as_symbol_t *said = find_as_symbol_const(ctx, ctx->symtab, name);

        sym = elf_add_symbol(ctx->obj, name, 0, 0,
                             said != NULL && said->bind == AS_SYM_BIND_WEAK ? STB_WEAK : STB_GLOBAL, STT_NOTYPE);
    }
    if (sym == NULL) {
        return NULL;
    }
    if (sec != NULL && elf_symbol_define(sym, sec, 0) != ELF_OK) {
        return NULL;
    }
    if (append_emit_symbol(ctx, name, sym) != 0) {
        return NULL;
    }
    return sym;
}

/* A symbol of the source by its name: the first of that name, found by
 * a table made the first time one is asked for. */
const as_symbol_t *find_as_symbol_const(emit_ctx_t *ctx, const as_symtab_t *symtab, const char *name) {
    size_t i;

    if (symtab == NULL || name == NULL) {
        return NULL;
    }
    if (!ctx->symtab_indexed) {
        for (i = symtab->count; i > 0; --i) {
            if (symtab->items[i - 1].name != NULL &&
                as_strmap_put(&ctx->symtab_by_name, symtab->items[i - 1].name, i - 1) != 0) {
                as_strmap_free(&ctx->symtab_by_name);
                return NULL;
            }
        }
        ctx->symtab_indexed = 1;
    }
    return as_strmap_get(&ctx->symtab_by_name, name, &i) ? &symtab->items[i] : NULL;
}

int append_emit_symbol(emit_ctx_t *ctx, const char *name, elf_symbol_t *sym) {
    emit_sym_t *next;
    size_t pos;

    if (ctx->sym_count == ctx->sym_cap) {
        size_t ncap = ctx->sym_cap == 0 ? 64 : ctx->sym_cap * 2;

        next = (emit_sym_t *)realloc(ctx->sym_map, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        ctx->sym_map = next;
        ctx->sym_cap = ncap;
    }
    ctx->sym_map[ctx->sym_count].name = xstrdup(name);
    ctx->sym_map[ctx->sym_count].sym = sym;
    if (ctx->sym_map[ctx->sym_count].name == NULL) {
        return -1;
    }
    if (ensure_emit_symbol_index(ctx) != 0) {
        free(ctx->sym_map[ctx->sym_count].name);
        ctx->sym_map[ctx->sym_count].name = NULL;
        return -1;
    }
    pos = hash_name(name) & (ctx->sym_index_cap - 1);
    while (ctx->sym_index[pos] != 0) {
        pos = (pos + 1) & (ctx->sym_index_cap - 1);
    }
    ctx->sym_index[pos] = ctx->sym_count + 1;
    ctx->sym_count++;
    return 0;
}

int numeric_local_label_number(const char *name, int *out) {
    size_t i;
    long value = 0;

    if (name == NULL || name[0] == '\0') {
        return -1;
    }
    for (i = 0; name[i] != '\0'; ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return -1;
        }
        value = value * 10 + (long)(name[i] - '0');
        if (value > INT_MAX) {
            return -1;
        }
    }
    if (out != NULL) {
        *out = (int)value;
    }
    return 0;
}

/* The relocation operator written on that symbol -- the first, as
 * first_symbol_in_expr() finds it -- or NULL. */
const char *first_symbol_op_in_expr(const as_expr_t *e) {
    if (e == NULL) {
        return NULL;
    }
    if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL) {
        return e->reloc_op;
    }
    if (e->lhs != NULL && first_symbol_in_expr(e->lhs) != NULL) {
        return first_symbol_op_in_expr(e->lhs);
    }
    return first_symbol_op_in_expr(e->rhs);
}

const char *first_symbol_in_expr(const as_expr_t *e) {
    if (e == NULL) {
        return NULL;
    }
    if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL) {
        return e->symbol;
    }
    if (e->lhs != NULL) {
        const char *s = first_symbol_in_expr(e->lhs);
        if (s != NULL) {
            return s;
        }
    }
    if (e->rhs != NULL) {
        return first_symbol_in_expr(e->rhs);
    }
    return NULL;
}

int parse_symbol_addend_arg(const char *arg, char **sym_out, int64_t *add_out) {
    return parse_symbol_addend_op_arg(arg, sym_out, NULL, add_out);
}

/* The same, and where op_out is not NULL the relocation operator
 * written on the symbol, or NULL for none: the caller frees it. */
int parse_symbol_addend_op_arg(const char *arg, char **sym_out, char **op_out, int64_t *add_out) {
    as_expr_t *e;
    as_expr_linear_t lin;
    int rc = -1;

    /*
     * A data directive's argument as as_expr.c reduces it.  One symbol
     * and a number give the symbol's name and the addend; a number alone
     * gives no name, and the caller has the number another way; a
     * difference of symbols is not this function's.
     */
    if (sym_out == NULL || add_out == NULL) {
        return -1;
    }
    *sym_out = NULL;
    *add_out = 0;
    if (op_out != NULL) {
        *op_out = NULL;
    }
    e = as_parse_expr_string(arg, NULL, 0);
    if (e == NULL) {
        return -1;
    }
    if (as_expr_eval_linear(e, NULL, NULL, &lin) == AS_EXPR_EVAL_OK &&
        lin.sub_symbol == NULL && lin.sub_local == NULL) {
        if (lin.add_symbol != NULL) {
            const char *rop = as_expr_reloc_op(e);

            if (op_out != NULL && rop != NULL) {
                *op_out = xstrdup(rop);
                if (*op_out == NULL) {
                    as_expr_free(e);
                    return -1;
                }
            }
            *sym_out = xstrdup(lin.add_symbol);
            rc = *sym_out != NULL ? 0 : -1;
        } else if (lin.add_local != NULL) {
            /* 1f and 1b by those names, which is how they are looked up. */
            char name[16];

            snprintf(name, sizeof(name), "%d%c", lin.add_local->local_digit,
                     lin.add_local->local_forward ? 'f' : 'b');
            *sym_out = xstrdup(name);
            rc = *sym_out != NULL ? 0 : -1;
        } else {
            rc = 0;
        }
        if (rc == 0 && *sym_out != NULL) {
            *add_out = (int64_t)lin.value;
        }
    }
    as_expr_free(e);
    return rc;
}

static char *dirname_dup2(const char *path) {
    const char *slash;
    size_t n;
    char *out;

    if (path == NULL) {
        return NULL;
    }
    slash = strrchr(path, '/');
    if (slash == NULL) {
        return xstrdup(".");
    }
    if (slash == path) {
        return xstrdup("/");
    }
    n = (size_t)(slash - path);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, path, n);
    out[n] = '\0';
    return out;
}

static char *join_path2(const char *a, const char *b) {
    size_t an;
    size_t bn;
    char *out;

    if (a == NULL || b == NULL) {
        return NULL;
    }
    an = strlen(a);
    bn = strlen(b);
    out = (char *)malloc(an + 1 + bn + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, a, an);
    out[an] = '/';
    memcpy(out + an + 1, b, bn);
    out[an + 1 + bn] = '\0';
    return out;
}

static char *decode_path_arg(const char *arg) {
    char *bytes = NULL;
    size_t len = 0;
    char *out;

    if (arg == NULL) {
        return NULL;
    }
    if (arg[0] != '"') {
        return xstrdup(arg);
    }
    if (as_decode_string_literal(arg, &bytes, &len) != 0) {
        return NULL;
    }
    if (memchr(bytes, '\0', len) != NULL) {
        free(bytes);
        return NULL;
    }
    out = (char *)malloc(len + 1);
    if (out == NULL) {
        free(bytes);
        return NULL;
    }
    memcpy(out, bytes, len);
    out[len] = '\0';
    free(bytes);
    return out;
}

static FILE *open_incbin_file(const as_stmt_t *st, const char *path, char **opened_path) {
    FILE *fp;

    if (opened_path != NULL) {
        *opened_path = NULL;
    }
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    fp = fopen(path, "rb");
    if (fp != NULL) {
        if (opened_path != NULL) {
            *opened_path = xstrdup(path);
        }
        return fp;
    }
    if (path[0] != '/' && st != NULL && st->file != NULL) {
        char *dir = dirname_dup2(st->file);
        char *joined = NULL;
        if (dir != NULL) {
            joined = join_path2(dir, path);
        }
        free(dir);
        if (joined != NULL) {
            fp = fopen(joined, "rb");
            if (fp != NULL) {
                if (opened_path != NULL) {
                    *opened_path = joined;
                } else {
                    free(joined);
                }
                return fp;
            }
            free(joined);
        }
    }
    return NULL;
}

int append_incbin_to_bytebuf(emit_ctx_t *ctx, const as_stmt_t *st, bytebuf_t *buf, const as_directive_t *d) {
    char *path = NULL;
    char *opened = NULL;
    unsigned long long skip = 0;
    unsigned long long count = 0;
    int has_count = 0;
    FILE *fp;
    unsigned char tmp[4096];

    if (d->arg_count < 1) {
        set_err(ctx, "%s:%u: .incbin requires path argument", st != NULL && st->file != NULL ? st->file : "<input>",
                st != NULL ? st->line : 0);
        return -1;
    }
    path = decode_path_arg(d->args[0]);
    if (path == NULL || path[0] == '\0') {
        free(path);
        set_err(ctx, "%s:%u: malformed .incbin path", st != NULL && st->file != NULL ? st->file : "<input>",
                st != NULL ? st->line : 0);
        return -1;
    }
    if (d->arg_count >= 2) {
        long long v;
        if (parse_int64(d->args[1], &v) != 0 || v < 0) {
            free(path);
            set_err(ctx, "%s:%u: malformed .incbin skip", st != NULL && st->file != NULL ? st->file : "<input>",
                    st != NULL ? st->line : 0);
            return -1;
        }
        skip = (unsigned long long)v;
    }
    if (d->arg_count >= 3) {
        long long v;
        if (parse_int64(d->args[2], &v) != 0 || v < 0) {
            free(path);
            set_err(ctx, "%s:%u: malformed .incbin count", st != NULL && st->file != NULL ? st->file : "<input>",
                    st != NULL ? st->line : 0);
            return -1;
        }
        count = (unsigned long long)v;
    }
    has_count = d->arg_count >= 3;

    fp = open_incbin_file(st, path, &opened);
    if (fp == NULL) {
        set_err(ctx, "%s:%u: failed to open .incbin file %s", st != NULL && st->file != NULL ? st->file : "<input>",
                st != NULL ? st->line : 0, path);
        free(path);
        return -1;
    }
    free(path);
    free(opened);

    while (skip > 0) {
        size_t want = skip > sizeof(tmp) ? sizeof(tmp) : (size_t)skip;
        size_t nread = fread(tmp, 1, want, fp);
        if (nread == 0) {
            break;
        }
        skip -= nread;
    }
    /* What is asked for is in the file, or it is refused: a skip past
     * the end gave nothing, and a count past it gave what there was. */
    if (skip > 0) {
        fclose(fp);
        set_err(ctx, "%s:%u: .incbin: the file is shorter than what is to be skipped",
                st != NULL && st->file != NULL ? st->file : "<input>", st != NULL ? st->line : 0);
        return -1;
    }
    while (!has_count || count > 0) {
        size_t want = sizeof(tmp);
        size_t nread;
        if (has_count && count < want) {
            want = (size_t)count;
        }
        nread = fread(tmp, 1, want, fp);
        if (nread == 0) {
            break;
        }
        if (bytebuf_append(buf, tmp, nread) != 0) {
            fclose(fp);
            set_err(ctx, "%s:%u: out of memory while appending .incbin",
                    st != NULL && st->file != NULL ? st->file : "<input>", st != NULL ? st->line : 0);
            return -1;
        }
        if (has_count) {
            count -= nread;
        }
    }
    fclose(fp);
    if (has_count && count > 0) {
        set_err(ctx, "%s:%u: .incbin: the file has fewer bytes after the skip than are asked for",
                st != NULL && st->file != NULL ? st->file : "<input>", st != NULL ? st->line : 0);
        return -1;
    }
    return 1;
}

/* A local label and the section it is defined in, to be looked up. */
typedef struct {
    const char *name; /* NULL for a numeric label, which is found by its line */
    unsigned line;
    int digit;
    size_t section;
    size_t stmt; /* the statement it is on */
} far_label_t;

/* The symbol a jump goes to, where it is written as a name and no
 * operator: the node of it, or NULL. */
static as_expr_t *jump_symbol_node(as_expr_t *e) {
    as_expr_t *found;

    if (e == NULL) {
        return NULL;
    }
    if (e->kind == AS_EXPR_SYMBOL) {
        return e->symbol != NULL && e->reloc_op == NULL ? e : NULL;
    }
    found = jump_symbol_node(e->lhs);
    return found != NULL ? found : jump_symbol_node(e->rhs);
}

static int far_label_cmp(const void *a, const void *b) {
    const far_label_t *x = (const far_label_t *)a;
    const far_label_t *y = (const far_label_t *)b;

    if ((x->name == NULL) != (y->name == NULL)) {
        return x->name == NULL ? -1 : 1;
    }
    if (x->name != NULL) {
        return as_strcmp(x->name, y->name);
    }
    if (x->line != y->line) {
        return x->line < y->line ? -1 : 1;
    }
    return x->digit - y->digit;
}

/* The local label a branch's operand names, alone or with something
 * added to it: `1b`, `.L5`, `1b+4`. */
static const as_expr_t *far_branch_label(const as_expr_t *e) {
    const as_expr_t *found;

    if (e == NULL) {
        return NULL;
    }
    if ((e->kind == AS_EXPR_LOCAL_REF && e->local_resolved) || expr_is_local_temp_symbol(e)) {
        return e;
    }
    found = far_branch_label(e->lhs);
    return found != NULL ? found : far_branch_label(e->rhs);
}

int as_elf_mark_far_branches(as_parse_result_t *parsed, const as_elf_cfg_t *cfg) {
    section_track_t track;
    sec_buf_vec_t names; /* for the sections' numbers only */
    far_label_t *labels = NULL;
    size_t label_count = 0, label_cap = 0;
    far_label_t *numbered = NULL;
    size_t numbered_count = 0;
    as_strmap_t label_by_name;
    const char **exported = NULL;
    size_t exported_count = 0, exported_cap = 0;
    const char **weak = NULL;
    size_t weak_count = 0, weak_cap = 0;
    size_t *stmt_section = NULL;
    size_t i;
    int rc = -1;

    near_names_free();
    if (parsed == NULL || parsed->count == 0) {
        return 0;
    }
    memset(&names, 0, sizeof(names));
    memset(&label_by_name, 0, sizeof(label_by_name));
    if (section_track_init(&track, cfg != NULL && cfg->is_64 ? 64u
                                                             : (cfg != NULL && cfg->x86_code_bits == 16u ? 16u : 32u)) !=
        0) {
        return -1;
    }
    stmt_section = (size_t *)calloc(parsed->count, sizeof(*stmt_section));
    if (stmt_section == NULL) {
        goto out;
    }
    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        sec_buf_t *sb = sec_buf_get_or_add(&names, track.current);
        size_t j;

        if (sb == NULL) {
            goto out;
        }
        stmt_section[i] = (size_t)(sb - names.items);
        for (j = 0; j < st->label_count; ++j) {
            const char *name = st->labels[j].name;
            int digit = -1;
            int numeric = numeric_local_label_number(name, &digit) == 0;

            if (label_count == label_cap) {
                size_t cap = label_cap != 0 ? label_cap * 2 : 64;
                far_label_t *grown = (far_label_t *)realloc(labels, cap * sizeof(*grown));

                if (grown == NULL) {
                    goto out;
                }
                labels = grown;
                label_cap = cap;
            }
            labels[label_count].name = numeric ? NULL : name;
            labels[label_count].line = numeric ? st->labels[j].ordinal : 0;
            labels[label_count].digit = numeric ? digit : 0;
            labels[label_count].section = stmt_section[i];
            labels[label_count].stmt = i;
            label_count++;
        }
        if (st->kind == AS_STMT_DIRECTIVE) {
            const as_directive_t *d = &st->u.directive;

            /* What another file may see, or replace, is not the
             * source's own. */
            if (as_strcmp(d->name, ".globl") == 0 || as_strcmp(d->name, ".global") == 0 || as_strcmp(d->name, ".weak") == 0) {
                for (j = 0; j < d->arg_count; ++j) {
                    /* (And those that may be replaced, apart.) */
                    if (as_strcmp(d->name, ".weak") == 0) {
                        if (weak_count == weak_cap) {
                            size_t cap = weak_cap != 0 ? weak_cap * 2 : 16;
                            const char **grown = (const char **)realloc(weak, cap * sizeof(*grown));

                            if (grown == NULL) {
                                goto out;
                            }
                            weak = grown;
                            weak_cap = cap;
                        }
                        weak[weak_count++] = d->args[j];
                    }
                    if (exported_count == exported_cap) {
                        size_t cap = exported_cap != 0 ? exported_cap * 2 : 64;
                        const char **grown = (const char **)realloc(exported, cap * sizeof(*grown));

                        if (grown == NULL) {
                            goto out;
                        }
                        exported = grown;
                        exported_cap = cap;
                    }
                    exported[exported_count++] = d->args[j];
                }
            }
            (void)section_track_apply_directive(&track, d);
        }
    }
    /*
     * A label that has a name is found by it, the first of the name;
     * one that has a number by where it is written, among those sorted.
     * (All of them were sorted, by name: a hundred thousand of a
     * compiler's labels compared a million and a half times over.)
     */
    for (i = 0; i < label_count; ++i) {
        size_t had;

        if (labels[i].name == NULL) {
            numbered_count++;
        } else if (!as_strmap_get(&label_by_name, labels[i].name, &had) &&
                   as_strmap_put(&label_by_name, labels[i].name, i) != 0) {
            goto out;
        }
    }
    if (numbered_count != 0) {
        size_t n = 0;

        numbered = (far_label_t *)malloc(numbered_count * sizeof(*numbered));
        if (numbered == NULL) {
            goto out;
        }
        for (i = 0; i < label_count; ++i) {
            if (labels[i].name == NULL) {
                numbered[n++] = labels[i];
            }
        }
        qsort(numbered, numbered_count, sizeof(*numbered), far_label_cmp);
    }
    /* The source's own labels, by name. */
    near_names_free();
    if (exported_count != 0) {
        qsort(exported, exported_count, sizeof(*exported), near_name_cmp);
    }
    if (weak_count != 0) {
        qsort(weak, weak_count, sizeof(*weak), near_name_cmp);
    }
    near_names = (char **)calloc(label_count != 0 ? label_count : 1, sizeof(*near_names));
    if (near_names == NULL) {
        goto out;
    }
    for (i = 0; i < label_count; ++i) {
        const char *name = labels[i].name;
        size_t first;

        /* (Once for a name that is defined twice: where it is first.) */
        if (name == NULL || is_local_temp_symbol_name(name) ||
            (as_strmap_get(&label_by_name, name, &first) && first != i) ||
            (exported_count != 0 &&
             bsearch(&name, exported, exported_count, sizeof(*exported), near_name_cmp) != NULL)) {
            continue;
        }
        near_names[near_name_count] = xstrdup(name);
        if (near_names[near_name_count] == NULL) {
            goto out;
        }
        near_name_count++;
    }
    /* (Sorted, for whoever looks a name up among them.) */
    if (near_name_count != 0) {
        qsort(near_names, near_name_count, sizeof(*near_names), near_name_cmp);
    }
    for (i = 0; i < parsed->count; ++i) {
        as_stmt_t *st = &parsed->items[i];
        const as_expr_t *e;
        far_label_t key;
        const far_label_t *found;

        /* (Nor does one before a far jump say anything.) */
        if (st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL && (cfg == NULL || cfg->target == 0) &&
            (strncasecmp(st->u.instr.mnemonic, "ljmp", 4) == 0 || strncasecmp(st->u.instr.mnemonic, "lcall", 5) == 0) &&
            (st->u.instr.prefixes & AS_PREFIX_SEG_OVERRIDE) != 0 && st->u.instr.operand_count != 0 &&
            st->u.instr.operands[0].kind != AS_OPERAND_MEMORY && !(cfg != NULL && cfg->is_64)) {
            free(st->u.instr.segment_override);
            st->u.instr.segment_override = NULL;
            st->u.instr.prefixes &= ~AS_PREFIX_SEG_OVERRIDE;
        }
        if (!as_parse_is_direct_branch(st)) {
            continue;
        }
        /*
         * A segment written before a jump or a call to a label says
         * nothing of where it goes.  %cs and %ds before a jump are the
         * hints of which way it will go, and are kept; any other, and
         * any before a call, GNU as leaves out, and so does this -- it
         * wrote them all.  (%es and %ss in 64-bit code are left for the
         * encoder to refuse.)
         */
        if ((st->u.instr.prefixes & AS_PREFIX_SEG_OVERRIDE) != 0 && st->u.instr.segment_override != NULL &&
            (cfg == NULL || cfg->target == 0)) {
            const char *seg = st->u.instr.segment_override;
            int hint = streq_ci(seg, "cs") || streq_ci(seg, "ds");
            int gone64 = cfg != NULL && cfg->is_64 && (streq_ci(seg, "es") || streq_ci(seg, "ss"));

            if (!gone64 && (is_call_mnemonic(st->u.instr.mnemonic) || !hint)) {
                free(st->u.instr.segment_override);
                st->u.instr.segment_override = NULL;
                st->u.instr.prefixes &= ~AS_PREFIX_SEG_OVERRIDE;
            }
        }
        /*
         * A jump to a label of its own section that the source exports
         * goes where the label is: another file may call the name, and
         * may not put something else where this one's code jumps to.
         * (A weak one it may; and a call or a `loop` of the name is the
         * linker's, as GNU as leaves them.)  The jump is made to go to
         * a name of the source's own, put on the label here: it was the
         * long form with a relocation, where the label was two bytes
         * away.
         */
        {
            as_expr_t *node = jump_symbol_node(st->u.instr.operands[0].u.expr);
            unsigned cc;
            char mnbuf[32];
            char suffix = '\0';

            if (node != NULL && !expr_is_local_temp_symbol(node) && as_strcmp(node->symbol, ".") != 0 &&
                normalize_x86_mnemonic(st->u.instr.mnemonic, mnbuf, sizeof(mnbuf), &suffix) == 0 &&
                (streq_ci(mnbuf, "jmp") || x86_cond_code(mnbuf, &cc) == 0) &&
                (cfg == NULL || cfg->target == 0) && exported_count != 0 &&
                bsearch(&node->symbol, exported, exported_count, sizeof(*exported), near_name_cmp) != NULL &&
                (weak_count == 0 ||
                 bsearch(&node->symbol, weak, weak_count, sizeof(*weak), near_name_cmp) == NULL)) {
                far_label_t want;
                const far_label_t *at;

                memset(&want, 0, sizeof(want));
                want.name = node->symbol;
                {
                    size_t which;

                    at = as_strmap_get(&label_by_name, want.name, &which) ? &labels[which] : NULL;
                }
                if (at != NULL && at->section == stmt_section[i]) {
                    size_t len = strlen(node->symbol) + sizeof(".L.jump.");
                    char *alias = (char *)malloc(len);

                    if (alias == NULL) {
                        goto out;
                    }
                    snprintf(alias, len, ".L.jump.%s", node->symbol);
                    if (as_stmt_add_label(&parsed->items[at->stmt], alias) != 0) {
                        free(alias);
                        goto out;
                    }
                    free(node->symbol);
                    node->symbol = alias;
                    /* (Its section is the jump's, which is all the rest
                     * of this asks.) */
                    continue;
                }
            }
        }
        e = far_branch_label(st->u.instr.operands[0].u.expr);
        if (e == NULL) {
            continue;
        }
        memset(&key, 0, sizeof(key));
        if (e->kind == AS_EXPR_LOCAL_REF && e->local_resolved) {
            key.line = e->local_target_line;
            key.digit = e->local_digit;
        } else if (expr_is_local_temp_symbol(e)) {
            key.name = e->symbol;
        } else {
            continue;
        }
        if (key.name != NULL) {
            size_t which;

            found = as_strmap_get(&label_by_name, key.name, &which) ? &labels[which] : NULL;
        } else {
            found = numbered_count != 0 ? (const far_label_t *)bsearch(&key, numbered, numbered_count,
                                                                         sizeof(*numbered), far_label_cmp)
                                        : NULL;
        }
        if (found == NULL) {
            /* A .L label that the source does not define: no nearer
             * than a symbol of another file, which is what it is. */
            if (key.name != NULL) {
                st->u.instr.target_in_other_section = AS_TARGET_UNDEFINED;
            }
            continue;
        }
        if (found->section == stmt_section[i]) {
            continue;
        }
        st->u.instr.target_in_other_section = 1;
        if (as_parse_name_branch_label(st) != 0) {
            goto out;
        }
    }
    rc = 0;

out:
    free(labels);
    free(numbered);
    as_strmap_free(&label_by_name);
    free(exported);
    free(weak);
    free(stmt_section);
    sec_buf_vec_free(&names);
    section_track_free(&track);
    return rc;
}
