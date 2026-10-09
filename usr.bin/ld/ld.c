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

static int run_internal_link(ld_ctx_t *ctx) {
    objvec_t inputs;
    ld_symtab_t symtab;
    elfobj_t *out = NULL;
    elf_err_t err;
    uint16_t out_type;
    int allow_undef_runtime;
    int is_program;             /* an executable or a PIE: not a library */
    uint64_t base_vaddr;

    memset(&inputs, 0, sizeof(inputs));
    memset(&symtab, 0, sizeof(symtab));
    if (plugin_discover_and_handshake(ctx) != 0) {
        return -1;
    }
    if (load_all_inputs(ctx, &inputs) != 0) {
        objvec_free(&inputs);
        return -1;
    }
    if (write_reproduce_bundle(ctx, &inputs) != 0) {
        objvec_free(&inputs);
        return -1;
    }
    emit_trace_inputs(ctx, &inputs);
    emit_trace_symbols(ctx, &inputs);
    if (emit_common_symbol_warnings(ctx, &inputs) != 0) {
        objvec_free(&inputs);
        return -1;
    }
    /* Every global name of the inputs, once: which definition the link
     * takes, who else defines it (two strong definitions end here), and
     * who refers to it. */
    if (ld_symtab_build(ctx, &inputs, &symtab) != 0) {
        objvec_free(&inputs);
        return -1;
    }
    if (inputs.count == 0) {
        fprintf(stderr, "ld: no compatible relocatable input objects found\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        return -1;
    }

    /* The inputs go in under their own names, so that a failure in one of
     * them can be reported as that one's. */
    {
        elf_link_plan_t *plan = elf_link_plan_create();
        size_t pi;

        err = plan != NULL ? ELF_OK : ELF_ERR_OOM;
        for (pi = 0; err == ELF_OK && pi < inputs.count; ++pi) {
            err = elf_link_plan_add_input(plan, inputs.objs[pi],
                                          inputs.names[pi] != NULL ? inputs.names[pi] : "?");
        }
        if (err == ELF_OK && ctx->script != NULL && ctx->script->has_sections) {
            err = elf_link_plan_set_section_name_hook(plan, script_output_name, ctx->script);
        } else if (err == ELF_OK && ctx->expect_type != ET_REL) {
            err = elf_link_plan_set_section_name_hook(plan, default_output_name, NULL);
        }
        /* --gc-sections: what nothing uses is decided now, of the
         * inputs' sections, and the merge passes over it.  (Not for a
         * relocatable output, whose user is the next link.) */
        if (err == ELF_OK && ctx->gc_sections && ctx->expect_type != ET_REL) {
            if (note_dso_names(ctx) != 0 || gc_collect_input_sections(ctx, &inputs, &symtab) != 0) {
                err = ELF_ERR_OOM;
            } else {
                err = elf_link_plan_set_gc_hook(plan, gc_keep_input_section, ctx);
            }
        }
        if (err == ELF_OK) {
            err = elf_link_plan_link(plan, &out);
        }
        if (plan != NULL) {
            elf_link_plan_destroy(plan);
        }
    }
    if (err != ELF_OK || out == NULL) {
        const char *why = out != NULL ? elf_last_diagnostics(out) : "";

        fprintf(stderr, "ld: link merge failed: %s%s%s\n", elf_errstr(err),
                why[0] != '\0' ? ": " : "", why);
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        if (out != NULL) {
            elf_close(out);
        }
        return -1;
    }

    out_type = ctx->expect_type == 0 ? ET_EXEC : ctx->expect_type;
    is_program = out_type == ET_EXEC || (out_type == ET_DYN && ctx->pie);
    allow_undef_runtime = ctx->allow_undefined;
    if (!is_program && out_type == ET_DYN && !ctx->explicit_unresolved_policy) {
        allow_undef_runtime = 1;
    }
    if (elf_set_type(out, out_type) != ELF_OK) {
        fprintf(stderr, "ld: failed to set output type\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    /*
     * What this linker makes is for substrate, and says so, whichever
     * system the linker itself was built to run on: the brand is what
     * the kernel picks the personality by, and the system's own shared
     * objects carry it as its programs do.
     */
    if ((out_type == ET_EXEC || out_type == ET_DYN) && elf_set_osabi(out, ELFOSABI_SUBSTRATE) != ELF_OK) {
        fprintf(stderr, "ld: failed to set Substrate ELF OSABI\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (ctx->strip_debug) {
        size_t si;

        for (si = elf_section_count(out); si > 0; --si) {
            elf_section_t *sec = elf_section_get(out, si - 1);
            const char *name = sec != NULL ? elf_section_name(sec) : NULL;

            if (name != NULL && (elf_section_flags(sec) & SHF_ALLOC) == 0 &&
                (strncmp(name, ".debug", 6) == 0 || strncmp(name, ".zdebug", 7) == 0 ||
                 strncmp(name, ".stab", 5) == 0 || strncmp(name, ".gnu.debuglto_", 14) == 0) &&
                elf_remove_section(out, sec) != ELF_OK) {
                fprintf(stderr, "ld: failed to leave out section %s\n", name);
                ld_symtab_free(&symtab);
                objvec_free(&inputs);
                elf_close(out);
                return -1;
            }
        }
    }
    if (plan_eh_frame_hdr(ctx, out) != 0) {
        fprintf(stderr, "ld: failed to make .eh_frame_hdr\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (script_declare_symbols(ctx, out) != 0) {
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (apply_defsyms(ctx, out) != 0) {
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (reorder_sections_default_policy(out) != 0) {
        fprintf(stderr, "ld: failed to apply default section placement policy\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (script_apply_sections(ctx, out) != 0) {
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (ctx->icf_mode != 0 && apply_identical_code_folding(out, ctx) != 0) {
        fprintf(stderr, "ld: --icf fold pass failed\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (out_type == ET_REL) {
        if (write_map_file(ctx, &inputs, &symtab, out) != 0) {
            ld_symtab_free(&symtab);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
        if (elf_write_file(out, ctx->out_path) != ELF_OK) {
            fprintf(stderr, "ld: failed to write output %s\n", ctx->out_path);
            ld_symtab_free(&symtab);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
        if (set_output_mode(ctx->out_path, 0) != 0) {
            if (ld_warn(ctx, "failed to set output mode on %s: %s",
                        ctx->out_path, strerror(errno)) != 0) {
                ld_symtab_free(&symtab);
                objvec_free(&inputs);
                elf_close(out);
                return -1;
            }
        }
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return 0;
    }

    if (is_program && ensure_substrate_ld_note(out) != 0) {
        fprintf(stderr, "ld: failed to emit .note.substrate_ld metadata\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (strip_group_sections_for_final(out) != 0) {
        fprintf(stderr, "ld: failed to strip SHT_GROUP sections for final output\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    set_definitions_preemptible(out_type == ET_DYN && !ctx->pie && !ctx->bsymbolic);
    if (is_program && relax_tls_dynamic_in_program(out) != 0) {
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (note_dso_names(ctx) != 0 || settle_undefined_weak(ctx, out) != 0 || plan_dynamic_imports(ctx, out) != 0) {
        fprintf(stderr, "ld: failed to plan GOT/PLT dynamic imports\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (plan_local_got(ctx, out) != 0) {
        fprintf(stderr, "ld: failed to make the global offset table\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (plan_dynamic_needed(ctx, out) != 0) {
        fprintf(stderr, "ld: failed to plan dynamic DT_NEEDED entries\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (reorder_sections_default_policy(out) != 0) {
        fprintf(stderr, "ld: failed to reorder sections after dynamic planning\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (add_default_segments(out, ctx) != 0) {
        fprintf(stderr, "ld: failed to add output program segments\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (reorder_sections_default_policy(out) != 0) {
        fprintf(stderr, "ld: failed to reorder sections after segment planning\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (ctx->mode == 64) {
        base_vaddr = (out_type == ET_DYN) ? 0x0ULL : 0x400000ULL;
    } else {
        base_vaddr = (out_type == ET_DYN) ? 0x0ULL : 0x08048000ULL;
    }
    if (ctx->have_image_base) {
        base_vaddr = ctx->image_base;
    }

    if (assign_section_addresses(out, base_vaddr) != 0) {
        fprintf(stderr, "ld: failed to assign section virtual addresses\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (script_assign_addresses(ctx, out) != 0) {
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (ctx->mode == 64) {
        if (finalize_dynamic_imports_x64(out, &ctx->dyn_imports) != 0) {
            fprintf(stderr, "ld: failed to finalize x86_64 GOT/PLT dynamic data\n");
            ld_symtab_free(&symtab);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
    } else if (ctx->mode == 32) {
        if (finalize_dynamic_imports_i386(out, &ctx->dyn_imports) != 0) {
            fprintf(stderr, "ld: failed to finalize i386 GOT/PLT dynamic data\n");
            ld_symtab_free(&symtab);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
    }
    if (patch_dynamic_tag_values(out) != 0) {
        fprintf(stderr, "ld: failed to finalize .dynamic tag values\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (enforce_wx_policy(out) != 0) {
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    {
        const char *textrel_sec = text_relocation_section(ctx, out);

        if (textrel_sec != NULL && ctx->z_text_mode == 1) {
            fprintf(stderr,
                    "ld: -z text: section %s is read-only and has relocations the dynamic linker would apply\n",
                    textrel_sec);
            ld_symtab_free(&symtab);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
        if (textrel_sec != NULL && ctx->z_text_mode != 2 &&
            ld_warn(ctx, "section %s is read-only and has relocations for the dynamic linker (DT_TEXTREL); "
                         "was it compiled without -fPIC?", textrel_sec) != 0) {
            ld_symtab_free(&symtab);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
    }

    if (check_undefined_symbols(out, ctx, allow_undef_runtime, &symtab) != 0) {
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (fill_local_got(ctx, out) != 0 ||
        apply_all_relocations(out, ctx, allow_undef_runtime) != 0) {
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (fill_eh_frame_hdr(out) != 0) {
        fprintf(stderr, "ld: failed to fill .eh_frame_hdr\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (set_entry_symbol(ctx, out, ctx->entry_symbol != NULL ? ctx->entry_symbol : "_start",
                         is_program, ctx->entry_symbol != NULL) != 0) {
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (finalize_symbol_values_for_output(out) != 0) {
        fprintf(stderr, "ld: failed to finalize output symbol value addresses\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (patch_dynsym_symbol_values(ctx, out) != 0) {
        fprintf(stderr, "ld: failed to patch .dynsym symbol value addresses\n");
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (write_map_file(ctx, &inputs, &symtab, out) != 0) {
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (elf_write_file(out, ctx->out_path) != ELF_OK) {
        fprintf(stderr, "ld: failed to write output %s\n", ctx->out_path);
        ld_symtab_free(&symtab);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (set_output_mode(ctx->out_path, is_program) != 0) {
        if (ld_warn(ctx, "failed to set output mode on %s: %s",
                    ctx->out_path, strerror(errno)) != 0) {
            ld_symtab_free(&symtab);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
    }

    ld_symtab_free(&symtab);
    objvec_free(&inputs);
    elf_close(out);
    return 0;
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
