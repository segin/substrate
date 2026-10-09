/*
 * ld_gc.c -- --gc-sections and identical code folding.
 */

#include "ld.h"

static int is_gc_candidate_section(const elf_section_t *sec) {
    const char *name;
    uint64_t flags;
    uint32_t type;

    if (sec == NULL) {
        return 0;
    }
    name = elf_section_name(sec);
    type = elf_section_type(sec);
    flags = elf_section_flags(sec);
    if ((flags & SHF_ALLOC) == 0) {
        return 0;
    }
    if (type == SHT_NULL || type == SHT_SYMTAB || type == SHT_STRTAB || type == SHT_REL || type == SHT_RELA) {
        return 0;
    }
    if (name != NULL &&
        (strcmp(name, ".shstrtab") == 0 || strcmp(name, ".symtab") == 0 || strcmp(name, ".strtab") == 0 ||
         strcmp(name, ".group") == 0)) {
        return 0;
    }
    return 1;
}

/*
 * --gc-sections: leaving out what nothing uses.
 *
 * What can be left out is a section, and the sections worth asking about
 * are the inputs' -- a compiler given -ffunction-sections and
 * -fdata-sections puts each function and variable in one of its own for
 * exactly this.  Once the inputs are merged there is one .text, and it is
 * either used or not; so this runs before the merge, on the input objects,
 * and hands the result to the merge as a list of sections not to take.
 *
 * A section is live if it is a root, or a live section refers to it: the
 * symbol a relocation names is in some section, of this object or, for a
 * global, of whichever object the link takes its definition from.  The
 * roots are what is reached from outside the code that can be seen:
 *
 *   - the section the entry is in, and each symbol named by -u;
 *   - what the output exports: in a shared object, or with
 *     --export-dynamic, every global of default visibility; in a program,
 *     what the shared objects of the link refer to;
 *   - what the startup code runs without being called by name: .init and
 *     .fini, the constructor and destructor arrays and tables;
 *   - notes, and sections flagged SHF_GNU_RETAIN;
 *   - sections whose name is a C identifier, since code may walk one from
 *     __start_NAME to __stop_NAME without naming anything in it;
 *   - what a linker script says to KEEP.
 *
 * Two kinds of section point at code without using it.  .eh_frame has a
 * record for every function, and following those would keep every
 * function: it is kept whole and only its references to global symbols
 * are followed (the personality routine).  A function's exception table,
 * .gcc_except_table.NAME, is referred to from .eh_frame alone, so it is
 * found from there once the function is known to be live
 * (gc_mark_exception_tables).  What .eh_frame says about a function that
 * is gone is left as it was, pointing nowhere that is code.
 *
 * Sections that are not loaded (debugging information) are neither
 * roots nor candidates: they stay, and are not followed.
 */
typedef struct {
    const ld_ctx_t *ctx;
    const objvec_t *inputs;
    const ld_symtab_t *symtab;  /* which definition of a name the link takes */
    uint8_t **live;             /* live[i][s]: section s of input i */
    size_t *work;               /* pairs: input, section */
    size_t work_count;
    size_t work_cap;
} gc_state_t;

static int gc_symbol_is_defined(const elf_symbol_t *sym) {
    uint16_t shndx = elf_symbol_shndx(sym);

    return shndx != SHN_UNDEF && shndx != SHN_ABS && shndx != SHN_COMMON && shndx < 0xff00;
}

/*
 * Which of an object's sections a section number means.  An object read
 * from a file has the null section the format begins with as its first,
 * so the numbers are the positions; one built in memory does not, and
 * they are one more.
 */
static size_t gc_section_of(const elfobj_t *o, uint32_t shndx) {
    const elf_section_t *first = elf_section_count(o) != 0 ? elf_section_get(o, 0) : NULL;

    return first != NULL && elf_section_type(first) == SHT_NULL ? (size_t)shndx : (size_t)shndx - 1;
}

static int gc_mark(gc_state_t *st, size_t obj, size_t sec_index) {
    elfobj_t *o = st->inputs->objs[obj];

    if (sec_index >= elf_section_count(o) || st->live[obj][sec_index] ||
        !is_gc_candidate_section(elf_section_get(o, sec_index))) {
        return 0;
    }
    st->live[obj][sec_index] = 1;
    /* Two are pushed: room for one more than one more. */
    if (ld_vec_room(&st->work, &st->work_cap, st->work_count + 1, sizeof(st->work[0])) != 0) {
        return -1;
    }
    st->work[st->work_count++] = obj;
    st->work[st->work_count++] = sec_index;
    return 0;
}

/* The section a symbol met in input `obj` is in, as the link will bind it. */
static int gc_mark_symbol(gc_state_t *st, size_t obj, const elf_symbol_t *sym) {
    const char *name = sym != NULL ? elf_symbol_name(sym) : NULL;

    if (sym == NULL) {
        return 0;
    }
    if (elf_symbol_bind(sym) != STB_LOCAL && name != NULL && name[0] != '\0') {
        const ld_sym_t *known = ld_symtab_find(st->symtab, name);

        if (known != NULL && known->def != NULL) {
            return gc_symbol_is_defined(known->def)
                       ? gc_mark(st, known->def_input,
                                 gc_section_of(st->inputs->objs[known->def_input], elf_symbol_shndx(known->def)))
                       : 0;
        }
    }
    return gc_symbol_is_defined(sym)
               ? gc_mark(st, obj, gc_section_of(st->inputs->objs[obj], elf_symbol_shndx(sym))) : 0;
}

/* A section group stands or falls together. */
static int gc_mark_group_of(gc_state_t *st, size_t obj, size_t sec_index) {
    elfobj_t *o = st->inputs->objs[obj];
    size_t g;

    for (g = 0; g < elf_section_count(o); ++g) {
        const elf_section_t *grp = elf_section_get(o, g);
        const uint8_t *words;
        size_t sz = 0, w;
        int member = 0;

        if (grp == NULL || elf_section_type(grp) != SHT_GROUP) {
            continue;
        }
        words = (const uint8_t *)elf_section_data(grp, &sz);
        for (w = 1; words != NULL && (w + 1) * 4 <= sz; ++w) {
            uint32_t idx = read_u32_endian(words + w * 4, elf_endian(o));

            member |= idx != 0 && gc_section_of(o, idx) == sec_index;
        }
        for (w = 1; member && (w + 1) * 4 <= sz; ++w) {
            uint32_t idx = read_u32_endian(words + w * 4, elf_endian(o));

            if (idx != 0 && gc_mark(st, obj, gc_section_of(o, idx)) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int gc_name_is(const char *name, const char *prefix) {
    size_t n = strlen(prefix);

    return name != NULL && strncmp(name, prefix, n) == 0 && (name[n] == '\0' || name[n] == '.');
}

static int gc_name_is_c_identifier(const char *name) {
    size_t i;

    if (name == NULL || !(isalpha((unsigned char)name[0]) || name[0] == '_')) {
        return 0;
    }
    for (i = 1; name[i] != '\0'; ++i) {
        if (!(isalnum((unsigned char)name[i]) || name[i] == '_')) {
            return 0;
        }
    }
    return 1;
}

static int gc_section_is_root(const gc_state_t *st, size_t obj, const elf_section_t *sec) {
    const char *name = elf_section_name(sec);
    uint32_t type = elf_section_type(sec);

    return type == SHT_INIT_ARRAY || type == SHT_FINI_ARRAY || type == SHT_PREINIT_ARRAY || type == SHT_NOTE ||
           (elf_section_flags(sec) & SHF_GNU_RETAIN) != 0 ||
           gc_name_is(name, ".init") || gc_name_is(name, ".fini") || gc_name_is(name, ".ctors") ||
           gc_name_is(name, ".dtors") || gc_name_is(name, ".init_array") || gc_name_is(name, ".fini_array") ||
           gc_name_is(name, ".preinit_array") || gc_name_is(name, ".eh_frame") || gc_name_is(name, ".jcr") ||
           gc_name_is_c_identifier(name) ||
           (st->ctx->script != NULL && st->ctx->script->has_sections &&
            script_keeps_input(st->ctx->script, name, st->inputs->names[obj]));
}

/* Follow everything on the list, which marking adds to. */
static int gc_drain(gc_state_t *st) {
    while (st->work_count != 0) {
        size_t sec_index = st->work[--st->work_count];
        size_t obj = st->work[--st->work_count];
        elf_section_t *sec = elf_section_get(st->inputs->objs[obj], sec_index);
        int unwind = gc_name_is(elf_section_name(sec), ".eh_frame");
        size_t ri;

        for (ri = 0; ri < elf_section_reloc_count(sec); ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
            const elf_symbol_t *sym = rel != NULL ? elf_reloc_symbol(rel) : NULL;

            if (sym == NULL || (unwind && elf_symbol_bind(sym) == STB_LOCAL)) {
                continue;
            }
            if (gc_mark_symbol(st, obj, sym) != 0) {
                return -1;
            }
        }
        if ((elf_section_flags(sec) & SHF_GROUP) != 0 && gc_mark_group_of(st, obj, sec_index) != 0) {
            return -1;
        }
    }
    return 0;
}

/*
 * The exception tables of the functions that are live.  Nothing but
 * .eh_frame says which table is whose: a function's record there has the
 * function's address and then, among what follows, its table's.  So the
 * record's relocations are read in order -- one against code names the
 * function, and one against an exception table after it, before the next
 * against code, names that function's table.  Returns 1 if a table was
 * marked, 0 if none, -1 on failure.
 */
static int gc_mark_exception_tables(gc_state_t *st, size_t obj, elf_section_t *eh) {
    elfobj_t *o = st->inputs->objs[obj];
    size_t function = (size_t)-1;
    int marked = 0;
    size_t ri;

    for (ri = 0; ri < elf_section_reloc_count(eh); ++ri) {
        const elf_reloc_t *rel = elf_section_reloc_at(eh, ri);
        const elf_symbol_t *sym = rel != NULL ? elf_reloc_symbol(rel) : NULL;
        const elf_section_t *target;
        size_t t;

        if (sym == NULL || !gc_symbol_is_defined(sym)) {
            continue;
        }
        t = gc_section_of(o, elf_symbol_shndx(sym));
        target = t < elf_section_count(o) ? elf_section_get(o, t) : NULL;
        if (target == NULL) {
            continue;
        }
        if ((elf_section_flags(target) & SHF_EXECINSTR) != 0) {
            function = t;
        } else if (gc_name_is(elf_section_name(target), ".gcc_except_table") && function != (size_t)-1 &&
                   st->live[obj][function] && !st->live[obj][t]) {
            if (gc_mark(st, obj, t) != 0) {
                return -1;
            }
            marked = 1;
        }
    }
    return marked;
}

static int gc_section_ptr_cmp(const void *a, const void *b) {
    const elf_section_t *x = *(const elf_section_t *const *)a;
    const elf_section_t *y = *(const elf_section_t *const *)b;

    return x < y ? -1 : x > y;
}

/* The elfobj hook: whether this input section goes into the output. */
int gc_keep_input_section(const elf_section_t *section, void *user) {
    const ld_ctx_t *ctx = (const ld_ctx_t *)user;
    size_t lo = 0, hi = ctx->gc_dead_count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (ctx->gc_dead[mid] < section) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return !(lo < ctx->gc_dead_count && ctx->gc_dead[lo] == section);
}

int gc_collect_input_sections(ld_ctx_t *ctx, const objvec_t *inputs, const ld_symtab_t *symtab) {
    gc_state_t st;
    size_t i, s, total = 0;
    int export_all = (ctx->opt.expect_type == ET_DYN && !ctx->opt.pie) || ctx->opt.export_dynamic;
    const char *entry = ctx->opt.entry_symbol != NULL && ctx->opt.entry_symbol[0] != '\0' ? ctx->opt.entry_symbol : "_start";
    int progress;
    int rc = -1;

    memset(&st, 0, sizeof(st));
    st.ctx = ctx;
    st.inputs = inputs;
    st.symtab = symtab;
    ctx->gc_dead_count = 0;
    st.live = (uint8_t **)calloc(inputs->count ? inputs->count : 1, sizeof(st.live[0]));
    if (st.live == NULL) {
        return -1;
    }
    for (i = 0; i < inputs->count; ++i) {
        total += elf_section_count(inputs->objs[i]);
        st.live[i] = (uint8_t *)calloc(elf_section_count(inputs->objs[i]) + 1, 1);
        if (st.live[i] == NULL) {
            goto out;
        }
    }

    /* The roots. */
    {
        const ld_sym_t *known = ld_symtab_find(symtab, entry);

        if (known != NULL && known->def != NULL && gc_mark_symbol(&st, known->def_input, known->def) != 0) {
            goto out;
        }
    }
    for (i = 0; i < ctx->opt.force_undefined.count; ++i) {
        const ld_sym_t *known = ld_symtab_find(symtab, ctx->opt.force_undefined.items[i]);

        if (known != NULL && known->def != NULL && gc_mark_symbol(&st, known->def_input, known->def) != 0) {
            goto out;
        }
    }
    for (i = 0; i < symtab->count; ++i) {
        const ld_sym_t *known = &symtab->syms[i];
        uint8_t vis = known->def != NULL ? elf_symbol_visibility(known->def) : STV_HIDDEN;

        if ((vis == STV_DEFAULT || vis == STV_PROTECTED) &&
            (export_all || symset_contains(&ctx->dso_wants, known->name)) &&
            gc_mark_symbol(&st, known->def_input, known->def) != 0) {
            goto out;
        }
    }
    for (i = 0; i < inputs->count; ++i) {
        for (s = 0; s < elf_section_count(inputs->objs[i]); ++s) {
            const elf_section_t *sec = elf_section_get(inputs->objs[i], s);

            if (sec != NULL && is_gc_candidate_section(sec) && gc_section_is_root(&st, i, sec) &&
                gc_mark(&st, i, s) != 0) {
                goto out;
            }
        }
    }

    /* Everything they reach; then the exception tables of what was
     * reached, and whatever those reach; until nothing is added. */
    do {
        progress = 0;
        if (gc_drain(&st) != 0) {
            goto out;
        }
        for (i = 0; i < inputs->count; ++i) {
            for (s = 0; s < elf_section_count(inputs->objs[i]); ++s) {
                elf_section_t *sec = elf_section_get(inputs->objs[i], s);
                int marked;

                if (sec == NULL || !gc_name_is(elf_section_name(sec), ".eh_frame")) {
                    continue;
                }
                marked = gc_mark_exception_tables(&st, i, sec);
                if (marked < 0) {
                    goto out;
                }
                progress |= marked;
            }
        }
    } while (progress);

    /* What is left is what the merge is to pass over. */
    free((void *)ctx->gc_dead);
    ctx->gc_dead = (const elf_section_t **)calloc(total ? total : 1, sizeof(ctx->gc_dead[0]));
    if (ctx->gc_dead == NULL) {
        goto out;
    }
    for (i = 0; i < inputs->count; ++i) {
        for (s = 0; s < elf_section_count(inputs->objs[i]); ++s) {
            const elf_section_t *sec = elf_section_get(inputs->objs[i], s);

            if (sec == NULL || st.live[i][s] || !is_gc_candidate_section(sec)) {
                continue;
            }
            ctx->gc_dead[ctx->gc_dead_count++] = sec;
            if (ctx->opt.gc_print_sections) {
                fprintf(stderr, "ld: gc-sections: removing %s in %s\n",
                        elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>",
                        inputs->names[i] != NULL ? inputs->names[i] : "?");
            }
        }
    }
    qsort((void *)ctx->gc_dead, ctx->gc_dead_count, sizeof(ctx->gc_dead[0]), gc_section_ptr_cmp);
    rc = 0;
out:
    for (i = 0; i < inputs->count; ++i) {
        free(st.live[i]);
    }
    free(st.live);
    free(st.work);
    return rc;
}

static int is_icf_special_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    return strcmp(name, ".init_array") == 0 || strcmp(name, ".fini_array") == 0 ||
           strcmp(name, ".preinit_array") == 0 || strcmp(name, ".dynamic") == 0 ||
           strcmp(name, ".dynsym") == 0 || strcmp(name, ".dynstr") == 0;
}

static int is_icf_candidate_section(const elf_section_t *sec, int icf_mode) {
    uint64_t flags;
    uint32_t type;
    const char *name;

    if (sec == NULL || icf_mode == 0) {
        return 0;
    }
    type = elf_section_type(sec);
    flags = elf_section_flags(sec);
    name = elf_section_name(sec);
    if (type != SHT_PROGBITS || (flags & SHF_ALLOC) == 0) {
        return 0;
    }
    if ((flags & SHF_GROUP) != 0) {
        return 0;
    }
    if (is_icf_special_name(name)) {
        return 0;
    }
    if (icf_mode == 1 && (flags & SHF_WRITE) != 0) {
        return 0;
    }
    return 1;
}

static int sections_reloc_signature_equal(const elf_section_t *a, const elf_section_t *b) {
    size_t ra_n;
    size_t rb_n;
    size_t i;

    if (a == NULL || b == NULL) {
        return 0;
    }
    ra_n = elf_section_reloc_count(a);
    rb_n = elf_section_reloc_count(b);
    if (ra_n != rb_n) {
        return 0;
    }
    for (i = 0; i < ra_n; ++i) {
        const elf_reloc_t *ra = elf_section_reloc_at((elf_section_t *)a, i);
        const elf_reloc_t *rb = elf_section_reloc_at((elf_section_t *)b, i);
        const elf_symbol_t *sa;
        const elf_symbol_t *sb;
        const char *na = NULL;
        const char *nb = NULL;

        if (ra == NULL || rb == NULL) {
            return 0;
        }
        if (elf_reloc_offset(ra) != elf_reloc_offset(rb) || elf_reloc_type(ra) != elf_reloc_type(rb) ||
            elf_reloc_addend(ra) != elf_reloc_addend(rb) || elf_reloc_has_addend(ra) != elf_reloc_has_addend(rb)) {
            return 0;
        }
        sa = elf_reloc_symbol(ra);
        sb = elf_reloc_symbol(rb);
        na = sa != NULL ? elf_symbol_name(sa) : NULL;
        nb = sb != NULL ? elf_symbol_name(sb) : NULL;
        if ((na == NULL) != (nb == NULL)) {
            return 0;
        }
        if (na != NULL && strcmp(na, nb) != 0) {
            return 0;
        }
    }
    return 1;
}

static int sections_icf_equal(const elf_section_t *a, const elf_section_t *b) {
    size_t asz = 0;
    size_t bsz = 0;
    const void *ad;
    const void *bd;

    if (a == NULL || b == NULL) {
        return 0;
    }
    if (elf_section_type(a) != elf_section_type(b) || elf_section_flags(a) != elf_section_flags(b) ||
        elf_section_align(a) != elf_section_align(b) || elf_section_size(a) != elf_section_size(b)) {
        return 0;
    }
    ad = elf_section_data(a, &asz);
    bd = elf_section_data(b, &bsz);
    if (asz != bsz) {
        return 0;
    }
    if (asz != 0 && (ad == NULL || bd == NULL || memcmp(ad, bd, asz) != 0)) {
        return 0;
    }
    if (!sections_reloc_signature_equal(a, b)) {
        return 0;
    }
    return 1;
}

static int icf_fold_section(elfobj_t *obj, size_t leader_idx, size_t dupe_idx) {
    size_t i;
    elf_section_t *dupe;
    uint16_t dupe_shndx;
    uint16_t leader_shndx;

    if (obj == NULL || leader_idx >= elf_section_count(obj) || dupe_idx >= elf_section_count(obj)) {
        return -1;
    }
    dupe = elf_section_get(obj, dupe_idx);
    if (dupe == NULL) {
        return -1;
    }
    dupe_shndx = (uint16_t)(dupe_idx + 1);
    leader_shndx = (uint16_t)(leader_idx + 1);
    for (i = 0; i < elf_symbol_count(obj); ++i) {
        elf_symbol_t *sym = elf_symbol_at(obj, i);
        if (sym == NULL) {
            continue;
        }
        if (elf_symbol_shndx(sym) == dupe_shndx) {
            if (elf_symbol_set_shndx(sym, leader_shndx) != ELF_OK) {
                return -1;
            }
        }
    }
    if (elf_remove_section(obj, dupe) != ELF_OK) {
        return -1;
    }
    return 0;
}

int apply_identical_code_folding(elfobj_t *obj, const ld_ctx_t *ctx) {
    size_t i;

    if (obj == NULL || ctx == NULL || ctx->opt.icf_mode == 0) {
        return 0;
    }
    for (i = 0; i < elf_section_count(obj); ++i) {
        elf_section_t *leader = elf_section_get(obj, i);
        size_t j;

        if (!is_icf_candidate_section(leader, ctx->opt.icf_mode)) {
            continue;
        }
        for (j = i + 1; j < elf_section_count(obj);) {
            elf_section_t *dupe = elf_section_get(obj, j);
            if (!is_icf_candidate_section(dupe, ctx->opt.icf_mode) || !sections_icf_equal(leader, dupe)) {
                j++;
                continue;
            }
            if (icf_fold_section(obj, i, j) != 0) {
                return -1;
            }
        }
    }
    return 0;
}
