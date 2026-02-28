#include <elfobj.h>

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} strlist_t;

typedef struct {
    const char *input_path;
    const char *output_path;
    uint16_t output_type;
    uint16_t output_machine;
    uint8_t output_osabi;
    uint8_t output_abiversion;
    uint32_t output_flags;
    uint64_t output_entry;
    int have_output_type;
    int have_output_machine;
    int have_output_osabi;
    int have_output_abiversion;
    int have_output_flags;
    int have_output_entry;
    strlist_t set_section_type_specs;
    strlist_t set_section_flags_specs;
    strlist_t set_section_align_specs;
    strlist_t rename_section_specs;
} elfedit_ctx_t;

static const char *g_progname = "elfedit";
static void ctx_cleanup(elfedit_ctx_t *ctx);

static void warnf(const char *fmt, ...) {
    va_list ap;

    fprintf(stderr, "%s: ", g_progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void usage(FILE *out) {
    fprintf(out,
            "usage: %s [-o output] [--output-type type] [--output-machine machine]\n"
            "       [--output-osabi osabi] [--output-abiversion version]\n"
            "       [--output-flags value] [--output-entry addr]\n"
            "       [--set-section-type name=type] [--set-section-flags name=flags]\n"
            "       [--set-section-align name=align] [--rename-section old=new] <input>\n",
            g_progname);
}

static char *xstrdup(const char *s) {
    size_t n;
    char *copy;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    copy = (char *)malloc(n);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, n);
    return copy;
}

static int strlist_push(strlist_t *list, const char *value) {
    char **next;
    size_t new_cap;
    char *dup;

    if (list == NULL || value == NULL) {
        return -1;
    }
    if (list->count == list->cap) {
        new_cap = list->cap == 0 ? 4 : list->cap * 2;
        next = (char **)realloc(list->items, new_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        list->items = next;
        list->cap = new_cap;
    }
    dup = xstrdup(value);
    if (dup == NULL) {
        return -1;
    }
    list->items[list->count++] = dup;
    return 0;
}

static void strlist_free(strlist_t *list) {
    size_t i;

    if (list == NULL) {
        return;
    }
    for (i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static int parse_u32(const char *text, uint32_t *out) {
    char *end = NULL;
    unsigned long v;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return -1;
    }
    errno = 0;
    v = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || v > 0xffffffffUL) {
        return -1;
    }
    *out = (uint32_t)v;
    return 0;
}

static int parse_u16(const char *text, uint16_t *out) {
    uint32_t v = 0;

    if (parse_u32(text, &v) != 0 || v > 0xffffu) {
        return -1;
    }
    *out = (uint16_t)v;
    return 0;
}

static int parse_u64(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long v;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return -1;
    }
    errno = 0;
    v = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }
    *out = (uint64_t)v;
    return 0;
}

static int parse_output_type(const char *arg, uint16_t *out) {
    if (strcasecmp(arg, "none") == 0) {
        *out = ET_NONE;
        return 0;
    }
    if (strcasecmp(arg, "rel") == 0) {
        *out = ET_REL;
        return 0;
    }
    if (strcasecmp(arg, "exec") == 0) {
        *out = ET_EXEC;
        return 0;
    }
    if (strcasecmp(arg, "dyn") == 0) {
        *out = ET_DYN;
        return 0;
    }
    if (strcasecmp(arg, "core") == 0) {
        *out = ET_CORE;
        return 0;
    }
    return parse_u16(arg, out);
}

static int parse_output_machine(const char *arg, uint16_t *out) {
    if (strcasecmp(arg, "i386") == 0) {
        *out = EM_386;
        return 0;
    }
    if (strcasecmp(arg, "x86_64") == 0 || strcasecmp(arg, "x86-64") == 0) {
        *out = EM_X86_64;
        return 0;
    }
    if (strcasecmp(arg, "arm") == 0) {
        *out = EM_ARM;
        return 0;
    }
    if (strcasecmp(arg, "aarch64") == 0) {
        *out = EM_AARCH64;
        return 0;
    }
    if (strcasecmp(arg, "mips") == 0) {
        *out = EM_MIPS;
        return 0;
    }
    if (strcasecmp(arg, "riscv") == 0) {
        *out = EM_RISCV;
        return 0;
    }
    return parse_u16(arg, out);
}

static int parse_output_osabi(const char *arg, uint8_t *out) {
    uint16_t parsed = 0;

    if (strcasecmp(arg, "none") == 0 || strcasecmp(arg, "sysv") == 0) {
        *out = ELFOSABI_SYSV;
        return 0;
    }
    if (strcasecmp(arg, "linux") == 0) {
        *out = ELFOSABI_LINUX;
        return 0;
    }
    if (strcasecmp(arg, "freebsd") == 0) {
        *out = ELFOSABI_FREEBSD;
        return 0;
    }
    if (strcasecmp(arg, "substrate") == 0) {
        *out = ELFOSABI_SUBSTRATE;
        return 0;
    }
    if (parse_u16(arg, &parsed) != 0 || parsed > 0xffu) {
        return -1;
    }
    *out = (uint8_t)parsed;
    return 0;
}

static int parse_u8(const char *text, uint8_t *out) {
    uint16_t parsed = 0;

    if (parse_u16(text, &parsed) != 0 || parsed > 0xffu) {
        return -1;
    }
    *out = (uint8_t)parsed;
    return 0;
}

static int split_assignment(const char *spec, char **left_out, char **right_out) {
    char *dup;
    char *eq;

    if (spec == NULL || left_out == NULL || right_out == NULL) {
        return -1;
    }
    dup = xstrdup(spec);
    if (dup == NULL) {
        return -1;
    }
    eq = strchr(dup, '=');
    if (eq == NULL || eq == dup || eq[1] == '\0') {
        free(dup);
        return -1;
    }
    *eq = '\0';
    *left_out = dup;
    *right_out = eq + 1;
    return 0;
}

static char *trim_ws(char *s) {
    char *end;

    if (s == NULL) {
        return NULL;
    }
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        ++s;
    }
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
        --end;
    }
    *end = '\0';
    return s;
}

static int parse_section_type(const char *arg, uint32_t *out) {
    uint32_t numeric = 0;

    if (strcasecmp(arg, "progbits") == 0) {
        *out = SHT_PROGBITS;
        return 0;
    }
    if (strcasecmp(arg, "nobits") == 0) {
        *out = SHT_NOBITS;
        return 0;
    }
    if (strcasecmp(arg, "note") == 0) {
        *out = SHT_NOTE;
        return 0;
    }
    if (strcasecmp(arg, "symtab") == 0) {
        *out = SHT_SYMTAB;
        return 0;
    }
    if (strcasecmp(arg, "strtab") == 0) {
        *out = SHT_STRTAB;
        return 0;
    }
    if (strcasecmp(arg, "rela") == 0) {
        *out = SHT_RELA;
        return 0;
    }
    if (strcasecmp(arg, "rel") == 0) {
        *out = SHT_REL;
        return 0;
    }
    if (strcasecmp(arg, "dynamic") == 0) {
        *out = SHT_DYNAMIC;
        return 0;
    }
    if (strcasecmp(arg, "hash") == 0) {
        *out = SHT_HASH;
        return 0;
    }
    if (parse_u32(arg, &numeric) != 0) {
        return -1;
    }
    *out = numeric;
    return 0;
}

static int parse_section_flags(const char *arg, uint64_t *out) {
    char *dup;
    char *tok;
    char *save = NULL;
    uint64_t flags = 0;
    uint64_t numeric = 0;

    if (parse_u64(arg, &numeric) == 0) {
        *out = numeric;
        return 0;
    }

    dup = xstrdup(arg);
    if (dup == NULL) {
        return -1;
    }
    for (tok = strtok_r(dup, ",", &save); tok != NULL; tok = strtok_r(NULL, ",", &save)) {
        tok = trim_ws(tok);
        if (strcmp(tok, "alloc") == 0) {
            flags |= SHF_ALLOC;
        } else if (strcmp(tok, "write") == 0) {
            flags |= SHF_WRITE;
        } else if (strcmp(tok, "execinstr") == 0) {
            flags |= SHF_EXECINSTR;
        } else if (strcmp(tok, "merge") == 0) {
            flags |= SHF_MERGE;
        } else if (strcmp(tok, "strings") == 0) {
            flags |= SHF_STRINGS;
        } else if (strcmp(tok, "tls") == 0) {
            flags |= SHF_TLS;
        } else if (strcmp(tok, "group") == 0) {
            flags |= SHF_GROUP;
        } else if (strcmp(tok, "compressed") == 0) {
            flags |= SHF_COMPRESSED;
        } else {
            free(dup);
            return -1;
        }
    }
    free(dup);
    *out = flags;
    return 0;
}

static elf_section_t *find_section_or_error(elfobj_t *obj, const char *name) {
    elf_section_t *sec = elf_find_section(obj, name);
    if (sec == NULL) {
        warnf("section '%s' not found", name);
    }
    return sec;
}

static int apply_section_type_specs(elfobj_t *obj, const strlist_t *specs) {
    size_t i;

    for (i = 0; i < specs->count; ++i) {
        char *lhs = NULL;
        char *rhs = NULL;
        uint32_t type;
        elf_section_t *sec;

        if (split_assignment(specs->items[i], &lhs, &rhs) != 0) {
            warnf("invalid section type assignment: %s", specs->items[i]);
            return -1;
        }
        if (parse_section_type(rhs, &type) != 0) {
            warnf("unknown section type: %s", rhs);
            free(lhs);
            return -1;
        }
        sec = find_section_or_error(obj, lhs);
        if (sec == NULL) {
            free(lhs);
            return -1;
        }
        if (elf_section_set_type(sec, type) != ELF_OK) {
            warnf("failed to set section type for '%s'", lhs);
            free(lhs);
            return -1;
        }
        free(lhs);
    }
    return 0;
}

static int apply_section_flags_specs(elfobj_t *obj, const strlist_t *specs) {
    size_t i;

    for (i = 0; i < specs->count; ++i) {
        char *lhs = NULL;
        char *rhs = NULL;
        uint64_t flags;
        elf_section_t *sec;

        if (split_assignment(specs->items[i], &lhs, &rhs) != 0) {
            warnf("invalid section flags assignment: %s", specs->items[i]);
            return -1;
        }
        if (parse_section_flags(rhs, &flags) != 0) {
            warnf("unknown section flags: %s", rhs);
            free(lhs);
            return -1;
        }
        sec = find_section_or_error(obj, lhs);
        if (sec == NULL) {
            free(lhs);
            return -1;
        }
        if (elf_section_set_flags(sec, flags) != ELF_OK) {
            warnf("failed to set section flags for '%s'", lhs);
            free(lhs);
            return -1;
        }
        free(lhs);
    }
    return 0;
}

static int apply_section_align_specs(elfobj_t *obj, const strlist_t *specs) {
    size_t i;

    for (i = 0; i < specs->count; ++i) {
        char *lhs = NULL;
        char *rhs = NULL;
        uint64_t align;
        elf_section_t *sec;

        if (split_assignment(specs->items[i], &lhs, &rhs) != 0) {
            warnf("invalid section alignment assignment: %s", specs->items[i]);
            return -1;
        }
        if (parse_u64(rhs, &align) != 0) {
            warnf("invalid section alignment: %s", rhs);
            free(lhs);
            return -1;
        }
        sec = find_section_or_error(obj, lhs);
        if (sec == NULL) {
            free(lhs);
            return -1;
        }
        if (elf_section_set_align(sec, align) != ELF_OK) {
            warnf("failed to set section alignment for '%s'", lhs);
            free(lhs);
            return -1;
        }
        free(lhs);
    }
    return 0;
}

static int apply_rename_section_specs(elfobj_t *obj, const strlist_t *specs) {
    size_t i;

    for (i = 0; i < specs->count; ++i) {
        char *lhs = NULL;
        char *rhs = NULL;
        elf_section_t *sec;

        if (split_assignment(specs->items[i], &lhs, &rhs) != 0) {
            warnf("invalid section rename assignment: %s", specs->items[i]);
            return -1;
        }
        sec = find_section_or_error(obj, lhs);
        if (sec == NULL) {
            free(lhs);
            return -1;
        }
        if (elf_section_set_name(sec, rhs) != ELF_OK) {
            warnf("failed to rename section '%s' to '%s'", lhs, rhs);
            free(lhs);
            return -1;
        }
        free(lhs);
    }
    return 0;
}

static int parse_args(int argc, char **argv, elfedit_ctx_t *ctx) {
    int ch;
    uint16_t parsed_value;
    uint8_t parsed_byte;
    static const struct option long_opts[] = {
        { "help", no_argument, NULL, 'h' },
        { "output", required_argument, NULL, 'o' },
        { "output-type", required_argument, NULL, 1000 },
        { "output-machine", required_argument, NULL, 1001 },
        { "output-osabi", required_argument, NULL, 1002 },
        { "output-abiversion", required_argument, NULL, 1003 },
        { "output-flags", required_argument, NULL, 1004 },
        { "output-entry", required_argument, NULL, 1005 },
        { "set-section-type", required_argument, NULL, 1010 },
        { "set-section-flags", required_argument, NULL, 1011 },
        { "set-section-align", required_argument, NULL, 1012 },
        { "rename-section", required_argument, NULL, 1013 },
        { NULL, 0, NULL, 0 }
    };

    memset(ctx, 0, sizeof(*ctx));

    while ((ch = getopt_long(argc, argv, "ho:", long_opts, NULL)) != -1) {
        switch (ch) {
        case 'h':
            usage(stdout);
            exit(0);
        case 'o':
            ctx->output_path = optarg;
            break;
        case 1000:
            if (parse_output_type(optarg, &parsed_value) != 0) {
                warnf("unknown type: %s", optarg);
                return -1;
            }
            ctx->have_output_type = 1;
            ctx->output_type = parsed_value;
            break;
        case 1001:
            if (parse_output_machine(optarg, &parsed_value) != 0) {
                warnf("unknown machine: %s", optarg);
                return -1;
            }
            ctx->have_output_machine = 1;
            ctx->output_machine = parsed_value;
            break;
        case 1002:
            if (parse_output_osabi(optarg, &parsed_byte) != 0) {
                warnf("unknown osabi: %s", optarg);
                return -1;
            }
            ctx->have_output_osabi = 1;
            ctx->output_osabi = parsed_byte;
            break;
        case 1003:
            if (parse_u8(optarg, &parsed_byte) != 0) {
                warnf("invalid abiversion: %s", optarg);
                return -1;
            }
            ctx->have_output_abiversion = 1;
            ctx->output_abiversion = parsed_byte;
            break;
        case 1004:
            if (parse_u32(optarg, &ctx->output_flags) != 0) {
                warnf("invalid flags value: %s", optarg);
                return -1;
            }
            ctx->have_output_flags = 1;
            break;
        case 1005:
            if (parse_u64(optarg, &ctx->output_entry) != 0) {
                warnf("invalid entry address: %s", optarg);
                return -1;
            }
            ctx->have_output_entry = 1;
            break;
        case 1010:
            if (strlist_push(&ctx->set_section_type_specs, optarg) != 0) {
                warnf("out of memory");
                return -1;
            }
            break;
        case 1011:
            if (strlist_push(&ctx->set_section_flags_specs, optarg) != 0) {
                warnf("out of memory");
                return -1;
            }
            break;
        case 1012:
            if (strlist_push(&ctx->set_section_align_specs, optarg) != 0) {
                warnf("out of memory");
                return -1;
            }
            break;
        case 1013:
            if (strlist_push(&ctx->rename_section_specs, optarg) != 0) {
                warnf("out of memory");
                return -1;
            }
            break;
        default:
            usage(stderr);
            return -1;
        }
    }

    if (optind >= argc) {
        usage(stderr);
        return -1;
    }
    ctx->input_path = argv[optind++];
    if (optind != argc) {
        usage(stderr);
        return -1;
    }
    return 0;
}

static int paths_same_file(const char *a, const char *b) {
    struct stat sa;
    struct stat sb;

    if (a == NULL || b == NULL) {
        return 0;
    }
    if (strcmp(a, b) == 0) {
        return 1;
    }
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0) {
        return 0;
    }
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static int mktemp_for_target(const char *target_path, char *out, size_t out_size) {
    int fd;
    size_t n;
    const char suffix[] = ".elfedit.tmp.XXXXXX";

    n = strlen(target_path) + sizeof(suffix);
    if (n > out_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    snprintf(out, out_size, "%s%s", target_path, suffix);
    fd = mkstemp(out);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    return 0;
}

static int apply_mutations(elfedit_ctx_t *ctx, elfobj_t *obj) {
    elf_err_t err;

    if (ctx->have_output_type) {
        uint16_t old_type = elf_type(obj);
        uint16_t phnum = elf_program_header_count(obj);

        if (old_type != ctx->output_type && ctx->output_type == ET_REL && phnum != 0) {
            warnf("warning: changing type to ET_REL with %u program headers may be structurally inconsistent",
                  (unsigned)phnum);
        }

        err = elf_set_type(obj, ctx->output_type);
        if (err != ELF_OK) {
            warnf("failed to set ELF type: %s", elf_errstr(err));
            return -1;
        }
    }

    if (ctx->have_output_machine) {
        uint16_t old_machine = elf_machine(obj);

        if (old_machine != ctx->output_machine) {
            warnf("warning: changing machine does not re-encode instructions or relocations");
        }

        err = elf_set_machine(obj, ctx->output_machine);
        if (err != ELF_OK) {
            warnf("failed to set ELF machine: %s", elf_errstr(err));
            return -1;
        }
    }

    if (ctx->have_output_osabi) {
        err = elf_set_osabi(obj, ctx->output_osabi);
        if (err != ELF_OK) {
            warnf("failed to set ELF OSABI: %s", elf_errstr(err));
            return -1;
        }
    }

    if (ctx->have_output_abiversion) {
        err = elf_set_abiversion(obj, ctx->output_abiversion);
        if (err != ELF_OK) {
            warnf("failed to set ELF ABI version: %s", elf_errstr(err));
            return -1;
        }
    }

    if (ctx->have_output_flags) {
        err = elf_set_flags(obj, ctx->output_flags);
        if (err != ELF_OK) {
            warnf("failed to set ELF flags: %s", elf_errstr(err));
            return -1;
        }
    }

    if (ctx->have_output_entry) {
        err = elf_set_entry(obj, ctx->output_entry);
        if (err != ELF_OK) {
            warnf("failed to set ELF entry: %s", elf_errstr(err));
            return -1;
        }
    }

    if (apply_section_type_specs(obj, &ctx->set_section_type_specs) != 0) {
        return -1;
    }
    if (apply_section_flags_specs(obj, &ctx->set_section_flags_specs) != 0) {
        return -1;
    }
    if (apply_section_align_specs(obj, &ctx->set_section_align_specs) != 0) {
        return -1;
    }
    if (apply_rename_section_specs(obj, &ctx->rename_section_specs) != 0) {
        return -1;
    }

    return 0;
}

static int validate_object(elfobj_t *obj) {
    elf_err_t err;
    char *diag = NULL;

    err = elf_validate(obj, &diag);
    if (err != ELF_OK) {
        const char *last = elf_last_diagnostics(obj);
        warnf("validation failed: %s",
              (diag != NULL && diag[0] != '\0') ? diag : (last != NULL ? last : elf_errstr(err)));
        free(diag);
        return -1;
    }
    free(diag);
    return 0;
}

static int write_output(const elfedit_ctx_t *ctx, elfobj_t *obj) {
    const char *target = ctx->output_path != NULL ? ctx->output_path : ctx->input_path;
    int in_place = paths_same_file(target, ctx->input_path);
    char tmp_path[PATH_MAX];

    if (!in_place) {
        if (elf_write_file(obj, target) != ELF_OK) {
            warnf("%s: write failed: %s", target, elf_errstr(elf_last_error(obj)));
            return -1;
        }
        return 0;
    }

    if (mktemp_for_target(target, tmp_path, sizeof(tmp_path)) != 0) {
        warnf("%s: failed to create temporary output: %s", target, strerror(errno));
        return -1;
    }

    if (elf_write_file(obj, tmp_path) != ELF_OK) {
        warnf("%s: write failed: %s", tmp_path, elf_errstr(elf_last_error(obj)));
        unlink(tmp_path);
        return -1;
    }
    if (rename(tmp_path, target) != 0) {
        warnf("%s: failed to replace original: %s", target, strerror(errno));
        unlink(tmp_path);
        return -1;
    }
    return 0;
}

static void ctx_cleanup(elfedit_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    strlist_free(&ctx->set_section_type_specs);
    strlist_free(&ctx->set_section_flags_specs);
    strlist_free(&ctx->set_section_align_specs);
    strlist_free(&ctx->rename_section_specs);
}

int main(int argc, char **argv) {
    elfedit_ctx_t ctx;
    elfobj_t *obj = NULL;
    int rc = 1;

    if (argv[0] != NULL && argv[0][0] != '\0') {
        const char *slash = strrchr(argv[0], '/');
        g_progname = slash != NULL ? slash + 1 : argv[0];
    }

    if (parse_args(argc, argv, &ctx) != 0) {
        ctx_cleanup(&ctx);
        return 1;
    }

    if (elf_open(ctx.input_path, &obj) != ELF_OK) {
        warnf("%s: failed to open ELF object", ctx.input_path);
        goto out;
    }

    if (apply_mutations(&ctx, obj) != 0) {
        goto out;
    }
    if (validate_object(obj) != 0) {
        goto out;
    }
    if (write_output(&ctx, obj) != 0) {
        goto out;
    }

    rc = 0;

out:
    if (obj != NULL) {
        elf_close(obj);
    }
    ctx_cleanup(&ctx);
    return rc;
}
