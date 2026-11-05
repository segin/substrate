/*
 * ld_options.c -- the command line: one table of the options there are,
 * and the code that reads an argument against it.
 */

#include "ld.h"

typedef enum {
    OPT_FLAG,           /* the word alone */
    OPT_VALUE,          /* WORD VALUE or WORD=VALUE */
    OPT_OPTVAL,         /* WORD or WORD=VALUE */
    OPT_JOINED          /* a letter: -xVALUE or -x VALUE */
} opt_kind_t;

typedef struct ld_opt ld_opt_t;

/*
 * What an option does: 0, or what ld is to exit with (LD_OPT_USAGE for
 * a mistake on the command line, having said what; 1 for no memory), or
 * LD_OPT_DONE when there is nothing more to do and nothing wrong.
 * `as` is the option as the table spells it, for messages; `val` its
 * value, never empty where the kind has one (NULL for OPT_OPTVAL without).
 */
typedef int (*opt_set_fn)(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val);

struct ld_opt {
    const char *names;  /* its spellings, dashes and all, between spaces */
    opt_kind_t kind;
    opt_set_fn set;
    size_t field;       /* for the setters of one member of ld_ctx_t: which */
    int n;              /* and what to set it to (for a string: whether it
                         * may be empty); or the setter's own */
};

void ld_usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [-m32|-m64|-m <emulation>] [-r|-shared|-pie|-static] "
            "[-o output] [-L dir] [-l name] [-Bstatic|-Bdynamic] "
            "[--start-group ... --end-group] [--whole-archive|--no-whole-archive] "
            "[-plugin path] [-plugin-opt opt] "
            "[-e symbol] [-T script] [--allow-undefined] [-z text|notext|execstack|noexecstack|relro|norelro|separate-code|noseparate-code] "
            "[--reproduce dir] "
            "[--hash-style=sysv|gnu|both] [--orphan-handling=place|warn|error|discard] "
            "[--compat=gnu|lld] input...\n",
            prog);
}

/* ---- setters of one member ------------------------------------------- */

static int opt_int(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)as;
    (void)val;
    *(int *)((char *)ctx + o->field) = o->n;
    return 0;
}

static int opt_str(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)as;
    *(const char **)((char *)ctx + o->field) = val;
    return 0;
}

static int opt_strvec(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)as;
    return strvec_push((strvec_t *)((char *)ctx + o->field), val) != 0 ? 1 : 0;
}

/* Accepted, and nothing done about it. */
static int opt_ignored(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)ctx;
    (void)o;
    (void)as;
    (void)val;
    return 0;
}

/* ---- the rest -------------------------------------------------------- */

static int opt_help(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)o;
    (void)as;
    (void)val;
    ld_usage(ctx->opt.prog);
    return LD_OPT_DONE;
}

static int opt_compat(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)o;
    (void)as;
    if (parse_compat_mode(val, &ctx->opt.compat_mode) != 0) {
        fprintf(stderr, "ld: unsupported compatibility mode '%s' (expected gnu or lld)\n", val);
        return LD_OPT_USAGE;
    }
    return 0;
}

/* -m32, -m64 (n says which) and -m EMULATION. */
static int opt_mode(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    int m = o->n;

    if (m == 0 && (m = parse_mode_token(val)) == 0) {
        fprintf(stderr,
                "ld: unsupported emulation '%s' for -m "
                "(supported: elf_x86_64, elf64-x86-64, x86_64, amd64, "
                "elf_i386, elf32-i386, i386, elf32_x86_64, elf32-x86-64, x32)\n",
                val);
        return LD_OPT_USAGE;
    }
    return set_explicit_mode(ctx, m, as) != 0 ? LD_OPT_USAGE : 0;
}

static int opt_plugin_opt(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)o;
    (void)as;
    if (ctx->opt.plugin_opt_count >= sizeof(ctx->opt.plugin_opts) / sizeof(ctx->opt.plugin_opts[0])) {
        fprintf(stderr, "ld: too many -plugin-opt arguments (max %zu)\n",
                sizeof(ctx->opt.plugin_opts) / sizeof(ctx->opt.plugin_opts[0]));
        return LD_OPT_USAGE;
    }
    ctx->opt.plugin_opts[ctx->opt.plugin_opt_count++] = val;
    return 0;
}

static int opt_relocatable(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)o;
    (void)as;
    (void)val;
    ctx->opt.expect_type = ET_REL;
    return 0;
}

/* -shared and -pie (n).  Both are ET_DYN.  A PIE is a program all the
 * same: it has an entry, what it leaves undefined is an error, and it is
 * made executable. */
static int opt_shared(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)as;
    (void)val;
    ctx->opt.expect_type = ET_DYN;
    ctx->opt.pie = o->n;
    if (!ctx->opt.explicit_lib_mode) {
        ctx->opt.current_lib_mode = LD_LIBMODE_DYNAMIC;
    }
    return 0;
}

static int opt_no_pie(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)o;
    (void)as;
    (void)val;
    if (ctx->opt.pie) {
        ctx->opt.pie = 0;
        ctx->opt.expect_type = ET_EXEC;
    }
    return 0;
}

/* -static, -Bstatic, -Bdynamic: which libraries to use, not what to
 * make.  -static -pie is a PIE, and -shared -static a shared object of
 * archives. */
static int opt_lib_mode(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)as;
    (void)val;
    ctx->opt.current_lib_mode = (ld_lib_mode_t)o->n;
    ctx->opt.explicit_lib_mode = 1;
    return 0;
}

static int opt_icf(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)o;
    (void)as;
    if (strcmp(val, "safe") == 0) {
        ctx->opt.icf_mode = 1;
    } else if (strcmp(val, "all") == 0) {
        ctx->opt.icf_mode = 2;
    } else if (strcmp(val, "none") == 0) {
        ctx->opt.icf_mode = 0;
    } else {
        fprintf(stderr, "ld: unsupported --icf mode '%s' (supported: safe, all, none)\n", val);
        return LD_OPT_USAGE;
    }
    return 0;
}

/* An input: a file, -l NAME, or the start or end of a group (n says
 * which), with the modes that are in force where it stands. */
static int opt_input(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)as;
    return inputvec_push(&ctx->opt.inputs, (ld_input_kind_t)o->n, ctx->opt.current_lib_mode,
                         ctx->opt.current_whole_archive, ctx->opt.current_as_needed, val) != 0 ? 1 : 0;
}

/* --allow-undefined, --no-undefined (n). */
static int opt_undefined_policy(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)as;
    (void)val;
    ctx->opt.allow_undefined = o->n;
    ctx->opt.explicit_unresolved_policy = 1;
    return 0;
}

static int opt_unresolved_symbols(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)o;
    (void)as;
    if (strcmp(val, "ignore-all") == 0) {
        ctx->opt.allow_undefined = 1;
    } else if (strcmp(val, "report-all") == 0) {
        ctx->opt.allow_undefined = 0;
    } else {
        fprintf(stderr,
                "ld: unsupported --unresolved-symbols policy '%s' "
                "(supported: ignore-all, report-all)\n",
                val);
        return LD_OPT_USAGE;
    }
    ctx->opt.explicit_unresolved_policy = 1;
    return 0;
}

/* --defsym NAME=VALUE */
static int opt_defsym(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    char *name = xstrdup(val);
    char *eq;
    uint64_t value;
    int rc = 0;

    (void)o;
    (void)as;
    if (name == NULL) {
        return 1;
    }
    eq = strchr(name, '=');
    if (eq == NULL) {
        fprintf(stderr, "ld: --defsym requires NAME=VALUE\n");
        rc = LD_OPT_USAGE;
    } else {
        *eq++ = '\0';
        if (name[0] == '\0' || parse_u64_auto(eq, &value) != 0 ||
            defsymvec_push(&ctx->opt.defsyms, name, value) != 0) {
            fprintf(stderr, "ld: invalid --defsym `%s`\n", val);
            rc = LD_OPT_USAGE;
        }
    }
    free(name);
    return rc;
}

/* --orphan-handling=MODE: place, warn, error or discard, as GNU ld takes them. */
static int opt_orphan_handling(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    static const struct {
        const char *name;
        ld_orphan_mode_t mode;
    } modes[] = {
        { "place", LD_ORPHAN_PLACE }, { "warn", LD_ORPHAN_WARN },
        { "error", LD_ORPHAN_ERROR }, { "discard", LD_ORPHAN_DISCARD },
    };
    size_t i;

    (void)o;
    (void)as;
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        if (strcmp(val, modes[i].name) == 0) {
            ctx->opt.orphan_handling = modes[i].mode;
            return 0;
        }
    }
    fprintf(stderr, "ld: invalid argument to option \"--orphan-handling\"\n");
    return LD_OPT_USAGE;
}

static int opt_hash_style(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)o;
    (void)as;
    if (parse_hash_style_option(val, &ctx->opt.hash_style) != 0) {
        fprintf(stderr, "ld: unsupported --hash-style value '%s' (expected sysv|gnu|both)\n", val);
        return LD_OPT_USAGE;
    }
    return 0;
}

static int opt_image_base(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)o;
    (void)as;
    if (parse_u64_auto(val, &ctx->opt.image_base) != 0 || (ctx->opt.image_base & 0xfffu) != 0) {
        fprintf(stderr, "ld: -Ttext-segment needs an address that is a multiple of the page size, not '%s'\n",
                val);
        return LD_OPT_USAGE;
    }
    ctx->opt.have_image_base = 1;
    return 0;
}

/* -Ttext and its kin, which are words of their own and not -T and a
 * script called "text". */
static int opt_section_address(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)ctx;
    (void)o;
    (void)val;
    fprintf(stderr,
            "ld: %s is not supported: say where a section goes in a linker script (-T), "
            "as in SECTIONS { .text ADDRESS : { *(.text) } }, or where the image begins with "
            "-Ttext-segment\n", as);
    return LD_OPT_USAGE;
}

/* -T SCRIPT, -TSCRIPT, -T=SCRIPT */
static int opt_script(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)o;
    if (val[0] == '=') {
        val++;
    }
    if (val[0] == '\0') {
        fprintf(stderr, "ld: %s needs a value\n", as);
        return LD_OPT_USAGE;
    }
    ctx->opt.script_path = val;
    return 0;
}

/* --build-id[=STYLE]: sha1 (the default), md5, uuid, 0xHEX, none. */
static int opt_build_id(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)o;
    (void)as;
    return ld_build_id_option(&ctx->opt, val);
}

/* --version-script FILE: its nodes read now, after any read before. */
static int opt_version_script(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)o;
    (void)as;
    return ld_version_script_read(&ctx->version_script, val) != 0 ? LD_OPT_USAGE : 0;
}

/*
 * -z KEYWORD.  A keyword this linker does nothing about is said to be so
 * and the link goes on: build systems pass -z defs, -z max-page-size=...,
 * -z separate-code and -z nodelete as a matter of course, and a link is
 * not wrong for lacking one.
 */
static int opt_z(ld_ctx_t *ctx, const ld_opt_t *o, const char *as, const char *val) {
    (void)o;
    (void)as;
    if (val[0] == '=') {
        val++;
    }
    if (val[0] == '\0') {
        fprintf(stderr, "ld: -z needs a keyword\n");
        return LD_OPT_USAGE;
    }
    if (parse_z_option(ctx, val) != 0 && ld_warn(ctx, "-z %s is not supported and is ignored", val) != 0) {
        return LD_OPT_USAGE;
    }
    return 0;
}

#define F(member) offsetof(ld_ctx_t, opt.member)

/*
 * The options.  A word is matched whole, so the order of the rows does
 * not matter; the letters that take a value joined on (OPT_JOINED) are
 * tried only when no word matches, which is what makes -Ttext a word and
 * not a script, and -m32 not an emulation called "32".
 */
static const ld_opt_t options[] = {
    { "--help", OPT_FLAG, opt_help, 0, 0 },
    { "--version -v", OPT_FLAG, opt_int, F(query_version), 1 },
    { "--compat", OPT_VALUE, opt_compat, 0, 0 },

    /* what is made, and for which machine */
    { "-m32", OPT_FLAG, opt_mode, 0, 32 },
    { "-m64", OPT_FLAG, opt_mode, 0, 64 },
    { "-m", OPT_JOINED, opt_mode, 0, 0 },
    { "-o", OPT_JOINED, opt_str, F(out_path), 0 },
    { "-r", OPT_FLAG, opt_relocatable, 0, 0 },
    { "-shared -Bshareable", OPT_FLAG, opt_shared, 0, 0 },
    { "-pie --pie", OPT_FLAG, opt_shared, 0, 1 },
    { "-no-pie --no-pie", OPT_FLAG, opt_no_pie, 0, 0 },
    { "-e --entry", OPT_VALUE, opt_str, F(entry_symbol), 0 },
    { "-soname --soname -h", OPT_VALUE, opt_str, F(soname), 0 },
    { "-dynamic-linker --dynamic-linker", OPT_VALUE, opt_str, F(interp_path), 0 },
    /* No interpreter asked for, whatever an earlier -dynamic-linker said; a
     * later one asks again, as in GNU ld.  (There is no default to undo.) */
    { "--no-dynamic-linker -no-dynamic-linker", OPT_FLAG, opt_str, F(interp_path), 0 },
    { "-rpath --rpath", OPT_VALUE, opt_strvec, F(rpaths), 0 },
    /* Where to look for the libraries of libraries at link time, which
     * this linker does not go looking for. */
    { "-rpath-link --rpath-link", OPT_VALUE, opt_ignored, 0, 0 },
    { "-Ttext-segment --Ttext-segment", OPT_VALUE, opt_image_base, 0, 0 },
    { "-Ttext --Ttext -Tdata --Tdata -Tbss --Tbss -Trodata-segment --Trodata-segment "
      "-Tldata-segment --Tldata-segment", OPT_VALUE, opt_section_address, 0, 0 },
    { "--script", OPT_VALUE, opt_script, 0, 0 },
    { "--version-script -version-script", OPT_VALUE, opt_version_script, 0, 0 },
    { "-T", OPT_JOINED, opt_script, 0, 0 },
    { "-z", OPT_JOINED, opt_z, 0, 0 },
    { "--hash-style", OPT_VALUE, opt_hash_style, 0, 0 },
    { "--orphan-handling", OPT_VALUE, opt_orphan_handling, 0, 0 },
    /* The linker makes no unwind information of its own (GNU ld's
     * default is an FDE for the PLT), which is what this asks for. */
    { "--no-ld-generated-unwind-info", OPT_FLAG, opt_ignored, 0, 0 },
    { "--eh-frame-hdr", OPT_FLAG, opt_int, F(eh_frame_hdr), 1 },
    { "--no-eh-frame-hdr", OPT_FLAG, opt_int, F(eh_frame_hdr), 0 },
    /* The debugging information is left out.  -s would leave the symbol
     * table out as well, which is not done. */
    { "--strip-all -s --strip-debug -S", OPT_FLAG, opt_int, F(strip_debug), 1 },
    { "--emit-relocs -q", OPT_FLAG, opt_int, F(emit_relocs), 1 },
    /* which locals the output leaves out */
    { "--discard-all -x", OPT_FLAG, opt_int, F(discard), LD_DISCARD_ALL },
    { "--discard-locals -X", OPT_FLAG, opt_int, F(discard), LD_DISCARD_LOCALS },
    { "--discard-none", OPT_FLAG, opt_int, F(discard), LD_DISCARD_NONE },
    { "--build-id", OPT_OPTVAL, opt_build_id, 0, 0 },

    /* the inputs, and how the ones that follow are taken */
    { "-L", OPT_JOINED, opt_strvec, F(lib_paths), 0 },
    { "-nostdlib --nostdlib", OPT_FLAG, opt_int, F(nostdlib), 1 },
    { "-l", OPT_JOINED, opt_input, 0, LD_INPUT_LIB },
    { "--start-group", OPT_FLAG, opt_input, 0, LD_INPUT_GROUP_START },
    { "--end-group", OPT_FLAG, opt_input, 0, LD_INPUT_GROUP_END },
    { "-static -Bstatic", OPT_FLAG, opt_lib_mode, 0, LD_LIBMODE_STATIC },
    { "-Bdynamic", OPT_FLAG, opt_lib_mode, 0, LD_LIBMODE_DYNAMIC },
    { "--whole-archive", OPT_FLAG, opt_int, F(current_whole_archive), 1 },
    { "--no-whole-archive", OPT_FLAG, opt_int, F(current_whole_archive), 0 },
    { "--as-needed", OPT_FLAG, opt_int, F(current_as_needed), 1 },
    { "--no-as-needed", OPT_FLAG, opt_int, F(current_as_needed), 0 },
    { "--copy-dt-needed-entries --add-needed", OPT_FLAG, opt_int, F(copy_dt_needed), 1 },
    { "--no-copy-dt-needed-entries --no-add-needed", OPT_FLAG, opt_int, F(copy_dt_needed), 0 },
    { "--sysroot", OPT_VALUE, opt_str, F(sysroot), 1 },    /* may be empty */
    { "-plugin --plugin", OPT_VALUE, opt_str, F(plugin_path), 0 },
    { "-plugin-opt --plugin-opt", OPT_VALUE, opt_plugin_opt, 0, 0 },

    /* symbols */
    { "--allow-undefined", OPT_FLAG, opt_undefined_policy, 0, 1 },
    { "--no-undefined", OPT_FLAG, opt_undefined_policy, 0, 0 },
    { "--unresolved-symbols", OPT_VALUE, opt_unresolved_symbols, 0, 0 },
    { "--undefined", OPT_VALUE, opt_strvec, F(force_undefined), 0 },
    { "-u", OPT_JOINED, opt_strvec, F(force_undefined), 0 },
    { "--defsym", OPT_VALUE, opt_defsym, 0, 0 },
    { "--export-dynamic -rdynamic -E", OPT_FLAG, opt_int, F(export_dynamic), 1 },
    { "--no-export-dynamic", OPT_FLAG, opt_int, F(export_dynamic), 0 },
    { "-Bsymbolic -Bsymbolic-functions", OPT_FLAG, opt_int, F(bsymbolic), 1 },
    { "--gc-sections", OPT_FLAG, opt_int, F(gc_sections), 1 },
    { "--no-gc-sections", OPT_FLAG, opt_int, F(gc_sections), 0 },
    { "--print-gc-sections", OPT_FLAG, opt_int, F(gc_print_sections), 1 },
    { "--icf", OPT_VALUE, opt_icf, 0, 0 },

    /* what is said */
    { "--warn-common", OPT_FLAG, opt_int, F(warn_common), 1 },
    { "--no-warn-common", OPT_FLAG, opt_int, F(warn_common), 0 },
    { "--fatal-warnings", OPT_FLAG, opt_int, F(fatal_warnings), 1 },
    { "--warn-execstack", OPT_FLAG, opt_int, F(warn_execstack), LD_WARN_EXECSTACK_ALL },
    { "--warn-execstack-objects", OPT_FLAG, opt_int, F(warn_execstack), LD_WARN_EXECSTACK_OBJECTS },
    { "--no-warn-execstack", OPT_FLAG, opt_int, F(warn_execstack), LD_WARN_EXECSTACK_NONE },
    { "--error-execstack", OPT_FLAG, opt_int, F(error_execstack), 1 },
    { "--no-error-execstack", OPT_FLAG, opt_int, F(error_execstack), 0 },
    { "--warn-rwx-segments", OPT_FLAG, opt_int, F(warn_rwx_segments), 1 },
    { "--no-warn-rwx-segments", OPT_FLAG, opt_int, F(warn_rwx_segments), 0 },
    { "--error-rwx-segments", OPT_FLAG, opt_int, F(error_rwx_segments), 1 },
    { "--no-error-rwx-segments", OPT_FLAG, opt_int, F(error_rwx_segments), 0 },
    { "--trace", OPT_FLAG, opt_int, F(trace_inputs), 1 },
    { "--trace-symbol", OPT_VALUE, opt_strvec, F(trace_symbols), 0 },
    { "-Map", OPT_VALUE, opt_str, F(map_path), 0 },
    { "--reproduce", OPT_VALUE, opt_str, F(reproduce_path), 0 },
};

#undef F

/*
 * Whether argument `a` is option `o`, under any of its spellings.  If it
 * is: 1, with `as` the spelling, *val what follows it in the argument
 * (after '=' for a word, at once for a letter) or NULL if nothing does.
 */
static int opt_matches(const ld_opt_t *o, const char *a, char *as, size_t as_cap, const char **val) {
    const char *s = o->names;

    while (*s != '\0') {
        size_t n = strcspn(s, " ");

        if (strncmp(a, s, n) == 0 && n < as_cap) {
            const char *rest = a + n;
            int hit;

            if (o->kind == OPT_JOINED) {
                hit = 1;
                *val = rest[0] != '\0' ? rest : NULL;
            } else {
                hit = rest[0] == '\0' || (rest[0] == '=' && o->kind != OPT_FLAG);
                *val = rest[0] == '=' ? rest + 1 : NULL;
            }
            if (hit) {
                memcpy(as, s, n);
                as[n] = '\0';
                return 1;
            }
        }
        s += n;
        s += strspn(s, " ");
    }
    return 0;
}

/** The response files one command line may expand, as GNU's limit: past it, @a naming @a is a loop. */
#define LD_RESPONSE_FILE_LIMIT 2000

/**
 * Split the text of a response file into arguments as GNU ld does
 * (libiberty's buildargv): white space separates them; within '...' or
 * "..." it does not; a backslash takes the character after it as it is,
 * quotes included.
 *
 * @param text   the file's text, NUL-terminated
 * @param count  receives the number of arguments
 * @return       the arguments (each malloc'd, the array too), or NULL out of memory
 */
static char **split_response_text(const char *text, int *count) {
    size_t len = strlen(text);
    char **args = NULL;
    int n = 0;
    int cap = 0;
    const char *p = text;

    *count = 0;
    for (;;) {
        char *arg;
        char *o;
        int squote = 0;
        int dquote = 0;
        int bsquote = 0;

        while (*p != '\0' && isspace((unsigned char)*p)) {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        arg = malloc(len + 1);
        if (arg == NULL) {
            goto oom;
        }
        o = arg;
        for (; *p != '\0'; ++p) {
            if (isspace((unsigned char)*p) && !squote && !dquote && !bsquote) {
                break;
            }
            if (bsquote) {
                bsquote = 0;
                *o++ = *p;
            } else if (*p == '\\') {
                bsquote = 1;
            } else if (squote) {
                if (*p == '\'') {
                    squote = 0;
                } else {
                    *o++ = *p;
                }
            } else if (dquote) {
                if (*p == '"') {
                    dquote = 0;
                } else {
                    *o++ = *p;
                }
            } else if (*p == '\'') {
                squote = 1;
            } else if (*p == '"') {
                dquote = 1;
            } else {
                *o++ = *p;
            }
        }
        *o = '\0';
        if (n == cap) {
            int ncap = cap > 0 ? cap * 2 : 16;
            char **nargs = realloc(args, (size_t)ncap * sizeof(*nargs));

            if (nargs == NULL) {
                free(arg);
                goto oom;
            }
            args = nargs;
            cap = ncap;
        }
        args[n++] = arg;
    }
    if (args == NULL) {
        args = malloc(sizeof(*args));
        if (args == NULL) {
            return NULL;
        }
    }
    *count = n;
    return args;
oom:
    while (n > 0) {
        free(args[--n]);
    }
    free(args);
    return NULL;
}

/**
 * The text of response file @p path, as GNU ld reads one: NULL when it
 * cannot be opened or is a directory, and the argument stays as it was.
 *
 * @param path  the file
 * @param oom   set to 1 when it could not be held in memory
 * @return      its text, NUL-terminated (malloc'd), or NULL
 */
static char *read_response_file(const char *path, int *oom) {
    struct stat st;
    FILE *f;
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    size_t got;

    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
        return NULL;
    }
    f = fopen(path, "r");
    if (f == NULL) {
        return NULL;
    }
    do {
        if (cap - len < 4096) {
            char *nbuf = realloc(buf, cap + 8192);

            if (nbuf == NULL) {
                free(buf);
                fclose(f);
                *oom = 1;
                return NULL;
            }
            buf = nbuf;
            cap += 8192;
        }
        got = fread(buf + len, 1, cap - len - 1, f);
        len += got;
    } while (got > 0);
    fclose(f);
    buf[len] = '\0';
    return buf;
}

int ld_expand_response_files(int *argcp, char ***argvp) {
    int argc = *argcp;
    char **argv;
    int expanded = 0;
    int i;

    /* each argument copied, so that one array of them, all malloc'd, is what the caller frees */
    argv = malloc(((size_t)argc + 1) * sizeof(*argv));
    if (argv == NULL) {
        goto oom;
    }
    for (i = 0; i < argc; ++i) {
        argv[i] = strdup((*argvp)[i]);
        if (argv[i] == NULL) {
            argc = i;
            goto oom_free;
        }
    }
    argv[argc] = NULL;
    for (i = 1; i < argc;) {
        char *text;
        char **args;
        char **nargv;
        int n;
        int oom = 0;

        if (argv[i][0] != '@') {
            ++i;
            continue;
        }
        text = read_response_file(argv[i] + 1, &oom);
        if (text == NULL) {
            if (oom) {
                goto oom_free;
            }
            ++i; /* not a file: the argument as it is, an input named so */
            continue;
        }
        if (++expanded > LD_RESPONSE_FILE_LIMIT) {
            free(text);
            fprintf(stderr, "ld: error: too many @-files encountered\n");
            for (i = 0; i < argc; ++i) {
                free(argv[i]);
            }
            free(argv);
            return LD_OPT_USAGE;
        }
        args = split_response_text(text, &n);
        free(text);
        if (args == NULL) {
            goto oom_free;
        }
        nargv = realloc(argv, ((size_t)argc + (size_t)n) * sizeof(*argv));
        if (nargv == NULL) {
            while (n > 0) {
                free(args[--n]);
            }
            free(args);
            goto oom_free;
        }
        argv = nargv;
        /* the file's arguments in its place, read again: one of them may be @another */
        free(argv[i]);
        memmove(&argv[i + n], &argv[i + 1], ((size_t)argc - (size_t)i) * sizeof(*argv));
        memcpy(&argv[i], args, (size_t)n * sizeof(*argv));
        free(args);
        argc += n - 1;
    }
    *argcp = argc;
    *argvp = argv;
    return 0;
oom_free:
    for (i = 0; i < argc; ++i) {
        free(argv[i]);
    }
    free(argv);
oom:
    fprintf(stderr, "ld: out of memory reading the command line\n");
    return 1;
}

void ld_free_args(int argc, char **argv) {
    int i;

    for (i = 0; i < argc; ++i) {
        free(argv[i]);
    }
    free(argv);
}

/*
 * Read the command line into ctx.  0, or what ld is to exit with;
 * LD_OPT_DONE if an option was all there was to do.
 */
int ld_parse_options(ld_ctx_t *ctx, int argc, char **argv) {
    int i;

    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const ld_opt_t *o = NULL;
        const char *val = NULL;
        char as[40];
        size_t k;
        int pass;
        int rc;

        /* Words first, then the letters with values joined on. */
        for (pass = 0; o == NULL && pass < 2; ++pass) {
            for (k = 0; o == NULL && k < sizeof(options) / sizeof(options[0]); ++k) {
                if ((options[k].kind == OPT_JOINED) == (pass == 1) &&
                    opt_matches(&options[k], a, as, sizeof(as), &val)) {
                    o = &options[k];
                }
            }
        }

        if (o != NULL) {
            if (val == NULL && (o->kind == OPT_VALUE || o->kind == OPT_JOINED)) {
                val = i + 1 < argc ? argv[++i] : NULL;
            }
            if ((o->kind == OPT_VALUE || o->kind == OPT_JOINED) &&
                (val == NULL || (val[0] == '\0' && !(o->set == opt_str && o->n)))) {
                fprintf(stderr, "ld: %s needs a value\n", as);
                return LD_OPT_USAGE;
            }
            rc = o->set(ctx, o, as, val);
            if (rc != 0) {
                return rc;
            }
            continue;
        }

        if (a[0] == '-') {
            if (ctx->opt.compat_mode == LD_COMPAT_LLD) {
                fprintf(stderr, "ld: error: unsupported option in lld mode: %s\n", a);
                return LD_OPT_USAGE;
            }
            if (ld_warn(ctx, "unsupported option ignored (gnu mode): %s", a) != 0) {
                return LD_OPT_USAGE;
            }
            continue;
        }
        /*
         * An empty argument, or one that is just "/", is passed over
         * with a warning.  Substrate's cc has made them of a library
         * path it failed to work out, and "ld: failed to open input /"
         * told nobody where the fault was; no input is really called
         * either.
         */
        if (a[0] == '\0' || (a[0] == '/' && a[1] == '\0')) {
            (void)ld_warn(ctx, "ignoring empty/bare-slash input argv slot (probable upstream cc bug)");
            continue;
        }
        if (inputvec_push(&ctx->opt.inputs, LD_INPUT_FILE, ctx->opt.current_lib_mode,
                          ctx->opt.current_whole_archive, ctx->opt.current_as_needed, a) != 0) {
            return 1;
        }
    }
    return 0;
}
