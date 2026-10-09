/*
 * ld_input.c -- inputs: objects, archives, libraries and where they are found.
 */

#include "ld.h"

static int load_object_input(const char *path, ld_ctx_t *ctx, objvec_t *objs, symstate_t *state, int quiet);

static void trim_trailing(char *s) {
    size_t n = strlen(s);
    while (n > 0) {
        char c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            s[n - 1] = '\0';
            n--;
            continue;
        }
        break;
    }
}

static char *decode_ar_name(const char *raw_name16, const unsigned char *member_data, uint64_t member_size,
                            const char *strtab, size_t strtab_sz, size_t *name_extra) {
    char raw[17];
    char *out;
    uint64_t ext_len = 0;

    memcpy(raw, raw_name16, 16);
    raw[16] = '\0';
    trim_trailing(raw);
    if (strcmp(raw, "/") == 0 || strcmp(raw, "__.SYMDEF") == 0 || strcmp(raw, "__.SYMDEF SORTED") == 0) {
        *name_extra = 0;
        return xstrdup(raw);
    }
    if (strcmp(raw, "//") == 0) {
        *name_extra = 0;
        return xstrdup(raw);
    }
    if (strncmp(raw, "#1/", 3) == 0) {
        if (parse_u64_dec(raw + 3, strlen(raw + 3), &ext_len) != 0 || ext_len > member_size) {
            return NULL;
        }
        out = (char *)malloc((size_t)ext_len + 1);
        if (out == NULL) {
            return NULL;
        }
        memcpy(out, member_data, (size_t)ext_len);
        out[ext_len] = '\0';
        *name_extra = (size_t)ext_len;
        return out;
    }
    if (raw[0] == '/' && isdigit((unsigned char)raw[1]) && strtab != NULL) {
        uint64_t off = 0;
        size_t i;
        size_t n = 0;
        if (parse_u64_dec(raw + 1, strlen(raw + 1), &off) != 0 || off >= strtab_sz) {
            return NULL;
        }
        for (i = (size_t)off; i < strtab_sz; ++i) {
            char c = strtab[i];
            if (c == '\0' || c == '\n') {
                break;
            }
            if (c == '/' && (i + 1 >= strtab_sz || strtab[i + 1] == '\n')) {
                break;
            }
            n++;
        }
        out = (char *)malloc(n + 1);
        if (out == NULL) {
            return NULL;
        }
        memcpy(out, strtab + off, n);
        out[n] = '\0';
        *name_extra = 0;
        return out;
    }
    if (raw[0] != '\0') {
        size_t n = strlen(raw);
        if (n > 0 && raw[n - 1] == '/') {
            raw[n - 1] = '\0';
        }
    }
    *name_extra = 0;
    return xstrdup(raw);
}

int obj_matches_mode(const elfobj_t *obj, int mode) {
    if (obj == NULL || elf_endian(obj) != ELFOBJ_ENDIAN_LE) {
        return 0;
    }
    if (mode == 64) {
        return elf_class(obj) == ELFOBJ_CLASS_64 && elf_machine(obj) == EM_X86_64;
    }
    if (mode == 32) {
        return elf_class(obj) == ELFOBJ_CLASS_32 && elf_machine(obj) == EM_386;
    }
    return (elf_class(obj) == ELFOBJ_CLASS_64 && elf_machine(obj) == EM_X86_64) ||
           (elf_class(obj) == ELFOBJ_CLASS_32 && elf_machine(obj) == EM_386);
}

static int detect_object_mode(const elfobj_t *obj) {
    if (obj == NULL || elf_endian(obj) != ELFOBJ_ENDIAN_LE) {
        return 0;
    }
    if (elf_class(obj) == ELFOBJ_CLASS_64 && elf_machine(obj) == EM_X86_64) {
        return 64;
    }
    if (elf_class(obj) == ELFOBJ_CLASS_32 && elf_machine(obj) == EM_386) {
        return 32;
    }
    return 0;
}

void maybe_autoswitch_mode(ld_ctx_t *ctx, const elfobj_t *obj, size_t loaded_count, const char *path) {
    int detected;

    /* The first input that is of a machine says which machine the link is
     * for, and that is the end of it: the shared objects looked into later
     * are asked whether they suit the link, not what it should be. */
    if (ctx == NULL || ctx->explicit_mode || ctx->mode_settled || loaded_count != 0) {
        return;
    }
    detected = detect_object_mode(obj);
    if (detected == 0) {
        return;
    }
    ctx->mode_settled = 1;
    if (detected == ctx->mode) {
        return;
    }
    ctx->mode = detected;
    if (ctx->trace_inputs) {
        fprintf(stderr, "ld: trace: auto-selected mode %s from %s\n",
                canonical_mode_name(ctx->mode), path != NULL ? path : "<input>");
    }
}

static int validate_relocatable_input(const elfobj_t *obj, const char *display_name) {
    size_t i;

    for (i = 0; i < elf_section_count(obj); ++i) {
        const elf_section_t *sec = elf_section_get(obj, i);
        uint64_t flags = sec != NULL ? elf_section_flags(sec) : 0;
        size_t rc;
        size_t sec_sz = 0;
        const void *sec_data;
        size_t ri;

        if (sec == NULL) {
            continue;
        }
        sec_data = elf_section_data(sec, &sec_sz);
        if (elf_section_type(sec) != SHT_NOBITS && elf_section_size(sec) > 0 && sec_data == NULL) {
            fprintf(stderr, "ld: input %s section %s has invalid data payload\n",
                    display_name, elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
            return -1;
        }
        if (elf_section_align(sec) == 0) {
            fprintf(stderr, "ld: input %s section %s has invalid zero alignment\n",
                    display_name, elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
            return -1;
        }
        if ((flags & SHF_ALLOC) == 0) {
            continue;
        }
        rc = elf_section_reloc_count(sec);
        for (ri = 0; ri < rc; ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at((elf_section_t *)sec, ri);
            const elf_symbol_t *sym;
            int width;

            if (rel == NULL) {
                fprintf(stderr, "ld: input %s section %s has null relocation entry\n",
                        display_name, elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
                return -1;
            }
            if (elf_reloc_offset(rel) > elf_section_size(sec)) {
                fprintf(stderr, "ld: input %s section %s has out-of-range relocation offset\n",
                        display_name, elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
                return -1;
            }
            width = elf_reloc_size_for_machine(elf_machine(obj), elf_reloc_type(rel));
            if (width <= 0 || width > 8) {
                fprintf(stderr, "ld: input %s section %s has unsupported relocation type %u\n",
                        display_name, elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>",
                        (unsigned)elf_reloc_type(rel));
                return -1;
            }
            if (elf_reloc_offset(rel) + (uint64_t)width > elf_section_size(sec)) {
                fprintf(stderr, "ld: input %s section %s has relocation exceeding section bounds\n",
                        display_name, elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
                return -1;
            }
            sym = elf_reloc_symbol(rel);
            if (sym == NULL) {
                fprintf(stderr, "ld: input %s section %s has relocation with missing symbol\n",
                        display_name, elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
                return -1;
            }
        }
    }

    return 0;
}

static int symstate_note_object(symstate_t *state, elfobj_t *obj) {
    size_t i;

    if (state == NULL || obj == NULL) {
        return 0;
    }
    for (i = 0; i < elf_symbol_count(obj); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(obj, i);
        const char *name;
        uint8_t bind;
        uint16_t shndx;

        if (sym == NULL) {
            continue;
        }
        name = elf_symbol_name(sym);
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        bind = elf_symbol_bind(sym);
        if (bind != STB_GLOBAL && bind != STB_WEAK) {
            continue;
        }
        shndx = elf_symbol_shndx(sym);
        if (shndx == SHN_UNDEF) {
            if (bind != STB_WEAK && !symset_contains(&state->defined, name)) {
                if (symset_add(&state->unresolved, name) != 0) {
                    return -1;
                }
            }
            continue;
        }
        if (symset_add(&state->defined, name) != 0) {
            return -1;
        }
        symset_remove(&state->unresolved, name);
    }
    return 0;
}

static int obj_defines_unresolved(const symstate_t *state, elfobj_t *obj) {
    size_t i;

    if (state == NULL || obj == NULL) {
        return 1;
    }
    for (i = 0; i < elf_symbol_count(obj); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(obj, i);
        const char *name;
        uint8_t bind;
        uint16_t shndx;

        if (sym == NULL) {
            continue;
        }
        name = elf_symbol_name(sym);
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        bind = elf_symbol_bind(sym);
        if (bind != STB_GLOBAL && bind != STB_WEAK) {
            continue;
        }
        shndx = elf_symbol_shndx(sym);
        if (shndx != SHN_UNDEF && symset_contains(&state->unresolved, name)) {
            return 1;
        }
    }
    return 0;
}

static int parse_archive_header(const char *path, unsigned char **out_buf, size_t *out_sz, int *out_thin) {
    if (read_file(path, out_buf, out_sz) != 0) {
        fprintf(stderr, "ld: cannot read archive %s\n", path);
        return -1;
    }
    if (*out_sz < 8) {
        free(*out_buf);
        *out_buf = NULL;
        *out_sz = 0;
        fprintf(stderr, "ld: unsupported archive format: %s\n", path);
        return -1;
    }
    if (memcmp(*out_buf, "!<arch>\n", 8) == 0) {
        *out_thin = 0;
        return 0;
    }
    if (memcmp(*out_buf, "!<thin>\n", 8) == 0) {
        *out_thin = 1;
        return 0;
    }
    free(*out_buf);
    *out_buf = NULL;
    *out_sz = 0;
    fprintf(stderr, "ld: unsupported archive format: %s\n", path);
    return -1;
}

static char *path_dirname_dup(const char *path) {
    char *dup = xstrdup(path);
    char *slash;

    if (dup == NULL) {
        return NULL;
    }
    slash = strrchr(dup, '/');
    if (slash == NULL) {
        dup[0] = '.';
        dup[1] = '\0';
    } else if (slash == dup) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    return dup;
}

static int path_is_within_dir(const char *dir, const char *path) {
    size_t dlen = strlen(dir);

    if (strncmp(dir, path, dlen) != 0) {
        return 0;
    }
    if (path[dlen] == '\0' || path[dlen] == '/') {
        return 1;
    }
    return 0;
}

static char *resolve_thin_member_path(const char *archive_path, const char *member_name) {
    char *archive_dir = NULL;
    char *archive_real = NULL;
    char *candidate = NULL;
    char *member_real = NULL;

    if (member_name == NULL || member_name[0] == '\0') {
        return NULL;
    }
    archive_dir = path_dirname_dup(archive_path);
    if (archive_dir == NULL) {
        return NULL;
    }
    archive_real = realpath(archive_dir, NULL);
    if (archive_real == NULL) {
        free(archive_dir);
        return NULL;
    }
    if (member_name[0] == '/') {
        candidate = xstrdup(member_name);
    } else {
        candidate = path_join(archive_real, member_name);
    }
    if (candidate == NULL) {
        free(archive_real);
        free(archive_dir);
        return NULL;
    }
    member_real = realpath(candidate, NULL);
    if (member_real == NULL || !path_is_within_dir(archive_real, member_real)) {
        free(member_real);
        free(candidate);
        free(archive_real);
        free(archive_dir);
        return NULL;
    }
    free(candidate);
    free(archive_real);
    free(archive_dir);
    return member_real;
}

static int load_archive_members(const char *path, ld_ctx_t *ctx, objvec_t *objs, symstate_t *state,
                                int whole_archive) {
    unsigned char *buf = NULL;
    size_t sz = 0;
    size_t off = 8;
    const char *strtab = NULL;
    size_t strtab_sz = 0;
    int thin = 0;
    symset_t seen_members;
    int pass_progress;
    int pass_count = 0;
    const char *bad = NULL;

    memset(&seen_members, 0, sizeof(seen_members));
    if (parse_archive_header(path, &buf, &sz, &thin) != 0) {
        return -1;
    }

    do {
        pass_count++;
        if (pass_count > LD_MAX_ARCHIVE_SCAN_PASSES) {
            fprintf(stderr, "ld: archive resolution pass limit exceeded (%d) for %s\n",
                    LD_MAX_ARCHIVE_SCAN_PASSES, path);
            symset_free(&seen_members);
            free(buf);
            return -1;
        }
        pass_progress = 0;
        off = 8;
        while (off + 60 <= sz) {
            const unsigned char *hdr = buf + off;
            uint64_t msize = 0;
            const unsigned char *mdata = NULL;
            const unsigned char *body = NULL;
            char *mname;
            size_t name_extra = 0;
            size_t body_sz = 0;
            char member_key[32];
            int is_special = 0;
            int has_member_payload = 0;

            /*
             * An archive is input like any other, and one that is not as
             * an archive should be is said to be so: to stop reading at
             * the first thing that does not fit, as this did, is to link
             * without the members after it and blame their symbols.
             */
            if (hdr[58] != '`' || hdr[59] != '\n') {
                bad = "a member header does not end as one does";
                goto malformed;
            }
            if (parse_u64_dec((const char *)hdr + 48, 10, &msize) != 0) {
                bad = "a member's size is not a number";
                goto malformed;
            }
            off += 60;
            mdata = buf + off;
            /* A name kept in the member's data is read out of it: no
             * further than the file goes, whatever size the header claims. */
            mname = decode_ar_name((const char *)hdr, mdata, msize < (uint64_t)(sz - off) ? msize : (uint64_t)(sz - off),
                                   strtab, strtab_sz, &name_extra);
            if (mname == NULL) {
                bad = "a member's name cannot be read";
                goto malformed;
            }
            is_special = strcmp(mname, "/") == 0 ||
                         strcmp(mname, "//") == 0 ||
                         strcmp(mname, "__.SYMDEF") == 0 ||
                         strcmp(mname, "__.SYMDEF SORTED") == 0;
            has_member_payload = !thin || is_special;
            /* In the member's own width: a size_t may be narrower. */
            if (has_member_payload && msize > (uint64_t)(sz - off)) {
                free(mname);
                bad = "a member is longer than what is left of the file";
                goto malformed;
            }
            if (has_member_payload && (uint64_t)name_extra > msize) {
                free(mname);
                bad = "a member's name is longer than the member";
                goto malformed;
            }
            if (has_member_payload) {
                body_sz = (size_t)msize - name_extra;
                body = mdata + name_extra;
            }

            if (strcmp(mname, "//") == 0) {
                strtab = (const char *)body;
                strtab_sz = body_sz;
            } else if (!is_special && has_member_payload &&
                       body_sz >= 4 && body[0] == 0x7f && body[1] == 'E' && body[2] == 'L' && body[3] == 'F') {
                elfobj_t *obj = NULL;
                /* Where it is in this archive is what it is: the set is this
                 * archive's alone. */
                snprintf(member_key, sizeof(member_key), "%zu", off);
                if (!symset_contains(&seen_members, member_key) && elf_open_memory(body, body_sz, &obj) == ELF_OK) {
                    maybe_autoswitch_mode(ctx, obj, objs != NULL ? objs->count : 0, path);
                    if (elf_type(obj) == ET_REL && obj_matches_mode(obj, ctx->mode) &&
                        (whole_archive || obj_defines_unresolved(state, obj))) {
                        char member_name[512];
                        if (symset_add(&seen_members, member_key) != 0) {
                            elf_close(obj);
                            free(mname);
                            symset_free(&seen_members);
                            free(buf);
                            return -1;
                        }
                        snprintf(member_name, sizeof(member_name), "%s(%s)", path, mname);
                        if (validate_relocatable_input(obj, member_name) != 0) {
                            elf_close(obj);
                            free(mname);
                            symset_free(&seen_members);
                            free(buf);
                            return -1;
                        }
                        if (objvec_push(objs, obj, member_name) != 0) {
                            elf_close(obj);
                            free(mname);
                            symset_free(&seen_members);
                            free(buf);
                            return -1;
                        }
                        /* The list has it now, and will close it. */
                        if (symstate_note_object(state, obj) != 0) {
                            free(mname);
                            symset_free(&seen_members);
                            free(buf);
                            return -1;
                        }
                        pass_progress = 1;
                    } else {
                        elf_close(obj);
                    }
                }
            } else if (!is_special && thin) {
                char *thin_member_path = resolve_thin_member_path(path, mname);
                elfobj_t *obj = NULL;
                if (thin_member_path == NULL) {
                    fprintf(stderr, "ld: invalid thin archive member path '%s' in %s\n", mname, path);
                    free(mname);
                    symset_free(&seen_members);
                    free(buf);
                    return -1;
                }
                if (!symset_contains(&seen_members, thin_member_path)) {
                    int should_load = 0;
                    if (elf_open(thin_member_path, &obj) == ELF_OK) {
                        maybe_autoswitch_mode(ctx, obj, objs != NULL ? objs->count : 0, thin_member_path);
                        should_load = elf_type(obj) == ET_REL && obj_matches_mode(obj, ctx->mode) &&
                                      (whole_archive || obj_defines_unresolved(state, obj));
                        elf_close(obj);
                    }
                    if (should_load) {
                        if (symset_add(&seen_members, thin_member_path) != 0) {
                            free(thin_member_path);
                            free(mname);
                            symset_free(&seen_members);
                            free(buf);
                            return -1;
                        }
                        if (load_object_input(thin_member_path, ctx, objs, state, 1) != 0) {
                            free(thin_member_path);
                            free(mname);
                            symset_free(&seen_members);
                            free(buf);
                            return -1;
                        }
                        pass_progress = 1;
                    }
                }
                free(thin_member_path);
            }
            free(mname);
            if (has_member_payload) {
                off += (size_t)msize;
                if ((off & 1u) != 0) {
                    off++;
                }
            }
        }
        if (off < sz) {
            bad = "it ends in the middle of a member header";
            goto malformed;
        }
    } while (!whole_archive && pass_progress);

    symset_free(&seen_members);
    free(buf);
    return 0;

malformed:
    fprintf(stderr, "ld: %s: malformed archive: %s (at offset %zu)\n", path, bad, off);
    symset_free(&seen_members);
    free(buf);
    return -1;
}

static int object_has_lto_sections(const elfobj_t *obj) {
    size_t i;

    if (obj == NULL) {
        return 0;
    }
    for (i = 0; i < elf_section_count(obj); ++i) {
        const elf_section_t *sec = elf_section_get(obj, i);
        const char *name = sec != NULL ? elf_section_name(sec) : NULL;
        if (name != NULL &&
            (strncmp(name, ".gnu.lto_", 9) == 0 ||
             strcmp(name, ".llvmbc") == 0 ||
             strcmp(name, ".llvmcmd") == 0 ||
             strncmp(name, ".llvm.lto", 9) == 0)) {
            return 1;
        }
    }
    return 0;
}

static int load_object_input(const char *path, ld_ctx_t *ctx, objvec_t *objs, symstate_t *state, int quiet) {
    elfobj_t *obj = NULL;
    char mat_path[1024];
    int mat_rc;

    if (elf_open(path, &obj) != ELF_OK) {
        if (!quiet) {
            fprintf(stderr, "ld: failed to open input %s\n", path);
        }
        return -1;
    }
    maybe_autoswitch_mode(ctx, obj, objs != NULL ? objs->count : 0, path);
    if (object_has_lto_sections(obj) && ctx != NULL && ctx->plugin_unusable) {
        fprintf(stderr, "ld: %s holds LTO bytecode, and the plugin to compile it is a shared object, "
                        "which this linker cannot load: compile without -flto\n", path);
        elf_close(obj);
        return -1;
    }
    if (object_has_lto_sections(obj) && ctx != NULL && ctx->plugin_path != NULL && ctx->plugin_path[0] != '\0') {
        mat_rc = plugin_materialize_object(ctx, path, mat_path, sizeof(mat_path));
        if (mat_rc < 0) {
            if (!quiet) {
                fprintf(stderr, "ld: plugin materialization failed for %s\n", path);
            }
            elf_close(obj);
            return -1;
        }
        if (mat_rc > 0 && strcmp(mat_path, path) != 0) {
            elf_close(obj);
            return load_object_input(mat_path, ctx, objs, state, quiet);
        }
    }
    if (!obj_matches_mode(obj, ctx->mode)) {
        if (!quiet) {
            fprintf(stderr, "ld: input %s has mismatched class/machine/endianness\n", path);
        }
        elf_close(obj);
        return -1;
    }
    if (elf_type(obj) != ET_REL) {
        if (elf_type(obj) == ET_DYN && ctx != NULL &&
            (ctx->expect_type == ET_EXEC || ctx->expect_type == ET_DYN)) {
            elf_close(obj);
            return register_dso_provider(ctx, path, state);
        }
        if (!quiet) {
            fprintf(stderr, "ld: input %s is not relocatable (only ET_REL supported)\n", path);
        }
        elf_close(obj);
        return -1;
    }
    if (validate_relocatable_input(obj, path) != 0) {
        elf_close(obj);
        return -1;
    }
    if (objvec_push(objs, obj, path) != 0) {
        elf_close(obj);
        return -1;
    }
    /* The list has it now, and will close it. */
    return symstate_note_object(state, obj) != 0 ? -1 : 0;
}

/*
 * Whether the file at `path` can be the library a -l asks for: it can be
 * read, and if it is an ELF file it is for the machine the link is for,
 * once that is known.  A library for another machine in an earlier
 * directory is passed over, not an error: a search path that names both
 * /lib64 and /lib is an ordinary one.  What is not an ELF file is an
 * archive or a script, and is looked into later.
 */
static int lib_candidate_suits(const ld_ctx_t *ctx, const char *path) {
    unsigned char h[20];
    FILE *f;
    size_t n;
    int mode;

    if (access(path, R_OK) != 0) {
        return 0;
    }
    if (!ctx->explicit_mode && !ctx->mode_settled) {
        return 1;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    n = fread(h, 1, sizeof(h), f);
    fclose(f);
    if (n < sizeof(h) || memcmp(h, "\177ELF", 4) != 0) {
        return 1;
    }
    /* e_ident[EI_CLASS], and e_machine at 18, little-endian. */
    mode = h[4] == 2 && h[18] == (EM_X86_64 & 0xff) && h[19] == 0 ? 64
         : h[4] == 1 && h[18] == EM_386 && h[19] == 0 ? 32 : 0;
    return mode == ctx->mode;
}

static char *resolve_library_path_suffix_ex(const ld_ctx_t *ctx, const char *name, const char *suffix,
                                            int include_default_dirs) {
    static const char *default_dirs[] = {
        "/usr/lib",
        "/usr/local/lib"
    };
    char leaf[512];
#ifdef LD_SUBSTRATE_BUILD
    const char *sysroot;
#endif
    size_t i;

    snprintf(leaf, sizeof(leaf), "lib%s%s", name, suffix != NULL ? suffix : "");
    for (i = 0; i < ctx->lib_paths.count; ++i) {
        char *cand = path_join(ctx->lib_paths.items[i], leaf);
        if (cand != NULL && lib_candidate_suits(ctx, cand)) {
            return cand;
        }
        free(cand);
        /* The directories are searched in order, and each for the shared
         * library and then the archive: an archive in an earlier
         * directory is found before a shared library in a later one. */
        if (suffix != NULL && strcmp(suffix, ".so") == 0) {
            char aleaf[512];
            int earlier;

            snprintf(aleaf, sizeof(aleaf), "lib%s.a", name);
            cand = path_join(ctx->lib_paths.items[i], aleaf);
            earlier = cand != NULL && lib_candidate_suits(ctx, cand);
            free(cand);
            if (earlier) {
                return NULL;
            }
        }
    }
    if (!include_default_dirs) {
        return NULL;
    }
#ifdef LD_SUBSTRATE_BUILD
    sysroot = getenv("SUBSTRATE_SYSROOT");
    if (sysroot != NULL && sysroot[0] != '\0') {
        for (i = 0; i < sizeof(default_dirs) / sizeof(default_dirs[0]); ++i) {
            char prefixed[PATH_MAX];
            char *cand;
            if (snprintf(prefixed, sizeof(prefixed), "%s%s", sysroot, default_dirs[i]) >= (int)sizeof(prefixed)) {
                continue;
            }
            cand = path_join(prefixed, leaf);
            if (cand != NULL && lib_candidate_suits(ctx, cand)) {
                return cand;
            }
            free(cand);
        }
        return NULL;
    }
#endif
    for (i = 0; i < sizeof(default_dirs) / sizeof(default_dirs[0]); ++i) {
        char *cand = path_join(default_dirs[i], leaf);
        if (cand != NULL && lib_candidate_suits(ctx, cand)) {
            return cand;
        }
        free(cand);
    }
    return NULL;
}

char *resolve_library_path_exact(const ld_ctx_t *ctx, const char *leaf) {
    static const char *default_dirs[] = {
        "/usr/lib",
        "/usr/local/lib"
    };
#ifdef LD_SUBSTRATE_BUILD
    const char *sysroot;
#endif
    size_t i;

    if (leaf == NULL || leaf[0] == '\0') {
        return NULL;
    }
    if (strchr(leaf, '/') != NULL) {
        return access(leaf, R_OK) == 0 ? xstrdup(leaf) : NULL;
    }
    for (i = 0; i < ctx->lib_paths.count; ++i) {
        char *cand = path_join(ctx->lib_paths.items[i], leaf);
        if (cand != NULL && lib_candidate_suits(ctx, cand)) {
            return cand;
        }
        free(cand);
    }
#ifdef LD_SUBSTRATE_BUILD
    sysroot = getenv("SUBSTRATE_SYSROOT");
    if (sysroot != NULL && sysroot[0] != '\0') {
        for (i = 0; i < sizeof(default_dirs) / sizeof(default_dirs[0]); ++i) {
            char prefixed[PATH_MAX];
            char *cand;
            if (snprintf(prefixed, sizeof(prefixed), "%s%s", sysroot, default_dirs[i]) >= (int)sizeof(prefixed)) {
                continue;
            }
            cand = path_join(prefixed, leaf);
            if (cand != NULL && lib_candidate_suits(ctx, cand)) {
                return cand;
            }
            free(cand);
        }
        return NULL;
    }
#endif
    for (i = 0; i < sizeof(default_dirs) / sizeof(default_dirs[0]); ++i) {
        char *cand = path_join(default_dirs[i], leaf);
        if (cand != NULL && lib_candidate_suits(ctx, cand)) {
            return cand;
        }
        free(cand);
    }
    return NULL;
}

static char *resolve_library_path_suffix(const ld_ctx_t *ctx, const char *name, const char *suffix) {
    return resolve_library_path_suffix_ex(ctx, name, suffix, 1);
}

static char *resolve_library_path_suffix_explicit(const ld_ctx_t *ctx, const char *name, const char *suffix) {
    return resolve_library_path_suffix_ex(ctx, name, suffix, 0);
}

/* Whether the file begins as an archive does, whatever it is called. */
static int file_is_archive(const char *path) {
    char magic[8];
    FILE *f = fopen(path, "rb");
    int is = 0;

    if (f != NULL) {
        is = fread(magic, 1, sizeof(magic), f) == sizeof(magic) &&
             (memcmp(magic, "!<arch>\n", 8) == 0 || memcmp(magic, "!<thin>\n", 8) == 0);
        fclose(f);
    }
    return is;
}

static int load_path_input(const char *path, ld_ctx_t *ctx, objvec_t *objs, symstate_t *state,
                           int whole_archive, int quiet) {
    if (file_is_archive(path)) {
        return load_archive_members(path, ctx, objs, state, whole_archive);
    }
    return load_object_input(path, ctx, objs, state, quiet);
}

static int load_library_script(const char *script_path, ld_ctx_t *ctx, objvec_t *objs, symstate_t *state,
                               int whole_archive, int as_needed, int *handled) {
    unsigned char *buf = NULL;
    size_t sz = 0;
    char *text = NULL;
    char *dir = NULL;
    size_t i = 0;
    int loaded_any = 0;
    int paren_depth = 0;
    int as_needed_depth = 0;
    int pending_as_needed = 0;

    if (handled != NULL) {
        *handled = 0;
    }
    if (script_path == NULL) {
        return 0;
    }
    if (read_file(script_path, &buf, &sz) != 0) {
        return -1;
    }
    text = (char *)malloc(sz + 1);
    if (text == NULL) {
        free(buf);
        return -1;
    }
    memcpy(text, buf, sz);
    text[sz] = '\0';
    if (strstr(text, "INPUT") == NULL && strstr(text, "GROUP") == NULL) {
        free(text);
        free(buf);
        return 0;
    }
    if (handled != NULL) {
        *handled = 1;
    }
    dir = path_dirname_dup(script_path);
    if (dir == NULL) {
        free(text);
        free(buf);
        return -1;
    }

    while (i < sz) {
        char tok[1024];
        size_t tn = 0;
        char *resolved = NULL;
        int effective_as_needed;
        int c;

        if (i + 1 < sz && text[i] == '/' && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < sz && !(text[i] == '*' && text[i + 1] == '/')) {
                i++;
            }
            if (i + 1 < sz) {
                i += 2;
            }
            continue;
        }
        c = (unsigned char)text[i];
        if (isspace(c) || c == ',') {
            i++;
            continue;
        }
        if (c == '(') {
            paren_depth++;
            if (pending_as_needed) {
                as_needed_depth = paren_depth;
                pending_as_needed = 0;
            }
            i++;
            continue;
        }
        if (c == ')') {
            if (as_needed_depth == paren_depth) {
                as_needed_depth = 0;
            }
            if (paren_depth > 0) {
                paren_depth--;
            }
            i++;
            continue;
        }
        while (i < sz && !isspace((unsigned char)text[i]) && text[i] != '(' && text[i] != ')' && text[i] != ',' &&
               tn + 1 < sizeof(tok)) {
            tok[tn++] = text[i++];
        }
        while (i < sz && !isspace((unsigned char)text[i]) && text[i] != '(' && text[i] != ')' && text[i] != ',') {
            i++;
        }
        tok[tn] = '\0';
        if (tok[0] == '\0') {
            i++;
            continue;
        }
        if (strcmp(tok, "INPUT") == 0 || strcmp(tok, "GROUP") == 0 ||
            strcmp(tok, "OUTPUT_FORMAT") == 0 ||
            strcmp(tok, "SEARCH_DIR") == 0) {
            continue;
        }
        if (strcmp(tok, "AS_NEEDED") == 0) {
            pending_as_needed = 1;
            continue;
        }
        if (tok[0] != '/' && strstr(tok, ".so") == NULL && strstr(tok, ".a") == NULL && strstr(tok, ".o") == NULL) {
            continue;
        }
        effective_as_needed = as_needed || as_needed_depth != 0;
        if (tok[0] == '/') {
            resolved = xstrdup(tok);
        } else {
            resolved = path_join(dir, tok);
        }
        if (resolved == NULL) {
            free(dir);
            free(text);
            free(buf);
            return -1;
        }
        if (strstr(tok, ".so") != NULL) {
            int shared_matches = 0;
            int have_shared_match = 0;

            if (effective_as_needed) {
                if (shared_object_matches_unresolved(resolved, ctx, state, &shared_matches) == 0) {
                    have_shared_match = 1;
                    if (!shared_matches) {
                        free(resolved);
                        continue;
                    }
                }
            }
            if (register_dso_provider(ctx, resolved, state) != 0) {
                if (!effective_as_needed || !have_shared_match || shared_matches) {
                    free(resolved);
                    free(dir);
                    free(text);
                    free(buf);
                    return -1;
                }
            } else {
                loaded_any = 1;
            }
        } else {
            if (load_path_input(resolved, ctx, objs, state, whole_archive, 0) != 0) {
                free(resolved);
                free(dir);
                free(text);
                free(buf);
                return -1;
            }
            loaded_any = 1;
        }
        free(resolved);
    }

    free(dir);
    free(text);
    free(buf);
    if (!loaded_any) {
        return -1;
    }
    return 0;
}

static int load_library_input(ld_ctx_t *ctx, const ld_input_t *in, objvec_t *objs, symstate_t *state) {
    char *path_so = NULL;
    char *path_a = NULL;
    char *path_a_explicit = NULL;
    int shared_matches = 0;
    int have_shared_match = 0;
    int handled = 0;

    if (in->text != NULL && in->text[0] == ':') {
        char *path_exact = resolve_library_path_exact(ctx, in->text + 1);
        if (path_exact == NULL) {
            fprintf(stderr, "ld: cannot find -l%s\n", in->text);
            return -1;
        }
        if (has_suffix(path_exact, ".so") &&
            load_library_script(path_exact, ctx, objs, state, in->whole_archive, in->as_needed, &handled) == 0 &&
            handled) {
            free(path_exact);
            return 0;
        }
        if (has_suffix(path_exact, ".so") && (ctx->expect_type == ET_DYN || ctx->expect_type == ET_EXEC)) {
            if (in->as_needed &&
                shared_object_matches_unresolved(path_exact, ctx, state, &shared_matches) == 0 &&
                !shared_matches) {
                free(path_exact);
                return 0;
            }
            if (register_dso_provider(ctx, path_exact, state) == 0) {
                free(path_exact);
                return 0;
            }
        }
        if (load_path_input(path_exact, ctx, objs, state, in->whole_archive, 0) != 0) {
            free(path_exact);
            return -1;
        }
        free(path_exact);
        return 0;
    }

    if (in->lib_mode == LD_LIBMODE_STATIC) {
        path_a = resolve_library_path_suffix(ctx, in->text, ".a");
        if (path_a == NULL) {
            fprintf(stderr, "ld: cannot find -l%s\n", in->text);
            return -1;
        }
        if (load_path_input(path_a, ctx, objs, state, in->whole_archive, 0) != 0) {
            free(path_a);
            return -1;
        }
        free(path_a);
        return 0;
    }

    path_so = resolve_library_path_suffix_explicit(ctx, in->text, ".so");
    if (path_so == NULL) {
        path_a_explicit = resolve_library_path_suffix_explicit(ctx, in->text, ".a");
        if (path_a_explicit != NULL) {
            if (load_path_input(path_a_explicit, ctx, objs, state, in->whole_archive, 0) != 0) {
                free(path_a_explicit);
                return -1;
            }
            free(path_a_explicit);
            return 0;
        }
        path_so = resolve_library_path_suffix(ctx, in->text, ".so");
    }
    if (path_so != NULL &&
        load_library_script(path_so, ctx, objs, state, in->whole_archive, in->as_needed, &handled) == 0 &&
        handled) {
        free(path_so);
        return 0;
    }
    if (path_so != NULL && (ctx->expect_type == ET_DYN || ctx->expect_type == ET_EXEC)) {
        if (in->as_needed) {
            if (shared_object_matches_unresolved(path_so, ctx, state, &shared_matches) == 0) {
                have_shared_match = 1;
                if (!shared_matches) {
                    free(path_so);
                    return 0;
                }
            }
        }
        if (register_dso_provider(ctx, path_so, state) == 0) {
            free(path_so);
            return 0;
        }
    }
    if (path_so != NULL && load_path_input(path_so, ctx, objs, state, in->whole_archive, 1) == 0) {
        free(path_so);
        return 0;
    }
    if (path_so != NULL && in->as_needed) {
        if (shared_object_matches_unresolved(path_so, ctx, state, &shared_matches) == 0) {
            have_shared_match = 1;
            if (!shared_matches) {
                free(path_so);
                return 0;
            }
        }
    }

    path_a = resolve_library_path_suffix(ctx, in->text, ".a");
    if (path_a != NULL) {
        if (load_library_script(path_a, ctx, objs, state, in->whole_archive, in->as_needed, &handled) == 0 &&
            handled) {
            free(path_so);
            free(path_a);
            return 0;
        }
        if (load_path_input(path_a, ctx, objs, state, in->whole_archive, 0) != 0) {
            free(path_so);
            free(path_a);
            return -1;
        }
        free(path_so);
        free(path_a);
        return 0;
    }

    if (path_so != NULL) {
        if (in->as_needed && have_shared_match && !shared_matches) {
            free(path_so);
            return 0;
        }
        fprintf(stderr,
                "ld: cannot use shared library %s for this link (shared-object linking not implemented)\n",
                path_so);
        free(path_so);
        return -1;
    }

    fprintf(stderr, "ld: cannot find -l%s\n", in->text);
    return -1;
}

static int process_input_once(ld_ctx_t *ctx, const ld_input_t *in, objvec_t *objs, symstate_t *state) {
    if (in->kind == LD_INPUT_FILE) {
        return load_path_input(in->text, ctx, objs, state, in->whole_archive, 0);
    }
    if (in->kind == LD_INPUT_LIB) {
        return load_library_input(ctx, in, objs, state);
    }
    return 0;
}

static int load_group_inputs(ld_ctx_t *ctx, objvec_t *objs, symstate_t *state,
                             size_t begin, size_t end) {
    int progress;

    do {
        size_t before = objs->count;
        size_t i;

        for (i = begin; i < end; ++i) {
            const ld_input_t *in = &ctx->inputs.items[i];
            if (in->kind == LD_INPUT_GROUP_START || in->kind == LD_INPUT_GROUP_END) {
                continue;
            }
            if (process_input_once(ctx, in, objs, state) != 0) {
                return -1;
            }
        }
        progress = objs->count > before;
    } while (progress);

    return 0;
}

int load_all_inputs(ld_ctx_t *ctx, objvec_t *objs) {
    symstate_t state;
    size_t i;

    memset(&state, 0, sizeof(state));
    for (i = 0; i < ctx->force_undefined.count; ++i) {
        if (symset_add(&state.unresolved, ctx->force_undefined.items[i]) != 0) {
            symstate_free(&state);
            return -1;
        }
    }
    for (i = 0; i < ctx->inputs.count; ++i) {
        const ld_input_t *in = &ctx->inputs.items[i];
        if (in->kind == LD_INPUT_GROUP_START) {
            size_t j;
            int depth = 1;

            for (j = i + 1; j < ctx->inputs.count; ++j) {
                if (ctx->inputs.items[j].kind == LD_INPUT_GROUP_START) {
                    depth++;
                } else if (ctx->inputs.items[j].kind == LD_INPUT_GROUP_END) {
                    depth--;
                    if (depth == 0) {
                        break;
                    }
                }
            }
            if (depth != 0) {
                fprintf(stderr, "ld: --start-group without matching --end-group\n");
                symstate_free(&state);
                return -1;
            }
            if (load_group_inputs(ctx, objs, &state, i + 1, j) != 0) {
                symstate_free(&state);
                return -1;
            }
            i = j;
            continue;
        }
        if (in->kind == LD_INPUT_GROUP_END) {
            fprintf(stderr, "ld: --end-group without matching --start-group\n");
            symstate_free(&state);
            return -1;
        }
        if (process_input_once(ctx, in, objs, &state) != 0) {
            symstate_free(&state);
            return -1;
        }
    }

    symstate_free(&state);
    return 0;
}
