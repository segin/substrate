#include "as_elf_emit.h"

#include "as_arm.h"

#include "as_x86_encode.h"

#include "as_x86_regs.h"

#include "as_reloc_op.h"

#include "as_cfi.h"

#include "as_dwline.h"

#include "as_x86_vec.h"

#include "as_x86_vex.h"

#include "as_x86_sse3.h"

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

static int is_numeric_local_label_name(const char *name);

static uint8_t map_bind(as_sym_bind_t bind) {
    switch (bind) {
    case AS_SYM_BIND_GLOBAL:
        return STB_GLOBAL;
    case AS_SYM_BIND_WEAK:
        return STB_WEAK;
    default:
        return STB_LOCAL;
    }
}

static uint8_t map_type(as_sym_type_t type) {
    switch (type) {
    case AS_SYM_TYPE_FUNCTION:
        return STT_FUNC;
    case AS_SYM_TYPE_OBJECT:
    case AS_SYM_TYPE_COMMON:
    case AS_SYM_TYPE_UNIQUE_OBJECT:
        return STT_OBJECT;
    case AS_SYM_TYPE_TLS_OBJECT:
        return STT_TLS;
    case AS_SYM_TYPE_IFUNC:
        return STT_GNU_IFUNC;
    default:
        return STT_NOTYPE;
    }
}

static uint8_t map_vis(as_sym_visibility_t vis) {
    switch (vis) {
    case AS_SYM_VIS_INTERNAL:
        return STV_INTERNAL;
    case AS_SYM_VIS_HIDDEN:
        return STV_HIDDEN;
    case AS_SYM_VIS_PROTECTED:
        return STV_PROTECTED;
    default:
        return STV_DEFAULT;
    }
}

/*
 * `.symver name, name@version`.
 *
 * A version of a symbol is a symbol whose name says so, which the
 * linker reads: `name@version` for a version that is not the one a
 * plain reference gets, `name@@version` for the one that is.  (This
 * assembler had made version sections of its own instead, which are the
 * linker's to make of these names.)  `name@@@version` is the two-at
 * form of a symbol defined here and the one-at form of one that is not,
 * and is the only name the symbol then has; a symbol of another file
 * has its versioned name alone whichever way it is written.
 *
 * The one versioned name a symbol is written under in place of its
 * own, to be freed; NULL if it keeps its own.
 */
static int symbol_is_not_emitted(emit_ctx_t *ctx, const as_symtab_t *symtab, const as_symbol_t *s);

static char *symver_spelled(const char *version, size_t len, int defined) {
    const char *three = NULL;
    char *out = (char *)malloc(len + 1);
    size_t i;

    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i + 2 < len; ++i) {
        if (version[i] == '@' && version[i + 1] == '@' && version[i + 2] == '@') {
            three = version + i;
            break;
        }
    }
    if (three == NULL) {
        memcpy(out, version, len);
        out[len] = '\0';
        return out;
    }
    i = (size_t)(three - version);
    memcpy(out, version, i);
    out[i++] = '@';
    if (defined) {
        out[i++] = '@';
    }
    memcpy(out + i, three + 3, len - (size_t)(three - version) - 3);
    out[i + len - (size_t)(three - version) - 3] = '\0';
    return out;
}

static char *symver_sole_name(const as_symbol_t *s) {
    int defined = s->defined || s->is_common;

    if (s->version == NULL || strchr(s->version, '\n') != NULL ||
        (defined && strstr(s->version, "@@@") == NULL)) {
        return NULL;
    }
    return symver_spelled(s->version, strlen(s->version), defined);
}

/* The versions of the symbols defined here that keep their own names
 * too: each a symbol beside the one it is a version of, and like it.
 * To be called when every symbol has its place. */
static int emit_symbol_versions(emit_ctx_t *ctx, const as_symtab_t *symtab) {
    size_t i;

    for (i = 0; i < symtab->count; ++i) {
        const as_symbol_t *s = &symtab->items[i];
        const char *v = s->version;
        elf_symbol_t *base;

        if (v == NULL || symbol_is_not_emitted(ctx, symtab, s)) {
            continue;
        }
        {
            char *sole = symver_sole_name(s);

            if (sole != NULL) {
                free(sole);
                continue;
            }
        }
        base = find_emit_symbol(ctx, s->name);
        if (base == NULL) {
            continue;
        }
        while (*v != '\0') {
            const char *end = strchr(v, '\n');
            size_t len = end != NULL ? (size_t)(end - v) : strlen(v);
            char *name = symver_spelled(v, len, 1);
            elf_symbol_t *sym;

            if (name == NULL) {
                return -1;
            }
            sym = elf_add_symbol(ctx->obj, name, elf_symbol_value(base), elf_symbol_size(base), elf_symbol_bind(base),
                                 elf_symbol_type(base));
            free(name);
            if (sym == NULL || elf_symbol_set_shndx(sym, elf_symbol_shndx(base)) != ELF_OK ||
                elf_symbol_set_value(sym, elf_symbol_value(base)) != ELF_OK ||
                elf_symbol_set_visibility(sym, elf_symbol_visibility(base)) != ELF_OK) {
                return -1;
            }
            v += len + (end != NULL ? 1 : 0);
        }
    }
    return 0;
}

/*
 * The symbols of the source that are not symbols of the object: a
 * compiler's .L labels; a name set to itself; and a name for a symbol
 * of another file, which is that symbol wherever it is used and has
 * nothing of its own to say -- it had been written as a local symbol
 * that no file defines.
 */
static int symbol_is_not_emitted(emit_ctx_t *ctx, const as_symtab_t *symtab, const as_symbol_t *s) {
    const as_symbol_t *target;

    /* (A .L label that is made global or weak is a symbol after all.) */
    if (is_local_temp_symbol_name(s->name) && s->bind == AS_SYM_BIND_LOCAL) {
        return 1;
    }
    /* A view of the line table is a number and no symbol. */
    {
        size_t at;

        if (s->name != NULL && !s->defined && line_view_names(ctx) == 0 &&
            as_strmap_get(&ctx->line_view_names, s->name, &at)) {
            return 1;
        }
    }
    /* A weak symbol that is neither here nor referred to says nothing:
     * GNU as leaves it out.  (One that is referred to is made when the
     * reference is.) */
    if (s->bind == AS_SYM_BIND_WEAK && !s->defined && !s->is_common && s->alias_target == NULL &&
        s->reference_count == 0) {
        return 1;
    }
    if (s->alias_target == NULL || s->name == NULL) {
        return 0;
    }
    if (as_strcmp(s->alias_target, s->name) == 0) {
        return 1;
    }
    if (s->alias_minus != NULL || s->alias_from_dot || is_local_temp_symbol_name(s->alias_target)) {
        return 0;
    }
    target = find_as_symbol_const(ctx, symtab, s->alias_target);
    return target == NULL || (!target->defined && !target->is_common);
}

typedef struct {
    char *name;
    elf_section_t *sec;
    uint64_t off;
} sym_loc_t;

typedef struct {
    size_t *slots;
    size_t cap;
} name_index_t;

typedef struct {
    const char *file;
    unsigned line;
    elf_section_t *sec;
    uint64_t off;
} dot_loc_t;

static void free_dot_locs(dot_loc_t *locs) {
    free(locs);
}

static const dot_loc_t *find_dot_loc(const dot_loc_t *locs, size_t count, const char *file, unsigned line) {
    size_t i;

    if (locs == NULL || file == NULL) {
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        if (locs[i].file != NULL && locs[i].line == line && as_strcmp(locs[i].file, file) == 0) {
            return &locs[i];
        }
    }
    return NULL;
}

static void free_sym_locs(sym_loc_t *locs, size_t count) {
    size_t i;

    if (locs == NULL) {
        return;
    }
    for (i = 0; i < count; ++i) {
        free(locs[i].name);
    }
    free(locs);
}

static void free_name_index(name_index_t *idx) {
    if (idx == NULL) {
        return;
    }
    free(idx->slots);
    idx->slots = NULL;
    idx->cap = 0;
}

static int ensure_name_index(name_index_t *idx, const sym_loc_t *locs, size_t count) {
    size_t cap;
    size_t *slots;
    size_t i;

    if (idx == NULL) {
        return -1;
    }
    if (idx->cap != 0 && (count + 1) * 2 < idx->cap) {
        return 0;
    }
    cap = 64;
    while (cap <= (count + 1) * 2) {
        cap <<= 1;
    }
    slots = (size_t *)calloc(cap, sizeof(*slots));
    if (slots == NULL) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        size_t pos = hash_name(locs[i].name) & (cap - 1);
        while (slots[pos] != 0) {
            pos = (pos + 1) & (cap - 1);
        }
        slots[pos] = i + 1;
    }
    free(idx->slots);
    idx->slots = slots;
    idx->cap = cap;
    return 0;
}

static int is_numeric_local_label_name(const char *name) {
    return numeric_local_label_number(name, NULL) == 0;
}

static const sym_loc_t *find_sym_loc(const sym_loc_t *locs, const name_index_t *idx, const char *name) {
    size_t pos;

    if (locs == NULL || idx == NULL || idx->slots == NULL || idx->cap == 0 || name == NULL) {
        return NULL;
    }
    pos = hash_name(name) & (idx->cap - 1);
    while (idx->slots[pos] != 0) {
        size_t hit = idx->slots[pos] - 1;
        if (as_strcmp(locs[hit].name, name) == 0) {
            return &locs[hit];
        }
        pos = (pos + 1) & (idx->cap - 1);
    }
    return NULL;
}

static int upsert_sym_loc(sym_loc_t **locs, size_t *count, size_t *cap, name_index_t *idx,
                          const char *name, elf_section_t *sec, uint64_t off) {
    sym_loc_t *next;
    size_t pos;

    if (locs == NULL || count == NULL || cap == NULL || idx == NULL || name == NULL) {
        return -1;
    }
    if (ensure_name_index(idx, *locs, *count) != 0) {
        return -1;
    }
    pos = hash_name(name) & (idx->cap - 1);
    while (idx->slots[pos] != 0) {
        size_t hit = idx->slots[pos] - 1;
        if (as_strcmp((*locs)[hit].name, name) == 0) {
            (*locs)[hit].sec = sec;
            (*locs)[hit].off = off;
            return 0;
        }
        pos = (pos + 1) & (idx->cap - 1);
    }

    if (*count == *cap) {
        size_t ncap = *cap == 0 ? 16 : *cap * 2;
        next = (sym_loc_t *)realloc(*locs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        *locs = next;
        *cap = ncap;
    }

    (*locs)[*count].name = xstrdup(name);
    if ((*locs)[*count].name == NULL) {
        return -1;
    }
    (*locs)[*count].sec = sec;
    (*locs)[*count].off = off;
    idx->slots[pos] = *count + 1;
    (*count)++;
    return 0;
}

static int collect_symbol_locations(emit_ctx_t *ctx, sym_loc_t **locs, size_t *loc_count, size_t *loc_cap,
                                    name_index_t *loc_index) {
    size_t i;
    sec_buf_vec_t secbufs;
    section_track_t track;

    if (ctx == NULL || locs == NULL || loc_count == NULL || loc_cap == NULL || loc_index == NULL) {
        return -1;
    }
    *locs = NULL;
    *loc_count = 0;
    *loc_cap = 0;
    loc_index->slots = NULL;
    loc_index->cap = 0;
    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track, ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                                   (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        return -1;
    }

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        uint64_t cur_off;
        elf_section_t *cur_sec;
        sec_buf_t *sb;
        size_t j;
        int trc;

        sb = sec_buf_get_or_add(&secbufs, track.current);
        if (sb == NULL) {
            free_sym_locs(*locs, *loc_count);
            *locs = NULL;
            *loc_count = 0;
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        cur_off = (uint64_t)sb->buf.len;
        cur_sec = section_for_name(ctx, track.current);

        for (j = 0; j < st->label_count; ++j) {
            if (is_numeric_local_label_name(st->labels[j].name)) {
                continue;
            }
            if (upsert_sym_loc(locs, loc_count, loc_cap, loc_index, st->labels[j].name, cur_sec, cur_off) != 0) {
                free_sym_locs(*locs, *loc_count);
                free_name_index(loc_index);
                *locs = NULL;
                *loc_count = 0;
                *loc_cap = 0;
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
        }

        if (st->kind == AS_STMT_DIRECTIVE) {
            int drc;

            trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                free_sym_locs(*locs, *loc_count);
                free_name_index(loc_index);
                *locs = NULL;
                *loc_count = 0;
    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    return -1;
}
            if (trc > 0) {
                continue;
            }
            drc = append_directive_data_ctx(ctx, &sb->buf, track.current, st, (uint64_t)sb->buf.len,
                                            track.x86_code_bits, &st->u.directive);
            if (drc < 0) {
                free_sym_locs(*locs, *loc_count);
                free_name_index(loc_index);
                *locs = NULL;
                *loc_count = 0;
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                set_err(ctx, "%s:%u: malformed directive data", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
            continue;
        }

        if (st->kind == AS_STMT_INSTRUCTION && section_name_is_executable(ctx, track.current)) {
            unsigned char code[32];
            size_t code_len = 0;
            char encerr[256];
            if (encode_x86_stmt_for_layout(ctx, track.current, cur_off, track.x86_code_bits,
                                           st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0) {
                free_sym_locs(*locs, *loc_count);
                free_name_index(loc_index);
                *locs = NULL;
                *loc_count = 0;
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                set_err(ctx, "%s:%u: %s", st->file != NULL ? st->file : "<input>", st->line, encerr);
                return -1;
            }
            if (bytebuf_append(&sb->buf, code, code_len) != 0) {
                free_sym_locs(*locs, *loc_count);
                free_name_index(loc_index);
                *locs = NULL;
                *loc_count = 0;
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
        }
    }

    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    return 0;
}

static int collect_dot_locations(emit_ctx_t *ctx, dot_loc_t **out, size_t *count_out) {
    size_t i;
    size_t count = 0;
    size_t cap = 0;
    dot_loc_t *items = NULL;
    sec_buf_vec_t secbufs;
    section_track_t track;

    if (ctx == NULL || out == NULL || count_out == NULL) {
        return -1;
    }
    *out = NULL;
    *count_out = 0;
    memset(&secbufs, 0, sizeof(secbufs));
    if (section_track_init(&track, ctx->cfg != NULL && ctx->cfg->is_64 ? 64u :
                                   (ctx->cfg != NULL && ctx->cfg->x86_code_bits == 16u ? 16u : 32u)) != 0) {
        return -1;
    }

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        sec_buf_t *sb = sec_buf_get_or_add(&secbufs, track.current);
        uint64_t cur_off;

        if (sb == NULL) {
            free(items);
            section_track_free(&track);
            sec_buf_vec_free(&secbufs);
            return -1;
        }
        cur_off = (uint64_t)sb->buf.len;
        if (st->kind == AS_STMT_DIRECTIVE) {
            int trc;
            int drc;

            if (count == cap) {
                size_t ncap = cap == 0 ? 128 : cap * 2;
                dot_loc_t *next = (dot_loc_t *)realloc(items, ncap * sizeof(*next));
                if (next == NULL) {
                    free(items);
                    section_track_free(&track);
                    sec_buf_vec_free(&secbufs);
                    return -1;
                }
                items = next;
                cap = ncap;
            }
            items[count].file = st->file;
            items[count].line = st->line;
            items[count].sec = section_for_name(ctx, track.current);
            items[count].off = cur_off;
            count++;

            trc = section_track_apply_directive(&track, &st->u.directive);
            if (trc < 0) {
                free(items);
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            if (trc > 0) {
                continue;
            }
            drc = append_directive_data_ctx(ctx, &sb->buf, track.current, st, (uint64_t)sb->buf.len,
                                            track.x86_code_bits, &st->u.directive);
            if (drc < 0) {
                free(items);
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
            continue;
        }
        if (st->kind == AS_STMT_INSTRUCTION && section_name_is_executable(ctx, track.current)) {
            unsigned char code[32];
            size_t code_len = 0;
            char encerr[256];
            if (encode_x86_stmt_for_layout(ctx, track.current, cur_off, track.x86_code_bits,
                                           st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0 ||
                bytebuf_append(&sb->buf, code, code_len) != 0) {
                free(items);
                section_track_free(&track);
                sec_buf_vec_free(&secbufs);
                return -1;
            }
        }
    }

    section_track_free(&track);
    sec_buf_vec_free(&secbufs);
    *out = items;
    *count_out = count;
    return 0;
}

int emit_symbols(emit_ctx_t *ctx, const as_symtab_t *symtab) {
    size_t i;
    sym_loc_t *locs = NULL;
    size_t loc_count = 0;
    size_t loc_cap = 0;
    dot_loc_t *dot_locs = NULL;
    size_t dot_count = 0;
    name_index_t loc_index;
    int trace_emit_phase = getenv("AS_DEBUG_EMIT_PHASE") != NULL;

    memset(&loc_index, 0, sizeof(loc_index));
    if (collect_symbol_locations(ctx, &locs, &loc_count, &loc_cap, &loc_index) != 0) {
        return -1;
    }
    if (trace_emit_phase) fprintf(stderr, "as: emit-phase symbol locations done count=%zu\n", loc_count);
    if (collect_dot_locations(ctx, &dot_locs, &dot_count) != 0) {
        free_sym_locs(locs, loc_count);
        free_dot_locs(dot_locs);
        free_name_index(&loc_index);
        return -1;
    }
    if (trace_emit_phase) fprintf(stderr, "as: emit-phase dot locations done count=%zu\n", dot_count);

    for (i = 0; i < symtab->count; ++i) {
        const as_symbol_t *s = &symtab->items[i];
        elf_symbol_t *esym;
        unsigned long long sym_size = s->size;

        if (symbol_is_not_emitted(ctx, symtab, s)) {
            continue;
        }
        if (s->size_target_symbol != NULL) {
            const sym_loc_t *target_loc = find_sym_loc(locs, &loc_index, s->size_target_symbol);
            elf_section_t *base_sec = NULL;
            uint64_t base_off = 0;

            if (target_loc == NULL || target_loc->sec == NULL) {
                set_err(ctx, "cannot resolve size target for %s", s->name);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            if (s->size_base_from_dot) {
                const dot_loc_t *dot = find_dot_loc(dot_locs, dot_count, s->size_anchor_file, s->size_anchor_line);
                if (dot == NULL || dot->sec == NULL) {
                    set_err(ctx, "cannot resolve symbolic size for %s", s->name);
                    free_sym_locs(locs, loc_count);
                    free_dot_locs(dot_locs);
                    free_name_index(&loc_index);
                    return -1;
                }
                base_sec = dot->sec;
                base_off = dot->off;
            } else {
                const as_symbol_t *base_sym = find_as_symbol_const(ctx, symtab, s->size_base_symbol);
                const sym_loc_t *base_loc = NULL;

                if (base_sym != NULL && base_sym->alias_from_dot && base_sym->alias_target != NULL &&
                    as_strcmp(base_sym->alias_target, s->size_target_symbol) == 0) {
                    const dot_loc_t *dot = find_dot_loc(dot_locs, dot_count, base_sym->def_file, base_sym->def_line);
                    if (dot == NULL || dot->sec == NULL || dot->sec != target_loc->sec ||
                        dot->off < target_loc->off) {
                        set_err(ctx, "cannot resolve symbolic size alias for %s", s->name);
                        free_sym_locs(locs, loc_count);
                        free_dot_locs(dot_locs);
                        free_name_index(&loc_index);
                        return -1;
                    }
                    sym_size = (unsigned long long)((int64_t)dot->off - (int64_t)target_loc->off +
                                                    base_sym->alias_addend);
                    base_sec = target_loc->sec;
                    base_off = target_loc->off + sym_size;
                } else {
                    base_loc = find_sym_loc(locs, &loc_index, s->size_base_symbol);
                    if (base_loc == NULL || base_loc->sec == NULL) {
                        set_err(ctx, "cannot resolve size base for %s", s->name);
                        free_sym_locs(locs, loc_count);
                        free_dot_locs(dot_locs);
                        free_name_index(&loc_index);
                        return -1;
                    }
                    base_sec = base_loc->sec;
                    base_off = base_loc->off;
                }
            }
            if (base_sec != target_loc->sec) {
                set_err(ctx, "%s:%u: the size of '%s' is the distance between two labels that are not of one section",
                        s->def_file != NULL ? s->def_file : "<input>", s->def_line, s->name);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            /* (A distance that is backwards is written as it comes, as
             * GNU as writes it; it was refused.) */
            sym_size = (unsigned long long)((long long)base_off - (long long)target_loc->off + s->size_addend);
        }

        {
            char *versioned = symver_sole_name(s);

            esym = elf_add_symbol(ctx->obj, versioned != NULL ? versioned : s->name, 0, sym_size, map_bind(s->bind),
                                  map_type(s->type));
            free(versioned);
        }
        if (esym == NULL) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        /* (An indirect function and a unique object are GNU's, and an
         * object that has one says it is of that ABI, or a reader does
         * not know what the numbers mean.) */
        if ((s->type == AS_SYM_TYPE_UNIQUE_OBJECT || s->type == AS_SYM_TYPE_IFUNC) &&
            elf_set_osabi(ctx->obj, ELFOSABI_LINUX) != ELF_OK) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        if (elf_symbol_set_visibility(esym, map_vis(s->visibility)) != ELF_OK ||
            (s->type == AS_SYM_TYPE_UNIQUE_OBJECT && elf_symbol_set_binding(esym, STB_GNU_UNIQUE) != ELF_OK)) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        if (append_emit_symbol(ctx, s->name, esym) != 0) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
    }
    if (trace_emit_phase) fprintf(stderr, "as: emit-phase primary symbols done count=%zu\n", symtab->count);

    for (i = 0; i < symtab->count; ++i) {
        const as_symbol_t *s = &symtab->items[i];
        elf_symbol_t *esym;
        const sym_loc_t *loc;

        if (symbol_is_not_emitted(ctx, symtab, s)) {
            continue;
        }
        esym = ensure_emit_symbol(ctx, s->name);
        if (esym == NULL) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        if (s->is_common) {
            if (elf_symbol_set_shndx(esym, SHN_COMMON) != ELF_OK) {
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            /*
             * A common symbol's value is its alignment and its size is
             * its size: the size was put for the value and nothing for
             * the size, so the linker gave `.comm buf,64,32` no room
             * at an alignment of 64.  With no alignment written it is
             * the size taken up to a power of two, and 16 at the most,
             * as GNU as has it.
             */
            {
                unsigned long long align = s->common_align;

                if (align == 0) {
                    for (align = 1; align < s->common_size && align < 16; align <<= 1) {
                    }
                }
                if (elf_symbol_set_value(esym, align) != ELF_OK ||
                    elf_symbol_set_size(esym, s->common_size) != ELF_OK) {
                    free_sym_locs(locs, loc_count);
                    free_dot_locs(dot_locs);
                    free_name_index(&loc_index);
                    return -1;
                }
            }
        } else if (s->is_absolute) {
            if (elf_symbol_set_shndx(esym, SHN_ABS) != ELF_OK ||
                elf_symbol_set_value(esym, s->absolute_value) != ELF_OK) {
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
        } else if (s->defined) {
            if (s->alias_target != NULL) {
                continue;
            }
            loc = find_sym_loc(locs, &loc_index, s->name);
            if (loc != NULL && loc->sec != NULL) {
                uint64_t thumb_bit = 0;
                size_t tf;

                for (tf = 0; tf < ctx->cfg->thumb_func_count; ++tf) {
                    if (as_strcmp(ctx->cfg->thumb_funcs[tf], s->name) == 0) {
                        thumb_bit = 1;
                        break;
                    }
                }
                if ((thumb_bit != 0 && elf_symbol_set_type(esym, STT_FUNC) != ELF_OK) ||
                    elf_symbol_define(esym, loc->sec, loc->off | thumb_bit) != ELF_OK) {
                    free_sym_locs(locs, loc_count);
                    free_dot_locs(dot_locs);
                    free_name_index(&loc_index);
                    return -1;
                }
                /*
                 * What is defined in a thread-local section is a
                 * thread-local symbol, whatever .type called it: a
                 * compiler writes `.type x, @object` for a __thread
                 * variable.  Left an object, the linker took its value
                 * for an address and not an offset in the thread's
                 * block, and the program read another variable.
                 */
                if ((elf_section_flags(loc->sec) & SHF_TLS) != 0 && elf_symbol_set_type(esym, STT_TLS) != ELF_OK) {
                    free_sym_locs(locs, loc_count);
                    free_dot_locs(dot_locs);
                    free_name_index(&loc_index);
                    return -1;
                }
            } else {
                /* Defined, and nowhere: it was put at the start of
                 * .text, where it was not.  There is no such symbol
                 * that this assembler should make. */
                set_err(ctx, "%s:%u: internal error: the symbol '%s' is defined and has no place",
                        s->def_file != NULL ? s->def_file : "<input>", s->def_line, s->name != NULL ? s->name : "");
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
        }
    }

    for (i = 0; i < symtab->count; ++i) {
        const as_symbol_t *s = &symtab->items[i];
        elf_symbol_t *esym;
        elf_symbol_t *target;
        uint16_t shndx;
        int64_t v;

        if (symbol_is_not_emitted(ctx, symtab, s)) {
            continue;
        }
        if (s->alias_target == NULL) {
            continue;
        }
        esym = ensure_emit_symbol(ctx, s->name);
        if (esym == NULL) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        if (s->alias_minus != NULL) {
            /* The distance between two labels: a number, if they are
             * of one section. */
            const sym_loc_t *to = find_sym_loc(locs, &loc_index, s->alias_target);
            const sym_loc_t *from = find_sym_loc(locs, &loc_index, s->alias_minus);

            if (to == NULL || from == NULL || to->sec == NULL || to->sec != from->sec) {
                set_err(ctx, "%s:%u: '%s' is set to '%s' less '%s', which are not two labels of one section",
                        s->def_file != NULL ? s->def_file : "<input>", s->def_line, s->name, s->alias_target,
                        s->alias_minus);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            v = (int64_t)to->off - (int64_t)from->off + s->alias_addend;
            if (elf_symbol_set_shndx(esym, SHN_ABS) != ELF_OK || elf_symbol_set_value(esym, (uint64_t)v) != ELF_OK) {
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            continue;
        }
        if (s->alias_from_dot) {
            const sym_loc_t *target_loc = find_sym_loc(locs, &loc_index, s->alias_target);
            elf_section_t *dot_sec = NULL;
            uint64_t dot_off = 0;

            if (target_loc == NULL || target_loc->sec == NULL) {
                set_err(ctx, "cannot resolve .-expression target for %s", s->name);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            {
                const dot_loc_t *dot = find_dot_loc(dot_locs, dot_count, s->def_file, s->def_line);
                if (dot != NULL) {
                    dot_sec = dot->sec;
                    dot_off = dot->off;
                }
            }
            if (dot_sec == NULL) {
                set_err(ctx, "cannot resolve current location for %s", s->name);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            if (dot_sec != target_loc->sec) {
                set_err(ctx, "cross-section .-symbol assignment is not supported for %s", s->name);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            v = (int64_t)dot_off - (int64_t)target_loc->off + s->alias_addend;
            if (v < 0) {
                set_err(ctx, "negative absolute value for %s", s->name);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            if (elf_symbol_set_shndx(esym, SHN_ABS) != ELF_OK || elf_symbol_set_value(esym, (uint64_t)v) != ELF_OK) {
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            continue;
        }
        /* A name for a place that has no symbol of its own -- a
         * compiler's label, or the `.` of a `.set` -- is at the place. */
        if (is_local_temp_symbol_name(s->alias_target)) {
            const sym_loc_t *at = find_sym_loc(locs, &loc_index, s->alias_target);

            if (at == NULL || at->sec == NULL ||
                elf_symbol_define(esym, at->sec, (uint64_t)((int64_t)at->off + s->alias_addend)) != ELF_OK) {
                set_err(ctx, "%s:%u: '%s' is set to '%s', which is no place in this source",
                        s->def_file != NULL ? s->def_file : "<input>", s->def_line, s->name, s->alias_target);
                free_sym_locs(locs, loc_count);
                free_dot_locs(dot_locs);
                free_name_index(&loc_index);
                return -1;
            }
            continue;
        }
        target = ensure_emit_symbol(ctx, s->alias_target);
        if (target == NULL) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        shndx = elf_symbol_shndx(target);
        if (elf_symbol_set_shndx(esym, shndx) != ELF_OK) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        v = (int64_t)elf_symbol_value(target) + s->alias_addend;
        if (elf_symbol_set_value(esym, (uint64_t)v) != ELF_OK) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
        /* Another name for a function or an object is one, and as
         * large, unless it was said to be something else. */
        if (elf_symbol_type(esym) == STT_NOTYPE &&
            (elf_symbol_set_type(esym, elf_symbol_type(target)) != ELF_OK ||
             (elf_symbol_size(esym) == 0 && s->alias_addend == 0 &&
              elf_symbol_set_size(esym, elf_symbol_size(target)) != ELF_OK))) {
            free_sym_locs(locs, loc_count);
            free_dot_locs(dot_locs);
            free_name_index(&loc_index);
            return -1;
        }
    }

    free_sym_locs(locs, loc_count);
    free_dot_locs(dot_locs);
    free_name_index(&loc_index);
    return emit_symbol_versions(ctx, symtab);
}
