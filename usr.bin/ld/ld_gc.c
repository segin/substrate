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
 * --icf=all, as lld folds it.  Nothing writable is: two variables that
 * start out alike do not stay so.  Nor is a section of a group, one whose
 * contents the merge rewrites (strings and constants, TLS, compressed),
 * or one ordered by another.
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
        (flags & (SHF_WRITE | SHF_GROUP | SHF_TLS | SHF_MERGE | SHF_LINK_ORDER | SHF_COMPRESSED)) != 0) {
        return 0;
    }
    if ((flags & SHF_EXECINSTR) != 0 && gc_name_is(name, ".text")) {
        return 1;
    }
    return icf_mode == 2 && gc_name_is(name, ".rodata");
}

/*
 * Whether the relocations of sections @p a (of input @p ia) and @p b (of
 * input @p ib) are alike: at the same places, of the same kinds and
 * addends, and against the same things.  A global is the same thing by
 * name, the link binding each name once; anything else -- a local, a
 * section's own symbol -- only when it is the same place in the same
 * input.
 */
static int sections_reloc_signature_equal(const elf_section_t *a, size_t ia, const elf_section_t *b, size_t ib) {
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
        if ((sa == NULL) != (sb == NULL)) {
            return 0;
        }
        if (sa == NULL || sa == sb) {
            continue;
        }
        na = elf_symbol_name(sa);
        nb = elf_symbol_name(sb);
        if (elf_symbol_bind(sa) != STB_LOCAL && elf_symbol_bind(sb) != STB_LOCAL && na != NULL && nb != NULL &&
            na[0] != '\0' && nb[0] != '\0') {
            if (strcmp(na, nb) != 0) {
                return 0;
            }
            continue;
        }
        if (ia != ib || elf_symbol_bind(sa) != STB_LOCAL || elf_symbol_bind(sb) != STB_LOCAL ||
            elf_symbol_shndx(sa) != elf_symbol_shndx(sb) || elf_symbol_value(sa) != elf_symbol_value(sb) ||
            !gc_symbol_is_defined(sa)) {
            return 0;
        }
    }
    return 1;
}

static int sections_icf_equal(const elf_section_t *a, size_t ia, const elf_section_t *b, size_t ib) {
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
    if (!sections_reloc_signature_equal(a, ia, b, ib)) {
        return 0;
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

/* A section that may be folded, while the classes are being found. */
typedef struct {
    const elf_section_t *section;
    size_t input;
    size_t seq;         /* the order it was met in: inputs, then sections */
    uint64_t hash;      /* of what sections_icf_equal() compares, but for the relocations' symbols */
    size_t cls;         /* its class, 0 until it has one */
} icf_candidate_t;

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

/* qsort: by section, for icf_class_of_input_section()'s search. */
static int icf_member_cmp(const void *a, const void *b) {
    const elf_section_t *x = ((const ld_icf_member_t *)a)->section;
    const elf_section_t *y = ((const ld_icf_member_t *)b)->section;

    return x < y ? -1 : x > y;
}

int icf_collect_input_sections(ld_ctx_t *ctx, const objvec_t *inputs, const ld_symtab_t *symtab) {
    icf_candidate_t *cands = NULL;
    uint8_t **taken = NULL;
    size_t count = 0, cap = 0, members = 0, i, j, s;
    int rc = -1;

    free(ctx->icf_members);
    ctx->icf_members = NULL;
    ctx->icf_member_count = 0;
    ctx->icf_class_count = 0;
    if (ctx->opt.icf_mode == 1) {
        taken = (uint8_t **)calloc(inputs->count ? inputs->count : 1, sizeof(taken[0]));
        if (taken == NULL) {
            goto out;
        }
        for (i = 0; i < inputs->count; ++i) {
            taken[i] = (uint8_t *)calloc(elf_section_count(inputs->objs[i]) + 1, 1);
            if (taken[i] == NULL) {
                goto out;
            }
        }
        icf_note_taken_addresses(inputs, symtab, taken);
    }
    for (i = 0; i < inputs->count; ++i) {
        for (s = 0; s < elf_section_count(inputs->objs[i]); ++s) {
            const elf_section_t *sec = elf_section_get(inputs->objs[i], s);

            if (!is_icf_candidate_section(sec, ctx->opt.icf_mode) ||
                (ctx->opt.gc_sections && !gc_keep_input_section(sec, ctx)) ||
                (taken != NULL && taken[i][s])) {
                continue;
            }
            if (ld_vec_room(&cands, &cap, count, sizeof(cands[0])) != 0) {
                goto out;
            }
            cands[count].section = sec;
            cands[count].input = i;
            cands[count].seq = count;
            cands[count].hash = icf_hash_section(sec);
            cands[count].cls = 0;
            count++;
        }
    }
    if (count > 1) {
        qsort(cands, count, sizeof(cands[0]), icf_candidate_cmp);
    }
    /* among those of a hash, each not yet in a class starts one, of what is like it after it */
    for (i = 0; i < count; ++i) {
        size_t cls = 0;

        if (cands[i].cls != 0) {
            continue;
        }
        for (j = i + 1; j < count && cands[j].hash == cands[i].hash; ++j) {
            if (cands[j].cls != 0 ||
                !sections_icf_equal(cands[i].section, cands[i].input, cands[j].section, cands[j].input)) {
                continue;
            }
            if (cls == 0) {
                cls = ++ctx->icf_class_count;
                cands[i].cls = cls;
                members++;
            }
            cands[j].cls = cls;
            members++;
        }
    }
    if (members != 0) {
        ctx->icf_members = (ld_icf_member_t *)calloc(members, sizeof(ctx->icf_members[0]));
        if (ctx->icf_members == NULL) {
            ctx->icf_class_count = 0;
            goto out;
        }
        for (i = 0; i < count; ++i) {
            if (cands[i].cls != 0) {
                ctx->icf_members[ctx->icf_member_count].section = cands[i].section;
                ctx->icf_members[ctx->icf_member_count].cls = cands[i].cls;
                ctx->icf_member_count++;
            }
        }
        qsort(ctx->icf_members, ctx->icf_member_count, sizeof(ctx->icf_members[0]), icf_member_cmp);
    }
    rc = 0;
out:
    for (i = 0; taken != NULL && i < inputs->count; ++i) {
        free(taken[i]);
    }
    free(taken);
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
