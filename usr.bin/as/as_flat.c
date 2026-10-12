#include "as_emit_int.h"

typedef struct {
    char *name;
    unsigned type;
    unsigned flags;
    unsigned align;
    int touched;
    /* For padding where instructions go: the mode the code is in, and
     * whether what was last put in the section was data. */
    unsigned code_bits;
    int after_data;
    bytebuf_t buf;
} bin_section_t;

typedef struct {
    bin_section_t *items;
    size_t count;
    size_t cap;
} bin_section_vec_t;

static void bin_section_vec_free(bin_section_vec_t *v) {
    size_t i;

    if (v == NULL) {
        return;
    }
    for (i = 0; i < v->count; ++i) {
        free(v->items[i].name);
        free(v->items[i].buf.data);
    }
    free(v->items);
    memset(v, 0, sizeof(*v));
}

static int bin_section_vec_push(bin_section_vec_t *v, const char *name, unsigned type, unsigned flags, unsigned align) {
    bin_section_t *next;

    if (v == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 16 : v->cap * 2;
        next = (bin_section_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }
    memset(&v->items[v->count], 0, sizeof(v->items[v->count]));
    v->items[v->count].name = xstrdup(name);
    if (v->items[v->count].name == NULL) {
        return -1;
    }
    v->items[v->count].type = type;
    v->items[v->count].flags = flags;
    v->items[v->count].align = align > 0 ? align : 1;
    v->count++;
    return 0;
}

static bin_section_t *bin_section_find(bin_section_vec_t *v, const char *name) {
    size_t i;

    if (v == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < v->count; ++i) {
        if (as_strcmp(v->items[i].name, name) == 0) {
            return &v->items[i];
        }
    }
    return NULL;
}

static int bin_sections_init(bin_section_vec_t *v, const as_section_state_t *sections) {
    size_t i;

    if (v == NULL) {
        return -1;
    }
    memset(v, 0, sizeof(*v));
    if (sections == NULL) {
        return 0;
    }
    for (i = 0; i < sections->count; ++i) {
        const as_section_t *s = &sections->items[i];
        bin_section_t *cur = bin_section_find(v, s->name);
        if (cur == NULL) {
            if (bin_section_vec_push(v, s->name, s->type, s->flags, s->align) != 0) {
                return -1;
            }
            continue;
        }
        if (s->align > cur->align) {
            cur->align = s->align;
        }
        cur->flags |= s->flags;
        if (s->type == SHT_NOBITS) {
            cur->type = SHT_NOBITS;
        }
    }
    return 0;
}

static bin_section_t *bin_section_get_or_add(bin_section_vec_t *v, const char *name) {
    bin_section_t *sec;
    unsigned type = SHT_PROGBITS;
    unsigned flags = 0;
    unsigned align = 1;

    sec = bin_section_find(v, name);
    if (sec != NULL) {
        return sec;
    }
    if (as_strcmp(name, ".text") == 0) {
        type = SHT_PROGBITS;
        flags = SHF_ALLOC | SHF_EXECINSTR;
        align = 16;
    } else if (as_strcmp(name, ".rodata") == 0) {
        type = SHT_PROGBITS;
        flags = SHF_ALLOC;
        align = 4;
    } else if (as_strcmp(name, ".data") == 0) {
        type = SHT_PROGBITS;
        flags = SHF_ALLOC | SHF_WRITE;
        align = 4;
    } else if (as_strcmp(name, ".bss") == 0) {
        type = SHT_NOBITS;
        flags = SHF_ALLOC | SHF_WRITE;
        align = 4;
    }
    if (bin_section_vec_push(v, name, type, flags, align) != 0) {
        return NULL;
    }
    return &v->items[v->count - 1];
}

static int parse_nonneg_u64_or_reloc(emit_ctx_t *ctx, const as_stmt_t *st, const char *arg,
                                     const char *what, unsigned long long *out) {
    long long v;
    char *sym = NULL;
    int64_t add = 0;

    if (parse_int64(arg, &v) == 0 || parse_const_expr_string(arg, &v) == 0) {
        if (v < 0) {
            set_err(ctx, "%s:%u: %s must be non-negative", st->file != NULL ? st->file : "<input>", st->line, what);
            return -1;
        }
        *out = (unsigned long long)v;
        return 0;
    }
    if (parse_symbol_addend_arg(arg, &sym, &add) == 0 && sym != NULL) {
        set_err(ctx, "%s:%u: unresolved relocation to '%s' not allowed with -O binary",
                st->file != NULL ? st->file : "<input>", st->line, sym);
        free(sym);
        return -1;
    }
    set_err(ctx, "%s:%u: invalid %s argument: %s", st->file != NULL ? st->file : "<input>", st->line, what, arg);
    free(sym);
    return -1;
}

static int append_incbin_bytes(emit_ctx_t *ctx, const as_stmt_t *st, bin_section_t *sec, const as_directive_t *d) {
    if (append_incbin_to_bytebuf(ctx, st, &sec->buf, d) < 0) {
        return -1;
    }
    sec->touched = 1;
    return 0;
}

static int append_data_directive_binary(emit_ctx_t *ctx, const as_stmt_t *st, bin_section_t *sec, int *handled) {
    const as_directive_t *d;
    size_t i;

    if (ctx == NULL || st == NULL || sec == NULL || handled == NULL || st->kind != AS_STMT_DIRECTIVE) {
        return -1;
    }
    d = &st->u.directive;
    *handled = 0;

    if (as_strcmp(d->name, ".incbin") == 0) {
        *handled = 1;
        return append_incbin_bytes(ctx, st, sec, d);
    }
    if (as_strcmp(d->name, ".ascii") == 0 || as_strcmp(d->name, ".asciz") == 0 || as_strcmp(d->name, ".string") == 0) {
        int nul = (as_strcmp(d->name, ".ascii") == 0) ? 0 : 1;
        *handled = 1;
        for (i = 0; i < d->arg_count; ++i) {
            const char *s = d->args[i] != NULL ? d->args[i] : "";
            char *bytes = NULL;
            size_t len = 0;
            if (as_decode_string_literal(s, &bytes, &len) != 0) {
                set_err(ctx, "%s:%u: malformed string literal", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
            if (bytebuf_append(&sec->buf, bytes, len) != 0) {
                free(bytes);
                set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
            if (nul && bytebuf_append_zeros(&sec->buf, 1) != 0) {
                set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
        }
        sec->touched = 1;
        return 0;
    }
    if (as_strcmp(d->name, ".float") == 0 || as_strcmp(d->name, ".double") == 0) {
        int is_double = as_strcmp(d->name, ".double") == 0;
        *handled = 1;
        for (i = 0; i < d->arg_count; ++i) {
            char *tmp = trim_copy_arg(d->args[i]);
            char *end;
            double dv;
            if (tmp == NULL || tmp[0] == '\0') {
                free(tmp);
                set_err(ctx, "%s:%u: malformed %s argument", st->file != NULL ? st->file : "<input>", st->line, d->name);
                return -1;
            }
            dv = strtod(tmp, &end);
            if (end == tmp || *end != '\0') {
                char *sym = NULL;
                int64_t add = 0;
                if (parse_symbol_addend_arg(d->args[i], &sym, &add) == 0 && sym != NULL) {
                    set_err(ctx, "%s:%u: unresolved relocation to '%s' not allowed with -O binary",
                            st->file != NULL ? st->file : "<input>", st->line, sym);
                    free(sym);
                } else {
                    set_err(ctx, "%s:%u: malformed %s argument", st->file != NULL ? st->file : "<input>", st->line,
                            d->name);
                }
                free(tmp);
                return -1;
            }
            free(tmp);
            if (is_double) {
                if (bytebuf_append(&sec->buf, &dv, sizeof(dv)) != 0) {
                    set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                    return -1;
                }
            } else {
                float fv = (float)dv;
                if (bytebuf_append(&sec->buf, &fv, sizeof(fv)) != 0) {
                    set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                    return -1;
                }
            }
        }
        sec->touched = 1;
        return 0;
    }
    if (as_strcmp(d->name, ".align") == 0 || as_strcmp(d->name, ".balign") == 0 || as_strcmp(d->name, ".p2align") == 0) {
        unsigned long long raw;
        unsigned long long fill = (sec->flags & SHF_EXECINSTR) != 0 ? 0x90 : 0;
        size_t align;
        size_t need;
        *handled = 1;
        if (d->arg_count < 1 || parse_nonneg_u64_or_reloc(ctx, st, d->args[0], d->name, &raw) != 0) {
            return -1;
        }
        if (directive_has_arg(d, 1) && parse_nonneg_u64_or_reloc(ctx, st, d->args[1], ".align fill", &fill) != 0) {
            return -1;
        }
        if (as_strcmp(d->name, ".p2align") == 0) {
            if (raw >= (unsigned long long)(sizeof(size_t) * CHAR_BIT - 1)) {
                set_err(ctx, "%s:%u: .p2align exponent too large", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
            align = (size_t)1u << (unsigned)raw;
        } else {
            align = (size_t)raw;
        }
        if (align == 0 || (align & (align - 1)) != 0) {
            set_err(ctx, "%s:%u: alignment must be a power of two", st->file != NULL ? st->file : "<input>", st->line);
            return -1;
        }
        need = (align - (sec->buf.len & (align - 1))) & (align - 1);
        switch (align_exceeds_max_skip(d, need)) {
        case 1: need = 0; break;
        case 0: break;
        default:
            set_err(ctx, "%s:%u: %s: the maximum to skip is not a number",
                    st->file != NULL ? st->file : "<input>", st->line, d->name);
            return -1;
        }
        /* Where instructions go, padding that is NOPs -- none named, or
         * 0x90 named -- is the NOPs the object writer pads with: this
         * wrote 0x90 for each byte. */
        if ((sec->flags & SHF_EXECINSTR) != 0 && ctx->cfg != NULL &&
            (ctx->cfg->target != 0 ? !directive_has_arg(d, 1)
                                   : fill == 0x90 && !(d->fill_width > 1 && directive_has_arg(d, 1)))) {
            int rc;

            if (ctx->cfg->target != 0) {
                rc = append_arm_nops(&sec->buf, need, ctx->cfg->target, sec->code_bits);
            } else {
                rc = 0;
                if (sec->after_data && need != 0) {
                    rc = bytebuf_append_u64_le(&sec->buf, 0x90, 1);
                    need--;
                }
                if (rc == 0) {
                    rc = append_x86_nops(&sec->buf, need, 0, sec->code_bits, ctx->cfg->machine == EM_X86_64);
                }
            }
            if (rc != 0) {
                set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
            sec->touched = 1;
            sec->after_data = 0;
            return 0;
        }
        switch (append_align_fill(&sec->buf, d, need, fill)) {
        case 0:
            break;
        case -2:
            set_err(ctx, "%s:%u: alignment padding (%lu bytes) is not a multiple of %u",
                    st->file != NULL ? st->file : "<input>", st->line, (unsigned long)need, d->fill_width);
            return -1;
        default:
            set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
            return -1;
        }
        sec->touched = 1;
        return 0;
    }
    if (as_strcmp(d->name, ".org") == 0) {
        unsigned long long off;
        *handled = 1;
        if (d->arg_count < 1 || parse_nonneg_u64_or_reloc(ctx, st, d->args[0], ".org offset", &off) != 0) {
            return -1;
        }
        if ((size_t)off < sec->buf.len) {
            set_err(ctx, "%s:%u: backward .org is not supported in binary mode",
                    st->file != NULL ? st->file : "<input>", st->line);
            return -1;
        }
        if ((size_t)off > sec->buf.len && bytebuf_append_zeros(&sec->buf, (size_t)off - sec->buf.len) != 0) {
            set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
            return -1;
        }
        sec->touched = 1;
        return 0;
    }
    if (as_strcmp(d->name, ".zero") == 0 || as_strcmp(d->name, ".space") == 0 || as_strcmp(d->name, ".skip") == 0) {
        unsigned long long n;
        unsigned long long fill = 0;
        long long signed_n = 0;
        size_t k;
        *handled = 1;
        if (d->arg_count < 1) {
            set_err(ctx, "%s:%u: %s requires count argument", st->file != NULL ? st->file : "<input>", st->line, d->name);
            return -1;
        }
        if (eval_linux_alt_pad_expr(ctx, sec->name, st, sec->buf.len,
                                    ctx->cfg != NULL ? ctx->cfg->x86_code_bits : 64u,
                                    d->args[0], &signed_n) == 0) {
            n = signed_n < 0 ? 0 : (unsigned long long)signed_n;
        } else if (parse_nonneg_u64_or_reloc(ctx, st, d->args[0], d->name, &n) != 0) {
            as_expr_t *expr = as_parse_expr_string(d->args[0], st != NULL ? st->file : NULL,
                                                  st != NULL ? st->line : 0);
            if (expr != NULL &&
                       (eval_abs_local_diff_expr_virtual(ctx, st, expr, &signed_n) == 0 ||
                        eval_local_rel_expr_virtual(ctx, sec->name, st, sec->buf.len,
                                                    ctx->cfg != NULL ? ctx->cfg->x86_code_bits : 64u,
                                                    expr, &signed_n) == 0)) {
                n = signed_n < 0 ? 0 : (unsigned long long)signed_n;
            } else {
                as_expr_free(expr);
                return -1;
            }
            as_expr_free(expr);
        }
        if (n > (1ULL << 20)) {
            set_err(ctx, "%s:%u: %s count is unreasonably large: %llu",
                    st->file != NULL ? st->file : "<input>", st->line, d->name, n);
            return -1;
        }
        if ((as_strcmp(d->name, ".space") == 0 || as_strcmp(d->name, ".skip") == 0) && d->arg_count >= 2) {
            long long fv = 0;
            if (parse_int64(d->args[1], &fv) != 0 && parse_const_expr_string(d->args[1], &fv) != 0) {
                return -1;
            }
            fill = (unsigned long long)fv;
        }
        for (k = 0; k < (size_t)n; ++k) {
            if (bytebuf_append_u64_le(&sec->buf, fill, 1) != 0) {
                set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
        }
        sec->touched = 1;
        return 0;
    }
    if (as_strcmp(d->name, ".fill") == 0) {
        unsigned long long repeat = 0;
        unsigned long long size = 1;
        unsigned long long value = 0;
        long long repeat_expr = 0;
        *handled = 1;
        if (d->arg_count < 1) {
            return -1;
        }
        if (parse_nonneg_u64_or_reloc(ctx, st, d->args[0], ".fill repeat", &repeat) != 0) {
            as_expr_t *expr = as_parse_expr_string(d->args[0], st != NULL ? st->file : NULL,
                                                  st != NULL ? st->line : 0);
            if (expr == NULL ||
                (eval_abs_local_diff_expr_virtual(ctx, st, expr, &repeat_expr) != 0 &&
                 eval_local_rel_expr_virtual(ctx, sec->name, st, sec->buf.len,
                                             ctx->cfg != NULL ? ctx->cfg->x86_code_bits : 64u, expr,
                                             &repeat_expr) != 0) ||
                repeat_expr < 0) {
                as_expr_free(expr);
                return -1;
            }
            as_expr_free(expr);
            repeat = (unsigned long long)repeat_expr;
        }
        if (d->arg_count >= 2 && parse_nonneg_u64_or_reloc(ctx, st, d->args[1], ".fill size", &size) != 0) {
            return -1;
        }
        if (d->arg_count >= 3 && parse_nonneg_u64_or_reloc(ctx, st, d->args[2], ".fill value", &value) != 0) {
            return -1;
        }
        if (repeat > (1ULL << 20)) {
            set_err(ctx, "%s:%u: .fill repeat is unreasonably large: %llu",
                    st->file != NULL ? st->file : "<input>", st->line, repeat);
            return -1;
        }
        if (size == 0 || size > 8) {
            set_err(ctx, "%s:%u: .fill size must be in [1,8]", st->file != NULL ? st->file : "<input>", st->line);
            return -1;
        }
        for (i = 0; i < (size_t)repeat; ++i) {
            if (bytebuf_append_u64_le(&sec->buf, value, (unsigned)size) != 0) {
                set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
        }
        sec->touched = 1;
        return 0;
    }
    if (as_strcmp(d->name, ".byte") == 0 || as_strcmp(d->name, ".word") == 0 || as_strcmp(d->name, ".short") == 0 ||
        as_strcmp(d->name, ".hword") == 0 || as_strcmp(d->name, ".2byte") == 0 || as_strcmp(d->name, ".long") == 0 ||
        as_strcmp(d->name, ".int") == 0 || as_strcmp(d->name, ".4byte") == 0 || as_strcmp(d->name, ".quad") == 0 ||
        as_strcmp(d->name, ".8byte") == 0) {
        unsigned width = 1;
        *handled = 1;
        if (as_strcmp(d->name, ".word") == 0 || as_strcmp(d->name, ".short") == 0 || as_strcmp(d->name, ".hword") == 0 ||
            as_strcmp(d->name, ".2byte") == 0) {
            width = 2;
        } else if (as_strcmp(d->name, ".long") == 0 || as_strcmp(d->name, ".int") == 0 || as_strcmp(d->name, ".4byte") == 0) {
            width = 4;
        } else if (as_strcmp(d->name, ".quad") == 0 || as_strcmp(d->name, ".8byte") == 0) {
            width = 8;
        }
        for (i = 0; i < d->arg_count; ++i) {
            long long v;
            if (eval_linux_alt_len_expr(ctx, st, d->args[i], &v) != 0 &&
                parse_int64(d->args[i], &v) != 0 &&
                parse_const_expr_string(d->args[i], &v) != 0 &&
                eval_arg_asm_vars(ctx, st, d->args[i], &v) != 0) {
                char *sym = NULL;
                int64_t add = 0;
                as_expr_t *expr = as_parse_expr_string(d->args[i], st != NULL ? st->file : NULL,
                                                       st != NULL ? st->line : 0);
                if (expr != NULL &&
                    (eval_abs_local_diff_expr_virtual(ctx, st, expr, &v) == 0 ||
                     eval_local_rel_expr_virtual(ctx, sec->name, st, sec->buf.len + (uint64_t)(i * width),
                                                 ctx->cfg != NULL ? ctx->cfg->x86_code_bits : 64u,
                                                 expr, &v) == 0)) {
                    as_expr_free(expr);
                } else if (parse_symbol_addend_arg(d->args[i], &sym, &add) == 0 && sym != NULL) {
                    as_expr_free(expr);
                    free(sym);
                    v = 0;
                } else if (expr != NULL && expr_has_symbol(expr)) {
                    as_expr_free(expr);
                    v = 0;
                } else {
                    as_expr_free(expr);
                    set_err(ctx, "%s:%u: malformed %s argument", st->file != NULL ? st->file : "<input>", st->line,
                            d->name);
                    return -1;
                }
            }
            if (bytebuf_append_u64_le(&sec->buf, (uint64_t)v, width) != 0) {
                set_err(ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                return -1;
            }
        }
        sec->touched = 1;
        return 0;
    }

    return 0;
}

static int is_binary_metadata_directive(const char *name) {
    if (name == NULL) {
        return 0;
    }
    if (as_strcmp(name, ".globl") == 0 || as_strcmp(name, ".global") == 0 || as_strcmp(name, ".local") == 0 ||
        as_strcmp(name, ".weak") == 0 || as_strcmp(name, ".type") == 0 || as_strcmp(name, ".size") == 0 ||
        as_strcmp(name, ".hidden") == 0 || as_strcmp(name, ".protected") == 0 || as_strcmp(name, ".internal") == 0 ||
        as_strcmp(name, ".symver") == 0 || as_strcmp(name, ".comm") == 0 || as_strcmp(name, ".lcomm") == 0 ||
        as_strcmp(name, ".file") == 0 || as_strcmp(name, ".loc") == 0 || as_strcmp(name, ".ident") == 0) {
        return 1;
    }
    if (strncmp(name, ".cfi_", 5) == 0) {
        return 1;
    }
    return 0;
}

static int write_binary_file(emit_ctx_t *ctx, const bin_section_vec_t *v, const char *out_path) {
    static const char *const first_order[] = {".text", ".rodata", ".data"};
    bytebuf_t out;
    size_t i;
    unsigned char *used;
    FILE *fp;

    memset(&out, 0, sizeof(out));
    used = (unsigned char *)calloc(v->count, 1);
    if (used == NULL) {
        return -1;
    }

    for (i = 0; i < sizeof(first_order) / sizeof(first_order[0]); ++i) {
        size_t j;
        for (j = 0; j < v->count; ++j) {
            const bin_section_t *s = &v->items[j];
            size_t align;
            size_t need;
            if (used[j] || as_strcmp(s->name, first_order[i]) != 0) {
                continue;
            }
            if (s->buf.len == 0 && !s->touched) {
                used[j] = 1;
                continue;
            }
            align = s->align > 0 ? s->align : 1;
            if ((align & (align - 1)) != 0) {
                align = 1;
            }
            need = (align - (out.len & (align - 1))) & (align - 1);
            if (need > 0 && bytebuf_append_zeros(&out, need) != 0) {
                free(used);
                free(out.data);
                return -1;
            }
            if (s->type == SHT_NOBITS) {
                if (s->buf.len > 0 && bytebuf_append_zeros(&out, s->buf.len) != 0) {
                    free(used);
                    free(out.data);
                    return -1;
                }
            } else if (s->buf.len > 0 && bytebuf_append(&out, s->buf.data, s->buf.len) != 0) {
                free(used);
                free(out.data);
                return -1;
            }
            used[j] = 1;
        }
    }

    for (i = 0; i < v->count; ++i) {
        const bin_section_t *s = &v->items[i];
        size_t align;
        size_t need;
        if (used[i] || as_strcmp(s->name, ".bss") == 0) {
            continue;
        }
        if (s->buf.len == 0 && !s->touched) {
            used[i] = 1;
            continue;
        }
        align = s->align > 0 ? s->align : 1;
        if ((align & (align - 1)) != 0) {
            align = 1;
        }
        need = (align - (out.len & (align - 1))) & (align - 1);
        if (need > 0 && bytebuf_append_zeros(&out, need) != 0) {
            free(used);
            free(out.data);
            return -1;
        }
        if (s->type == SHT_NOBITS) {
            if (s->buf.len > 0 && bytebuf_append_zeros(&out, s->buf.len) != 0) {
                free(used);
                free(out.data);
                return -1;
            }
        } else if (s->buf.len > 0 && bytebuf_append(&out, s->buf.data, s->buf.len) != 0) {
            free(used);
            free(out.data);
            return -1;
        }
        used[i] = 1;
    }

    for (i = 0; i < v->count; ++i) {
        const bin_section_t *s = &v->items[i];
        size_t align;
        size_t need;
        if (used[i] || as_strcmp(s->name, ".bss") != 0) {
            continue;
        }
        if (s->buf.len == 0 && !s->touched) {
            used[i] = 1;
            continue;
        }
        align = s->align > 0 ? s->align : 1;
        if ((align & (align - 1)) != 0) {
            align = 1;
        }
        need = (align - (out.len & (align - 1))) & (align - 1);
        if (need > 0 && bytebuf_append_zeros(&out, need) != 0) {
            free(used);
            free(out.data);
            return -1;
        }
        if (s->buf.len > 0 && bytebuf_append_zeros(&out, s->buf.len) != 0) {
            free(used);
            free(out.data);
            return -1;
        }
        used[i] = 1;
    }

    free(used);
    fp = fopen(out_path, "wb");
    if (fp == NULL) {
        free(out.data);
        set_err(ctx, "can't create %s: %s", out_path, strerror(errno));
        return -1;
    }
    if (out.len > 0 && fwrite(out.data, 1, out.len, fp) != out.len) {
        fclose(fp);
        free(out.data);
        set_err(ctx, "failed to write output file: %s", out_path);
        return -1;
    }
    fclose(fp);
    free(out.data);
    return 0;
}

int as_elf_emit_binary_file(const as_parse_result_t *parsed,
                            const as_section_state_t *sections,
                            const as_elf_cfg_t *cfg,
                            const char *out_path,
                            char *errbuf,
                            size_t errbuf_sz) {
    emit_ctx_t ctx;
    bin_section_vec_t sv;
    const char *cur_section = ".text";
    unsigned cur_code_bits;
    size_t i;

    if (parsed == NULL || cfg == NULL || out_path == NULL) {
        return -1;
    }
    if (cfg->target != 0 || !(cfg->machine == EM_386 || cfg->machine == EM_X86_64)) {
        if (errbuf != NULL && errbuf_sz > 0) {
            snprintf(errbuf, errbuf_sz, "a flat image (-O binary) is written for i386 and x86-64 only");
        }
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = cfg;
    ctx.parsed = parsed;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    if (fold_absolute_operands(&ctx) != 0) {
        set_err(&ctx, "out of memory");
        return -1;
    }
    if (bin_sections_init(&sv, sections) != 0) {
        set_err(&ctx, "failed to initialize section layout");
        return -1;
    }
    cur_code_bits = cfg->is_64 ? 64u : (cfg->x86_code_bits == 16u ? 16u : 32u);

    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        bin_section_t *sec = bin_section_get_or_add(&sv, cur_section);
        if (sec == NULL) {
            set_err(&ctx, "out of memory");
            goto fail;
        }

        if (st->kind == AS_STMT_DIRECTIVE) {
            const as_directive_t *d = &st->u.directive;
            const char *next = section_from_directive(d);
            int handled = 0;

            if (next != NULL) {
                cur_section = next;
                continue;
            }
            if (as_strcmp(d->name, ".code16") == 0) {
                cur_code_bits = 16u;
                continue;
            }
            if (as_strcmp(d->name, ".code32") == 0) {
                cur_code_bits = 32u;
                continue;
            }
            if (as_strcmp(d->name, ".code64") == 0) {
                cur_code_bits = 64u;
                continue;
            }
            if (is_binary_metadata_directive(d->name)) {
                set_err(&ctx, "%s:%u: ELF metadata directive %s is not supported with -O binary",
                        st->file != NULL ? st->file : "<input>", st->line, d->name);
                goto fail;
            }
            {
                size_t before = sec->buf.len;
                int aligns = as_strcmp(d->name, ".align") == 0 || as_strcmp(d->name, ".balign") == 0 ||
                             as_strcmp(d->name, ".p2align") == 0;

                sec->code_bits = cur_code_bits;
                if (append_data_directive_binary(&ctx, st, sec, &handled) != 0) {
                    goto fail;
                }
                if (!aligns && sec->buf.len != before) {
                    sec->after_data = 1;
                }
            }
            (void)handled;
            continue;
        }
        if (st->kind == AS_STMT_INSTRUCTION) {
            unsigned char code[32];
            size_t code_len = 0;
            char encerr[256];

            if (sec->type == SHT_NOBITS) {
                set_err(&ctx, "%s:%u: instructions are not allowed in SHT_NOBITS section %s",
                        st->file != NULL ? st->file : "<input>", st->line, sec->name);
                goto fail;
            }
            if (instruction_has_symbolic_reloc(&st->u.instr)) {
                set_err(&ctx, "%s:%u: unresolved relocation in instruction is not allowed with -O binary",
                        st->file != NULL ? st->file : "<input>", st->line);
                goto fail;
            }
            if (encode_x86_stmt_for_layout(&ctx, sec->name, (uint64_t)sec->buf.len, cur_code_bits,
                                           st, code, sizeof(code), &code_len, encerr, sizeof(encerr)) != 0) {
                set_err(&ctx, "%s:%u: %s", st->file != NULL ? st->file : "<input>", st->line, encerr);
                goto fail;
            }
            if (bytebuf_append(&sec->buf, code, code_len) != 0) {
                set_err(&ctx, "%s:%u: out of memory", st->file != NULL ? st->file : "<input>", st->line);
                goto fail;
            }
            sec->touched = 1;
            sec->after_data = 0;
        }
    }

    if (write_binary_file(&ctx, &sv, out_path) != 0) {
        goto fail;
    }
    bin_section_vec_free(&sv);
    return 0;

fail:
    bin_section_vec_free(&sv);
    return -1;
}
