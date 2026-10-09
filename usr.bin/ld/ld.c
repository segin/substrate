/*
 * ld.c -- the driver: options, the order of a link, the output file.
 */

#include "ld.h"

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [-m32|-m64|-m <emulation>] [-r|-shared|-pie|-static] "
            "[-o output] [-L dir] [-l name] [-Bstatic|-Bdynamic] "
            "[--start-group ... --end-group] [--whole-archive|--no-whole-archive] "
            "[-plugin path] [-plugin-opt opt] "
            "[-e symbol] [-T script] [--allow-undefined] [-z text|notext|execstack|noexecstack|relro|norelro] "
            "[--reproduce dir] "
            "[--hash-style=sysv|gnu|both] "
            "[--compat=gnu|lld] input...\n",
            prog);
}

static int symbol_is_defined(const elf_symbol_t *sym) {
    return sym != NULL && elf_symbol_shndx(sym) != SHN_UNDEF;
}

/*
 * The mode of the output: what creat() would have given it, readable and
 * writable by all and executable if it is a program, less what the umask
 * takes away.  The file was made private, by mkstemp(), and was then set
 * to 0755 or 0644 whatever the umask said.  A device or a pipe that was
 * written into keeps the mode it has.
 */
static int set_output_mode(const char *path, int executable) {
    struct stat st;
    mode_t mask;

    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    mask = umask(0);
    (void)umask(mask);
    return chmod(path, (executable ? 0777 : 0666) & ~mask);
}

/* Take away an output that turned out wrong, if it is a file of ours. */
static void remove_output(const char *path) {
    struct stat st;

    if (path != NULL && lstat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        (void)unlink(path);
    }
}

static int set_entry_symbol(ld_ctx_t *ctx, elfobj_t *obj, const char *entry_symbol, int require_entry,
                            int entry_explicit) {
    const elf_symbol_t *sym;
    uint64_t addr = 0;

    if (entry_symbol == NULL || entry_symbol[0] == '\0') {
        return 0;
    }
    sym = elf_find_symbol(obj, entry_symbol);
    if (!symbol_is_defined(sym)) {
        if (require_entry) {
            if (entry_explicit) {
                fprintf(stderr, "ld: entry symbol '%s' not found\n", entry_symbol);
                return -1;
            }
            /*
             * No _start.  The program then begins where its text does,
             * and is told so: to begin instead at whichever global
             * function came first in the symbol table, as this did, is
             * to run something nobody chose.
             */
            {
                elf_section_t *text = elf_find_section(obj, ".text");

                if (text == NULL || (elf_section_flags(text) & SHF_ALLOC) == 0) {
                    fprintf(stderr, "ld: entry symbol '%s' not found, and there is no .text to begin at\n",
                            entry_symbol);
                    return -1;
                }
                addr = elf_section_addr(text);
                if (ld_warn(ctx, "cannot find entry symbol %s; the program begins at the start of .text, 0x%llx",
                            entry_symbol, (unsigned long long)addr) != 0) {
                    return -1;
                }
                if (elf_set_entry(obj, addr) != ELF_OK) {
                    fprintf(stderr, "ld: failed to set fallback .text entry address\n");
                    return -1;
                }
                return 0;
            }
        }
        return 0;
    }
    if (resolve_symbol_addr(obj, sym, 0, &addr, NULL) != 0) {
        fprintf(stderr, "ld: failed to resolve entry symbol '%s'\n", entry_symbol);
        return -1;
    }
    if (elf_set_entry(obj, addr) != ELF_OK) {
        fprintf(stderr, "ld: failed to set entry address\n");
        return -1;
    }
    return 0;
}

static int check_undefined_symbols(elfobj_t *obj, const ld_ctx_t *ctx, int allow_undefined,
                                   const ld_symtab_t *symtab) {
    size_t i;
    if (allow_undefined) {
        return 0;
    }
    for (i = 0; i < elf_symbol_count(obj); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(obj, i);
        if (sym == NULL) {
            continue;
        }
        if (elf_symbol_shndx(sym) != SHN_UNDEF) {
            continue;
        }
        if (elf_symbol_bind(sym) == STB_WEAK) {
            continue;
        }
        if (elf_symbol_bind(sym) == STB_GLOBAL) {
            const char *name = elf_symbol_name(sym);
            if (name != NULL && name[0] != '\0') {
                if (strcmp(name, "_GLOBAL_OFFSET_TABLE_") == 0) {
                    continue;
                }
                {
                    int has_provider = 0;
                    if (unresolved_symbol_has_dso_provider((ld_ctx_t *)ctx, name, &has_provider) != 0) {
                        fprintf(stderr, "ld: failed while validating unresolved symbol providers\n");
                        return -1;
                    }
                    if (has_provider) {
                        continue;
                    }
                }
                const ld_sym_t *known = ld_symtab_find(symtab, name);
                const char *src = known != NULL ? known->ref_src : NULL;
                if (src != NULL) {
                    fprintf(stderr, "ld: undefined reference to `%s` (referenced by %s)\n", name, src);
                    ld_diag_note("unresolved-symbol", src, "add defining object/library before this reference");
                } else {
                    fprintf(stderr, "ld: undefined reference to `%s`\n", name);
                    ld_diag_note("unresolved-symbol", NULL, "add defining object/library to link inputs");
                }
                return -1;
            }
        }
    }
    return 0;
}

static int validate_output(const ld_ctx_t *ctx) {
#ifdef LD_SUBSTRATE_BUILD
    (void)ctx;
    return 0;
#else
    elfobj_t *obj = NULL;
    struct stat st;

    /* Only what can be read back: an output that went to a pipe or a
     * device is gone, and opening a pipe to look would wait for ever. */
    if (stat(ctx->out_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    if (elf_open(ctx->out_path, &obj) != ELF_OK) {
        fprintf(stderr, "ld: failed to open output %s\n", ctx->out_path);
        return -1;
    }
    if (ctx->expect_type != 0 && elf_type(obj) != ctx->expect_type) {
        fprintf(stderr, "ld: wrong output ELF type\n");
        elf_close(obj);
        return -1;
    }
    if (ctx->mode == 64) {
        if (elf_class(obj) != ELFOBJ_CLASS_64 || elf_machine(obj) != EM_X86_64) {
            fprintf(stderr, "ld: expected x86_64 ELF64 output\n");
            elf_close(obj);
            return -1;
        }
    } else {
        if (elf_class(obj) != ELFOBJ_CLASS_32 || elf_machine(obj) != EM_386) {
            fprintf(stderr, "ld: expected i386 ELF32 output\n");
            elf_close(obj);
            return -1;
        }
    }
    elf_close(obj);
    return 0;
#endif
}

static int ensure_substrate_ld_note(elfobj_t *out) {
    static const char note_name[] = "Substrate";
    static const char note_desc[] = "Substrate Linker v0.1";
    static const uint32_t note_type = 0x5355424cU; /* "SUBL" */
    elf_section_t *sec;
    elfobj_endian_t endian;
    uint8_t *buf;
    size_t namesz;
    size_t descsz;
    size_t name_pad;
    size_t desc_pad;
    size_t total;

    if (out == NULL) {
        return -1;
    }
    sec = elf_find_section(out, ".note.substrate_ld");
    if (sec == NULL) {
        sec = elf_add_section(out, ".note.substrate_ld", SHT_NOTE, SHF_ALLOC);
        if (sec == NULL) {
            return -1;
        }
    }

    namesz = sizeof(note_name);
    descsz = sizeof(note_desc);
    name_pad = (namesz + 3u) & ~(size_t)3u;
    desc_pad = (descsz + 3u) & ~(size_t)3u;
    total = 12u + name_pad + desc_pad;

    buf = (uint8_t *)calloc(1, total);
    if (buf == NULL) {
        return -1;
    }

    endian = elf_endian(out);
    write_u32_endian(buf + 0, endian, (uint32_t)namesz);
    write_u32_endian(buf + 4, endian, (uint32_t)descsz);
    write_u32_endian(buf + 8, endian, note_type);
    memcpy(buf + 12, note_name, namesz);
    memcpy(buf + 12 + name_pad, note_desc, descsz);

    if (elf_section_set_align(sec, 4) != ELF_OK || elf_section_set_data(sec, buf, total) != ELF_OK) {
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}

/*
 * Where an input section goes when no script says: into the section whose
 * name its own begins with.  A compiler asked to (-ffunction-sections), and
 * a C++ compiler unasked, gives each function and each inline function's
 * tables a section of their own, .text.NAME and .gcc_except_table.NAME, so
 * that a linker can leave out or share them one by one; they are parts of
 * .text and .gcc_except_table all the same, and a program is not better
 * for a hundred sections where it had seven.
 *
 * The constructor tables are not gathered: .init_array.00100 comes before
 * .init_array.65535 by its number, which appending in input order would
 * not respect.  Nor is a relocatable output's anything, which the next
 * link is to see as this one did.
 */
static const char *default_output_name(const char *section, const char *file, void *user) {
    static const char *const gathered[] = { ".text", ".rodata", ".data.rel.ro", ".data", ".bss",
                                            ".tdata", ".tbss", ".gcc_except_table", NULL };
    size_t i;

    (void)file;
    (void)user;
    for (i = 0; gathered[i] != NULL; ++i) {
        size_t n = strlen(gathered[i]);

        if (strncmp(section, gathered[i], n) == 0 && section[n] == '.') {
            return gathered[i];
        }
    }
    return section;
}

/*
 * A link, as its phases see it: the options and the plan (ctx), the
 * inputs, what is known of their symbols, and the output being made.
 */
typedef struct {
    ld_ctx_t *ctx;
    objvec_t inputs;
    ld_symtab_t symtab;
    elfobj_t *out;
    uint16_t out_type;          /* ET_REL, ET_EXEC or ET_DYN */
    int is_program;             /* an executable or a PIE: not a library */
    int allow_undef;            /* what is left undefined is not an error */
    unsigned done;              /* LD_DID_*: how far the link has got */
} ld_link_t;

/*
 * The points a link passes that other phases depend on.  The order of the
 * phases is the order of the table below; these say what about that
 * order is not free, and the driver checks it, so that a phase moved to
 * where it cannot work fails at once and not in the output.
 */
enum {
    LD_DID_INPUTS = 1u << 0,    /* the inputs are chosen and read */
    LD_DID_SYMTAB = 1u << 1,    /* their symbols are in the table */
    LD_DID_MERGE = 1u << 2,     /* there is an output, with their sections */
    LD_DID_PLAN = 1u << 3,      /* the dynamic tables exist and are sized */
    LD_DID_LAYOUT = 1u << 4,    /* sections have addresses: no section may
                                 * be added, removed, reordered or resized */
    LD_DID_RELOC = 1u << 5,     /* relocations are applied */
    LD_DID_SYMVALUES = 1u << 6  /* symbol values are addresses, no longer
                                 * offsets in their sections */
};

/* Which outputs a phase is for. */
enum {
    LD_FOR_REL = 1u << 0,       /* -r */
    LD_FOR_FINAL = 1u << 1,     /* a program or a shared object */
    LD_FOR_PROGRAM = 1u << 2,   /* a program only */
    LD_FOR_ANY = LD_FOR_REL | LD_FOR_FINAL
};

typedef struct {
    const char *name;           /* for the order check's own message */
    int (*run)(ld_link_t *l);
    unsigned kinds;             /* LD_FOR_* */
    unsigned needs;             /* LD_DID_* that must have happened */
    unsigned before;            /* LD_DID_* that must not have happened yet */
    unsigned gives;             /* LD_DID_* this phase establishes */
    const char *failed;         /* said if it fails; NULL where the phase
                                 * says what went wrong itself */
} ld_phase_t;

static int phase_plugin(ld_link_t *l) {
    return plugin_discover_and_handshake(l->ctx);
}

static int phase_load_inputs(ld_link_t *l) {
    return load_all_inputs(l->ctx, &l->inputs);
}

static int phase_reproduce(ld_link_t *l) {
    return write_reproduce_bundle(l->ctx, &l->inputs);
}

static int phase_trace(ld_link_t *l) {
    emit_trace_inputs(l->ctx, &l->inputs);
    emit_trace_symbols(l->ctx, &l->inputs);
    return emit_common_symbol_warnings(l->ctx, &l->inputs);
}

/* Every global name of the inputs, once: which definition the link takes,
 * who else defines it (two strong definitions end here), and who refers
 * to it. */
static int phase_symtab(ld_link_t *l) {
    if (ld_symtab_build(l->ctx, &l->inputs, &l->symtab) != 0) {
        return -1;
    }
    if (l->inputs.count == 0) {
        fprintf(stderr, "ld: no compatible relocatable input objects found\n");
        return -1;
    }
    return 0;
}

/* The inputs' sections into the output's.  The inputs go in under their
 * own names, so that a failure in one of them can be reported as that
 * one's. */
static int phase_merge(ld_link_t *l) {
    ld_ctx_t *ctx = l->ctx;
    elf_link_plan_t *plan = elf_link_plan_create();
    elf_err_t err = plan != NULL ? ELF_OK : ELF_ERR_OOM;
    size_t pi;

    for (pi = 0; err == ELF_OK && pi < l->inputs.count; ++pi) {
        err = elf_link_plan_add_input(plan, l->inputs.objs[pi],
                                      l->inputs.names[pi] != NULL ? l->inputs.names[pi] : "?");
    }
    if (err == ELF_OK && ctx->script != NULL && ctx->script->has_sections) {
        err = elf_link_plan_set_section_name_hook(plan, script_output_name, ctx->script);
    } else if (err == ELF_OK && ctx->expect_type != ET_REL) {
        err = elf_link_plan_set_section_name_hook(plan, default_output_name, NULL);
    }
    /* --gc-sections: what nothing uses is decided now, of the inputs'
     * sections, and the merge passes over it.  (Not for a relocatable
     * output, whose user is the next link.) */
    if (err == ELF_OK && ctx->gc_sections && ctx->expect_type != ET_REL) {
        if (note_dso_names(ctx) != 0 || gc_collect_input_sections(ctx, &l->inputs, &l->symtab) != 0) {
            err = ELF_ERR_OOM;
        } else {
            err = elf_link_plan_set_gc_hook(plan, gc_keep_input_section, ctx);
        }
    }
    if (err == ELF_OK) {
        err = elf_link_plan_link(plan, &l->out);
    }
    if (plan != NULL) {
        elf_link_plan_destroy(plan);
    }
    if (err != ELF_OK || l->out == NULL) {
        const char *why = l->out != NULL ? elf_last_diagnostics(l->out) : "";

        fprintf(stderr, "ld: link merge failed: %s%s%s\n", elf_errstr(err),
                why[0] != '\0' ? ": " : "", why);
        return -1;
    }
    return 0;
}

/*
 * What this linker makes is for substrate, and says so, whichever system
 * the linker itself was built to run on: the brand is what the kernel
 * picks the personality by, and the system's own shared objects carry it
 * as its programs do.
 */
static int phase_brand(ld_link_t *l) {
    if (elf_set_type(l->out, l->out_type) != ELF_OK) {
        fprintf(stderr, "ld: failed to set output type\n");
        return -1;
    }
    if (l->out_type != ET_REL && elf_set_osabi(l->out, ELFOSABI_SUBSTRATE) != ELF_OK) {
        fprintf(stderr, "ld: failed to set Substrate ELF OSABI\n");
        return -1;
    }
    return 0;
}

static int phase_strip_debug(ld_link_t *l) {
    size_t si;

    if (!l->ctx->strip_debug) {
        return 0;
    }
    for (si = elf_section_count(l->out); si > 0; --si) {
        elf_section_t *sec = elf_section_get(l->out, si - 1);
        const char *name = sec != NULL ? elf_section_name(sec) : NULL;

        if (name != NULL && (elf_section_flags(sec) & SHF_ALLOC) == 0 &&
            (strncmp(name, ".debug", 6) == 0 || strncmp(name, ".zdebug", 7) == 0 ||
             strncmp(name, ".stab", 5) == 0 || strncmp(name, ".gnu.debuglto_", 14) == 0) &&
            elf_remove_section(l->out, sec) != ELF_OK) {
            fprintf(stderr, "ld: failed to leave out section %s\n", name);
            return -1;
        }
    }
    return 0;
}

static int phase_plan_eh_frame_hdr(ld_link_t *l) {
    return plan_eh_frame_hdr(l->ctx, l->out);
}

static int phase_script_symbols(ld_link_t *l) {
    if (script_declare_symbols(l->ctx, l->out) != 0) {
        return -1;
    }
    return apply_defsyms(l->ctx, l->out);
}

static int phase_order_sections(ld_link_t *l) {
    return reorder_sections_default_policy(l->out);
}

static int phase_script_sections(ld_link_t *l) {
    return script_apply_sections(l->ctx, l->out);
}

static int phase_icf(ld_link_t *l) {
    return l->ctx->icf_mode != 0 ? apply_identical_code_folding(l->out, l->ctx) : 0;
}

static int phase_ld_note(ld_link_t *l) {
    return ensure_substrate_ld_note(l->out);
}

static int phase_strip_groups(ld_link_t *l) {
    return strip_group_sections_for_final(l->out);
}

/* Whether this output's own definitions can be taken over by another's
 * at run time, which everything after asks; and, in a program, the
 * thread-local sequences that need not go through the dynamic linker. */
static int phase_relax_tls(ld_link_t *l) {
    set_definitions_preemptible(l->out_type == ET_DYN && !l->ctx->pie && !l->ctx->bsymbolic);
    return l->is_program ? relax_tls_dynamic_in_program(l->out) : 0;
}

static int phase_plan_imports(ld_link_t *l) {
    return note_dso_names(l->ctx) != 0 || settle_undefined_weak(l->ctx, l->out) != 0 ||
           plan_dynamic_imports(l->ctx, l->out) != 0 ? -1 : 0;
}

static int phase_plan_local_got(ld_link_t *l) {
    return plan_local_got(l->ctx, l->out);
}

static int phase_plan_needed(ld_link_t *l) {
    return plan_dynamic_needed(l->ctx, l->out);
}

static int phase_segments(ld_link_t *l) {
    return add_default_segments(l->out, l->ctx);
}

static int phase_assign_addresses(ld_link_t *l) {
    uint64_t base_vaddr;

    if (l->ctx->have_image_base) {
        base_vaddr = l->ctx->image_base;
    } else if (l->out_type == ET_DYN) {
        base_vaddr = 0;
    } else {
        base_vaddr = l->ctx->mode == 64 ? 0x400000ULL : 0x08048000ULL;
    }
    return assign_section_addresses(l->out, base_vaddr);
}

static int phase_script_addresses(ld_link_t *l) {
    return script_assign_addresses(l->ctx, l->out);
}

static int phase_fill_imports(ld_link_t *l) {
    const ld_arch_t *arch = ld_arch_of_mode(l->ctx->mode);

    if (arch != NULL && finalize_dynamic_imports(arch, l->out, &l->ctx->dyn_imports) != 0) {
        fprintf(stderr, "ld: failed to finalize %s GOT/PLT dynamic data\n", canonical_mode_name(l->ctx->mode));
        return -1;
    }
    return 0;
}

static int phase_dynamic_tags(ld_link_t *l) {
    return patch_dynamic_tag_values(l->out);
}

static int phase_wx_policy(ld_link_t *l) {
    return enforce_wx_policy(l->out);
}

static int phase_text_relocations(ld_link_t *l) {
    const char *textrel_sec = text_relocation_section(l->ctx, l->out);

    if (textrel_sec == NULL) {
        return 0;
    }
    if (l->ctx->z_text_mode == 1) {
        fprintf(stderr,
                "ld: -z text: section %s is read-only and has relocations the dynamic linker would apply\n",
                textrel_sec);
        return -1;
    }
    if (l->ctx->z_text_mode != 2 &&
        ld_warn(l->ctx, "section %s is read-only and has relocations for the dynamic linker (DT_TEXTREL); "
                        "was it compiled without -fPIC?", textrel_sec) != 0) {
        return -1;
    }
    return 0;
}

static int phase_undefined(ld_link_t *l) {
    return check_undefined_symbols(l->out, l->ctx, l->allow_undef, &l->symtab);
}

static int phase_relocate(ld_link_t *l) {
    if (fill_local_got(l->ctx, l->out) != 0) {
        return -1;
    }
    return apply_all_relocations(l->out, l->ctx, l->allow_undef);
}

static int phase_fill_eh_frame_hdr(ld_link_t *l) {
    return fill_eh_frame_hdr(l->out);
}

static int phase_entry(ld_link_t *l) {
    const char *entry = l->ctx->entry_symbol;

    return set_entry_symbol(l->ctx, l->out, entry != NULL ? entry : "_start", l->is_program, entry != NULL);
}

static int phase_symbol_values(ld_link_t *l) {
    return finalize_symbol_values_for_output(l->out);
}

static int phase_dynsym_values(ld_link_t *l) {
    return patch_dynsym_symbol_values(l->ctx, l->out);
}

static int phase_map(ld_link_t *l) {
    return write_map_file(l->ctx, &l->inputs, &l->symtab, l->out);
}

static int phase_write(ld_link_t *l) {
    const char *path = l->ctx->out_path;

    if (elf_write_file(l->out, path) != ELF_OK) {
        fprintf(stderr, "ld: failed to write output %s\n", path);
        return -1;
    }
    if (set_output_mode(path, l->is_program) != 0 &&
        ld_warn(l->ctx, "failed to set output mode on %s: %s", path, strerror(errno)) != 0) {
        return -1;
    }
    return 0;
}

#define DID(x) LD_DID_##x

/*
 * A link, in order.  Three things about the order are not obvious from
 * reading it and are what the needs/before columns hold: everything that
 * adds, removes, reorders or resizes a section comes before addresses
 * are assigned; what is filled in from addresses comes after; and symbol
 * values turn from offsets into addresses only once nothing more reads
 * them as offsets.
 */
static const ld_phase_t link_phases[] = {
    /* name, function, for, needs, before, gives, said on failure */
    { "plugin", phase_plugin, LD_FOR_ANY, 0, DID(INPUTS), 0, NULL },
    { "load-inputs", phase_load_inputs, LD_FOR_ANY, 0, DID(INPUTS), DID(INPUTS), NULL },
    { "reproduce", phase_reproduce, LD_FOR_ANY, DID(INPUTS), 0, 0, NULL },
    { "trace", phase_trace, LD_FOR_ANY, DID(INPUTS), 0, 0, NULL },
    { "symtab", phase_symtab, LD_FOR_ANY, DID(INPUTS), DID(MERGE), DID(SYMTAB), NULL },
    { "merge", phase_merge, LD_FOR_ANY, DID(SYMTAB), DID(MERGE), DID(MERGE), NULL },
    { "brand", phase_brand, LD_FOR_ANY, DID(MERGE), 0, 0, NULL },
    { "strip-debug", phase_strip_debug, LD_FOR_ANY, DID(MERGE), DID(LAYOUT), 0, NULL },
    { "plan-eh-frame-hdr", phase_plan_eh_frame_hdr, LD_FOR_ANY, DID(MERGE), DID(LAYOUT), 0,
      "failed to make .eh_frame_hdr" },
    { "script-symbols", phase_script_symbols, LD_FOR_ANY, DID(MERGE), DID(SYMVALUES), 0, NULL },
    { "order-sections", phase_order_sections, LD_FOR_ANY, DID(MERGE), DID(LAYOUT), 0,
      "failed to apply default section placement policy" },
    { "script-sections", phase_script_sections, LD_FOR_ANY, DID(MERGE), DID(LAYOUT), 0, NULL },
    { "icf", phase_icf, LD_FOR_ANY, DID(MERGE), DID(LAYOUT), 0, "--icf fold pass failed" },

    { "ld-note", phase_ld_note, LD_FOR_PROGRAM, DID(MERGE), DID(LAYOUT), 0,
      "failed to emit .note.substrate_ld metadata" },
    { "strip-groups", phase_strip_groups, LD_FOR_FINAL, DID(MERGE), DID(LAYOUT), 0,
      "failed to strip SHT_GROUP sections for final output" },
    { "relax-tls", phase_relax_tls, LD_FOR_FINAL, DID(MERGE), DID(PLAN), 0, NULL },
    { "plan-imports", phase_plan_imports, LD_FOR_FINAL, DID(MERGE), DID(PLAN) | DID(LAYOUT), DID(PLAN),
      "failed to plan GOT/PLT dynamic imports" },
    { "plan-local-got", phase_plan_local_got, LD_FOR_FINAL, DID(PLAN), DID(LAYOUT), 0,
      "failed to make the global offset table" },
    { "plan-needed", phase_plan_needed, LD_FOR_FINAL, DID(PLAN), DID(LAYOUT), 0,
      "failed to plan dynamic DT_NEEDED entries" },
    { "order-sections", phase_order_sections, LD_FOR_FINAL, DID(PLAN), DID(LAYOUT), 0,
      "failed to reorder sections after dynamic planning" },
    { "segments", phase_segments, LD_FOR_FINAL, DID(PLAN), DID(LAYOUT), 0,
      "failed to add output program segments" },
    { "order-sections", phase_order_sections, LD_FOR_FINAL, DID(PLAN), DID(LAYOUT), 0,
      "failed to reorder sections after segment planning" },
    { "assign-addresses", phase_assign_addresses, LD_FOR_FINAL, DID(PLAN), DID(LAYOUT), DID(LAYOUT),
      "failed to assign section virtual addresses" },
    { "script-addresses", phase_script_addresses, LD_FOR_FINAL, DID(LAYOUT), DID(RELOC), 0, NULL },
    { "fill-imports", phase_fill_imports, LD_FOR_FINAL, DID(LAYOUT), DID(RELOC), 0, NULL },
    { "dynamic-tags", phase_dynamic_tags, LD_FOR_FINAL, DID(LAYOUT), 0, 0,
      "failed to finalize .dynamic tag values" },
    { "wx-policy", phase_wx_policy, LD_FOR_FINAL, DID(LAYOUT), 0, 0, NULL },
    { "text-relocations", phase_text_relocations, LD_FOR_FINAL, DID(LAYOUT), DID(RELOC), 0, NULL },
    { "undefined", phase_undefined, LD_FOR_FINAL, DID(LAYOUT), DID(RELOC), 0, NULL },
    { "relocate", phase_relocate, LD_FOR_FINAL, DID(LAYOUT), DID(RELOC) | DID(SYMVALUES), DID(RELOC), NULL },
    { "fill-eh-frame-hdr", phase_fill_eh_frame_hdr, LD_FOR_FINAL, DID(RELOC), 0, 0,
      "failed to fill .eh_frame_hdr" },
    { "entry", phase_entry, LD_FOR_FINAL, DID(LAYOUT), DID(SYMVALUES), 0, NULL },
    { "symbol-values", phase_symbol_values, LD_FOR_FINAL, DID(RELOC), DID(SYMVALUES), DID(SYMVALUES),
      "failed to finalize output symbol value addresses" },
    { "dynsym-values", phase_dynsym_values, LD_FOR_FINAL, DID(SYMVALUES), 0, 0,
      "failed to patch .dynsym symbol value addresses" },

    { "map", phase_map, LD_FOR_ANY, DID(MERGE), 0, 0, NULL },
    { "write", phase_write, LD_FOR_ANY, DID(MERGE), 0, 0, NULL },
};

#undef DID

static int run_internal_link(ld_ctx_t *ctx) {
    ld_link_t l;
    size_t i;
    int rc = 0;

    memset(&l, 0, sizeof(l));
    l.ctx = ctx;
    l.out_type = ctx->expect_type == 0 ? ET_EXEC : ctx->expect_type;
    l.is_program = l.out_type == ET_EXEC || (l.out_type == ET_DYN && ctx->pie);
    l.allow_undef = ctx->allow_undefined ||
                    (!l.is_program && l.out_type == ET_DYN && !ctx->explicit_unresolved_policy);

    for (i = 0; rc == 0 && i < sizeof(link_phases) / sizeof(link_phases[0]); ++i) {
        const ld_phase_t *p = &link_phases[i];
        unsigned kind = l.out_type == ET_REL ? LD_FOR_REL : LD_FOR_FINAL;

        if (l.is_program) {
            kind |= LD_FOR_PROGRAM;
        }
        if ((p->kinds & kind) == 0) {
            continue;
        }
        if ((l.done & p->needs) != p->needs || (l.done & p->before) != 0) {
            fprintf(stderr, "ld: internal error: link phase %s is out of order\n", p->name);
            rc = -1;
        } else if (p->run(&l) != 0) {
            if (p->failed != NULL) {
                fprintf(stderr, "ld: %s\n", p->failed);
            }
            rc = -1;
        } else {
            l.done |= p->gives;
        }
    }

    ld_symtab_free(&l.symtab);
    objvec_free(&l.inputs);
    if (l.out != NULL) {
        elf_close(l.out);
    }
    return rc;
}

static int parse_arg_value(const char *arg, const char *opt, const char **out_val) {
    size_t n = strlen(opt);
    if (strncmp(arg, opt, n) != 0) {
        return 0;
    }
    if (arg[n] == '\0') {
        return 1;
    }
    *out_val = arg + n;
    return 2;
}

/*
 * An option that is a word and takes a value: -word VALUE, --word VALUE,
 * -word=VALUE, --word=VALUE.  1, with *val the value and *i on the last
 * argument used; 0 if argv[*i] is not this option; -1 if it is and has no
 * value.
 */
static int long_opt_value(int argc, char **argv, int *i, const char *word, const char **val) {
    const char *a = argv[*i];
    size_t n = strlen(word);

    if (a[0] != '-') {
        return 0;
    }
    a += a[1] == '-' ? 2 : 1;
    if (strncmp(a, word, n) != 0 || (a[n] != '\0' && a[n] != '=')) {
        return 0;
    }
    if (a[n] == '=') {
        *val = a + n + 1;
        return 1;
    }
    if (*i + 1 >= argc) {
        return -1;
    }
    *val = argv[++*i];
    return 1;
}

int main(int argc, char **argv) {
    ld_ctx_t ctx;
    int i;

    memset(&ctx, 0, sizeof(ctx));
    ctx.out_path = "a.out";
    ctx.compat_mode = LD_COMPAT_GNU;
#ifdef LD_SUBSTRATE_BUILD
    ctx.current_lib_mode = LD_LIBMODE_STATIC;
#else
    ctx.current_lib_mode = LD_LIBMODE_DYNAMIC;
#endif
    ctx.current_whole_archive = 0;
    ctx.current_as_needed = 0;
    ctx.z_text_mode = 0;
    ctx.z_execstack = -1;
    ctx.z_relro = 1;
    ctx.hash_style = LD_HASH_BOTH;

    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char *val = NULL;
        int p;

        if (strcmp(a, "--help") == 0) {
            usage(argv[0]);
            inputvec_free(&ctx.inputs);
            strvec_free(&ctx.lib_paths);
            strvec_free(&ctx.trace_symbols);
            strvec_free(&ctx.force_undefined);
            defsymvec_free(&ctx.defsyms);
            strvec_free(&ctx.dso_inputs);
            return 0;
        }
        if ((p = parse_arg_value(a, "--compat=", &val)) != 0) {
            ld_compat_mode_t compat;
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    return 2;
                }
                val = argv[++i];
            }
            if (parse_compat_mode(val, &compat) != 0) {
                fprintf(stderr, "ld: unsupported compatibility mode '%s' (expected gnu or lld)\n", val);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            ctx.compat_mode = compat;
            continue;
        }
        if (strcmp(a, "--version") == 0 || strcmp(a, "-v") == 0) {
            ctx.query_version = 1;
            continue;
        }
        if (strcmp(a, "-m32") == 0) {
            if (set_explicit_mode(&ctx, 32, "-m32") != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            continue;
        }
        if (strcmp(a, "-m64") == 0) {
            if (set_explicit_mode(&ctx, 64, "-m64") != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            continue;
        }
        if ((p = parse_arg_value(a, "-m", &val)) != 0) {
            int m;
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    return 2;
                }
                val = argv[++i];
            }
            m = parse_mode_token(val);
            if (m == 0) {
                fprintf(stderr,
                        "ld: unsupported emulation '%s' for -m "
                        "(supported: elf_x86_64, elf64-x86-64, x86_64, amd64, "
                        "elf_i386, elf32-i386, i386)\n",
                        val);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            if (set_explicit_mode(&ctx, m, a) != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            continue;
        }
        if (strcmp(a, "-o") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            ctx.out_path = argv[++i];
            continue;
        }
        if ((p = parse_arg_value(a, "-plugin-opt", &val)) != 0 ||
            (p = parse_arg_value(a, "--plugin-opt", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0') {
                fprintf(stderr, "ld: -plugin-opt requires an argument\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            if (ctx.plugin_opt_count >= sizeof(ctx.plugin_opts) / sizeof(ctx.plugin_opts[0])) {
                fprintf(stderr, "ld: too many -plugin-opt arguments (max %zu)\n",
                        sizeof(ctx.plugin_opts) / sizeof(ctx.plugin_opts[0]));
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            ctx.plugin_opts[ctx.plugin_opt_count++] = val;
            continue;
        }
        if ((p = parse_arg_value(a, "-plugin", &val)) != 0 || (p = parse_arg_value(a, "--plugin", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0') {
                fprintf(stderr, "ld: -plugin requires a path\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            ctx.plugin_path = val;
            continue;
        }
        if (strcmp(a, "-r") == 0) {
            ctx.expect_type = ET_REL;
            continue;
        }
        if (strcmp(a, "-shared") == 0 || strcmp(a, "-pie") == 0 || strcmp(a, "--pie") == 0 ||
            strcmp(a, "-Bshareable") == 0) {
            /* Both are ET_DYN.  A PIE is a program all the same: it has
             * an entry, what it leaves undefined is an error, and it is
             * made executable. */
            ctx.expect_type = ET_DYN;
            ctx.pie = a[1] == 'p' || a[2] == 'p';
            if (!ctx.explicit_lib_mode) {
                ctx.current_lib_mode = LD_LIBMODE_DYNAMIC;
            }
            continue;
        }
        if (strcmp(a, "-no-pie") == 0 || strcmp(a, "--no-pie") == 0) {
            if (ctx.pie) {
                ctx.pie = 0;
                ctx.expect_type = ET_EXEC;
            }
            continue;
        }
        if (strcmp(a, "-static") == 0) {
            /* Which libraries to use, not what to make: -static -pie is a
             * PIE, and -shared -static a shared object of archives. */
            ctx.current_lib_mode = LD_LIBMODE_STATIC;
            ctx.explicit_lib_mode = 1;
            continue;
        }
        if (strcmp(a, "-Bstatic") == 0) {
            ctx.current_lib_mode = LD_LIBMODE_STATIC;
            ctx.explicit_lib_mode = 1;
            continue;
        }
        if (strcmp(a, "-Bdynamic") == 0) {
            ctx.current_lib_mode = LD_LIBMODE_DYNAMIC;
            ctx.explicit_lib_mode = 1;
            continue;
        }
        if (strcmp(a, "--whole-archive") == 0) {
            ctx.current_whole_archive = 1;
            continue;
        }
        if (strcmp(a, "--no-whole-archive") == 0) {
            ctx.current_whole_archive = 0;
            continue;
        }
        if (strcmp(a, "--as-needed") == 0) {
            ctx.current_as_needed = 1;
            continue;
        }
        if (strcmp(a, "--no-as-needed") == 0) {
            ctx.current_as_needed = 0;
            continue;
        }
        if (strcmp(a, "--gc-sections") == 0) {
            ctx.gc_sections = 1;
            continue;
        }
        if (strcmp(a, "--no-gc-sections") == 0) {
            ctx.gc_sections = 0;
            continue;
        }
        if (strcmp(a, "--print-gc-sections") == 0) {
            ctx.gc_print_sections = 1;
            continue;
        }
        if ((p = parse_arg_value(a, "--icf", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (strcmp(val, "safe") == 0) {
                ctx.icf_mode = 1;
            } else if (strcmp(val, "all") == 0) {
                ctx.icf_mode = 2;
            } else if (strcmp(val, "none") == 0) {
                ctx.icf_mode = 0;
            } else {
                fprintf(stderr,
                        "ld: unsupported --icf mode '%s' (supported: safe, all, none)\n",
                        val);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            continue;
        }
        if (strcmp(a, "--start-group") == 0) {
            if (inputvec_push(&ctx.inputs, LD_INPUT_GROUP_START, ctx.current_lib_mode,
                              ctx.current_whole_archive, ctx.current_as_needed, NULL) != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 1;
            }
            continue;
        }
        if (strcmp(a, "--end-group") == 0) {
            if (inputvec_push(&ctx.inputs, LD_INPUT_GROUP_END, ctx.current_lib_mode,
                              ctx.current_whole_archive, ctx.current_as_needed, NULL) != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 1;
            }
            continue;
        }
        if (strcmp(a, "--allow-undefined") == 0) {
            ctx.allow_undefined = 1;
            ctx.explicit_unresolved_policy = 1;
            continue;
        }
        if (strcmp(a, "--no-undefined") == 0) {
            ctx.allow_undefined = 0;
            ctx.explicit_unresolved_policy = 1;
            continue;
        }
        if (strcmp(a, "--warn-common") == 0) {
            ctx.warn_common = 1;
            continue;
        }
        if (strcmp(a, "--no-warn-common") == 0) {
            ctx.warn_common = 0;
            continue;
        }
        if (strcmp(a, "--fatal-warnings") == 0) {
            ctx.fatal_warnings = 1;
            continue;
        }
        if ((p = parse_arg_value(a, "--unresolved-symbols", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (strcmp(val, "ignore-all") == 0) {
                ctx.allow_undefined = 1;
                ctx.explicit_unresolved_policy = 1;
            } else if (strcmp(val, "report-all") == 0) {
                ctx.allow_undefined = 0;
                ctx.explicit_unresolved_policy = 1;
            } else {
                fprintf(stderr,
                        "ld: unsupported --unresolved-symbols policy '%s' "
                        "(supported: ignore-all, report-all)\n",
                        val);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            continue;
        }
        if ((p = parse_arg_value(a, "-u", &val)) != 0 || (p = parse_arg_value(a, "--undefined", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    strvec_free(&ctx.dso_inputs);
                    defsymvec_free(&ctx.defsyms);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0' || strvec_push(&ctx.force_undefined, val) != 0) {
                fprintf(stderr, "ld: --undefined requires a symbol name\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                strvec_free(&ctx.dso_inputs);
                defsymvec_free(&ctx.defsyms);
                return 2;
            }
            continue;
        }
        if ((p = parse_arg_value(a, "--defsym", &val)) != 0) {
            char *eq;
            char *name_dup;
            uint64_t value;

            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    strvec_free(&ctx.dso_inputs);
                    defsymvec_free(&ctx.defsyms);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            name_dup = xstrdup(val != NULL ? val : "");
            if (name_dup == NULL) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                strvec_free(&ctx.dso_inputs);
                defsymvec_free(&ctx.defsyms);
                return 1;
            }
            eq = strchr(name_dup, '=');
            if (eq == NULL) {
                free(name_dup);
                fprintf(stderr, "ld: --defsym requires NAME=VALUE\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                strvec_free(&ctx.dso_inputs);
                defsymvec_free(&ctx.defsyms);
                return 2;
            }
            *eq++ = '\0';
            if (name_dup[0] == '\0' || parse_u64_auto(eq, &value) != 0 ||
                defsymvec_push(&ctx.defsyms, name_dup, value) != 0) {
                free(name_dup);
                fprintf(stderr, "ld: invalid --defsym `%s`\n", val != NULL ? val : "");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                strvec_free(&ctx.dso_inputs);
                defsymvec_free(&ctx.defsyms);
                return 2;
            }
            free(name_dup);
            continue;
        }
        if (strcmp(a, "--export-dynamic") == 0) {
            ctx.export_dynamic = 1;
            continue;
        }
        if (strcmp(a, "-Bsymbolic") == 0 || strcmp(a, "-Bsymbolic-functions") == 0) {
            ctx.bsymbolic = 1;
            continue;
        }
        if (strcmp(a, "-rdynamic") == 0 || strcmp(a, "-E") == 0) {
            ctx.export_dynamic = 1;
            continue;
        }
        if (strcmp(a, "--no-export-dynamic") == 0) {
            ctx.export_dynamic = 0;
            continue;
        }
        if (strcmp(a, "-e") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            ctx.entry_symbol = argv[++i];
            continue;
        }
        if ((p = parse_arg_value(a, "--entry", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0') {
                fprintf(stderr, "ld: --entry requires a non-empty symbol name\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            ctx.entry_symbol = val;
            continue;
        }
        {
            /* -name VALUE, --name VALUE, -name=VALUE, --name=VALUE */
            const char *lval = NULL;
            int got;

            if ((got = long_opt_value(argc, argv, &i, "dynamic-linker", &lval)) > 0) {
                ctx.interp_path = lval;
            } else if (got == 0 && ((got = long_opt_value(argc, argv, &i, "soname", &lval)) > 0 ||
                                    (got == 0 && strcmp(a, "-h") == 0 && i + 1 < argc && (lval = argv[++i]) != NULL &&
                                     (got = 1) != 0))) {
                ctx.soname = lval;
            } else if (got == 0 && (got = long_opt_value(argc, argv, &i, "rpath", &lval)) > 0) {
                if (strvec_push(&ctx.rpaths, lval) != 0) {
                    got = -1;
                }
            } else if (got == 0) {
                /* Where to look for the libraries of libraries at link
                 * time, which this linker does not go looking for. */
                got = long_opt_value(argc, argv, &i, "rpath-link", &lval);
            }
            if (got < 0) {
                fprintf(stderr, "ld: %s needs a value\n", a);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            if (got > 0) {
                continue;
            }
        }
        if ((p = parse_arg_value(a, "-L", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    return 2;
                }
                val = argv[++i];
            }
            if (strvec_push(&ctx.lib_paths, val) != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 1;
            }
            continue;
        }
        if ((p = parse_arg_value(a, "-l", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    return 2;
                }
                val = argv[++i];
            }
            if (inputvec_push(&ctx.inputs, LD_INPUT_LIB, ctx.current_lib_mode,
                              ctx.current_whole_archive, ctx.current_as_needed, val) != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 1;
            }
            continue;
        }
        if ((p = parse_arg_value(a, "-Map", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0') {
                fprintf(stderr, "ld: -Map requires a non-empty path\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            ctx.map_path = val;
            continue;
        }
        if ((p = parse_arg_value(a, "--reproduce", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0') {
                fprintf(stderr, "ld: --reproduce requires a non-empty directory path\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            ctx.reproduce_path = val;
            continue;
        }
        if ((p = parse_arg_value(a, "--hash-style", &val)) != 0) {
            ld_hash_style_t style;
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (parse_hash_style_option(val, &style) != 0) {
                fprintf(stderr, "ld: unsupported --hash-style value '%s' (expected sysv|gnu|both)\n",
                        val != NULL ? val : "");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            ctx.hash_style = style;
            continue;
        }
        if (strcmp(a, "--trace") == 0) {
            ctx.trace_inputs = 1;
            continue;
        }
        if ((p = parse_arg_value(a, "--trace-symbol", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0' || strvec_push(&ctx.trace_symbols, val) != 0) {
                fprintf(stderr, "ld: --trace-symbol requires a symbol name\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            continue;
        }

        {
            /* -Ttext and its kin are words of their own, not -T and a
             * script called "text". */
            static const char *const unsupported[] = { "Ttext", "Tdata", "Tbss", "Trodata-segment",
                                                       "Tldata-segment", NULL };
            const char *lval = NULL;
            size_t u;
            int got;

            if ((got = long_opt_value(argc, argv, &i, "Ttext-segment", &lval)) > 0) {
                if (parse_u64_auto(lval, &ctx.image_base) != 0 || (ctx.image_base & 0xfffu) != 0) {
                    fprintf(stderr, "ld: -Ttext-segment needs an address that is a multiple of the page size, not '%s'\n",
                            lval);
                    got = -2;
                } else {
                    ctx.have_image_base = 1;
                }
            }
            for (u = 0; got == 0 && unsupported[u] != NULL; ++u) {
                if (long_opt_value(argc, argv, &i, unsupported[u], &lval) != 0) {
                    fprintf(stderr,
                            "ld: -%s is not supported: say where a section goes in a linker script (-T), "
                            "as in SECTIONS { .text ADDRESS : { *(.text) } }, or where the image begins with "
                            "-Ttext-segment\n", unsupported[u]);
                    got = -2;
                }
            }
            if (got < 0) {
                if (got == -1) {
                    fprintf(stderr, "ld: %s needs a value\n", a);
                }
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            if (got > 0) {
                continue;
            }
        }
        if ((p = parse_arg_value(a, "-T", &val)) != 0 || (p = parse_arg_value(a, "--script", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0') {
                fprintf(stderr, "ld: -T/--script requires a path\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            ctx.script_path = val;
            continue;
        }
        if (strcmp(a, "-z") == 0 || strncmp(a, "-z", 2) == 0) {
            const char *zval = NULL;
            if (strcmp(a, "-z") == 0) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                zval = argv[++i];
            } else {
                zval = a + 2;
                if (zval[0] == '=') {
                    zval++;
                }
            }
            /*
             * A keyword this linker does nothing about is said to be so
             * and the link goes on: build systems pass -z defs, -z
             * max-page-size=..., -z separate-code and -z nodelete as a
             * matter of course, and a link is not wrong for lacking one.
             */
            if (zval != NULL && zval[0] != '\0' &&
                (parse_z_option(&ctx, zval) == 0 ||
                 ld_warn(&ctx, "-z %s is not supported and is ignored", zval) == 0)) {
                continue;
            }
            if (zval == NULL || zval[0] == '\0') {
                fprintf(stderr, "ld: -z needs a keyword\n");
            }
            inputvec_free(&ctx.inputs);
            strvec_free(&ctx.lib_paths);
            strvec_free(&ctx.trace_symbols);
            return 2;
        }
        if (strcmp(a, "--strip-all") == 0 || strcmp(a, "-s") == 0 || strcmp(a, "--strip-debug") == 0 ||
            strcmp(a, "-S") == 0) {
            /* The debugging information is left out.  -s would leave the
             * symbol table out as well, which is not done. */
            ctx.strip_debug = 1;
            continue;
        }
        if (strcmp(a, "--emit-relocs") == 0 || strcmp(a, "-q") == 0) {
            ctx.emit_relocs = 1;
            continue;
        }
        if (strcmp(a, "--build-id") == 0 || strncmp(a, "--build-id=", 11) == 0) {
            continue;           /* no build ID is made */
        }
        if (strcmp(a, "--eh-frame-hdr") == 0 || strcmp(a, "--no-eh-frame-hdr") == 0) {
            ctx.eh_frame_hdr = a[2] == 'e';
            continue;
        }
        if (strncmp(a, "--sysroot=", 10) == 0) {
            ctx.sysroot = a + 10;
            continue;
        }
        if (strcmp(a, "--copy-dt-needed-entries") == 0 || strcmp(a, "--no-copy-dt-needed-entries") == 0 ||
            strcmp(a, "--add-needed") == 0 || strcmp(a, "--no-add-needed") == 0) {
            ctx.copy_dt_needed = strncmp(a, "--no-", 5) != 0;
            continue;
        }
        if (a[0] == '-' && a[1] == 'o' && a[2] != '\0') {
            ctx.out_path = a + 2;       /* -oFILE */
            continue;
        }

        if (a[0] == '-') {
            if (ctx.compat_mode == LD_COMPAT_LLD) {
                fprintf(stderr, "ld: error: unsupported option in lld mode: %s\n", a);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            if (ld_warn(&ctx, "unsupported option ignored (gnu mode): %s", a) != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            continue;
        }
        /* Defensive: silently skip an empty argv entry or a bare "/"
         * argv slot.  Substrate's cc occasionally synthesised one of
         * these from a misresolved runtime path (uninitialised libgcc
         * buffer + access("/", R_OK) succeeds + libgcc kept as just
         * "/").  Treating this as a hard error left users staring at
         * `ld: failed to open input /` with no idea what produced it.
         * Rather than failing the whole link, ignore the bogus input
         * and let cc be fixed at its source.  A real one-character "/"
         * input file would be wrong on every conceivable system, so
         * losing nothing legitimate. */
        if (a == NULL || a[0] == '\0' ||
            (a[0] == '/' && a[1] == '\0')) {
            ld_warn(&ctx, "ignoring empty/bare-slash input argv slot "
                          "(probable upstream cc bug)");
            continue;
        }
        if (inputvec_push(&ctx.inputs, LD_INPUT_FILE, ctx.current_lib_mode,
                          ctx.current_whole_archive, ctx.current_as_needed, a) != 0) {
            inputvec_free(&ctx.inputs);
            strvec_free(&ctx.lib_paths);
            return 1;
        }
    }

    if (ctx.query_version && ctx.inputs.count == 0) {
        printf("GNU ld (Substrate) 2.42.0\n");
        inputvec_free(&ctx.inputs);
        strvec_free(&ctx.lib_paths);
        strvec_free(&ctx.trace_symbols);
        strvec_free(&ctx.force_undefined);
        defsymvec_free(&ctx.defsyms);
        strvec_free(&ctx.dso_inputs);
        dyn_import_vec_free(&ctx.dyn_imports);
        return 0;
    }
    if (ctx.script_path != NULL) {
        /* The script is read here, once; the link consults what it says. */
        ctx.script = lds_script_parse(ctx.script_path, &ctx);
        if (ctx.script != NULL && ctx.script->entry != NULL && ctx.entry_symbol == NULL) {
            ctx.entry_symbol = ctx.script->entry;
        }
        if (ctx.script == NULL) {
            inputvec_free(&ctx.inputs);
            strvec_free(&ctx.lib_paths);
            strvec_free(&ctx.trace_symbols);
            strvec_free(&ctx.force_undefined);
            defsymvec_free(&ctx.defsyms);
            strvec_free(&ctx.dso_inputs);
            dyn_import_vec_free(&ctx.dyn_imports);
            return 2;
        }
    }
    if (ctx.inputs.count == 0) {
        usage(argv[0]);
        inputvec_free(&ctx.inputs);
        strvec_free(&ctx.lib_paths);
        strvec_free(&ctx.trace_symbols);
        strvec_free(&ctx.force_undefined);
        defsymvec_free(&ctx.defsyms);
        strvec_free(&ctx.dso_inputs);
        dyn_import_vec_free(&ctx.dyn_imports);
        return 2;
    }

    if (ctx.mode == 0) {
        ctx.mode = default_mode();
    }
    if (ctx.expect_type == 0) {
        ctx.expect_type = ET_EXEC;
    }

    if (run_internal_link(&ctx) != 0) {
        inputvec_free(&ctx.inputs);
        strvec_free(&ctx.lib_paths);
        strvec_free(&ctx.trace_symbols);
        strvec_free(&ctx.force_undefined);
        defsymvec_free(&ctx.defsyms);
        strvec_free(&ctx.dso_inputs);
        dyn_import_vec_free(&ctx.dyn_imports);
        return 1;
    }
    if (validate_output(&ctx) != 0) {
        remove_output(ctx.out_path);
        inputvec_free(&ctx.inputs);
        strvec_free(&ctx.lib_paths);
        strvec_free(&ctx.trace_symbols);
        strvec_free(&ctx.force_undefined);
        defsymvec_free(&ctx.defsyms);
        strvec_free(&ctx.dso_inputs);
        dyn_import_vec_free(&ctx.dyn_imports);
        return 1;
    }

    inputvec_free(&ctx.inputs);
    strvec_free(&ctx.lib_paths);
    strvec_free(&ctx.trace_symbols);
    strvec_free(&ctx.force_undefined);
    defsymvec_free(&ctx.defsyms);
    strvec_free(&ctx.dso_inputs);
    dyn_import_vec_free(&ctx.dyn_imports);
    lds_script_free(ctx.script);
    return 0;
}
