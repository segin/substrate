/*
 * ld_script.c -- linker scripts: lexer, expressions, parser, and what a script does to a link.
 */

#include "ld.h"

static int lds_eval_unary_nested(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);
static int lds_eval_cond(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);
static int lds_eval_expr_slice(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t begin, size_t end, uint64_t *out);
static int lds_parse_file(lds_script_t *sc, const ld_ctx_t *ctx, strvec_t *include_stack, const char *path,
                          lds_where_t where, lds_stmtvec_t *v, int depth);
static lds_stmt_t *script_find_outsec(lds_script_t *sc, const char *name, size_t *index);

static void lds_tok_free(lds_tok_t *tok) {
    if (tok == NULL) {
        return;
    }
    free(tok->text);
    tok->text = NULL;
}

static int lds_tok_dup(lds_tok_t *dst, const lds_tok_t *src) {
    if (dst == NULL || src == NULL) {
        return -1;
    }
    memset(dst, 0, sizeof(*dst));
    dst->kind = src->kind;
    dst->path = src->path;
    dst->line = src->line;
    dst->col = src->col;
    if (src->text != NULL) {
        dst->text = xstrdup(src->text);
        if (dst->text == NULL) {
            return -1;
        }
    }
    return 0;
}

static void lds_tokvec_free(lds_tokvec_t *v) {
    size_t i;

    if (v == NULL) {
        return;
    }
    for (i = 0; i < v->count; ++i) {
        lds_tok_free(&v->items[i]);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static int lds_tokvec_push(lds_tokvec_t *v, const lds_tok_t *tok) {
    lds_tok_t *next;

    if (v == NULL || tok == NULL) {
        return -1;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 16 : v->cap * 2;
        next = (lds_tok_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }
    if (lds_tok_dup(&v->items[v->count], tok) != 0) {
        return -1;
    }
    v->count++;
    return 0;
}

static void lds_phdr_vec_free(lds_phdr_vec_t *v) {
    size_t i;

    if (v == NULL) {
        return;
    }
    for (i = 0; i < v->count; ++i) {
        free(v->items[i].name);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static int lds_phdr_vec_push(lds_phdr_vec_t *v, const char *name, uint32_t type, uint32_t flags, uint64_t align) {
    lds_phdr_entry_t *next;

    if (v == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 8 : v->cap * 2;
        next = (lds_phdr_entry_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }
    v->items[v->count].name = xstrdup(name);
    if (v->items[v->count].name == NULL) {
        return -1;
    }
    v->items[v->count].type = type;
    v->items[v->count].flags = flags;
    v->items[v->count].align = align;
    v->count++;
    return 0;
}

static int lds_phdr_vec_find(const lds_phdr_vec_t *v, const char *name) {
    size_t i;

    if (v == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < v->count; ++i) {
        if (v->items[i].name != NULL && strcmp(v->items[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int align_u64(uint64_t value, uint64_t align, uint64_t *out) {
    uint64_t rem;
    uint64_t add;

    if (out == NULL || align == 0) {
        return -1;
    }
    rem = value % align;
    if (rem == 0) {
        *out = value;
        return 0;
    }
    add = align - rem;
    if (value > UINT64_MAX - add) {
        return -1;
    }
    *out = value + add;
    return 0;
}

static int lds_tok_is(const lds_tok_t *tok, lds_tok_kind_t kind, const char *text) {
    if (tok == NULL || tok->kind != kind) {
        return 0;
    }
    if (text == NULL) {
        return 1;
    }
    return tok->text != NULL && strcmp(tok->text, text) == 0;
}

static int script_lookup_symbol_value(const lds_eval_ctx_t *ec, const char *name, uint64_t *out) {
    if (ec == NULL || ec->ctx == NULL || name == NULL || out == NULL) {
        return -1;
    }
    if (defsymvec_get(&ec->ctx->defsyms, name, out) == 0 ||
        (ec->ctx->script != NULL && defsymvec_get(&ec->ctx->script->locals, name, out) == 0)) {
        return 0;
    }
    if (ec->obj != NULL) {
        /* A symbol of the program: its address, the layout being done by
         * the time a script's expressions are evaluated. */
        const elf_symbol_t *sym = elf_find_symbol((elfobj_t *)ec->obj, name);
        const char *undef = NULL;

        if (sym != NULL && elf_symbol_shndx(sym) != SHN_UNDEF &&
            resolve_symbol_addr((elfobj_t *)ec->obj, sym, 0, out, &undef) == 0) {
            return 0;
        }
    }
    return -1;
}

/* Whether a symbol has a definition: the script's, or an input's. */
static int script_symbol_defined(const lds_eval_ctx_t *ec, const char *name) {
    const elf_symbol_t *sym;

    if (ec == NULL || ec->ctx == NULL || name == NULL) {
        return 0;
    }
    if (defsymvec_find(&ec->ctx->defsyms, name) >= 0) {
        return 1;
    }
    sym = ec->obj != NULL ? elf_find_symbol((elfobj_t *)ec->obj, name) : NULL;
    return sym != NULL && elf_symbol_shndx(sym) != SHN_UNDEF;
}

static int script_lookup_section_metric(const lds_eval_ctx_t *ec, const char *name, int metric, uint64_t *out) {
    elf_section_t *sec;

    if (ec == NULL || name == NULL || out == NULL || ec->obj == NULL) {
        if (out != NULL) {
            *out = 0;
        }
        return 0;
    }
    sec = elf_find_section((elfobj_t *)ec->obj, name);
    if (sec == NULL) {
        *out = 0;
        return 0;
    }
    if (metric == 0) {
        *out = elf_section_addr(sec);
    } else if (metric == 1) {
        *out = elf_section_size(sec);
    } else {
        *out = elf_section_addr(sec);
    }
    return 0;
}

static int lds_find_matching_rparen(const lds_tok_t *items, size_t begin, size_t end, size_t *close_idx) {
    size_t i;
    int depth = 0;

    if (items == NULL || close_idx == NULL || begin >= end || items[begin].kind != LDS_TOK_LPAREN) {
        return -1;
    }
    for (i = begin; i < end; ++i) {
        if (items[i].kind == LDS_TOK_LPAREN) {
            depth++;
        } else if (items[i].kind == LDS_TOK_RPAREN) {
            depth--;
            if (depth == 0) {
                *close_idx = i;
                return 0;
            }
            if (depth < 0) {
                return -1;
            }
        }
    }
    return -1;
}

static int lds_eval_builtin_call(lds_eval_ctx_t *ec, const lds_tok_t *name_tok, const lds_tok_t *items, size_t begin,
                                 size_t end, uint64_t *out) {
    const char *name;
    size_t open_idx;
    size_t close_idx;
    size_t arg_starts[8];
    size_t arg_ends[8];
    size_t arg_count = 0;
    size_t i;
    int depth = 0;
    int takes_name;
    const char *word;           /* the argument, of a function that takes a name */
    uint64_t vals[8];

    if (ec == NULL || name_tok == NULL || name_tok->text == NULL || items == NULL || out == NULL || begin >= end) {
        return -1;
    }
    name = name_tok->text;
    open_idx = begin;
    if (items[open_idx].kind != LDS_TOK_LPAREN || lds_find_matching_rparen(items, open_idx, end, &close_idx) != 0) {
        ec->err_tok = name_tok;
        ec->err_msg = "malformed function call";
        return -1;
    }

    if (open_idx + 1 <= close_idx) {
        size_t start = open_idx + 1;
        for (i = open_idx + 1; i < close_idx; ++i) {
            if (items[i].kind == LDS_TOK_LPAREN) {
                depth++;
            } else if (items[i].kind == LDS_TOK_RPAREN) {
                depth--;
            } else if (items[i].kind == LDS_TOK_COMMA && depth == 0) {
                if (arg_count >= sizeof(arg_starts) / sizeof(arg_starts[0])) {
                    ec->err_tok = &items[i];
                    ec->err_msg = "too many arguments";
                    return -1;
                }
                arg_starts[arg_count] = start;
                arg_ends[arg_count] = i;
                arg_count++;
                start = i + 1;
            }
        }
        if (start < close_idx || (close_idx == open_idx + 1 && items[open_idx + 1].kind != LDS_TOK_RPAREN)) {
            if (arg_count >= sizeof(arg_starts) / sizeof(arg_starts[0])) {
                ec->err_tok = &items[open_idx];
                ec->err_msg = "too many arguments";
                return -1;
            }
            arg_starts[arg_count] = start;
            arg_ends[arg_count] = close_idx;
            arg_count++;
        }
    }

    /* The argument of these is a name, of a section or a symbol, and not
     * something that has a value. */
    takes_name = strcmp(name, "ADDR") == 0 || strcmp(name, "LOADADDR") == 0 || strcmp(name, "SIZEOF") == 0 ||
                 strcmp(name, "ALIGNOF") == 0 || strcmp(name, "CONSTANT") == 0 || strcmp(name, "ORIGIN") == 0 ||
                 strcmp(name, "LENGTH") == 0 || strcmp(name, "DEFINED") == 0 || strcmp(name, "defined") == 0;
    word = takes_name && arg_count == 1 && arg_starts[0] + 1 == arg_ends[0] &&
           (items[arg_starts[0]].kind == LDS_TOK_IDENT || items[arg_starts[0]].kind == LDS_TOK_STRING)
               ? items[arg_starts[0]].text : NULL;
    for (i = 0; !takes_name && i < arg_count; ++i) {
        if (lds_eval_expr_slice(ec, items, arg_starts[i], arg_ends[i], &vals[i]) != 0) {
            return -1;
        }
    }

    if (strcmp(name, "ALIGN") == 0) {
        if (arg_count == 1) {
            /* ALIGN(n) is the location counter, aligned. */
            if (!ec->have_dot) {
                ec->err_tok = name_tok;
                ec->err_msg = "ALIGN(n) aligns the location counter, which has no value outside SECTIONS";
                return -1;
            }
            if (align_u64(ec->dot, vals[0], out) != 0) {
                ec->err_tok = name_tok;
                ec->err_msg = "ALIGN argument must be non-zero";
                return -1;
            }
        } else if (arg_count == 2) {
            if (align_u64(vals[0], vals[1], out) != 0) {
                ec->err_tok = name_tok;
                ec->err_msg = "invalid ALIGN arguments";
                return -1;
            }
        } else {
            ec->err_tok = name_tok;
            ec->err_msg = "ALIGN expects one or two arguments";
            return -1;
        }
    } else if (strcmp(name, "ADDR") == 0 || strcmp(name, "LOADADDR") == 0 || strcmp(name, "SIZEOF") == 0) {
        const char *section_name = NULL;
        int metric = strcmp(name, "SIZEOF") == 0 ? 1 : 0;
        if (arg_count != 1) {
            ec->err_tok = name_tok;
            ec->err_msg = "section builtin expects one argument";
            return -1;
        }
        if (arg_starts[0] < arg_ends[0]) {
            const lds_tok_t *at = &items[arg_starts[0]];
            if (at->kind == LDS_TOK_IDENT || at->kind == LDS_TOK_STRING) {
                section_name = at->text;
            }
        }
        if (section_name == NULL) {
            *out = 0;
        } else if (script_lookup_section_metric(ec, section_name, metric, out) != 0) {
            ec->err_tok = name_tok;
            ec->err_msg = "failed to resolve section builtin";
            return -1;
        }
    } else if (strcmp(name, "DEFINED") == 0 || strcmp(name, "defined") == 0) {
        const char *sym = NULL;
        if (arg_count != 1 || arg_starts[0] >= arg_ends[0]) {
            ec->err_tok = name_tok;
            ec->err_msg = "DEFINED expects one symbol argument";
            return -1;
        }
        if (items[arg_starts[0]].kind == LDS_TOK_IDENT || items[arg_starts[0]].kind == LDS_TOK_STRING) {
            sym = items[arg_starts[0]].text;
        }
        *out = sym != NULL && script_symbol_defined(ec, sym) ? 1 : 0;
    } else if ((strcmp(name, "MAX") == 0 || strcmp(name, "MIN") == 0) && arg_count == 2) {
        *out = (name[1] == 'A') == (vals[0] > vals[1]) ? vals[0] : vals[1];
    } else if ((strcmp(name, "ABSOLUTE") == 0 || strcmp(name, "DATA_SEGMENT_END") == 0) && arg_count == 1) {
        *out = vals[0];
    } else if ((strcmp(name, "SEGMENT_START") == 0 || strcmp(name, "DATA_SEGMENT_RELRO_END") == 0) &&
               arg_count == 2) {
        /* No -T<segment> option overrides the default; no gap is left
         * after the read-only part of the data. */
        *out = vals[1];
    } else if (strcmp(name, "DATA_SEGMENT_ALIGN") == 0 && arg_count == 2 && vals[0] != 0 && ec->have_dot) {
        /* The next page, at the same place within it: the data begins
         * where the text ended in the file, a page further on in memory. */
        if (align_u64(ec->dot, vals[0], out) != 0) {
            ec->err_tok = name_tok;
            ec->err_msg = "invalid DATA_SEGMENT_ALIGN arguments";
            return -1;
        }
        *out += ec->dot & (vals[0] - 1);
    } else if (strcmp(name, "CONSTANT") == 0 && word != NULL &&
               (strcmp(word, "MAXPAGESIZE") == 0 || strcmp(word, "COMMONPAGESIZE") == 0)) {
        *out = 0x1000u;
    } else if (strcmp(name, "ALIGNOF") == 0 && word != NULL) {
        elf_section_t *sec = ec->obj != NULL ? elf_find_section((elfobj_t *)ec->obj, word) : NULL;

        *out = sec != NULL ? elf_section_align(sec) : 0;
    } else if ((strcmp(name, "ORIGIN") == 0 || strcmp(name, "LENGTH") == 0) && word != NULL) {
        const lds_script_t *sc = ec->ctx != NULL ? ec->ctx->script : NULL;
        size_t r;

        for (r = 0; sc != NULL && r < sc->region_count && strcmp(sc->regions[r].name, word) != 0; ++r) {
        }
        if (sc == NULL || r == sc->region_count) {
            snprintf(ec->err_buf, sizeof(ec->err_buf), "%s of '%s', which MEMORY does not define", name, word);
            ec->err_tok = name_tok;
            ec->err_msg = ec->err_buf;
            return -1;
        }
        *out = name[0] == 'O' ? sc->regions[r].org : sc->regions[r].len;
    } else {
        snprintf(ec->err_buf, sizeof(ec->err_buf), "%s(): not a function of this linker, or not with these arguments",
                 name);
        ec->err_tok = name_tok;
        ec->err_msg = ec->err_buf;
        return -1;
    }
    return (int)(close_idx + 1);
}

static int lds_eval_primary(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    const lds_tok_t *tok;
    char *num_end;
    unsigned long long parsed;
    uint64_t inner = 0;

    if (idx == NULL || out == NULL || *idx >= end) {
        if (ec != NULL && idx != NULL && *idx < end) {
            ec->err_tok = &items[*idx];
        }
        if (ec != NULL) {
            ec->err_msg = "unexpected end of expression";
        }
        return -1;
    }
    tok = &items[*idx];
    if (tok->kind == LDS_TOK_NUMBER && tok->text != NULL) {
        errno = 0;
        parsed = strtoull(tok->text, &num_end, 0);
        /* 64K, 2M: kilobytes and megabytes. */
        if (errno == 0 && num_end != tok->text && num_end[0] != '\0' && num_end[1] == '\0') {
            unsigned shift = (num_end[0] == 'K' || num_end[0] == 'k') ? 10
                           : (num_end[0] == 'M' || num_end[0] == 'm') ? 20 : 0;

            if (shift != 0 && parsed <= (~0ULL >> shift)) {
                parsed <<= shift;
                num_end++;
            }
        }
        if (errno != 0 || num_end == tok->text || *num_end != '\0') {
            ec->err_tok = tok;
            ec->err_msg = "invalid integer literal";
            return -1;
        }
        *out = (uint64_t)parsed;
        (*idx)++;
        return 0;
    }
    if (tok->kind == LDS_TOK_LPAREN) {
        (*idx)++;
        if (lds_eval_cond(ec, items, idx, end, &inner) != 0) {
            return -1;
        }
        if (*idx >= end || items[*idx].kind != LDS_TOK_RPAREN) {
            ec->err_tok = tok;
            ec->err_msg = "expected ')'";
            return -1;
        }
        (*idx)++;
        *out = inner;
        return 0;
    }
    if (tok->kind == LDS_TOK_IDENT && tok->text != NULL && strcmp(tok->text, ".") == 0) {
        if (!ec->have_dot) {
            ec->err_tok = tok;
            ec->err_msg = "the location counter has no value outside SECTIONS";
            return -1;
        }
        *out = ec->dot;
        (*idx)++;
        return 0;
    }
    if (tok->kind == LDS_TOK_IDENT && tok->text != NULL) {
        if (*idx + 1 < end && items[*idx + 1].kind == LDS_TOK_LPAREN) {
            int consumed = lds_eval_builtin_call(ec, tok, items, *idx + 1, end, out);
            if (consumed < 0) {
                return -1;
            }
            *idx = (size_t)consumed;
            return 0;
        }
        if (strcmp(tok->text, "SIZEOF_HEADERS") == 0 || strcmp(tok->text, "sizeof_headers") == 0) {
            /* The ELF header and the program headers, which begin the file. */
            int wide = ec->obj != NULL && elf_class(ec->obj) == ELFOBJ_CLASS_64;

            *out = (wide ? 64u : 52u) +
                   (uint64_t)(ec->obj != NULL ? elf_segment_count(ec->obj) : 0) * (wide ? 56u : 32u);
            (*idx)++;
            return 0;
        }
        if (script_lookup_symbol_value(ec, tok->text, out) != 0 && ec->skip != 0) {
            /* In the arm of a conditional that is not taken. */
            *out = 0;
            (*idx)++;
            return 0;
        }
        if (script_lookup_symbol_value(ec, tok->text, out) != 0) {
            snprintf(ec->err_buf, sizeof(ec->err_buf), "undefined symbol '%s' in expression", tok->text);
            ec->err_tok = tok;
            ec->err_msg = ec->err_buf;
            return -1;
        }
        (*idx)++;
        return 0;
    }
    if (tok->kind == LDS_TOK_STRING) {
        *out = 0;
        (*idx)++;
        return 0;
    }
    ec->err_tok = tok;
    ec->err_msg = "unexpected token in expression";
    return -1;
}

/*
 * Every level of nesting an expression can have -- a parenthesis, a unary
 * operator, an argument of a builtin -- comes through here, so this is
 * where the depth is counted.  The evaluator recurses on the C stack and a
 * script is input: a megabyte of '(' must be a syntax error.
 */
static int lds_eval_unary(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    int rc;

    if (ec->depth >= LD_MAX_SCRIPT_EXPR_DEPTH) {
        ec->err_tok = *idx < end ? &items[*idx] : NULL;
        ec->err_msg = "expression nested too deeply";
        return -1;
    }
    ec->depth++;
    rc = lds_eval_unary_nested(ec, items, idx, end, out);
    ec->depth--;
    return rc;
}

static int lds_eval_unary_nested(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "+")) {
        (*idx)++;
        return lds_eval_unary(ec, items, idx, end, out);
    }
    if (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "-")) {
        uint64_t v = 0;
        (*idx)++;
        if (lds_eval_unary(ec, items, idx, end, &v) != 0) {
            return -1;
        }
        *out = (uint64_t)(0ULL - v);
        return 0;
    }
    if (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "~")) {
        uint64_t v = 0;
        (*idx)++;
        if (lds_eval_unary(ec, items, idx, end, &v) != 0) {
            return -1;
        }
        *out = ~v;
        return 0;
    }
    if (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "!")) {
        uint64_t v = 0;
        (*idx)++;
        if (lds_eval_unary(ec, items, idx, end, &v) != 0) {
            return -1;
        }
        *out = v == 0 ? 1 : 0;
        return 0;
    }
    return lds_eval_primary(ec, items, idx, end, out);
}

static int lds_eval_mul(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_unary(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end) {
        const lds_tok_t *op = &items[*idx];
        uint64_t rhs = 0;
        if (!lds_tok_is(op, LDS_TOK_OTHER, "*") && !lds_tok_is(op, LDS_TOK_OTHER, "/") &&
            !lds_tok_is(op, LDS_TOK_OTHER, "%")) {
            break;
        }
        (*idx)++;
        if (lds_eval_unary(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        if (lds_tok_is(op, LDS_TOK_OTHER, "*")) {
            *out = (*out) * rhs;
        } else if (lds_tok_is(op, LDS_TOK_OTHER, "/")) {
            if (rhs == 0 && ec->skip == 0) {
                ec->err_tok = op;
                ec->err_msg = "division by zero";
                return -1;
            }
            *out = rhs != 0 ? (*out) / rhs : 0;
        } else {
            if (rhs == 0 && ec->skip == 0) {
                ec->err_tok = op;
                ec->err_msg = "modulo by zero";
                return -1;
            }
            *out = rhs != 0 ? (*out) % rhs : 0;
        }
    }
    return 0;
}

static int lds_eval_add(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_mul(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end) {
        const lds_tok_t *op = &items[*idx];
        uint64_t rhs = 0;
        if (!lds_tok_is(op, LDS_TOK_OTHER, "+") && !lds_tok_is(op, LDS_TOK_OTHER, "-")) {
            break;
        }
        (*idx)++;
        if (lds_eval_mul(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        if (lds_tok_is(op, LDS_TOK_OTHER, "+")) {
            *out = (*out) + rhs;
        } else {
            *out = (*out) - rhs;
        }
    }
    return 0;
}

static int lds_eval_shift(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_add(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end) {
        const lds_tok_t *op = &items[*idx];
        uint64_t rhs = 0;
        if (!lds_tok_is(op, LDS_TOK_OTHER, "<<") && !lds_tok_is(op, LDS_TOK_OTHER, ">>")) {
            break;
        }
        (*idx)++;
        if (lds_eval_add(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        rhs &= 63;
        if (lds_tok_is(op, LDS_TOK_OTHER, "<<")) {
            *out <<= rhs;
        } else {
            *out >>= rhs;
        }
    }
    return 0;
}

static int lds_eval_rel(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_shift(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end) {
        const lds_tok_t *op = &items[*idx];
        uint64_t rhs = 0;
        if (!lds_tok_is(op, LDS_TOK_OTHER, "<") && !lds_tok_is(op, LDS_TOK_OTHER, ">") &&
            !lds_tok_is(op, LDS_TOK_OTHER, "<=") && !lds_tok_is(op, LDS_TOK_OTHER, ">=")) {
            break;
        }
        (*idx)++;
        if (lds_eval_shift(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        if (lds_tok_is(op, LDS_TOK_OTHER, "<")) {
            *out = (*out < rhs) ? 1 : 0;
        } else if (lds_tok_is(op, LDS_TOK_OTHER, ">")) {
            *out = (*out > rhs) ? 1 : 0;
        } else if (lds_tok_is(op, LDS_TOK_OTHER, "<=")) {
            *out = (*out <= rhs) ? 1 : 0;
        } else {
            *out = (*out >= rhs) ? 1 : 0;
        }
    }
    return 0;
}

static int lds_eval_eq(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_rel(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end) {
        const lds_tok_t *op = &items[*idx];
        uint64_t rhs = 0;
        if (!lds_tok_is(op, LDS_TOK_OTHER, "==") && !lds_tok_is(op, LDS_TOK_OTHER, "!=")) {
            break;
        }
        (*idx)++;
        if (lds_eval_rel(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        if (lds_tok_is(op, LDS_TOK_OTHER, "==")) {
            *out = (*out == rhs) ? 1 : 0;
        } else {
            *out = (*out != rhs) ? 1 : 0;
        }
    }
    return 0;
}

static int lds_eval_band(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_eq(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "&")) {
        uint64_t rhs = 0;
        (*idx)++;
        if (lds_eval_eq(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        *out &= rhs;
    }
    return 0;
}

static int lds_eval_bxor(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_band(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "^")) {
        uint64_t rhs = 0;
        (*idx)++;
        if (lds_eval_band(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        *out ^= rhs;
    }
    return 0;
}

static int lds_eval_bor(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_bxor(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "|")) {
        uint64_t rhs = 0;
        (*idx)++;
        if (lds_eval_bxor(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        *out |= rhs;
    }
    return 0;
}

static int lds_eval_land(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_bor(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "&&")) {
        uint64_t rhs = 0;
        (*idx)++;
        if (lds_eval_bor(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        *out = ((*out != 0) && (rhs != 0)) ? 1 : 0;
    }
    return 0;
}

static int lds_eval_lor(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_land(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "||")) {
        uint64_t rhs = 0;
        (*idx)++;
        if (lds_eval_land(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        *out = ((*out != 0) || (rhs != 0)) ? 1 : 0;
    }
    return 0;
}

/*
 * cond ? a : b.  The arm not taken is read, to find where it ends, with
 * its errors off: "DEFINED(x) ? x : 0" is what the operator is for.
 */
static int lds_eval_cond(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    uint64_t a = 0, b = 0;
    int rc;

    if (lds_eval_lor(ec, items, idx, end, out) != 0) {
        return -1;
    }
    if (*idx >= end || !lds_tok_is(&items[*idx], LDS_TOK_OTHER, "?")) {
        return 0;
    }
    if (ec->depth >= LD_MAX_SCRIPT_EXPR_DEPTH) {
        ec->err_tok = &items[*idx];
        ec->err_msg = "expression nested too deeply";
        return -1;
    }
    (*idx)++;
    ec->depth++;
    ec->skip += *out == 0;
    rc = lds_eval_cond(ec, items, idx, end, &a);
    ec->skip -= *out == 0;
    if (rc == 0 && (*idx >= end || items[*idx].kind != LDS_TOK_COLON)) {
        ec->err_tok = *idx < end ? &items[*idx] : &items[end - 1];
        ec->err_msg = "expected ':' of the conditional";
        rc = -1;
    }
    if (rc == 0) {
        (*idx)++;
        ec->skip += *out != 0;
        rc = lds_eval_cond(ec, items, idx, end, &b);
        ec->skip -= *out != 0;
    }
    ec->depth--;
    if (rc == 0) {
        *out = *out != 0 ? a : b;
    }
    return rc;
}

static int lds_eval_expr_slice(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t begin, size_t end, uint64_t *out) {
    size_t idx = begin;

    if (ec == NULL || items == NULL || out == NULL || begin > end) {
        return -1;
    }
    if (begin == end) {
        ec->err_tok = begin < end ? &items[begin] : NULL;
        ec->err_msg = "empty expression";
        return -1;
    }
    if (lds_eval_cond(ec, items, &idx, end, out) != 0) {
        return -1;
    }
    if (idx != end) {
        ec->err_tok = &items[idx];
        ec->err_msg = "unexpected trailing tokens in expression";
        return -1;
    }
    return 0;
}

static int lds_lex_push_text(lds_tok_t *tok, const unsigned char *start, size_t n) {
    if (n > LD_MAX_SCRIPT_TOKEN_LEN) {
        return -1;
    }
    tok->text = (char *)malloc(n + 1);
    if (tok->text == NULL) {
        return -1;
    }
    memcpy(tok->text, start, n);
    tok->text[n] = '\0';
    return 0;
}

static int lds_is_ident_start(int c) {
    return isalpha(c) || c == '_' || c == '.' || c == '$';
}

static int lds_is_ident_char(int c) {
    return isalnum(c) || c == '_' || c == '.' || c == '$';
}

/* A character of a file or section name, wildcards included. */
static int lds_is_name_char(int c) {
    return c > 0 && (isalnum(c) || strchr("_.$/-+*?[]!~\\^@%", c) != NULL);
}

static void lds_advance(lds_lexer_t *lx) {
    if (lx->pos >= lx->len) {
        return;
    }
    if (lx->buf[lx->pos] == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    lx->pos++;
}

static int lds_peek(const lds_lexer_t *lx, size_t off) {
    if (lx->pos + off >= lx->len) {
        return -1;
    }
    return lx->buf[lx->pos + off];
}

static int lds_skip_ws_comments(lds_lexer_t *lx) {
    for (;;) {
        int c = lds_peek(lx, 0);
        if (c < 0) {
            return 0;
        }
        if (isspace(c)) {
            lds_advance(lx);
            continue;
        }
        if (c == '/' && lds_peek(lx, 1) == '/') {
            while ((c = lds_peek(lx, 0)) >= 0 && c != '\n') {
                lds_advance(lx);
            }
            continue;
        }
        if (c == '/' && lds_peek(lx, 1) == '*') {
            lds_advance(lx);
            lds_advance(lx);
            while ((c = lds_peek(lx, 0)) >= 0) {
                if (c == '*' && lds_peek(lx, 1) == '/') {
                    lds_advance(lx);
                    lds_advance(lx);
                    break;
                }
                lds_advance(lx);
            }
            if (c < 0) {
                return -1;
            }
            continue;
        }
        return 0;
    }
}

static int lds_lex_next(lds_lexer_t *lx, lds_tok_t *out) {
    size_t start;
    int c;

    memset(out, 0, sizeof(*out));
    out->path = lx->path;
    out->line = lx->line;
    out->col = lx->col;
    if (lds_skip_ws_comments(lx) != 0) {
        out->kind = LDS_TOK_OTHER;
        out->text = xstrdup("<unterminated-comment>");
        out->line = lx->line;
        out->col = lx->col;
        return out->text == NULL ? -1 : 0;
    }
    out->path = lx->path;
    out->line = lx->line;
    out->col = lx->col;
    c = lds_peek(lx, 0);
    if (c < 0) {
        out->kind = LDS_TOK_EOF;
        return 0;
    }
    if (lx->names && lds_is_name_char(c)) {
        start = lx->pos;
        while ((c = lds_peek(lx, 0)) >= 0 && lds_is_name_char(c)) {
            /* A comment directly after a name ends the name. */
            if (c == '/' && (lds_peek(lx, 1) == '*' || lds_peek(lx, 1) == '/') && lx->pos > start) {
                break;
            }
            lds_advance(lx);
        }
        out->kind = LDS_TOK_IDENT;
        return lds_lex_push_text(out, lx->buf + start, lx->pos - start);
    }
    if (lds_is_ident_start(c)) {
        start = lx->pos;
        while ((c = lds_peek(lx, 0)) >= 0 && lds_is_ident_char(c)) {
            lds_advance(lx);
        }
        out->kind = LDS_TOK_IDENT;
        return lds_lex_push_text(out, lx->buf + start, lx->pos - start);
    }
    if (isdigit(c)) {
        start = lx->pos;
        while ((c = lds_peek(lx, 0)) >= 0 && (isalnum(c) || c == 'x' || c == 'X')) {
            lds_advance(lx);
        }
        out->kind = LDS_TOK_NUMBER;
        return lds_lex_push_text(out, lx->buf + start, lx->pos - start);
    }
    if (c == '"') {
        start = ++lx->pos;
        lx->col++;
        while ((c = lds_peek(lx, 0)) >= 0) {
            if (c == '"') {
                size_t end = lx->pos;
                lds_advance(lx);
                out->kind = LDS_TOK_STRING;
                return lds_lex_push_text(out, lx->buf + start, end - start);
            }
            if (c == '\\' && lds_peek(lx, 1) >= 0) {
                lds_advance(lx);
            }
            lds_advance(lx);
        }
        out->kind = LDS_TOK_OTHER;
        out->text = xstrdup("<unterminated-string>");
        return out->text == NULL ? -1 : 0;
    }

    if ((c == '=' && lds_peek(lx, 1) == '=') || (c == '!' && lds_peek(lx, 1) == '=') ||
        (c == '<' && (lds_peek(lx, 1) == '=' || lds_peek(lx, 1) == '<')) ||
        (c == '>' && (lds_peek(lx, 1) == '=' || lds_peek(lx, 1) == '>')) ||
        (c == '&' && lds_peek(lx, 1) == '&') || (c == '|' && lds_peek(lx, 1) == '|')) {
        char op[3];
        op[0] = (char)c;
        op[1] = (char)lds_peek(lx, 1);
        op[2] = '\0';
        lds_advance(lx);
        lds_advance(lx);
        out->kind = LDS_TOK_OTHER;
        out->text = xstrdup(op);
        return out->text == NULL ? -1 : 0;
    }

    lds_advance(lx);
    switch (c) {
    case '{': out->kind = LDS_TOK_LBRACE; break;
    case '}': out->kind = LDS_TOK_RBRACE; break;
    case '(': out->kind = LDS_TOK_LPAREN; break;
    case ')': out->kind = LDS_TOK_RPAREN; break;
    case ';': out->kind = LDS_TOK_SEMI; break;
    case ':': out->kind = LDS_TOK_COLON; break;
    case ',': out->kind = LDS_TOK_COMMA; break;
    case '=': out->kind = LDS_TOK_EQUAL; break;
    default:
        out->kind = LDS_TOK_OTHER;
        out->text = (char *)malloc(2);
        if (out->text == NULL) {
            return -1;
        }
        out->text[0] = (char)c;
        out->text[1] = '\0';
        break;
    }
    return 0;
}

static void lds_report_error(const strvec_t *include_stack, const lds_tok_t *tok, const char *msg) {
    size_t i;
    const char *path = tok != NULL && tok->path != NULL ? tok->path : "<script>";
    size_t line = tok != NULL ? tok->line : 1;
    size_t col = tok != NULL ? tok->col : 1;

    fprintf(stderr, "ld: %s:%zu:%zu: linker script parse error: %s\n", path, line, col, msg != NULL ? msg : "error");
    ld_diag_note("script-parse", path, "check linker script syntax and block delimiters");
    if (include_stack != NULL && include_stack->count > 1) {
        fprintf(stderr, "ld: include stack:\n");
        for (i = 0; i < include_stack->count; ++i) {
            fprintf(stderr, "ld:   %s\n", include_stack->items[i]);
        }
    }
}

static char *dirname_copy(const char *path) {
    const char *slash;
    size_t n;
    char *out;

    if (path == NULL || path[0] == '\0') {
        return xstrdup(".");
    }
    slash = strrchr(path, '/');
    if (slash == NULL) {
        return xstrdup(".");
    }
    n = (size_t)(slash - path);
    if (n == 0) {
        return xstrdup("/");
    }
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, path, n);
    out[n] = '\0';
    return out;
}

static char *resolve_script_include_path(const char *parent_path, const char *name) {
    char *dir;
    char *joined;

    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    if (name[0] == '/') {
        return xstrdup(name);
    }
    dir = dirname_copy(parent_path);
    if (dir == NULL) {
        return NULL;
    }
    joined = path_join(dir, name);
    free(dir);
    return joined;
}

/*
 * A script's wildcard against a name: '*' is any run of characters, '?'
 * any one, [abc] [a-z] [!a-z] one of a set, '\' takes the next character
 * as itself.  A '*' matches '/' too, as it does for GNU ld.
 */
static int lds_glob(const char *pat, const char *s) {
    const char *star = NULL;
    const char *resume = NULL;

    if (pat == NULL || s == NULL) {
        return 0;
    }
    for (;;) {
        if (*pat == '*') {
            star = ++pat;
            resume = s;
            continue;
        }
        if (*s == '\0') {
            return *pat == '\0';
        }
        if (*pat == '[' && strchr(pat + 2, ']') != NULL) {
            const char *q = pat + 1;
            int negate = *q == '!' || *q == '^';
            int hit = 0;

            if (negate) {
                q++;
            }
            do {
                if (q[1] == '-' && q[2] != ']' && q[2] != '\0') {
                    hit |= (unsigned char)*s >= (unsigned char)q[0] && (unsigned char)*s <= (unsigned char)q[2];
                    q += 3;
                } else {
                    hit |= *s == *q;
                    q++;
                }
            } while (*q != ']' && *q != '\0');
            if (*q == ']' && hit != negate) {
                pat = q + 1;
                s++;
                continue;
            }
        } else if (*pat == '?' || (*pat == '\\' && pat[1] != '\0' && pat[1] == *s) ||
                   (*pat != '\\' && *pat != '\0' && *pat == *s)) {
            pat += *pat == '\\' ? 2 : 1;
            s++;
            continue;
        }
        if (star == NULL) {
            return 0;
        }
        pat = star;
        s = ++resume;
    }
}

static int script_section_pattern_match(const char *pattern, const char *name) {
    return lds_glob(pattern, name);
}

static int apply_script_keep_pattern(elfobj_t *obj, const char *pattern) {
    size_t i;
    size_t count;

    if (obj == NULL || pattern == NULL || pattern[0] == '\0') {
        return 0;
    }
    count = elf_section_count(obj);
    for (i = 0; i < count; ++i) {
        elf_section_t *sec = elf_section_get(obj, i);
        const char *name = sec != NULL ? elf_section_name(sec) : NULL;
        uint64_t flags;
        if (name == NULL || !script_section_pattern_match(pattern, name)) {
            continue;
        }
        flags = elf_section_flags(sec);
        if ((flags & SHF_GNU_RETAIN) == 0 && elf_section_set_flags(sec, flags | SHF_GNU_RETAIN) != ELF_OK) {
            return -1;
        }
    }
    return 0;
}

static int is_protected_output_section_name(const char *name) {
    if (name == NULL) {
        return 1;
    }
    return strcmp(name, ".shstrtab") == 0 || strcmp(name, ".symtab") == 0 || strcmp(name, ".strtab") == 0;
}

static int apply_script_discard_pattern(elfobj_t *obj, const char *pattern) {
    size_t i;

    if (obj == NULL || pattern == NULL || pattern[0] == '\0') {
        return 0;
    }
    for (i = elf_section_count(obj); i > 0; --i) {
        size_t idx = i - 1;
        elf_section_t *sec = elf_section_get(obj, idx);
        const char *name = sec != NULL ? elf_section_name(sec) : NULL;
        if (sec == NULL || name == NULL || is_protected_output_section_name(name)) {
            continue;
        }
        if (!script_section_pattern_match(pattern, name)) {
            continue;
        }
        if (elf_remove_section(obj, sec) != ELF_OK) {
            return -1;
        }
    }
    return 0;
}

static int phdr_type_from_token(const char *tok, uint32_t *out_type) {
    if (tok == NULL || out_type == NULL) {
        return -1;
    }
    if (strcmp(tok, "PT_LOAD") == 0) {
        *out_type = PT_LOAD;
    } else if (strcmp(tok, "PT_DYNAMIC") == 0) {
        *out_type = PT_DYNAMIC;
    } else if (strcmp(tok, "PT_NOTE") == 0) {
        *out_type = PT_NOTE;
    } else if (strcmp(tok, "PT_TLS") == 0) {
        *out_type = PT_TLS;
    } else if (strcmp(tok, "PT_GNU_EH_FRAME") == 0) {
        *out_type = PT_GNU_EH_FRAME;
    } else if (strcmp(tok, "PT_GNU_RELRO") == 0) {
        *out_type = PT_GNU_RELRO;
    } else if (strcmp(tok, "PT_GNU_STACK") == 0) {
        *out_type = PT_GNU_STACK;
    } else if (strcmp(tok, "PT_GNU_PROPERTY") == 0) {
        *out_type = PT_GNU_PROPERTY;
    } else if (strcmp(tok, "PT_INTERP") == 0) {
        *out_type = PT_INTERP;
    } else if (strcmp(tok, "PT_PHDR") == 0) {
        *out_type = PT_PHDR;
    } else {
        return -1;
    }
    return 0;
}

static uint32_t phdr_default_flags(uint32_t type) {
    if (type == PT_LOAD) {
        return LD_PF_R;
    }
    if (type == PT_DYNAMIC || type == PT_TLS || type == PT_GNU_STACK) {
        return LD_PF_R | LD_PF_W;
    }
    return LD_PF_R;
}

/*
 * The program headers a script's PHDRS declares, with the sections that
 * its SECTIONS puts in each.  A section goes in the headers named after
 * its output section (":text :note"), and one that names none goes where
 * the one before it went.  A PT_LOAD the script gives no FLAGS gets them
 * from what is in it.  1: done; 0: the script declares none.
 */
int add_script_segments(elfobj_t *obj, const ld_ctx_t *ctx) {
    lds_script_t *sc = ctx->script;
    const lds_phdr_vec_t *phdrs;
    const strvec_t *current = NULL;
    elf_segment_t **segs = NULL;
    uint32_t *flags = NULL;
    size_t i, k;
    int pass;
    int rc = -1;

    if (sc == NULL || sc->phdrs.count == 0) {
        return 0;
    }
    phdrs = &sc->phdrs;
    segs = (elf_segment_t **)calloc(phdrs->count, sizeof(*segs));
    flags = (uint32_t *)calloc(phdrs->count, sizeof(*flags));
    if (segs == NULL || flags == NULL) {
        goto done;
    }
    /* Once to learn each header's flags, once more to fill them. */
    for (pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            int have_phdr = 0, have_interp = 0;

            for (k = 0; k < phdrs->count; ++k) {
                have_phdr |= phdrs->items[k].type == PT_PHDR;
                have_interp |= phdrs->items[k].type == PT_INTERP;
            }
            if (!have_phdr && elf_add_segment(obj, PT_PHDR, LD_PF_R, 8) == NULL) {
                goto done;
            }
            if (!have_interp && ctx->interp_path != NULL && ctx->interp_path[0] != '\0' &&
                elf_add_interp_segment(obj, ctx->interp_path) == NULL) {
                goto done;
            }
            for (k = 0; k < phdrs->count; ++k) {
                const lds_phdr_entry_t *ph = &phdrs->items[k];

                if (ph->type == PT_INTERP && ctx->interp_path != NULL && ctx->interp_path[0] != '\0') {
                    segs[k] = elf_add_interp_segment(obj, ctx->interp_path);
                } else {
                    segs[k] = elf_add_segment(obj, ph->type,
                                              ph->has_flags || ph->type != PT_LOAD ? ph->flags : (flags[k] | LD_PF_R),
                                              ph->align);
                }
                if (segs[k] == NULL) {
                    goto done;
                }
            }
        }
        current = NULL;
        for (i = 0; i < elf_section_count(obj); ++i) {
            elf_section_t *sec = elf_section_get(obj, i);
            const char *name = sec != NULL ? elf_section_name(sec) : NULL;
            const lds_stmt_t *os;
            uint64_t sflags;

            if (name == NULL || ((sflags = elf_section_flags(sec)) & SHF_ALLOC) == 0) {
                continue;
            }
            os = script_find_outsec(sc, name, NULL);
            if (os != NULL && os->phdrs.count != 0) {
                current = &os->phdrs;
            }
            for (k = 0; current != NULL && k < current->count; ++k) {
                int pidx = lds_phdr_vec_find(phdrs, current->items[k]);

                if (pidx < 0) {
                    fprintf(stderr, "ld: section %s is assigned to program header '%s', which PHDRS does not declare\n",
                            name, current->items[k]);
                    goto done;
                }
                if (pass == 0) {
                    flags[pidx] |= ((sflags & SHF_WRITE) != 0 ? LD_PF_W : 0) |
                                   ((sflags & SHF_EXECINSTR) != 0 ? LD_PF_X : 0);
                } else if (phdrs->items[pidx].type != PT_INTERP &&
                           elf_segment_add_section(segs[pidx], sec) != ELF_OK) {
                    goto done;
                }
            }
        }
    }
    rc = 1;
done:
    free(segs);
    free(flags);
    return rc;
}

static void lds_stmtvec_free(lds_stmtvec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        lds_stmt_t *st = &v->items[i];

        lds_tok_free(&st->at);
        lds_tokvec_free(&st->expr);
        lds_tokvec_free(&st->align);
        free(st->message);
        free(st->region);
        lds_stmtvec_free(&st->body);
        strvec_free(&st->phdrs);
        strvec_free(&st->patterns);
        strvec_free(&st->excludes);
    }
    free(v->items);
    memset(v, 0, sizeof(*v));
}

void lds_script_free(lds_script_t *sc) {
    size_t i;

    if (sc == NULL) {
        return;
    }
    lds_stmtvec_free(&sc->stmts);
    lds_phdr_vec_free(&sc->phdrs);
    for (i = 0; i < sc->region_count; ++i) {
        free(sc->regions[i].name);
        lds_tokvec_free(&sc->regions[i].origin);
        lds_tokvec_free(&sc->regions[i].length);
    }
    free(sc->regions);
    defsymvec_free(&sc->locals);
    free(sc->entry);
    strvec_free(&sc->files);
    free(sc);
}

static lds_stmt_t *lds_stmt_new(lds_stmtvec_t *v, lds_stmt_kind_t kind, lds_where_t where, const lds_tok_t *at) {
    lds_stmt_t *st;

    if (v->count == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 16;
        lds_stmt_t *n = (lds_stmt_t *)realloc(v->items, ncap * sizeof(*n));

        if (n == NULL) {
            return NULL;
        }
        v->items = n;
        v->cap = ncap;
    }
    st = &v->items[v->count];
    memset(st, 0, sizeof(*st));
    st->kind = kind;
    st->where = where;
    st->op = '=';
    if (lds_tok_dup(&st->at, at) != 0) {
        return NULL;
    }
    v->count++;
    return st;
}

static void lp_error(lds_parser_t *p, const lds_tok_t *tok, const char *msg) {
    lds_report_error(p->include_stack, tok, msg);
}

/* The next token, unread.  NULL when there is none to be had, reported. */
static const lds_tok_t *lp_peek(lds_parser_t *p, int names) {
    if (p->have && p->lx.names != names) {
        lds_tok_free(&p->tok);
        p->lx = p->before;
        p->have = 0;
    }
    if (!p->have) {
        p->before = p->lx;
        p->lx.names = names;
        if (lds_lex_next(&p->lx, &p->tok) != 0) {
            lds_tok_t here;

            memset(&here, 0, sizeof(here));
            here.path = p->lx.path;
            here.line = p->lx.line;
            here.col = p->lx.col;
            lp_error(p, &here, "out of memory reading the script");
            return NULL;
        }
        if (p->tok.kind == LDS_TOK_OTHER && p->tok.text != NULL && p->tok.text[0] == '<' &&
            strncmp(p->tok.text, "<unterminated-", 14) == 0) {
            lp_error(p, &p->tok, strcmp(p->tok.text, "<unterminated-comment>") == 0
                                     ? "comment is not closed" : "string is not closed");
            lds_tok_free(&p->tok);
            return NULL;
        }
        p->have = 1;
    }
    return &p->tok;
}

static void lp_take(lds_parser_t *p) {
    if (p->have) {
        lds_tok_free(&p->tok);
        p->have = 0;
    }
}

static int lp_expect(lds_parser_t *p, lds_tok_kind_t kind, const char *what) {
    const lds_tok_t *t = lp_peek(p, 0);

    if (t == NULL) {
        return -1;
    }
    if (t->kind != kind) {
        lp_error(p, t, what);
        return -1;
    }
    lp_take(p);
    return 0;
}

/* An optional ';'. */
static int lp_semi(lds_parser_t *p) {
    const lds_tok_t *t = lp_peek(p, 0);

    if (t == NULL) {
        return -1;
    }
    if (t->kind == LDS_TOK_SEMI) {
        lp_take(p);
    }
    return 0;
}

static int lds_word_in(const char *w, const char *const *list) {
    size_t i;

    for (i = 0; list[i] != NULL; ++i) {
        if (strcmp(w, list[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The operator of a compound assignment, if `t` begins one. */
static char lds_assign_op(const lds_tok_t *t) {
    static const char *const ops[] = { "+", "-", "*", "/", "&", "|", "<<", ">>", NULL };

    if (t->kind == LDS_TOK_OTHER && t->text != NULL && lds_word_in(t->text, ops)) {
        return t->text[0];
    }
    return 0;
}

/* "( ... )" or "{ ... }" that this linker has no use for. */
static int lp_skip_group(lds_parser_t *p, lds_tok_kind_t open, lds_tok_kind_t close, const char *what) {
    int depth = 0;

    if (lp_expect(p, open, what) != 0) {
        return -1;
    }
    depth = 1;
    while (depth > 0) {
        const lds_tok_t *t = lp_peek(p, 1);

        if (t == NULL) {
            return -1;
        }
        if (t->kind == LDS_TOK_EOF) {
            lp_error(p, t, what);
            return -1;
        }
        if (t->kind == open) {
            depth++;
        } else if (t->kind == close) {
            depth--;
        }
        lp_take(p);
    }
    return 0;
}

/*
 * An expression's tokens, up to what ends it, which is left unread: a ';'
 * or ',', the ')' of whatever it is inside, or with `colon` a ':' that is
 * not a conditional's.  With `juxta` it also ends where a second operand
 * follows a first with nothing between, which is all that separates the
 * lines of MEMORY.
 */
static int lp_expr(lds_parser_t *p, lds_tokvec_t *out, int colon, int juxta) {
    int depth = 0;
    int cond = 0;
    int operand = 0;            /* the last token was the end of an operand */

    for (;;) {
        const lds_tok_t *t = lp_peek(p, 0);
        int starts;

        if (t == NULL) {
            return -1;
        }
        if (t->kind == LDS_TOK_EOF || t->kind == LDS_TOK_LBRACE ||
            (t->kind == LDS_TOK_RBRACE && !juxta)) {
            lp_error(p, t, "expression is not finished");
            return -1;
        }
        starts = t->kind == LDS_TOK_IDENT || t->kind == LDS_TOK_NUMBER || t->kind == LDS_TOK_STRING;
        if (depth == 0) {
            if (t->kind == LDS_TOK_SEMI || t->kind == LDS_TOK_COMMA || t->kind == LDS_TOK_RPAREN ||
                t->kind == LDS_TOK_RBRACE || t->kind == LDS_TOK_EQUAL) {
                break;
            }
            if (t->kind == LDS_TOK_COLON) {
                if (cond == 0) {
                    if (colon) {
                        break;
                    }
                    lp_error(p, t, "unexpected ':' in expression");
                    return -1;
                }
                cond--;
            }
            if (lds_tok_is(t, LDS_TOK_OTHER, "?")) {
                cond++;
            }
            if (juxta && operand && starts) {
                break;
            }
        }
        if (t->kind == LDS_TOK_LPAREN) {
            depth++;
        } else if (t->kind == LDS_TOK_RPAREN) {
            depth--;
        }
        operand = starts || t->kind == LDS_TOK_RPAREN;
        if (out->count >= LD_MAX_SCRIPT_EXPR_TOKENS) {
            lp_error(p, t, "expression is too long");
            return -1;
        }
        if (lds_tokvec_push(out, t) != 0) {
            return -1;
        }
        lp_take(p);
    }
    if (out->count == 0) {
        const lds_tok_t *t = lp_peek(p, 0);

        if (t != NULL) {
            lp_error(p, t, "expected an expression");
        }
        return -1;
    }
    return 0;
}

/*
 * name = expr, name op= expr; the name has been read.  `preop` is the
 * operator when the name swallowed it ("a+" before "=").  `closer` is what
 * follows: a ';', or the ')' of PROVIDE(...).
 */
static int lp_assignment(lds_parser_t *p, lds_stmtvec_t *v, lds_where_t where, const lds_tok_t *name, char preop,
                         int provide, int hidden, lds_tok_kind_t closer) {
    lds_stmt_t *st = lds_stmt_new(v, LDS_ST_ASSIGN, where, name);
    const lds_tok_t *t;

    if (st == NULL) {
        return -1;
    }
    st->provide = provide;
    st->hidden = hidden;
    t = lp_peek(p, 0);
    if (t == NULL) {
        return -1;
    }
    if (preop != 0) {
        st->op = preop;
    } else if (lds_assign_op(t) != 0) {
        st->op = lds_assign_op(t);
        lp_take(p);
    }
    if (lp_expect(p, LDS_TOK_EQUAL, "expected '=' after the name") != 0 ||
        lp_expr(p, &st->expr, 0, 0) != 0) {
        return -1;
    }
    if (closer == LDS_TOK_RPAREN) {
        return lp_expect(p, LDS_TOK_RPAREN, "expected ')'") != 0 ? -1 : lp_semi(p);
    }
    return lp_expect(p, LDS_TOK_SEMI, "expected ';' after the assignment");
}

/* PROVIDE(name = expr), PROVIDE_HIDDEN(...), HIDDEN(...); the keyword has been read. */
static int lp_provide(lds_parser_t *p, lds_stmtvec_t *v, lds_where_t where, const lds_tok_t *kw) {
    int provide = strcmp(kw->text, "HIDDEN") != 0;
    int hidden = strcmp(kw->text, "PROVIDE") != 0;
    const lds_tok_t *t;
    lds_tok_t name;
    int rc;

    if (lp_expect(p, LDS_TOK_LPAREN, "expected '('") != 0 || (t = lp_peek(p, 0)) == NULL) {
        return -1;
    }
    if (t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_STRING) {
        lp_error(p, t, "expected a symbol name");
        return -1;
    }
    if (lds_tok_dup(&name, t) != 0) {
        return -1;
    }
    lp_take(p);
    rc = lp_assignment(p, v, where, &name, 0, provide, hidden, LDS_TOK_RPAREN);
    lds_tok_free(&name);
    return rc;
}

/* ASSERT(expr, "message"); the keyword has been read.  The ';' is optional. */
static int lp_assert(lds_parser_t *p, lds_stmtvec_t *v, lds_where_t where, const lds_tok_t *kw) {
    lds_stmt_t *st = lds_stmt_new(v, LDS_ST_ASSERT, where, kw);
    const lds_tok_t *t;

    if (st == NULL || lp_expect(p, LDS_TOK_LPAREN, "expected '(' after ASSERT") != 0 ||
        lp_expr(p, &st->expr, 0, 0) != 0 || (t = lp_peek(p, 0)) == NULL) {
        return -1;
    }
    if (t->kind == LDS_TOK_COMMA) {
        lp_take(p);
        t = lp_peek(p, 0);
        if (t == NULL) {
            return -1;
        }
        if (t->kind != LDS_TOK_STRING && t->kind != LDS_TOK_IDENT) {
            lp_error(p, t, "expected the message of the ASSERT");
            return -1;
        }
        st->message = xstrdup(t->text);
        if (st->message == NULL) {
            return -1;
        }
        lp_take(p);
    }
    return lp_expect(p, LDS_TOK_RPAREN, "expected ')' after ASSERT") != 0 ? -1 : lp_semi(p);
}

/* ENTRY(symbol); the keyword has been read. */
static int lp_entry(lds_parser_t *p) {
    const lds_tok_t *t;

    if (lp_expect(p, LDS_TOK_LPAREN, "expected '(' after ENTRY") != 0 || (t = lp_peek(p, 1)) == NULL) {
        return -1;
    }
    if (t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_STRING) {
        lp_error(p, t, "expected a symbol name");
        return -1;
    }
    free(p->sc->entry);
    p->sc->entry = xstrdup(t->text);
    if (p->sc->entry == NULL) {
        return -1;
    }
    lp_take(p);
    return lp_expect(p, LDS_TOK_RPAREN, "expected ')' after ENTRY") != 0 ? -1 : lp_semi(p);
}

/* INCLUDE file, where a statement may stand; the keyword has been read. */
static int lp_include(lds_parser_t *p, lds_where_t where, lds_stmtvec_t *v) {
    const lds_tok_t *t = lp_peek(p, 1);
    char *path = NULL;
    size_t i;
    int rc;

    if (t == NULL) {
        return -1;
    }
    if (t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_STRING) {
        lp_error(p, t, "expected a file name after INCLUDE");
        return -1;
    }
    /* Beside the script that names it, then as given, then along -L. */
    path = resolve_script_include_path(p->lx.path, t->text);
    if (path != NULL && access(path, R_OK) != 0) {
        free(path);
        path = access(t->text, R_OK) == 0 ? xstrdup(t->text) : NULL;
        for (i = 0; path == NULL && p->ctx != NULL && i < p->ctx->lib_paths.count; ++i) {
            size_t n = strlen(p->ctx->lib_paths.items[i]) + strlen(t->text) + 2;
            char *cand = (char *)malloc(n);

            if (cand == NULL) {
                return -1;
            }
            snprintf(cand, n, "%s/%s", p->ctx->lib_paths.items[i], t->text);
            if (access(cand, R_OK) == 0) {
                path = cand;
            } else {
                free(cand);
            }
        }
        if (path == NULL) {
            lp_error(p, t, "INCLUDE: file not found");
            return -1;
        }
    }
    if (path == NULL) {
        return -1;
    }
    lp_take(p);
    rc = lds_parse_file(p->sc, p->ctx, p->include_stack, path, where, v, p->depth + 1);
    free(path);
    return rc;
}

/*
 * file ( section ... ): which input sections go here.  The file pattern
 * has been read.  SORT and its relatives are accepted and change nothing,
 * the order within an output section being the order of the inputs.
 */
static int lp_input(lds_parser_t *p, lds_stmtvec_t *v, const lds_tok_t *file, int keep) {
    static const char *const sorts[] = { "SORT", "SORT_BY_NAME", "SORT_BY_ALIGNMENT",
                                         "SORT_BY_INIT_PRIORITY", "SORT_NONE", NULL };
    lds_stmt_t *st = lds_stmt_new(v, LDS_ST_INPUT, LDS_IN_BODY, file);
    int nested = 0;

    if (st == NULL || lp_expect(p, LDS_TOK_LPAREN, "expected '(' after the file name") != 0) {
        return -1;
    }
    st->keep = keep;
    for (;;) {
        const lds_tok_t *t = lp_peek(p, 1);
        char *word;

        if (t == NULL) {
            return -1;
        }
        if (t->kind == LDS_TOK_RPAREN) {
            lp_take(p);
            if (nested == 0) {
                break;
            }
            nested--;
            continue;
        }
        if (t->kind == LDS_TOK_COMMA) {
            lp_take(p);
            continue;
        }
        if (t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_STRING) {
            lp_error(p, t, "expected a section name or ')'");
            return -1;
        }
        word = xstrdup(t->text);
        if (word == NULL) {
            return -1;
        }
        lp_take(p);
        t = lp_peek(p, 1);
        if (t == NULL) {
            free(word);
            return -1;
        }
        if (t->kind == LDS_TOK_LPAREN && lds_word_in(word, sorts)) {
            lp_take(p);
            nested++;
        } else if (t->kind == LDS_TOK_LPAREN && strcmp(word, "EXCLUDE_FILE") == 0) {
            lp_take(p);
            for (;;) {
                t = lp_peek(p, 1);
                if (t == NULL) {
                    free(word);
                    return -1;
                }
                if (t->kind == LDS_TOK_RPAREN) {
                    lp_take(p);
                    break;
                }
                if ((t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_STRING) ||
                    strvec_push(&st->excludes, t->text) != 0) {
                    lp_error(p, t, "expected a file name in EXCLUDE_FILE");
                    free(word);
                    return -1;
                }
                lp_take(p);
            }
        } else if (strvec_push(&st->patterns, word) != 0) {
            free(word);
            return -1;
        }
        free(word);
    }
    return 0;
}

/* Whether `text` is a name as an expression has them; *op is set to a
 * trailing operator that reading it as a file name took in ("a+"). */
static int lds_expr_name(char *text, char *op) {
    size_t n = strlen(text);
    size_t i;

    *op = 0;
    if (n > 1 && strchr("+-*/", text[n - 1]) != NULL) {
        *op = text[n - 1];
        text[--n] = '\0';
    }
    if (n == 0 || !lds_is_ident_start((unsigned char)text[0])) {
        return 0;
    }
    for (i = 1; i < n; ++i) {
        if (!lds_is_ident_char((unsigned char)text[i])) {
            return 0;
        }
    }
    return 1;
}

/*
 * One statement of an output section's body.  1: the '}' that ends the
 * body (read); 2: end of file; 0: a statement; -1: an error, reported.
 */
static int lp_body_statement(lds_parser_t *p, lds_stmtvec_t *v) {
    static const char *const data[] = { "BYTE", "SHORT", "LONG", "QUAD", "SQUAD", NULL };
    static const char *const nothing[] = { "CONSTRUCTORS", "CREATE_OBJECT_SYMBOLS", NULL };
    const lds_tok_t *t = lp_peek(p, 1);
    lds_tok_t head;
    char op = 0;
    int rc = -1;

    if (t == NULL) {
        return -1;
    }
    if (t->kind == LDS_TOK_RBRACE) {
        lp_take(p);
        return 1;
    }
    if (t->kind == LDS_TOK_EOF) {
        return 2;
    }
    if (t->kind == LDS_TOK_SEMI) {
        lp_take(p);
        return 0;
    }
    if (t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_STRING) {
        lp_error(p, t, "expected a statement or '}'");
        return -1;
    }
    if (lds_tok_dup(&head, t) != 0) {
        return -1;
    }
    lp_take(p);
    t = lp_peek(p, 0);
    if (t == NULL) {
        lds_tok_free(&head);
        return -1;
    }
    if (strcmp(head.text, "KEEP") == 0 && t->kind == LDS_TOK_LPAREN) {
        lds_tok_t file;

        lp_take(p);
        t = lp_peek(p, 1);
        if (t != NULL && (t->kind == LDS_TOK_IDENT || t->kind == LDS_TOK_STRING) && lds_tok_dup(&file, t) == 0) {
            lp_take(p);
            rc = lp_input(p, v, &file, 1);
            lds_tok_free(&file);
            if (rc == 0) {
                rc = lp_expect(p, LDS_TOK_RPAREN, "expected ')' after KEEP");
            }
        } else if (t != NULL) {
            lp_error(p, t, "expected a file name in KEEP");
        }
    } else if (strcmp(head.text, "PROVIDE") == 0 || strcmp(head.text, "PROVIDE_HIDDEN") == 0 ||
               strcmp(head.text, "HIDDEN") == 0) {
        rc = lp_provide(p, v, LDS_IN_BODY, &head);
    } else if (strcmp(head.text, "ASSERT") == 0) {
        rc = lp_assert(p, v, LDS_IN_BODY, &head);
    } else if (strcmp(head.text, "INCLUDE") == 0) {
        rc = lp_include(p, LDS_IN_BODY, v);
    } else if (lds_word_in(head.text, data) && t->kind == LDS_TOK_LPAREN) {
        lds_stmt_t *st = lds_stmt_new(v, LDS_ST_DATA, LDS_IN_BODY, &head);

        lp_take(p);
        if (st != NULL && lp_expr(p, &st->expr, 0, 0) == 0) {
            rc = lp_expect(p, LDS_TOK_RPAREN, "expected ')'");
        }
    } else if (strcmp(head.text, "FILL") == 0 && t->kind == LDS_TOK_LPAREN) {
        rc = lp_skip_group(p, LDS_TOK_LPAREN, LDS_TOK_RPAREN, "FILL( is not closed");
    } else if (lds_word_in(head.text, nothing)) {
        rc = 0;
    } else if (t->kind == LDS_TOK_LPAREN) {
        rc = lp_input(p, v, &head, 0);
    } else if ((t->kind == LDS_TOK_EQUAL || lds_assign_op(t) != 0) && lds_expr_name(head.text, &op)) {
        rc = lp_assignment(p, v, LDS_IN_BODY, &head, op, 0, 0, LDS_TOK_SEMI);
    } else if (t->kind == LDS_TOK_EQUAL || lds_assign_op(t) != 0) {
        lp_error(p, &head, "not a name that can be assigned to");
    } else {
        /* A file by itself: all of its sections. */
        lds_stmt_t *st = lds_stmt_new(v, LDS_ST_INPUT, LDS_IN_BODY, &head);

        rc = st != NULL && strvec_push(&st->patterns, "*") == 0 ? 0 : -1;
    }
    lds_tok_free(&head);
    return rc;
}

/*
 * name [address] [(type)] : [AT(lma)] [ALIGN(n)] { body } [>region]
 * [AT>region] [:phdr ...] [=fill] [,] -- the name has been read.
 */
static int lp_outsec(lds_parser_t *p, lds_stmtvec_t *v, const lds_tok_t *name) {
    static const char *const types[] = { "NOLOAD", "DSECT", "COPY", "INFO", "OVERLAY", NULL };
    static const char *const skipped[] = { "AT", "SUBALIGN", "BLOCK", NULL };
    static const char *const flags[] = { "ONLY_IF_RO", "ONLY_IF_RW", "SPECIAL", NULL };
    lds_stmt_t *st = lds_stmt_new(v, LDS_ST_OUTSEC, LDS_IN_SECTIONS, name);
    const lds_tok_t *t;
    int rc;

    if (st == NULL || (t = lp_peek(p, 0)) == NULL) {
        return -1;
    }
    st->discard = strcmp(name->text, "/DISCARD/") == 0;
    if (t->kind != LDS_TOK_COLON) {
        size_t n;

        if (lp_expr(p, &st->expr, 1, 0) != 0) {
            return -1;
        }
        n = st->expr.count;
        if (n >= 3 && st->expr.items[n - 1].kind == LDS_TOK_RPAREN && st->expr.items[n - 3].kind == LDS_TOK_LPAREN &&
            st->expr.items[n - 2].kind == LDS_TOK_IDENT && lds_word_in(st->expr.items[n - 2].text, types)) {
            lds_tok_free(&st->expr.items[n - 1]);
            lds_tok_free(&st->expr.items[n - 2]);
            lds_tok_free(&st->expr.items[n - 3]);
            st->expr.count = n - 3;
        }
    }
    if (lp_expect(p, LDS_TOK_COLON, "expected ':' after the output section's name") != 0) {
        return -1;
    }
    for (;;) {
        t = lp_peek(p, 0);
        if (t == NULL) {
            return -1;
        }
        if (t->kind == LDS_TOK_LBRACE) {
            lp_take(p);
            break;
        }
        if (t->kind == LDS_TOK_IDENT && strcmp(t->text, "ALIGN") == 0) {
            lp_take(p);
            if (lp_expect(p, LDS_TOK_LPAREN, "expected '(' after ALIGN") != 0 ||
                lp_expr(p, &st->align, 0, 0) != 0 || lp_expect(p, LDS_TOK_RPAREN, "expected ')'") != 0) {
                return -1;
            }
        } else if (t->kind == LDS_TOK_IDENT && lds_word_in(t->text, skipped)) {
            lp_take(p);
            if (lp_skip_group(p, LDS_TOK_LPAREN, LDS_TOK_RPAREN, "'(' is not closed") != 0) {
                return -1;
            }
        } else if (t->kind == LDS_TOK_IDENT && lds_word_in(t->text, flags)) {
            lp_take(p);
        } else {
            lp_error(p, t, "expected '{' to begin the output section");
            return -1;
        }
    }
    while ((rc = lp_body_statement(p, &st->body)) == 0) {
    }
    if (rc != 1) {
        if (rc == 2) {
            lp_error(p, &st->at, "output section is not closed: missing '}'");
        }
        return -1;
    }
    for (;;) {
        t = lp_peek(p, 0);
        if (t == NULL) {
            return -1;
        }
        if (lds_tok_is(t, LDS_TOK_OTHER, ">") || lds_tok_is(t, LDS_TOK_IDENT, "AT")) {
            int lma = t->kind == LDS_TOK_IDENT;

            lp_take(p);
            if (lma) {
                t = lp_peek(p, 0);
                if (t == NULL || !lds_tok_is(t, LDS_TOK_OTHER, ">")) {
                    if (t != NULL) {
                        lp_error(p, t, "expected '>' after AT");
                    }
                    return -1;
                }
                lp_take(p);
            }
            t = lp_peek(p, 1);
            if (t == NULL) {
                return -1;
            }
            if (t->kind != LDS_TOK_IDENT) {
                lp_error(p, t, "expected the name of a memory region");
                return -1;
            }
            if (!lma) {
                free(st->region);
                st->region = xstrdup(t->text);
                if (st->region == NULL) {
                    return -1;
                }
            }
            lp_take(p);
        } else if (t->kind == LDS_TOK_COLON) {
            lp_take(p);
            t = lp_peek(p, 1);
            if (t == NULL) {
                return -1;
            }
            if (t->kind != LDS_TOK_IDENT || strvec_push(&st->phdrs, t->text) != 0) {
                lp_error(p, t, "expected the name of a program header");
                return -1;
            }
            lp_take(p);
        } else if (t->kind == LDS_TOK_EQUAL) {
            lp_take(p);
            t = lp_peek(p, 0);
            if (t == NULL) {
                return -1;
            }
            if (t->kind != LDS_TOK_NUMBER && t->kind != LDS_TOK_IDENT) {
                lp_error(p, t, "expected the fill value");
                return -1;
            }
            lp_take(p);
        } else {
            if (t->kind == LDS_TOK_COMMA) {
                lp_take(p);
            }
            break;
        }
    }
    return 0;
}

/* One statement of SECTIONS; results as lp_body_statement's. */
static int lp_sections_statement(lds_parser_t *p, lds_stmtvec_t *v) {
    const lds_tok_t *t = lp_peek(p, 1);
    lds_tok_t head;
    char op = 0;
    int rc = -1;

    if (t == NULL) {
        return -1;
    }
    if (t->kind == LDS_TOK_RBRACE) {
        lp_take(p);
        return 1;
    }
    if (t->kind == LDS_TOK_EOF) {
        return 2;
    }
    if (t->kind == LDS_TOK_SEMI) {
        lp_take(p);
        return 0;
    }
    if (t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_STRING) {
        lp_error(p, t, "expected an output section, an assignment or '}'");
        return -1;
    }
    if (lds_tok_dup(&head, t) != 0) {
        return -1;
    }
    lp_take(p);
    t = lp_peek(p, 0);
    if (t == NULL) {
        lds_tok_free(&head);
        return -1;
    }
    if (strcmp(head.text, "ENTRY") == 0 && t->kind == LDS_TOK_LPAREN) {
        rc = lp_entry(p);
    } else if (strcmp(head.text, "ASSERT") == 0 && t->kind == LDS_TOK_LPAREN) {
        rc = lp_assert(p, v, LDS_IN_SECTIONS, &head);
    } else if ((strcmp(head.text, "PROVIDE") == 0 || strcmp(head.text, "PROVIDE_HIDDEN") == 0 ||
                strcmp(head.text, "HIDDEN") == 0) && t->kind == LDS_TOK_LPAREN) {
        rc = lp_provide(p, v, LDS_IN_SECTIONS, &head);
    } else if (strcmp(head.text, "INCLUDE") == 0) {
        rc = lp_include(p, LDS_IN_SECTIONS, v);
    } else if (t->kind == LDS_TOK_EQUAL || lds_assign_op(t) != 0) {
        if (lds_expr_name(head.text, &op)) {
            rc = lp_assignment(p, v, LDS_IN_SECTIONS, &head, op, 0, 0, LDS_TOK_SEMI);
        } else {
            lp_error(p, &head, "not a name that can be assigned to");
        }
    } else {
        rc = lp_outsec(p, v, &head);
    }
    lds_tok_free(&head);
    return rc;
}

/* PHDRS { name type [FILEHDR] [PHDRS] [AT(addr)] [FLAGS(bits)] ; ... } */
static int lp_phdrs(lds_parser_t *p) {
    if (lp_expect(p, LDS_TOK_LBRACE, "expected '{' after PHDRS") != 0) {
        return -1;
    }
    for (;;) {
        const lds_tok_t *t = lp_peek(p, 1);
        char *name;
        uint32_t type = 0;
        uint32_t flags = 0;
        int has_flags = 0;

        if (t == NULL) {
            return -1;
        }
        if (t->kind == LDS_TOK_RBRACE) {
            lp_take(p);
            return 0;
        }
        if (t->kind == LDS_TOK_SEMI) {
            lp_take(p);
            continue;
        }
        if (t->kind != LDS_TOK_IDENT) {
            lp_error(p, t, t->kind == LDS_TOK_EOF ? "PHDRS is not closed: missing '}'"
                                                    : "expected the name of a program header");
            return -1;
        }
        if (lds_phdr_vec_find(&p->sc->phdrs, t->text) >= 0) {
            lp_error(p, t, "program header is declared twice");
            return -1;
        }
        name = xstrdup(t->text);
        if (name == NULL) {
            return -1;
        }
        lp_take(p);
        t = lp_peek(p, 0);
        if (t == NULL || (t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_NUMBER) ||
            (t->kind == LDS_TOK_IDENT && phdr_type_from_token(t->text, &type) != 0)) {
            if (t != NULL) {
                lp_error(p, t, "expected the type of the program header");
            }
            free(name);
            return -1;
        }
        if (t->kind == LDS_TOK_NUMBER) {
            type = (uint32_t)strtoul(t->text, NULL, 0);
        }
        lp_take(p);
        for (;;) {
            t = lp_peek(p, 0);
            if (t == NULL) {
                free(name);
                return -1;
            }
            if (t->kind == LDS_TOK_SEMI) {
                lp_take(p);
                break;
            }
            if (lds_tok_is(t, LDS_TOK_IDENT, "FILEHDR") || lds_tok_is(t, LDS_TOK_IDENT, "PHDRS")) {
                lp_take(p);
            } else if (lds_tok_is(t, LDS_TOK_IDENT, "AT")) {
                lp_take(p);
                if (lp_skip_group(p, LDS_TOK_LPAREN, LDS_TOK_RPAREN, "AT( is not closed") != 0) {
                    free(name);
                    return -1;
                }
            } else if (lds_tok_is(t, LDS_TOK_IDENT, "FLAGS")) {
                lds_tokvec_t e;
                lds_eval_ctx_t ec;
                uint64_t val = 0;
                int ok;

                memset(&e, 0, sizeof(e));
                memset(&ec, 0, sizeof(ec));
                lp_take(p);
                ok = lp_expect(p, LDS_TOK_LPAREN, "expected '(' after FLAGS") == 0 && lp_expr(p, &e, 0, 0) == 0;
                if (ok && lds_eval_expr_slice(&ec, e.items, 0, e.count, &val) != 0) {
                    lp_error(p, ec.err_tok != NULL ? ec.err_tok : &e.items[0],
                             ec.err_msg != NULL ? ec.err_msg : "FLAGS must be a constant");
                    ok = 0;
                }
                lds_tokvec_free(&e);
                if (!ok || lp_expect(p, LDS_TOK_RPAREN, "expected ')' after FLAGS") != 0) {
                    free(name);
                    return -1;
                }
                flags = (uint32_t)(val & 0x7u);
                has_flags = 1;
            } else {
                lp_error(p, t, "expected ';' after the program header");
                free(name);
                return -1;
            }
        }
        if (lds_phdr_vec_push(&p->sc->phdrs, name, type, has_flags ? flags : phdr_default_flags(type),
                              type == PT_LOAD ? 0x1000u : 8u) != 0) {
            free(name);
            return -1;
        }
        p->sc->phdrs.items[p->sc->phdrs.count - 1].has_flags = has_flags;
        free(name);
    }
}

/* MEMORY { name [(attr)] : ORIGIN = expr, LENGTH = expr ... } */
static int lp_memory(lds_parser_t *p) {
    static const char *const org[] = { "ORIGIN", "org", "o", NULL };
    static const char *const len[] = { "LENGTH", "len", "l", NULL };

    if (lp_expect(p, LDS_TOK_LBRACE, "expected '{' after MEMORY") != 0) {
        return -1;
    }
    for (;;) {
        const lds_tok_t *t = lp_peek(p, 1);
        lds_region_t *r;
        int k;

        if (t == NULL) {
            return -1;
        }
        if (t->kind == LDS_TOK_RBRACE) {
            lp_take(p);
            return 0;
        }
        if (t->kind == LDS_TOK_SEMI || t->kind == LDS_TOK_COMMA) {
            lp_take(p);
            continue;
        }
        if (t->kind != LDS_TOK_IDENT) {
            lp_error(p, t, t->kind == LDS_TOK_EOF ? "MEMORY is not closed: missing '}'"
                                                    : "expected the name of a memory region");
            return -1;
        }
        if (p->sc->region_count == p->sc->region_cap) {
            size_t ncap = p->sc->region_cap ? p->sc->region_cap * 2 : 4;
            lds_region_t *n = (lds_region_t *)realloc(p->sc->regions, ncap * sizeof(*n));

            if (n == NULL) {
                return -1;
            }
            p->sc->regions = n;
            p->sc->region_cap = ncap;
        }
        r = &p->sc->regions[p->sc->region_count];
        memset(r, 0, sizeof(*r));
        r->name = xstrdup(t->text);
        if (r->name == NULL) {
            return -1;
        }
        p->sc->region_count++;
        lp_take(p);
        t = lp_peek(p, 0);
        if (t == NULL) {
            return -1;
        }
        if (t->kind == LDS_TOK_LPAREN && lp_skip_group(p, LDS_TOK_LPAREN, LDS_TOK_RPAREN, "'(' is not closed") != 0) {
            return -1;
        }
        if (lp_expect(p, LDS_TOK_COLON, "expected ':' after the region's name") != 0) {
            return -1;
        }
        for (k = 0; k < 2; ++k) {
            t = lp_peek(p, 0);
            if (t == NULL) {
                return -1;
            }
            if (t->kind == LDS_TOK_COMMA) {
                lp_take(p);
                t = lp_peek(p, 0);
                if (t == NULL) {
                    return -1;
                }
            }
            if (t->kind != LDS_TOK_IDENT || !lds_word_in(t->text, k == 0 ? org : len)) {
                lp_error(p, t, k == 0 ? "expected ORIGIN" : "expected LENGTH");
                return -1;
            }
            lp_take(p);
            if (lp_expect(p, LDS_TOK_EQUAL, "expected '='") != 0 ||
                lp_expr(p, k == 0 ? &r->origin : &r->length, 0, 1) != 0) {
                return -1;
            }
        }
    }
}

/* One command at the top of a script; results as lp_body_statement's. */
static int lp_top_statement(lds_parser_t *p) {
    static const char *const call[] = { "OUTPUT_FORMAT", "OUTPUT_ARCH", "OUTPUT", "TARGET", "SEARCH_DIR", "INPUT",
                                        "GROUP", "STARTUP", "EXTERN", "NOCROSSREFS", "NOCROSSREFS_TO",
                                        "REGION_ALIAS", NULL };
    static const char *const bare[] = { "FORCE_COMMON_ALLOCATION", "INHIBIT_COMMON_ALLOCATION",
                                        "FORCE_GROUP_ALLOCATION", NULL };
    lds_stmtvec_t *v = &p->sc->stmts;
    const lds_tok_t *t = lp_peek(p, 0);
    lds_tok_t head;
    int rc = -1;

    if (t == NULL) {
        return -1;
    }
    if (t->kind == LDS_TOK_EOF) {
        return 2;
    }
    if (t->kind == LDS_TOK_RBRACE) {
        lp_take(p);
        return 1;
    }
    if (t->kind == LDS_TOK_SEMI) {
        lp_take(p);
        return 0;
    }
    if (t->kind != LDS_TOK_IDENT) {
        lp_error(p, t, "expected a command");
        return -1;
    }
    if (lds_tok_dup(&head, t) != 0) {
        return -1;
    }
    lp_take(p);
    if (strcmp(head.text, "SECTIONS") == 0) {
        if (lp_expect(p, LDS_TOK_LBRACE, "expected '{' after SECTIONS") == 0) {
            p->sc->has_sections = 1;
            while ((rc = lp_sections_statement(p, v)) == 0) {
            }
            if (rc == 2) {
                lp_error(p, &head, "SECTIONS is not closed: missing '}'");
            }
            rc = rc == 1 ? 0 : -1;
        }
    } else if (strcmp(head.text, "PHDRS") == 0) {
        rc = lp_phdrs(p);
    } else if (strcmp(head.text, "MEMORY") == 0) {
        rc = lp_memory(p);
    } else if (strcmp(head.text, "ENTRY") == 0) {
        rc = lp_entry(p);
    } else if (strcmp(head.text, "ASSERT") == 0) {
        rc = lp_assert(p, v, LDS_IN_TOP, &head);
    } else if (strcmp(head.text, "PROVIDE") == 0 || strcmp(head.text, "PROVIDE_HIDDEN") == 0 ||
               strcmp(head.text, "HIDDEN") == 0) {
        rc = lp_provide(p, v, LDS_IN_TOP, &head);
    } else if (strcmp(head.text, "INCLUDE") == 0) {
        rc = lp_include(p, LDS_IN_TOP, v);
    } else if (strcmp(head.text, "VERSION") == 0) {
        rc = lp_skip_group(p, LDS_TOK_LBRACE, LDS_TOK_RBRACE, "VERSION { is not closed");
    } else if (strcmp(head.text, "INSERT") == 0) {
        /* Read and not acted on: there is no default script to insert into. */
        t = lp_peek(p, 0);
        if (t != NULL && (lds_tok_is(t, LDS_TOK_IDENT, "AFTER") || lds_tok_is(t, LDS_TOK_IDENT, "BEFORE"))) {
            lp_take(p);
            t = lp_peek(p, 1);
            if (t != NULL && t->kind == LDS_TOK_IDENT) {
                lp_take(p);
                rc = lp_semi(p);
            } else if (t != NULL) {
                lp_error(p, t, "expected a section name after INSERT BEFORE/AFTER");
            }
        } else if (t != NULL) {
            lp_error(p, t, "expected BEFORE or AFTER after INSERT");
        }
    } else if (lds_word_in(head.text, call)) {
        rc = lp_skip_group(p, LDS_TOK_LPAREN, LDS_TOK_RPAREN, "'(' is not closed") != 0 ? -1 : lp_semi(p);
    } else if (lds_word_in(head.text, bare)) {
        rc = lp_semi(p);
    } else {
        rc = lp_assignment(p, v, LDS_IN_TOP, &head, 0, 0, 0, LDS_TOK_SEMI);
    }
    lds_tok_free(&head);
    return rc;
}

/*
 * Parse one file into `sc`: the script itself, or one it INCLUDEs, whose
 * statements are of the kind that the INCLUDE stood among and go where it
 * stood.
 */
static int lds_parse_file(lds_script_t *sc, const ld_ctx_t *ctx, strvec_t *include_stack, const char *path,
                          lds_where_t where, lds_stmtvec_t *v, int depth) {
    lds_parser_t p;
    lds_tok_t here;
    unsigned char *buf = NULL;
    size_t sz = 0;
    int rc;

    memset(&p, 0, sizeof(p));
    memset(&here, 0, sizeof(here));
    here.path = path;
    here.line = 1;
    here.col = 1;
    if (depth >= LD_MAX_SCRIPT_INCLUDE_DEPTH) {
        lds_report_error(include_stack, &here, "INCLUDE depth exceeds limit");
        return -1;
    }
    if (strvec_push(include_stack, path) != 0 || strvec_push(&sc->files, path) != 0) {
        return -1;
    }
    if (read_file(path, &buf, &sz) != 0) {
        lds_report_error(include_stack, &here, "unable to read linker script");
        strvec_pop(include_stack);
        return -1;
    }
    p.lx.buf = buf;
    p.lx.len = sz;
    p.lx.path = sc->files.items[sc->files.count - 1];
    p.lx.line = 1;
    p.lx.col = 1;
    p.sc = sc;
    p.ctx = ctx;
    p.include_stack = include_stack;
    p.depth = depth;
    do {
        rc = where == LDS_IN_TOP ? lp_top_statement(&p)
           : where == LDS_IN_SECTIONS ? lp_sections_statement(&p, v) : lp_body_statement(&p, v);
    } while (rc == 0);
    if (rc == 1) {
        here.path = p.lx.path;
        here.line = p.lx.line;
        here.col = p.lx.col;
        lds_report_error(include_stack, &here, "unexpected '}'");
    }
    lp_take(&p);
    free(buf);
    strvec_pop(include_stack);
    return rc == 2 ? 0 : -1;
}

/* Read the script at `path`.  NULL: it could not be, and why was said. */
lds_script_t *lds_script_parse(const char *path, const ld_ctx_t *ctx) {
    lds_script_t *sc = (lds_script_t *)calloc(1, sizeof(*sc));
    strvec_t include_stack;
    int rc;

    if (sc == NULL) {
        return NULL;
    }
    memset(&include_stack, 0, sizeof(include_stack));
    rc = lds_parse_file(sc, ctx, &include_stack, path, LDS_IN_TOP, &sc->stmts, 0);
    strvec_free(&include_stack);
    if (rc != 0) {
        lds_script_free(sc);
        return NULL;
    }
    return sc;
}

/*
 * What a parsed linker script does to the link.
 *
 * script_output_name()     as the inputs are merged: the output section
 *                          an input section belongs to
 * script_declare_symbols() before symbols are resolved: the names the
 *                          script will define exist
 * script_apply_sections()  once the output's sections exist: discard,
 *                          keep, make, and put in the script's order
 * script_assign_addresses() after the default layout: the location
 *                          counter, the script's symbols, its assertions
 */
static lds_stmt_t *script_find_outsec(lds_script_t *sc, const char *name, size_t *index) {
    size_t i;

    for (i = 0; i < sc->stmts.count; ++i) {
        lds_stmt_t *st = &sc->stmts.items[i];

        if (st->kind == LDS_ST_OUTSEC && !st->discard && strcmp(st->at.text, name) == 0) {
            if (index != NULL) {
                *index = i;
            }
            return st;
        }
    }
    return NULL;
}

static int script_input_matches(const lds_stmt_t *in, const char *section, const char *file) {
    const char *base = file != NULL && strrchr(file, '/') != NULL ? strrchr(file, '/') + 1 : file;
    size_t i;

    if (file != NULL && !lds_glob(in->at.text, file) && !lds_glob(in->at.text, base)) {
        return 0;
    }
    for (i = 0; file != NULL && i < in->excludes.count; ++i) {
        if (lds_glob(in->excludes.items[i], file) || lds_glob(in->excludes.items[i], base)) {
            return 0;
        }
    }
    for (i = 0; i < in->patterns.count; ++i) {
        if (lds_glob(in->patterns.items[i], section)) {
            return 1;
        }
    }
    return 0;
}

/* The elfobj hook: the first output section of the script that asks for
 * this input section has it; one nothing asks for keeps its own name. */
const char *script_output_name(const char *section, const char *file, void *user) {
    const lds_script_t *sc = (const lds_script_t *)user;
    size_t i, k;

    for (i = 0; i < sc->stmts.count; ++i) {
        const lds_stmt_t *os = &sc->stmts.items[i];

        if (os->kind != LDS_ST_OUTSEC) {
            continue;
        }
        for (k = 0; k < os->body.count; ++k) {
            if (os->body.items[k].kind == LDS_ST_INPUT &&
                script_input_matches(&os->body.items[k], section, file)) {
                return os->discard ? NULL : os->at.text;
            }
        }
    }
    return section;
}

static int script_declare_in(ld_ctx_t *ctx, elfobj_t *out, lds_stmtvec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        lds_stmt_t *st = &v->items[i];

        if (st->kind == LDS_ST_OUTSEC) {
            if (script_declare_in(ctx, out, &st->body) != 0) {
                return -1;
            }
            continue;
        }
        if (st->kind != LDS_ST_ASSIGN || strcmp(st->at.text, ".") == 0) {
            continue;
        }
        st->active = 1;
        if (st->provide) {
            /* PROVIDE defines what is asked for and nobody has. */
            const elf_symbol_t *sym = elf_find_symbol(out, st->at.text);

            st->active = sym != NULL && elf_symbol_shndx(sym) == SHN_UNDEF &&
                         defsymvec_find(&ctx->defsyms, st->at.text) < 0;
        }
        if (st->active && defsymvec_find(&ctx->defsyms, st->at.text) < 0 &&
            defsymvec_set(&ctx->defsyms, st->at.text, 0) != 0) {
            return -1;
        }
        if (!st->active && elf_find_symbol(out, st->at.text) == NULL &&
            defsymvec_find(&ctx->defsyms, st->at.text) < 0 &&
            defsymvec_set(&ctx->script->locals, st->at.text, 0) != 0) {
            return -1;
        }
    }
    return 0;
}

int script_declare_symbols(ld_ctx_t *ctx, elfobj_t *out) {
    return ctx->script != NULL ? script_declare_in(ctx, out, &ctx->script->stmts) : 0;
}

static int script_body_moves_dot(const lds_stmt_t *os) {
    size_t k;

    for (k = 0; k < os->body.count; ++k) {
        if (os->body.items[k].kind == LDS_ST_ASSIGN && strcmp(os->body.items[k].at.text, ".") == 0) {
            return 1;
        }
    }
    return 0;
}

int script_apply_sections(ld_ctx_t *ctx, elfobj_t *out) {
    lds_script_t *sc = ctx->script;
    elf_section_t **want = NULL;
    size_t *slot = NULL;
    size_t n = 0, i, k, j;
    int rc = -1;

    if (sc == NULL || !sc->has_sections) {
        return 0;
    }
    for (i = 0; i < sc->stmts.count; ++i) {
        lds_stmt_t *os = &sc->stmts.items[i];
        int keep = 0;

        if (os->kind != LDS_ST_OUTSEC) {
            continue;
        }
        for (k = 0; k < os->body.count; ++k) {
            const lds_stmt_t *in = &os->body.items[k];

            if (in->kind != LDS_ST_INPUT) {
                continue;
            }
            keep |= in->keep;
            /* What the linker made itself was not there to be left out
             * when the inputs were merged. */
            for (j = 0; os->discard && j < in->patterns.count; ++j) {
                if (apply_script_discard_pattern(out, in->patterns.items[j]) != 0) {
                    fprintf(stderr, "ld: failed to discard sections matching '%s'\n", in->patterns.items[j]);
                    return -1;
                }
            }
        }
        if (os->discard) {
            continue;
        }
        if (elf_find_section(out, os->at.text) == NULL && script_body_moves_dot(os)) {
            /* Room the script reserves and no input fills. */
            if (elf_add_section(out, os->at.text, SHT_NOBITS, SHF_ALLOC | SHF_WRITE) == NULL) {
                return -1;
            }
        }
        if (keep && apply_script_keep_pattern(out, os->at.text) != 0) {
            return -1;
        }
    }

    /*
     * The script's order.  The sections it names change places among
     * themselves: the places they hold stay the ones the default policy
     * gave to them as a set, so that what the script does not mention
     * stays where it was.
     */
    want = (elf_section_t **)calloc(sc->stmts.count + 1, sizeof(*want));
    slot = (size_t *)calloc(sc->stmts.count + 1, sizeof(*slot));
    if (want == NULL || slot == NULL) {
        goto done;
    }
    for (i = 0; i < sc->stmts.count; ++i) {
        const lds_stmt_t *os = &sc->stmts.items[i];
        elf_section_t *sec;

        if (os->kind != LDS_ST_OUTSEC || os->discard || (sec = elf_find_section(out, os->at.text)) == NULL) {
            continue;
        }
        for (k = 0; k < n && want[k] != sec; ++k) {
        }
        if (k == n) {
            want[n++] = sec;
        }
    }
    for (i = 0, k = 0; i < elf_section_count(out) && k < n; ++i) {
        elf_section_t *sec = elf_section_get(out, i);

        for (j = 0; j < n; ++j) {
            if (want[j] == sec) {
                slot[k++] = i;
                break;
            }
        }
    }
    for (k = 0; k < n; ++k) {
        if (elf_section_get(out, slot[k]) != want[k] && elf_reorder_section(out, want[k], slot[k]) != ELF_OK) {
            fprintf(stderr, "ld: failed to put the sections in the script's order\n");
            goto done;
        }
    }
    rc = 0;
done:
    free(want);
    free(slot);
    return rc;
}

static int lw_eval(lds_walk_t *w, const lds_tokvec_t *e, int have_dot, uint64_t dot, const lds_tok_t *at,
                   uint64_t *out) {
    lds_eval_ctx_t ec;

    memset(&ec, 0, sizeof(ec));
    ec.ctx = w->ctx;
    ec.obj = w->out;
    ec.have_dot = have_dot;
    ec.dot = dot;
    if (lds_eval_expr_slice(&ec, e->items, 0, e->count, out) != 0) {
        lds_report_error(NULL, ec.err_tok != NULL ? ec.err_tok : at,
                         ec.err_msg != NULL ? ec.err_msg : "expression evaluation failed");
        return -1;
    }
    return 0;
}

/* Give a symbol the script defines its value in the output.  One defined
 * among the sections belongs to the section its address is in, so that it
 * moves with the program if the program is moved. */
static int lw_define(lds_walk_t *w, const lds_stmt_t *st, uint64_t value, elf_section_t *hint, uint64_t hint_end) {
    elf_symbol_t *sym = elf_find_symbol(w->out, st->at.text);
    elf_section_t *home = NULL;
    size_t i;

    if (sym == NULL) {
        sym = elf_add_symbol(w->out, st->at.text, value, 0, STB_GLOBAL, STT_NOTYPE);
        if (sym == NULL) {
            return -1;
        }
    }
    if (st->where != LDS_IN_TOP) {
        if (hint != NULL && value >= elf_section_addr(hint) && value <= hint_end) {
            home = hint;
        }
        for (i = 0; home == NULL && i < elf_section_count(w->out); ++i) {
            elf_section_t *sec = elf_section_get(w->out, i);

            if (sec != NULL && (elf_section_flags(sec) & SHF_ALLOC) != 0 && value >= elf_section_addr(sec) &&
                value <= elf_section_addr(sec) + elf_section_size(sec)) {
                home = sec;
            }
        }
    }
    if (home != NULL) {
        if (elf_symbol_define(sym, home, value - elf_section_addr(home)) != ELF_OK) {
            return -1;
        }
    } else if (elf_symbol_set_value(sym, value) != ELF_OK || elf_symbol_set_shndx(sym, SHN_ABS) != ELF_OK) {
        return -1;
    }
    if (st->hidden && elf_symbol_set_visibility(sym, STV_HIDDEN) != ELF_OK) {
        return -1;
    }
    return 0;
}

/* Carry out one assignment.  *dot is the counter where the statement
 * stands, and is what an assignment to "." changes. */
static int lw_assign(lds_walk_t *w, const lds_stmt_t *st, int have_dot, uint64_t *dot, elf_section_t *hint,
                     uint64_t hint_end) {
    int is_dot = strcmp(st->at.text, ".") == 0;
    uint64_t v = 0;
    uint64_t cur = 0;

    if (!is_dot && !st->active) {
        /* A PROVIDE that provides nothing still has a value the rest of
         * the script may use, if nothing else defines the name. */
        if (st->op != '=' || defsymvec_find(&w->sc->locals, st->at.text) < 0) {
            return 0;
        }
        return lw_eval(w, &st->expr, have_dot, *dot, &st->at, &v) != 0 ? -1
             : defsymvec_set(&w->sc->locals, st->at.text, v);
    }
    if (lw_eval(w, &st->expr, have_dot, *dot, &st->at, &v) != 0) {
        return -1;
    }
    if (st->op != '=') {
        if (is_dot) {
            cur = *dot;
        } else if (defsymvec_get(&w->ctx->defsyms, st->at.text, &cur) != 0) {
            lds_report_error(NULL, &st->at, "the symbol has no value to change");
            return -1;
        }
        switch (st->op) {
        case '+': v = cur + v; break;
        case '-': v = cur - v; break;
        case '*': v = cur * v; break;
        case '&': v = cur & v; break;
        case '|': v = cur | v; break;
        case '<': v = v < 64 ? cur << v : 0; break;
        case '>': v = v < 64 ? cur >> v : 0; break;
        case '/':
            if (v == 0) {
                lds_report_error(NULL, &st->at, "division by zero");
                return -1;
            }
            v = cur / v;
            break;
        default:
            return -1;
        }
    }
    if (is_dot) {
        if (!have_dot) {
            lds_report_error(NULL, &st->at, "the location counter has no value outside SECTIONS");
            return -1;
        }
        *dot = v;
        return 0;
    }
    if (defsymvec_set(&w->ctx->defsyms, st->at.text, v) != 0) {
        return -1;
    }
    return w->final ? lw_define(w, st, v, hint, hint_end) : 0;
}

static int lw_assert(lds_walk_t *w, const lds_stmt_t *st, int have_dot, uint64_t dot) {
    uint64_t v = 0;

    if (!w->final) {
        return 0;
    }
    if (lw_eval(w, &st->expr, have_dot, dot, &st->at, &v) != 0) {
        return -1;
    }
    if (v == 0) {
        fprintf(stderr, "ld: %s:%zu: %s\n", st->at.path != NULL ? st->at.path : "<script>", st->at.line,
                st->message != NULL ? st->message : "ASSERT failed");
        return -1;
    }
    return 0;
}

/*
 * The body of an output section that is at `start` and `size` long.  What
 * stands before its first input description is at its start, what stands
 * after its last is at its end, and there the counter may move on, which
 * makes the section that much longer.  Where the pieces of the section
 * meet is not kept once they are merged, so a statement that depends on
 * it, between two input descriptions, is refused.
 */
static int lw_body(lds_walk_t *w, lds_stmt_t *os, elf_section_t *sec, uint64_t start, uint64_t size,
                   uint64_t *end_out) {
    size_t first = os->body.count, last = 0, k;
    uint64_t end = start + size;

    for (k = 0; k < os->body.count; ++k) {
        if (os->body.items[k].kind == LDS_ST_INPUT) {
            if (first == os->body.count) {
                first = k;
            }
            last = k;
        }
    }
    for (k = 0; k < os->body.count; ++k) {
        const lds_stmt_t *st = &os->body.items[k];
        int at_start = size != 0 && k < first;
        uint64_t dot = at_start ? start : end;

        if (st->kind == LDS_ST_INPUT) {
            continue;
        }
        if (st->kind == LDS_ST_DATA) {
            lds_report_error(NULL, &st->at, "data statements (BYTE, SHORT, LONG, QUAD) are not supported");
            return -1;
        }
        if (size != 0 && first != os->body.count && k > first && k < last) {
            lds_report_error(NULL, &st->at,
                             "a statement between two input section descriptions is not supported");
            return -1;
        }
        if (st->kind == LDS_ST_ASSERT) {
            if (lw_assert(w, st, 1, dot) != 0) {
                return -1;
            }
            continue;
        }
        if (lw_assign(w, st, 1, &dot, sec, end) != 0) {
            return -1;
        }
        if (at_start && dot != start) {
            lds_report_error(NULL, &st->at,
                             "moving the location counter before a section's contents is not supported");
            return -1;
        }
        if (!at_start) {
            if (dot < end) {
                lds_report_error(NULL, &st->at, "the location counter may not move backwards");
                return -1;
            }
            end = dot;
        }
    }
    os->pad = end - (start + size);
    *end_out = end;
    return 0;
}

/* The statements stmts[lo..hi) that stand between output sections. */
static int lw_run(lds_walk_t *w, size_t lo, size_t hi) {
    size_t i;

    for (i = lo; i < hi; ++i) {
        lds_stmt_t *st = &w->sc->stmts.items[i];
        int have_dot = st->where != LDS_IN_TOP;
        uint64_t dot = w->dot;

        if (st->kind == LDS_ST_ASSERT) {
            if (lw_assert(w, st, have_dot, dot) != 0) {
                return -1;
            }
        } else if (st->kind == LDS_ST_ASSIGN) {
            if (lw_assign(w, st, have_dot, &dot, NULL, 0) != 0) {
                return -1;
            }
        } else if (st->kind == LDS_ST_OUTSEC && !st->discard) {
            /* An output section nothing went into: its symbols are where
             * it would have been. */
            uint64_t end = dot;

            if (st->expr.count != 0 && lw_eval(w, &st->expr, 1, dot, &st->at, &dot) != 0) {
                return -1;
            }
            if (lw_body(w, st, NULL, dot, 0, &end) != 0) {
                return -1;
            }
            st->pad = 0;
            dot = end;
        }
        if (dot != w->dot) {
            if (dot < w->high) {
                lds_report_error(NULL, &st->at, "the location counter may not move back over what is placed");
                return -1;
            }
            w->dot = dot;
            w->moved = 1;
        }
    }
    return 0;
}

static lds_region_t *script_find_region(lds_script_t *sc, const char *name) {
    size_t i;

    for (i = 0; i < sc->region_count; ++i) {
        if (strcmp(sc->regions[i].name, name) == 0) {
            return &sc->regions[i];
        }
    }
    return NULL;
}

/* One pass over the output's sections with the script's location counter. */
static int lw_pass(lds_walk_t *w, uint64_t first_addr) {
    const uint64_t page = 0x1000u;
    lds_script_t *sc = w->sc;
    size_t next = 0, i;
    int last_class = -1;
    int last_relro = -1;

    w->dot = first_addr;
    w->high = 0;
    w->moved = 0;
    for (i = 0; i < sc->region_count; ++i) {
        lds_region_t *r = &sc->regions[i];
        lds_tok_t none;

        memset(&none, 0, sizeof(none));
        if (lw_eval(w, &r->origin, 0, 0, &none, &r->org) != 0 || lw_eval(w, &r->length, 0, 0, &none, &r->len) != 0) {
            return -1;
        }
        r->cursor = r->org;
    }
    if (!sc->has_sections) {
        return lw_run(w, 0, sc->stmts.count);
    }
    /* What stands before the first output section comes before anything
     * is placed: ". = 0x100000;" is where the image begins. */
    while (next < sc->stmts.count && sc->stmts.items[next].kind != LDS_ST_OUTSEC) {
        next++;
    }
    if (lw_run(w, 0, next) != 0) {
        return -1;
    }
    for (i = 0; i < elf_section_count(w->out); ++i) {
        elf_section_t *sec = elf_section_get(w->out, i);
        const char *name = sec != NULL ? elf_section_name(sec) : NULL;
        lds_stmt_t *os = NULL;
        lds_region_t *region = NULL;
        uint64_t flags, align, size, end;
        size_t si = 0;
        int explicit;
        int cls;

        if (sec == NULL || name == NULL) {
            continue;
        }
        flags = elf_section_flags(sec);
        if ((flags & SHF_ALLOC) == 0) {
            continue;
        }
        os = script_find_outsec(sc, name, &si);
        if (os != NULL && si < next) {
            os = NULL;          /* out of the script's order: placed as found */
        }
        if (os != NULL) {
            if (lw_run(w, next, si) != 0) {
                return -1;
            }
            next = si + 1;
        }
        explicit = w->moved;
        w->moved = 0;
        if (os != NULL && os->expr.count != 0) {
            if (lw_eval(w, &os->expr, 1, w->dot, &os->at, &w->dot) != 0) {
                return -1;
            }
            explicit = 1;
        }
        if (os != NULL && os->region != NULL) {
            region = script_find_region(sc, os->region);
            if (region == NULL) {
                lds_report_error(NULL, &os->at, "the section's memory region is not defined in MEMORY");
                return -1;
            }
            if (os->expr.count == 0) {
                w->dot = region->cursor;
                explicit = 1;
            }
        }
        cls = alloc_section_class(flags);
        if (!explicit) {
            /* As the default layout has it: what is protected differently
             * does not share a page. */
            int relro = cls == 2 && is_relro_candidate_name(name);

            if ((last_class != -1 && cls != last_class) ||
                (cls == 2 && last_class == 2 && last_relro != -1 && relro != last_relro)) {
                if (!align_up_u64_checked(w->dot, page, &w->dot)) {
                    return -1;
                }
            }
        }
        last_class = cls;
        last_relro = cls == 2 ? (is_relro_candidate_name(name) ? 1 : 0) : -1;
        if (os != NULL && os->align.count != 0) {
            uint64_t a = 0;

            if (lw_eval(w, &os->align, 1, w->dot, &os->at, &a) != 0) {
                return -1;
            }
            if (a == 0 || !align_up_u64_checked(w->dot, a, &w->dot)) {
                lds_report_error(NULL, &os->at, "invalid ALIGN for the output section");
                return -1;
            }
        }
        align = elf_section_align(sec);
        if (!align_up_u64_checked(w->dot, align != 0 ? align : 1, &w->dot)) {
            return -1;
        }
        if (w->dot < w->high) {
            fprintf(stderr, "ld: section %s at 0x%llx would begin before the end (0x%llx) of the one before it\n",
                    name, (unsigned long long)w->dot, (unsigned long long)w->high);
            return -1;
        }
        if (elf_section_set_addr(sec, w->dot) != ELF_OK) {
            return -1;
        }
        size = elf_section_size(sec);
        end = w->dot + size;
        if (os != NULL && lw_body(w, os, sec, w->dot, size, &end) != 0) {
            return -1;
        }
        w->dot = end;
        w->high = end;
        /* The symbols a script defines straight after a section are that
         * section's end ("etext = .;"), not the end of whatever the
         * script does not mention and is placed after it.  What moves the
         * counter waits its turn. */
        while (os != NULL && next < sc->stmts.count &&
               (sc->stmts.items[next].kind == LDS_ST_ASSERT ||
                (sc->stmts.items[next].kind == LDS_ST_ASSIGN && strcmp(sc->stmts.items[next].at.text, ".") != 0))) {
            if (lw_run(w, next, next + 1) != 0) {
                return -1;
            }
            next++;
        }
        if (region != NULL) {
            region->cursor = end;
            if (end > region->org + region->len) {
                fprintf(stderr, "ld: section %s does not fit in memory region %s (ends at 0x%llx, the region at 0x%llx)\n",
                        name, region->name, (unsigned long long)end,
                        (unsigned long long)(region->org + region->len));
                return -1;
            }
        }
    }
    return lw_run(w, next, sc->stmts.count);
}

int script_assign_addresses(ld_ctx_t *ctx, elfobj_t *out) {
    lds_script_t *sc = ctx->script;
    lds_walk_t w;
    uint64_t first_addr = 0;
    int have_first = 0;
    size_t i;
    int pass;

    if (sc == NULL) {
        return 0;
    }
    /* Where the default layout began is where the script's begins, until
     * the script says otherwise. */
    for (i = 0; i < elf_section_count(out); ++i) {
        elf_section_t *sec = elf_section_get(out, i);

        if (sec != NULL && (elf_section_flags(sec) & SHF_ALLOC) != 0 &&
            (!have_first || elf_section_addr(sec) < first_addr)) {
            first_addr = elf_section_addr(sec);
            have_first = 1;
        }
    }
    memset(&w, 0, sizeof(w));
    w.ctx = ctx;
    w.out = out;
    w.sc = sc;
    /* A statement may use what a later one defines: twice over to learn
     * the values, a third time to act on them. */
    for (pass = 0; pass < 3; ++pass) {
        w.final = pass == 2;
        if (lw_pass(&w, first_addr) != 0) {
            return -1;
        }
    }
    for (i = 0; i < sc->stmts.count; ++i) {
        lds_stmt_t *os = &sc->stmts.items[i];
        elf_section_t *sec;
        size_t size;

        if (os->kind != LDS_ST_OUTSEC || os->pad == 0 || (sec = elf_find_section(out, os->at.text)) == NULL) {
            continue;
        }
        size = (size_t)elf_section_size(sec);
        if (elf_section_type(sec) == SHT_NOBITS) {
            if (set_section_zero_data(sec, size + (size_t)os->pad) != 0) {
                return -1;
            }
        } else {
            const void *old = elf_section_data(sec, &size);
            uint8_t *buf = (uint8_t *)calloc(1, size + (size_t)os->pad);
            int ok;

            if (buf == NULL) {
                return -1;
            }
            if (old != NULL && size != 0) {
                memcpy(buf, old, size);
            }
            ok = elf_section_set_data(sec, buf, size + (size_t)os->pad) == ELF_OK;
            free(buf);
            if (!ok) {
                return -1;
            }
        }
        os->pad = 0;
    }
    return 0;
}
