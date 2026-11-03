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
    uint32_t shndx = elf_symbol_shndx(sym);

    return shndx != SHN_UNDEF && !ELF_SHNDX_RESERVED(shndx);
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

/**
 * The section a symbol met in input @p obj is in, as the link will bind it: a global's in whichever input
 * the link takes its definition from, anything else's in @p obj.
 *
 * @param inputs  the inputs
 * @param symtab  which definition of a name the link takes
 * @param obj     the input the symbol was met in
 * @param sym     the symbol
 * @param in      set to the input the section is of
 * @param sec     set to the section's position in that input
 * @return 1 if the symbol is in a section of an input, 0 if not (undefined, absolute, common, or a
 *         shared object's)
 */
static int gc_symbol_section(const objvec_t *inputs, const ld_symtab_t *symtab, size_t obj,
                             const elf_symbol_t *sym, size_t *in, size_t *sec) {
    const char *name = sym != NULL ? elf_symbol_name(sym) : NULL;

    if (sym == NULL) {
        return 0;
    }
    if (elf_symbol_bind(sym) != STB_LOCAL && name != NULL && name[0] != '\0') {
        const ld_sym_t *known = ld_symtab_find(symtab, name);

        if (known != NULL && known->def != NULL) {
            if (!gc_symbol_is_defined(known->def)) {
                return 0;
            }
            *in = known->def_input;
            *sec = gc_section_of(inputs->objs[known->def_input], elf_symbol_shndx(known->def));
            return 1;
        }
    }
    if (!gc_symbol_is_defined(sym)) {
        return 0;
    }
    *in = obj;
    *sec = gc_section_of(inputs->objs[obj], elf_symbol_shndx(sym));
    return 1;
}

/* The section a symbol met in input `obj` is in, as the link will bind it. */
static int gc_mark_symbol(gc_state_t *st, size_t obj, const elf_symbol_t *sym) {
    size_t in, sec;

    return gc_symbol_section(st->inputs, st->symtab, obj, sym, &in, &sec) ? gc_mark(st, in, sec) : 0;
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

/*
 * --icf: identical code folding.
 *
 * What is folded is an input section: a compiler given -ffunction-sections
 * puts each function in one of its own, and two functions compiled to the
 * same bytes, referring to the same things, can be one.  Once the inputs
 * are merged every .text.NAME is part of one .text and there is nothing
 * left to compare, so this runs before the merge, as --gc-sections does,
 * and tells the merge which sections are alike; the merge lays out the
 * first of each class and puts the rest where it is (elfobj's fold hook).
 *
 * Code is folded in both modes, and read-only data (.rodata) under
 * --icf=all, as lld folds it.  A section of a group (COMDAT: a template's
 * instance, an inline function) is folded as any other; one of a group
 * the link drops is never laid out, and the first of its class that is
 * stands for it.  Nothing writable is: two variables that start out alike
 * do not stay so.  Nor is a section whose contents the merge rewrites
 * (strings and constants, TLS, compressed), or one ordered by another.
 * Nor is code with an LSDA, whose FDE says what its handlers catch
 * (icf_note_lsda_sections).
 *
 * Sections are alike when their bytes and the places, kinds and addends
 * of their relocations are, and each relocation refers to the same thing
 * -- or to the same place in two sections that are alike, found by
 * refining the classes until none splits (icf_refine_classes), as gold
 * iterates: two callers of two identical functions are identical.
 *
 * --icf=safe also leaves out code whose address is taken, as gold does:
 * two functions are two addresses in C, and a program may compare
 * pointers to them.  What only calls a function, or jumps to it, cannot
 * tell where it is; every other reference to it might be the making of a
 * pointer (icf_note_taken_addresses).
 */
static int is_icf_candidate_section(const elf_section_t *sec, int icf_mode) {
    uint64_t flags;
    const char *name;

    if (sec == NULL || icf_mode == 0 || elf_section_type(sec) != SHT_PROGBITS || elf_section_size(sec) == 0) {
        return 0;
    }
    flags = elf_section_flags(sec);
    name = elf_section_name(sec);
    if ((flags & SHF_ALLOC) == 0 ||
        (flags & (SHF_WRITE | SHF_TLS | SHF_MERGE | SHF_LINK_ORDER | SHF_COMPRESSED)) != 0) {
        return 0;
    }
    if ((flags & SHF_EXECINSTR) != 0 && gc_name_is(name, ".text")) {
        return 1;
    }
    return icf_mode == 2 && gc_name_is(name, ".rodata");
}

/**
 * Whether sections @p a and @p b are alike but for what their relocations refer to: in kind, flags,
 * alignment, size and contents, with relocations at the same places, of the same kinds and addends.
 *
 * @param a a section
 * @param b another
 * @return 1 if they are, 0 if not
 */
static int sections_icf_shape_equal(const elf_section_t *a, const elf_section_t *b) {
    size_t asz = 0;
    size_t bsz = 0;
    size_t n, i;
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
    n = elf_section_reloc_count(a);
    if (n != elf_section_reloc_count(b)) {
        return 0;
    }
    for (i = 0; i < n; ++i) {
        const elf_reloc_t *ra = elf_section_reloc_at((elf_section_t *)a, i);
        const elf_reloc_t *rb = elf_section_reloc_at((elf_section_t *)b, i);

        if (ra == NULL || rb == NULL || elf_reloc_offset(ra) != elf_reloc_offset(rb) ||
            elf_reloc_type(ra) != elf_reloc_type(rb) || elf_reloc_addend(ra) != elf_reloc_addend(rb) ||
            elf_reloc_has_addend(ra) != elf_reloc_has_addend(rb)) {
            return 0;
        }
    }
    return 1;
}

/* What the comparison of two candidates' relocations needs, while the classes are being refined. */
typedef struct {
    const objvec_t *inputs;
    const ld_symtab_t *symtab;
    size_t **cand_at;           /* cand_at[i][s]: 1 + the candidate section s of input i is, 0 if none */
    const size_t *cls;          /* each candidate's class, as the last round left it */
    int preempt;                /* a global of default visibility may be bound elsewhere at run time */
} icf_cmp_t;

/**
 * Where symbol @p sym, met in input @p obj, is as the link binds it: the input and section it is defined in
 * and its value there.
 *
 * @param c     the comparison
 * @param obj   the input it was met in
 * @param sym   the symbol
 * @param in    set to the input of its definition
 * @param at    set to the section of that input
 * @param value set to its value
 * @return 1, or 0 if it is not defined in a section of an input
 */
static int icf_ref_target(const icf_cmp_t *c, size_t obj, const elf_symbol_t *sym, size_t *in, size_t *at,
                          uint64_t *value) {
    const char *name = elf_symbol_name(sym);
    const elf_symbol_t *def = sym;

    if (!gc_symbol_section(c->inputs, c->symtab, obj, sym, in, at) ||
        *at >= elf_section_count(c->inputs->objs[*in])) {
        return 0;
    }
    if (elf_symbol_bind(sym) != STB_LOCAL && name != NULL && name[0] != '\0') {
        const ld_sym_t *known = ld_symtab_find(c->symtab, name);

        if (known != NULL && known->def != NULL) {
            def = known->def;
        }
    }
    *value = elf_symbol_value(def);
    return 1;
}

/**
 * Whether symbol @p sa, referred to from input @p ia, and @p sb, from input @p ib, are the same thing as far
 * as folding goes.  One global name is one thing, the link binding it once.  A global that may be bound
 * elsewhere at run time (a shared object's, of default visibility, without -Bsymbolic) is nothing but its
 * name.  Anything else is the place the link binds it to: the same place in the same section, or the same
 * place in two sections of one class -- which are taken to be one, so that two functions calling two
 * identical ones are identical too.
 *
 * @param c  the comparison
 * @param ia the input @p sa was met in
 * @param sa a symbol
 * @param ib the input @p sb was met in
 * @param sb another
 * @return 1 if they are the same, 0 if not
 */
static int icf_refs_equal(const icf_cmp_t *c, size_t ia, const elf_symbol_t *sa, size_t ib,
                          const elf_symbol_t *sb) {
    const char *na;
    const char *nb;
    int ga, gb;
    size_t ina, ata, inb, atb, ka, kb;
    uint64_t va, vb;

    if ((sa == NULL) != (sb == NULL)) {
        return 0;
    }
    if (sa == NULL || sa == sb) {
        return 1;
    }
    na = elf_symbol_name(sa);
    nb = elf_symbol_name(sb);
    ga = elf_symbol_bind(sa) != STB_LOCAL && na != NULL && na[0] != '\0';
    gb = elf_symbol_bind(sb) != STB_LOCAL && nb != NULL && nb[0] != '\0';
    if (ga && gb && strcmp(na, nb) == 0) {
        return 1;
    }
    if (c->preempt && ((ga && elf_symbol_visibility(sa) == STV_DEFAULT) ||
                       (gb && elf_symbol_visibility(sb) == STV_DEFAULT))) {
        return 0;
    }
    if (!icf_ref_target(c, ia, sa, &ina, &ata, &va) || !icf_ref_target(c, ib, sb, &inb, &atb, &vb) || va != vb) {
        return 0;
    }
    if (ina == inb && ata == atb) {
        return 1;
    }
    ka = c->cand_at[ina][ata];
    kb = c->cand_at[inb][atb];
    return ka != 0 && kb != 0 && c->cls[ka - 1] == c->cls[kb - 1];
}

/**
 * Whether candidates @p a (of input @p ia) and @p b (of input @p ib), alike in shape
 * (sections_icf_shape_equal()), refer to the same things at each of their relocations (icf_refs_equal()).
 *
 * @param c  the comparison
 * @param a  a section
 * @param ia its input
 * @param b  another
 * @param ib its input
 * @return 1 if they do, 0 if not
 */
static int sections_icf_refs_equal(const icf_cmp_t *c, const elf_section_t *a, size_t ia, const elf_section_t *b,
                                   size_t ib) {
    size_t n = elf_section_reloc_count(a), i;

    for (i = 0; i < n; ++i) {
        const elf_reloc_t *ra = elf_section_reloc_at((elf_section_t *)a, i);
        const elf_reloc_t *rb = elf_section_reloc_at((elf_section_t *)b, i);

        if (ra == NULL || rb == NULL || !icf_refs_equal(c, ia, elf_reloc_symbol(ra), ib, elf_reloc_symbol(rb))) {
            return 0;
        }
    }
    return 1;
}

/**
 * Whether relocation @p r, of section @p sec of an input for @p machine, is the target of a call or a
 * jump: pc-relative (PC32 or PLT32), in code, and the 32-bit displacement of a `call`, `jmp` or `jcc`
 * instruction, by the opcode bytes before it.  Such a reference uses a function without being able to
 * tell its address from another's.
 *
 * @param machine the inputs' e_machine
 * @param sec     the section the relocation applies to
 * @param r       the relocation
 * @return 1 if it is a branch's, 0 if it may take the address of what it names
 */
static int icf_reloc_is_branch(uint16_t machine, const elf_section_t *sec, const elf_reloc_t *r) {
    uint32_t type = elf_reloc_type(r);
    uint64_t off = elf_reloc_offset(r);
    const uint8_t *data;
    size_t size = 0;

    if (machine == EM_386) {
        if (type != R_386_PC32 && type != R_386_PLT32) {
            return 0;
        }
    } else if (machine == EM_X86_64) {
        if (type != R_X86_64_PC32 && type != R_X86_64_PLT32) {
            return 0;
        }
    } else {
        return 0;
    }
    if ((elf_section_flags(sec) & SHF_EXECINSTR) == 0 || off < 1) {
        return 0;
    }
    data = (const uint8_t *)elf_section_data(sec, &size);
    if (data == NULL || off > size) {
        return 0;
    }
    if (data[off - 1] == 0xe8 || data[off - 1] == 0xe9) {
        return 1;               /* call rel32, jmp rel32 */
    }
    return off >= 2 && data[off - 2] == 0x0f && (data[off - 1] & 0xf0) == 0x80;    /* jcc rel32 */
}

/**
 * --icf=safe: mark each input section whose address a relocation of a loaded section may take -- any
 * reference but a call's or a jump's (icf_reloc_is_branch).  .eh_frame's references to the functions it
 * describes take no address a program can see, and are passed over; so are the references of sections that
 * are not loaded (debugging information).
 *
 * @param inputs the inputs
 * @param symtab which definition of a name the link takes
 * @param taken  taken[i][s] set for section s of input i
 */
static void icf_note_taken_addresses(const objvec_t *inputs, const ld_symtab_t *symtab, uint8_t **taken) {
    size_t i, s, ri;

    for (i = 0; i < inputs->count; ++i) {
        elfobj_t *o = inputs->objs[i];

        for (s = 0; s < elf_section_count(o); ++s) {
            elf_section_t *sec = elf_section_get(o, s);

            if (sec == NULL || (elf_section_flags(sec) & SHF_ALLOC) == 0 ||
                gc_name_is(elf_section_name(sec), ".eh_frame")) {
                continue;
            }
            for (ri = 0; ri < elf_section_reloc_count(sec); ++ri) {
                const elf_reloc_t *r = elf_section_reloc_at(sec, ri);
                size_t in, at;

                if (r == NULL || icf_reloc_is_branch(elf_machine(o), sec, r) ||
                    !gc_symbol_section(inputs, symtab, i, elf_reloc_symbol(r), &in, &at)) {
                    continue;
                }
                if (at < elf_section_count(inputs->objs[in])) {
                    taken[in][at] = 1;
                }
            }
        }
    }
}

/* An FDE of an input's .eh_frame, while icf_note_lsda_sections() reads it. */
typedef struct {
    uint64_t start;             /* where its record begins in .eh_frame */
    uint64_t end;               /* and ends */
    const elf_reloc_t *pc;      /* the relocation of its initial location, NULL if none */
    int other;                  /* it has another relocation: its LSDA's */
} icf_fde_t;

/**
 * Mark every section a relocation of .eh_frame section @p sec of input @p i refers to: a frame that cannot
 * be read may name an LSDA for any of them.
 *
 * @param inputs the inputs
 * @param symtab which definition of a name the link takes
 * @param i      the input
 * @param sec    its .eh_frame
 * @param skip   skip[in][s] set for each section referred to
 */
static void icf_note_eh_frame_targets(const objvec_t *inputs, const ld_symtab_t *symtab, size_t i,
                                      const elf_section_t *sec, uint8_t **skip) {
    size_t ri, in, at;

    for (ri = 0; ri < elf_section_reloc_count(sec); ++ri) {
        const elf_reloc_t *r = elf_section_reloc_at((elf_section_t *)sec, ri);

        if (r != NULL && gc_symbol_section(inputs, symtab, i, elf_reloc_symbol(r), &in, &at) &&
            at < elf_section_count(inputs->objs[in])) {
            skip[in][at] = 1;
        }
    }
}

/**
 * Mark each input section whose code has an LSDA: its FDE in .eh_frame names one (in .gcc_except_table),
 * which says what the code's handlers catch and where they are.  Two functions of the same code and
 * relocations may catch different exceptions; the unwinder finds the LSDA by the FDE of the address it
 * unwinds through, and a folded pair has one address, so such a section is never folded, as lld leaves it.
 *
 * An FDE has relocations only for its initial location (8 bytes into its record) and its LSDA pointer: one
 * with another relocation names an LSDA.  An .eh_frame that cannot be read marks every section it refers to.
 *
 * @param inputs the inputs
 * @param symtab which definition of a name the link takes
 * @param skip   skip[i][s] set for section s of input i
 * @return 0, or -1 out of memory
 */
static int icf_note_lsda_sections(const objvec_t *inputs, const ld_symtab_t *symtab, uint8_t **skip) {
    icf_fde_t *fdes = NULL;
    size_t cap = 0, i, s;
    int rc = 0;

    for (i = 0; i < inputs->count && rc == 0; ++i) {
        elfobj_t *o = inputs->objs[i];

        for (s = 0; s < elf_section_count(o) && rc == 0; ++s) {
            const elf_section_t *sec = elf_section_get(o, s);
            const char *name = sec != NULL ? elf_section_name(sec) : NULL;
            const uint8_t *data;
            size_t size = 0, off = 0, n = 0, ri, k, in, at;
            int bad = 0;

            if (name == NULL || strcmp(name, ".eh_frame") != 0 || elf_section_reloc_count(sec) == 0) {
                continue;
            }
            data = (const uint8_t *)elf_section_data(sec, &size);
            if (data == NULL) {
                bad = 1;
            }
            while (!bad && off + 4 <= size) {
                uint32_t len = read_u32_endian(data + off, elf_endian(o));

                if (len == 0) {
                    off += 4;
                    continue;
                }
                if (len == 0xffffffffu || len < 4 || (size_t)len > size - off - 4) {
                    bad = 1;
                    break;
                }
                if (read_u32_endian(data + off + 4, elf_endian(o)) != 0) {        /* an FDE, not a CIE */
                    if (ld_vec_room(&fdes, &cap, n, sizeof(fdes[0])) != 0) {
                        rc = -1;
                        break;
                    }
                    fdes[n].start = off;
                    fdes[n].end = off + 4 + (size_t)len;
                    fdes[n].pc = NULL;
                    fdes[n].other = 0;
                    n++;
                }
                off += 4 + (size_t)len;
            }
            if (rc != 0) {
                break;
            }
            if (bad) {
                icf_note_eh_frame_targets(inputs, symtab, i, sec, skip);
                continue;
            }
            /* each relocation to its FDE (the records are in order of place) */
            for (ri = 0; ri < elf_section_reloc_count(sec); ++ri) {
                const elf_reloc_t *r = elf_section_reloc_at((elf_section_t *)sec, ri);
                uint64_t roff;
                size_t lo = 0, hi = n;

                if (r == NULL) {
                    continue;
                }
                roff = elf_reloc_offset(r);
                while (lo < hi) {
                    size_t mid = lo + (hi - lo) / 2;

                    if (fdes[mid].end <= roff) {
                        lo = mid + 1;
                    } else {
                        hi = mid;
                    }
                }
                if (lo >= n || roff < fdes[lo].start) {
                    continue;   /* a CIE's: its personality routine */
                }
                if (roff == fdes[lo].start + 8 && fdes[lo].pc == NULL) {
                    fdes[lo].pc = r;
                } else {
                    fdes[lo].other = 1;
                }
            }
            for (k = 0; k < n; ++k) {
                if (fdes[k].pc != NULL && fdes[k].other &&
                    gc_symbol_section(inputs, symtab, i, elf_reloc_symbol(fdes[k].pc), &in, &at) &&
                    at < elf_section_count(inputs->objs[in])) {
                    skip[in][at] = 1;
                }
            }
        }
    }
    free(fdes);
    return rc;
}

/* A section that may be folded, while the classes are being found. */
typedef struct {
    const elf_section_t *section;
    size_t input;
    size_t index;       /* its place among its input's sections */
    size_t seq;         /* the order it was met in: inputs, then sections */
    uint64_t hash;      /* of what sections_icf_shape_equal() compares */
} icf_candidate_t;

/* A candidate and its class in the last round, to be sorted by class. */
typedef struct {
    size_t cls;
    size_t cand;
} icf_order_t;

/**
 * A hash of what two identical sections have alike: kind, flags, alignment, size, contents, and the places,
 * kinds and addends of their relocations.  Two sections with different hashes differ.
 *
 * @param sec the section
 * @return the hash (FNV-1a)
 */
static uint64_t icf_hash_section(const elf_section_t *sec) {
    uint64_t h = 1469598103934665603ULL;
    uint64_t words[4];
    const uint8_t *data;
    size_t size = 0, i, w;

    words[0] = elf_section_type(sec);
    words[1] = elf_section_flags(sec);
    words[2] = elf_section_align(sec);
    words[3] = elf_section_size(sec);
    for (w = 0; w < 4; ++w) {
        for (i = 0; i < 8; ++i) {
            h = (h ^ ((words[w] >> (8 * i)) & 0xffu)) * 1099511628211ULL;
        }
    }
    data = (const uint8_t *)elf_section_data(sec, &size);
    for (i = 0; data != NULL && i < size; ++i) {
        h = (h ^ data[i]) * 1099511628211ULL;
    }
    for (i = 0; i < elf_section_reloc_count(sec); ++i) {
        const elf_reloc_t *r = elf_section_reloc_at((elf_section_t *)sec, i);

        if (r != NULL) {
            h = (h ^ elf_reloc_offset(r)) * 1099511628211ULL;
            h = (h ^ elf_reloc_type(r)) * 1099511628211ULL;
            h = (h ^ (uint64_t)elf_reloc_addend(r)) * 1099511628211ULL;
        }
    }
    return h;
}

/* qsort: by hash, then in the order met. */
static int icf_candidate_cmp(const void *a, const void *b) {
    const icf_candidate_t *x = (const icf_candidate_t *)a;
    const icf_candidate_t *y = (const icf_candidate_t *)b;

    if (x->hash != y->hash) {
        return x->hash < y->hash ? -1 : 1;
    }
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}

/* qsort: by class, then by candidate, the order they were met in after sorting by hash. */
static int icf_order_cmp(const void *a, const void *b) {
    const icf_order_t *x = (const icf_order_t *)a;
    const icf_order_t *y = (const icf_order_t *)b;

    if (x->cls != y->cls) {
        return x->cls < y->cls ? -1 : 1;
    }
    return x->cand < y->cand ? -1 : x->cand > y->cand;
}

/**
 * Split the classes of candidates @p cands until no class splits: two of a class stay together only if each
 * of their relocations refers to the same thing (sections_icf_refs_equal()) as the classes of the last round
 * have it.  The classes start as those of sections alike in shape, all taken to be one, and only ever split,
 * so what is left is the largest grouping in which each class's members refer to the same classes: two
 * callers of two identical functions are one, as are two functions that call themselves.
 *
 * @param c      the comparison, whose cls is the snapshot of each round
 * @param cands  the candidates
 * @param count  how many
 * @param cls    each candidate's class, from 1, refined in place
 * @param nclass the number of classes so far, raised as they split
 * @return 0, or -1 out of memory
 */
static int icf_refine_classes(icf_cmp_t *c, const icf_candidate_t *cands, size_t count, size_t *cls,
                              size_t *nclass) {
    icf_order_t *order = (icf_order_t *)malloc((count ? count : 1) * sizeof(order[0]));
    size_t *old = (size_t *)malloc((count ? count : 1) * sizeof(old[0]));
    size_t *reps = (size_t *)malloc((count ? count : 1) * sizeof(reps[0]));
    int changed = 1;
    size_t k;

    if (order == NULL || old == NULL || reps == NULL) {
        free(order);
        free(old);
        free(reps);
        return -1;
    }
    c->cls = old;
    while (changed) {
        size_t b, e;

        changed = 0;
        memcpy(old, cls, count * sizeof(old[0]));
        for (k = 0; k < count; ++k) {
            order[k].cls = old[k];
            order[k].cand = k;
        }
        qsort(order, count, sizeof(order[0]), icf_order_cmp);
        for (b = 0; b < count; b = e) {
            size_t nreps = 0;

            for (e = b + 1; e < count && order[e].cls == order[b].cls; ++e) {
            }
            if (e - b < 2) {
                continue;
            }
            /* each member joins the first of the class's new parts it is like, or starts one */
            for (k = b; k < e; ++k) {
                size_t m = order[k].cand, r;

                for (r = 0; r < nreps; ++r) {
                    if (sections_icf_refs_equal(c, cands[m].section, cands[m].input, cands[reps[r]].section,
                                                cands[reps[r]].input)) {
                        cls[m] = cls[reps[r]];
                        break;
                    }
                }
                if (r == nreps) {
                    if (nreps != 0) {
                        cls[m] = ++*nclass;
                        changed = 1;
                    }
                    reps[nreps++] = m;
                }
            }
        }
    }
    c->cls = NULL;
    free(order);
    free(old);
    free(reps);
    return 0;
}

/* qsort: by section, for icf_class_of_input_section()'s search. */
static int icf_member_cmp(const void *a, const void *b) {
    const elf_section_t *x = ((const ld_icf_member_t *)a)->section;
    const elf_section_t *y = ((const ld_icf_member_t *)b)->section;

    return x < y ? -1 : x > y;
}

int icf_collect_input_sections(ld_ctx_t *ctx, const objvec_t *inputs, const ld_symtab_t *symtab) {
    icf_candidate_t *cands = NULL;
    uint8_t **skip = NULL;      /* skip[i][s]: section s of input i is never folded */
    size_t **cand_at = NULL;    /* cand_at[i][s]: 1 + the candidate section s of input i is, 0 if none */
    size_t *cls = NULL;         /* each candidate's class */
    size_t *size_of = NULL;     /* each class's size, then the number it is folded under */
    size_t count = 0, cap = 0, members = 0, nclass = 0, i, j, s;
    icf_cmp_t cmp;
    int rc = -1;

    free(ctx->icf_members);
    ctx->icf_members = NULL;
    ctx->icf_member_count = 0;
    ctx->icf_class_count = 0;
    skip = (uint8_t **)calloc(inputs->count ? inputs->count : 1, sizeof(skip[0]));
    if (skip == NULL) {
        goto out;
    }
    for (i = 0; i < inputs->count; ++i) {
        skip[i] = (uint8_t *)calloc(elf_section_count(inputs->objs[i]) + 1, 1);
        if (skip[i] == NULL) {
            goto out;
        }
    }
    if (ctx->opt.icf_mode == 1) {
        icf_note_taken_addresses(inputs, symtab, skip);
    }
    if (icf_note_lsda_sections(inputs, symtab, skip) != 0) {
        goto out;
    }
    for (i = 0; i < inputs->count; ++i) {
        for (s = 0; s < elf_section_count(inputs->objs[i]); ++s) {
            const elf_section_t *sec = elf_section_get(inputs->objs[i], s);

            if (!is_icf_candidate_section(sec, ctx->opt.icf_mode) ||
                (ctx->opt.gc_sections && !gc_keep_input_section(sec, ctx)) || skip[i][s]) {
                continue;
            }
            if (ld_vec_room(&cands, &cap, count, sizeof(cands[0])) != 0) {
                goto out;
            }
            cands[count].section = sec;
            cands[count].input = i;
            cands[count].index = s;
            cands[count].seq = count;
            cands[count].hash = icf_hash_section(sec);
            count++;
        }
    }
    if (count > 1) {
        qsort(cands, count, sizeof(cands[0]), icf_candidate_cmp);
    }
    cls = (size_t *)calloc(count ? count : 1, sizeof(cls[0]));
    cand_at = (size_t **)calloc(inputs->count ? inputs->count : 1, sizeof(cand_at[0]));
    if (cls == NULL || cand_at == NULL) {
        goto out;
    }
    for (i = 0; i < inputs->count; ++i) {
        cand_at[i] = (size_t *)calloc(elf_section_count(inputs->objs[i]) + 1, sizeof(cand_at[i][0]));
        if (cand_at[i] == NULL) {
            goto out;
        }
    }
    for (i = 0; i < count; ++i) {
        cand_at[cands[i].input][cands[i].index] = i + 1;
    }
    /* the classes of what is alike but for what its relocations refer to: among those of a hash, each not
     * yet in a class starts one, of what is like it after it */
    for (i = 0; i < count; ++i) {
        if (cls[i] != 0) {
            continue;
        }
        cls[i] = ++nclass;
        for (j = i + 1; j < count && cands[j].hash == cands[i].hash; ++j) {
            if (cls[j] == 0 && sections_icf_shape_equal(cands[i].section, cands[j].section)) {
                cls[j] = cls[i];
            }
        }
    }
    /* then split until each class's members refer to the same things, or to the same classes */
    cmp.inputs = inputs;
    cmp.symtab = symtab;
    cmp.cand_at = cand_at;
    cmp.cls = NULL;
    cmp.preempt = ctx->opt.expect_type == ET_DYN && !ctx->opt.pie && !ctx->opt.bsymbolic;
    if (icf_refine_classes(&cmp, cands, count, cls, &nclass) != 0) {
        goto out;
    }
    /* what is left in a class of two or more is folded: those classes are numbered from 1 */
    size_of = (size_t *)calloc(nclass + 1, sizeof(size_of[0]));
    if (size_of == NULL) {
        goto out;
    }
    for (i = 0; i < count; ++i) {
        size_of[cls[i]]++;
    }
    for (j = 1; j <= nclass; ++j) {
        if (size_of[j] > 1) {
            members += size_of[j];
            size_of[j] = ++ctx->icf_class_count;    /* (now the number it is folded under) */
        } else {
            size_of[j] = 0;
        }
    }
    if (members != 0) {
        ctx->icf_members = (ld_icf_member_t *)calloc(members, sizeof(ctx->icf_members[0]));
        if (ctx->icf_members == NULL) {
            ctx->icf_class_count = 0;
            goto out;
        }
        for (i = 0; i < count; ++i) {
            if (size_of[cls[i]] != 0) {
                ctx->icf_members[ctx->icf_member_count].section = cands[i].section;
                ctx->icf_members[ctx->icf_member_count].cls = size_of[cls[i]];
                ctx->icf_member_count++;
            }
        }
        qsort(ctx->icf_members, ctx->icf_member_count, sizeof(ctx->icf_members[0]), icf_member_cmp);
    }
    rc = 0;
out:
    for (i = 0; skip != NULL && i < inputs->count; ++i) {
        free(skip[i]);
    }
    for (i = 0; cand_at != NULL && i < inputs->count; ++i) {
        free(cand_at[i]);
    }
    free(skip);
    free(cand_at);
    free(cls);
    free(size_of);
    free(cands);
    return rc;
}

size_t icf_class_of_input_section(const elf_section_t *section, void *user) {
    const ld_ctx_t *ctx = (const ld_ctx_t *)user;
    size_t lo = 0, hi = ctx->icf_member_count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (ctx->icf_members[mid].section < section) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < ctx->icf_member_count && ctx->icf_members[lo].section == section ? ctx->icf_members[lo].cls : 0;
}
