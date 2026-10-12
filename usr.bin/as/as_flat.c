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
    /* Where in the image the section begins, once it has been put. */
    size_t base;
    bytebuf_t buf;
} bin_section_t;

/* A number in the image that is a place in it: `.long label`.  Where a
 * section begins is not known until all of them are written, so what is
 * known is kept -- which section, how far into it -- and the number is
 * written when the image is put together. */
typedef struct {
    size_t field_section;
    size_t field_off;
    unsigned width;
    size_t target_section;
    long long target_off;
} bin_patch_t;

typedef struct {
    bin_section_t *items;
    size_t count;
    size_t cap;
    bin_patch_t *patches;
    size_t patch_count;
    size_t patch_cap;
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
    free(v->patches);
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

/*
 * The numbers of `.byte`, `.word`, `.long` and `.quad` in a flat image,
 * written over what the one emitter of them put at `at`: that emitter
 * leaves a symbol's place for the linker, and an image has none.  What
 * is the distance between two labels is that; what is a label is how
 * far it is from the number; and a symbol of nowhere is 0.
 */
static int flat_patch_push(bin_section_vec_t *v, const bin_section_t *sec, size_t field_off, unsigned width,
                           size_t target_section, long long target_off) {
    bin_patch_t *p;

    if (v->patch_count == v->patch_cap) {
        size_t cap = v->patch_cap != 0 ? v->patch_cap * 2 : 16;
        bin_patch_t *grown = (bin_patch_t *)realloc(v->patches, cap * sizeof(*grown));

        if (grown == NULL) {
            return -1;
        }
        v->patches = grown;
        v->patch_cap = cap;
    }
    p = &v->patches[v->patch_count++];
    p->field_section = (size_t)(sec - v->items);
    p->field_off = field_off;
    p->width = width;
    p->target_section = target_section;
    p->target_off = target_off;
    return 0;
}

static int flat_scalar_values(emit_ctx_t *ctx, const as_stmt_t *st, bin_section_vec_t *vec, bin_section_t *sec,
                              const as_directive_t *d, size_t at) {
    unsigned width = directive_fixed_scalar_width(d);
    size_t i;

    if (width == 0 || sec->buf.len < at + (size_t)d->arg_count * width) {
        return 0;
    }
    for (i = 0; i < d->arg_count; ++i) {
        long long v;
        unsigned k;

        if (eval_linux_alt_len_expr(ctx, st, d->args[i], &v) != 0 && parse_int64(d->args[i], &v) != 0 &&
            parse_const_expr_string(d->args[i], &v) != 0 && eval_arg_asm_vars(ctx, st, d->args[i], &v) != 0) {
            as_expr_t *expr = as_parse_expr_string(d->args[i], st->file, st->line);

            if (expr == NULL) {
                set_err(ctx, "%s:%u: '%s' is not an expression", st->file != NULL ? st->file : "<input>", st->line,
                        d->args[i] != NULL ? d->args[i] : "");
                return -1;
            }
            if (eval_abs_local_diff_expr_virtual(ctx, st, expr, &v) != 0) {
                unsigned bits = ctx->cfg != NULL ? ctx->cfg->x86_code_bits : 64u;
                size_t field_off = at + i * width;
                size_t k;
                long long from_start = 0;
                long long from_field = 0;
                int found = 0;

                /*
                 * A place: a label and what is added to it, or `.`.
                 * The evaluator gives a label as its distance from a
                 * place named to it and `.` as none, so it is asked
                 * from the start of each section and again from the
                 * number: the answers differ for a label, which is
                 * then that far into the section, and are the same for
                 * `.`, which is the number's own place.  (This wrote
                 * a label as its distance from the number, and further
                 * with each number of the directive: no address.)
                 */
                for (k = 0; k < vec->count && !found; ++k) {
                    if (eval_local_rel_expr_virtual(ctx, vec->items[k].name, st, 0, bits, expr, &from_start) == 0 &&
                        eval_local_rel_expr_virtual(ctx, vec->items[k].name, st, 1, bits, expr, &from_field) == 0) {
                        found = 1;
                        if (from_start == from_field) {
                            if (flat_patch_push(vec, sec, field_off, width, (size_t)(sec - vec->items),
                                                (long long)field_off + from_start) != 0) {
                                as_expr_free(expr);
                                return -1;
                            }
                        } else if (flat_patch_push(vec, sec, field_off, width, k, from_start) != 0) {
                            as_expr_free(expr);
                            return -1;
                        }
                    }
                }
                as_expr_free(expr);
                if (!found) {
                    set_err(ctx, "%s:%u: '%s' is no place in the image: a flat image has no relocations",
                            st->file != NULL ? st->file : "<input>", st->line, d->args[i]);
                    return -1;
                }
                continue;
            }
            as_expr_free(expr);
        }
        for (k = 0; k < width; ++k) {
            sec->buf.data[at + i * width + k] = (unsigned char)(((unsigned long long)v >> (k * 8u)) & 0xffu);
        }
    }
    return 0;
}

/*
 * The bytes of a data directive, in a flat image: what they are in an
 * object, from the one emitter of them there is.  (A symbol's place is
 * the linker's to put in an object and nobody's here, and is written
 * 0 in both; the distance between two labels is a number in both.)
 *
 * The image had an emitter of its own: the strings, the numbers, the
 * space, the fills, the alignments and `.org` over again, 330 lines,
 * to be kept in step with the other by hand.
 */
static int append_data_directive_flat(emit_ctx_t *ctx, const as_stmt_t *st, bin_section_vec_t *vec,
                                      bin_section_t *sec, int *handled) {
    const as_directive_t *d;
    size_t before;
    int rc;

    if (ctx == NULL || st == NULL || sec == NULL || handled == NULL || st->kind != AS_STMT_DIRECTIVE) {
        return -1;
    }
    d = &st->u.directive;
    *handled = 0;
    before = sec->buf.len;
    /* (Whether padding is of instructions is the section's flags to
     * say, which an image has in its own table and not in an object.) */
    ctx->flat_image = 1;
    ctx->flat_section_exec = (sec->flags & SHF_EXECINSTR) != 0;
    ctx->pad_after_data = sec->after_data;
    rc = append_directive_data_ctx(ctx, &sec->buf, sec->name, st, (uint64_t)sec->buf.len, sec->code_bits, d);
    ctx->pad_after_data = 0;
    if (rc > 0 && flat_scalar_values(ctx, st, vec, sec, d, before) != 0) {
        return -1;
    }
    if (rc < 0) {
        if (ctx->errbuf != NULL && ctx->errbuf[0] == '\0') {
            if (as_strcmp(d->name, ".org") == 0) {
                set_err(ctx, "%s:%u: .org: not a place at or after this one in its section",
                        st->file != NULL ? st->file : "<input>", st->line);
            } else {
                set_err(ctx, "%s:%u: malformed %s directive data", st->file != NULL ? st->file : "<input>",
                        st->line, d->name);
            }
        }
        return -1;
    }
    if (rc > 0) {
        *handled = 1;
        sec->touched = 1;
        if (as_strcmp(d->name, ".align") == 0 || as_strcmp(d->name, ".balign") == 0 ||
            as_strcmp(d->name, ".p2align") == 0) {
            if (sec->buf.len != before) {
                sec->after_data = 0;
            }
        }
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

static int write_binary_file(emit_ctx_t *ctx, bin_section_vec_t *v, const char *out_path) {
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
            bin_section_t *s = &v->items[j];
            size_t align;
            size_t need;
            if (used[j] || as_strcmp(s->name, first_order[i]) != 0) {
                continue;
            }
            if (s->buf.len == 0 && !s->touched) {
                s->base = out.len;
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
            s->base = out.len;
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
        bin_section_t *s = &v->items[i];
        size_t align;
        size_t need;
        if (used[i] || as_strcmp(s->name, ".bss") == 0) {
            continue;
        }
        if (s->buf.len == 0 && !s->touched) {
            s->base = out.len;
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
        s->base = out.len;
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
        bin_section_t *s = &v->items[i];
        size_t align;
        size_t need;
        if (used[i] || as_strcmp(s->name, ".bss") != 0) {
            continue;
        }
        if (s->buf.len == 0 && !s->touched) {
            s->base = out.len;
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
        s->base = out.len;
        if (s->buf.len > 0 && bytebuf_append_zeros(&out, s->buf.len) != 0) {
            free(used);
            free(out.data);
            return -1;
        }
        used[i] = 1;
    }

    free(used);
    /* The numbers that are places in the image, now that each section
     * has one. */
    for (i = 0; i < v->patch_count; ++i) {
        const bin_patch_t *p = &v->patches[i];
        const bin_section_t *in = &v->items[p->field_section];
        unsigned long long value = (unsigned long long)((long long)v->items[p->target_section].base + p->target_off);
        size_t at = in->base + p->field_off;
        unsigned k;

        if (in->type == SHT_NOBITS || at + p->width > out.len) {
            continue;
        }
        for (k = 0; k < p->width; ++k) {
            out.data[at + k] = (unsigned char)((value >> (k * 8u)) & 0xffu);
        }
    }
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
                if (append_data_directive_flat(&ctx, st, &sv, sec, &handled) != 0) {
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
