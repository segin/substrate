#include "elfobj.h"

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
    int input_mmap;
    int verbose;
    uint16_t output_type;
    uint16_t output_machine;
    uint8_t output_osabi;
    uint8_t output_abiversion;
    uint32_t output_flags;
    uint64_t output_entry;
    int force;
    int dry_run;
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
    strlist_t set_segment_type_specs;
    strlist_t set_segment_flags_specs;
    strlist_t set_segment_align_specs;
} elfedit_ctx_t;

static const char *g_progname = "elfedit";
static int g_verbose = 0;
static void ctx_cleanup(elfedit_ctx_t *ctx);
#define ELFEDIT_VERSION "0.1.0"

static void warnf(const char *fmt, ...) {
    va_list ap;

    fprintf(stderr, "%s: ", g_progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void verbf(const char *fmt, ...) {
    va_list ap;

    if (!g_verbose) {
        return;
    }
    fprintf(stderr, "%s: ", g_progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void usage(FILE *out) {
    fprintf(out,
            "usage: %s [-f] [-n] [-v] [--input-mmap] [-o output]\n"
            "       [--output-type type] [--output-machine machine]\n"
            "       [--output-osabi osabi] [--output-abiversion version]\n"
            "       [--output-flags value] [--output-entry addr]\n"
            "       [--set-section-type name=type] [--set-section-flags name=flags]\n"
            "       [--set-section-align name=align] [--rename-section old=new]\n"
            "       [--set-segment-type idx=type] [--set-segment-flags idx=flags]\n"
            "       [--set-segment-align idx=align] <input>\n",
            g_progname);
}

static void print_version(void) {
    printf("%s %s\n", g_progname, ELFEDIT_VERSION);
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

static int parse_segment_type(const char *arg, uint32_t *out) {
    uint32_t numeric = 0;

    if (strcasecmp(arg, "null") == 0) {
        *out = PT_NULL;
        return 0;
    }
    if (strcasecmp(arg, "load") == 0) {
        *out = PT_LOAD;
        return 0;
    }
    if (strcasecmp(arg, "dynamic") == 0) {
        *out = PT_DYNAMIC;
        return 0;
    }
    if (strcasecmp(arg, "interp") == 0) {
        *out = PT_INTERP;
        return 0;
    }
    if (strcasecmp(arg, "note") == 0) {
        *out = PT_NOTE;
        return 0;
    }
    if (strcasecmp(arg, "phdr") == 0) {
        *out = PT_PHDR;
        return 0;
    }
    if (strcasecmp(arg, "tls") == 0) {
        *out = PT_TLS;
        return 0;
    }
    if (parse_u32(arg, &numeric) != 0) {
        return -1;
    }
    *out = numeric;
    return 0;
}

static int parse_segment_flags(const char *arg, uint32_t *out) {
    uint32_t numeric = 0;
    uint32_t flags = 0;
    size_t i;

    if (parse_u32(arg, &numeric) == 0) {
        *out = numeric;
        return 0;
    }
    if (arg == NULL || arg[0] == '\0') {
        return -1;
    }
    for (i = 0; arg[i] != '\0'; ++i) {
        char ch = arg[i];
        if (ch == 'r' || ch == 'R') {
            flags |= 0x4;
        } else if (ch == 'w' || ch == 'W') {
            flags |= 0x2;
        } else if (ch == 'x' || ch == 'X') {
            flags |= 0x1;
        } else {
            return -1;
        }
    }
    *out = flags;
    return 0;
}

static void warn_flags_conflict(uint16_t machine, uint32_t flags) {
    if ((machine == EM_386 || machine == EM_X86_64) && flags != 0) {
        warnf("warning: e_flags=0x%x may conflict with machine ABI", flags);
    }
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
        verbf("set section '%s' type -> %u", lhs, (unsigned)type);
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
        verbf("set section '%s' flags -> 0x%llx", lhs, (unsigned long long)flags);
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
        verbf("set section '%s' align -> %llu", lhs, (unsigned long long)align);
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
        verbf("rename section '%s' -> '%s'", lhs, rhs);
        if (elf_section_set_name(sec, rhs) != ELF_OK) {
            warnf("failed to rename section '%s' to '%s'", lhs, rhs);
            free(lhs);
            return -1;
        }
        free(lhs);
    }
    return 0;
}

static int parse_segment_index(const char *text, size_t *out) {
    uint64_t value;

    if (parse_u64(text, &value) != 0 || value > (uint64_t)SIZE_MAX) {
        return -1;
    }
    *out = (size_t)value;
    return 0;
}

static int segment_index_in_range(elfobj_t *obj, size_t idx) {
    size_t count = (size_t)elf_program_header_count(obj);
    if (idx < count) {
        return 1;
    }
    if (count == 0) {
        warnf("segment index %lu out of range (0-0)", (unsigned long)idx);
    } else {
        warnf("segment index %lu out of range (0-%lu)",
              (unsigned long)idx, (unsigned long)(count - 1));
    }
    return 0;
}

static int apply_segment_type_specs(elfobj_t *obj, const strlist_t *specs) {
    size_t i;

    for (i = 0; i < specs->count; ++i) {
        char *lhs = NULL;
        char *rhs = NULL;
        size_t idx;
        uint32_t type;

        if (split_assignment(specs->items[i], &lhs, &rhs) != 0) {
            warnf("invalid segment type assignment: %s", specs->items[i]);
            return -1;
        }
        if (parse_segment_index(lhs, &idx) != 0) {
            warnf("invalid segment index: %s", lhs);
            free(lhs);
            return -1;
        }
        if (!segment_index_in_range(obj, idx)) {
            free(lhs);
            return -1;
        }
        if (parse_segment_type(rhs, &type) != 0) {
            warnf("unknown segment type: %s", rhs);
            free(lhs);
            return -1;
        }
        verbf("set segment[%lu] type -> %u", (unsigned long)idx, (unsigned)type);
        if (elf_program_header_set_type(obj, idx, type) != ELF_OK) {
            warnf("failed to set segment type for index %lu", (unsigned long)idx);
            free(lhs);
            return -1;
        }
        free(lhs);
    }
    return 0;
}

static int apply_segment_flags_specs(elfobj_t *obj, const strlist_t *specs) {
    size_t i;

    for (i = 0; i < specs->count; ++i) {
        char *lhs = NULL;
        char *rhs = NULL;
        size_t idx;
        uint32_t flags;

        if (split_assignment(specs->items[i], &lhs, &rhs) != 0) {
            warnf("invalid segment flags assignment: %s", specs->items[i]);
            return -1;
        }
        if (parse_segment_index(lhs, &idx) != 0) {
            warnf("invalid segment index: %s", lhs);
            free(lhs);
            return -1;
        }
        if (!segment_index_in_range(obj, idx)) {
            free(lhs);
            return -1;
        }
        if (parse_segment_flags(rhs, &flags) != 0) {
            warnf("invalid segment flags: %s", rhs);
            free(lhs);
            return -1;
        }
        verbf("set segment[%lu] flags -> 0x%x", (unsigned long)idx, (unsigned)flags);
        if (elf_program_header_set_flags(obj, idx, flags) != ELF_OK) {
            warnf("failed to set segment flags for index %lu", (unsigned long)idx);
            free(lhs);
            return -1;
        }
        free(lhs);
    }
    return 0;
}

static int apply_segment_align_specs(elfobj_t *obj, const strlist_t *specs) {
    size_t i;

    for (i = 0; i < specs->count; ++i) {
        char *lhs = NULL;
        char *rhs = NULL;
        size_t idx;
        uint64_t align;

        if (split_assignment(specs->items[i], &lhs, &rhs) != 0) {
            warnf("invalid segment align assignment: %s", specs->items[i]);
            return -1;
        }
        if (parse_segment_index(lhs, &idx) != 0) {
            warnf("invalid segment index: %s", lhs);
            free(lhs);
            return -1;
        }
        if (!segment_index_in_range(obj, idx)) {
            free(lhs);
            return -1;
        }
        if (parse_u64(rhs, &align) != 0) {
            warnf("invalid segment alignment: %s", rhs);
            free(lhs);
            return -1;
        }
        verbf("set segment[%lu] align -> %llu", (unsigned long)idx, (unsigned long long)align);
        if (elf_program_header_set_align(obj, idx, align) != ELF_OK) {
            warnf("failed to set segment alignment for index %lu", (unsigned long)idx);
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
        { "force", no_argument, NULL, 'f' },
        { "dry-run", no_argument, NULL, 'n' },
        { "verbose", no_argument, NULL, 'v' },
        { "version", no_argument, NULL, 'V' },
        { "help", no_argument, NULL, 'h' },
        { "input-mmap", no_argument, NULL, 2000 },
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
        { "set-segment-type", required_argument, NULL, 1020 },
        { "set-segment-flags", required_argument, NULL, 1021 },
        { "set-segment-align", required_argument, NULL, 1022 },
        { NULL, 0, NULL, 0 }
    };

    memset(ctx, 0, sizeof(*ctx));

    while ((ch = getopt_long(argc, argv, "fhnvVo:", long_opts, NULL)) != -1) {
        switch (ch) {
        case 'f':
            ctx->force = 1;
            break;
        case 'n':
            ctx->dry_run = 1;
            break;
        case 'v':
            ctx->verbose = 1;
            break;
        case 'V':
            print_version();
            exit(0);
        case 'h':
            usage(stdout);
            exit(0);
        case 2000:
            ctx->input_mmap = 1;
            break;
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
        case 1020:
            if (strlist_push(&ctx->set_segment_type_specs, optarg) != 0) {
                warnf("out of memory");
                return -1;
            }
            break;
        case 1021:
            if (strlist_push(&ctx->set_segment_flags_specs, optarg) != 0) {
                warnf("out of memory");
                return -1;
            }
            break;
        case 1022:
            if (strlist_push(&ctx->set_segment_align_specs, optarg) != 0) {
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

        verbf("set e_type: %u -> %u", (unsigned)old_type, (unsigned)ctx->output_type);
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

        verbf("set e_machine: %u -> %u", (unsigned)old_machine, (unsigned)ctx->output_machine);
        err = elf_set_machine(obj, ctx->output_machine);
        if (err != ELF_OK) {
            warnf("failed to set ELF machine: %s", elf_errstr(err));
            return -1;
        }
    }

    if (ctx->have_output_osabi) {
        verbf("set EI_OSABI: %u -> %u", (unsigned)elf_osabi(obj), (unsigned)ctx->output_osabi);
        err = elf_set_osabi(obj, ctx->output_osabi);
        if (err != ELF_OK) {
            warnf("failed to set ELF OSABI: %s", elf_errstr(err));
            return -1;
        }
    }

    if (ctx->have_output_abiversion) {
        verbf("set EI_ABIVERSION: %u -> %u", (unsigned)elf_abiversion(obj),
              (unsigned)ctx->output_abiversion);
        err = elf_set_abiversion(obj, ctx->output_abiversion);
        if (err != ELF_OK) {
            warnf("failed to set ELF ABI version: %s", elf_errstr(err));
            return -1;
        }
    }

    if (ctx->have_output_flags) {
        uint16_t machine = ctx->have_output_machine ? ctx->output_machine : elf_machine(obj);
        warn_flags_conflict(machine, ctx->output_flags);
        verbf("set e_flags: 0x%x -> 0x%x", (unsigned)elf_flags(obj), (unsigned)ctx->output_flags);
        err = elf_set_flags(obj, ctx->output_flags);
        if (err != ELF_OK) {
            warnf("failed to set ELF flags: %s", elf_errstr(err));
            return -1;
        }
    }

    if (ctx->have_output_entry) {
        verbf("set e_entry: 0x%llx -> 0x%llx", (unsigned long long)elf_entry(obj),
              (unsigned long long)ctx->output_entry);
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

    if (ctx->set_segment_type_specs.count != 0 || ctx->set_segment_flags_specs.count != 0 ||
        ctx->set_segment_align_specs.count != 0) {
        warnf("note: segment content is not modified; segment edits are metadata-only");
    }
    if (apply_segment_type_specs(obj, &ctx->set_segment_type_specs) != 0) {
        return -1;
    }
    if (apply_segment_flags_specs(obj, &ctx->set_segment_flags_specs) != 0) {
        return -1;
    }
    if (apply_segment_align_specs(obj, &ctx->set_segment_align_specs) != 0) {
        return -1;
    }

    return 0;
}

static void print_validation_diagnostics(elfobj_t *obj) {
    const char *diag = elf_last_diagnostics(obj);
    const char *line_start;
    const char *p;

    if (diag == NULL || diag[0] == '\0') {
        return;
    }
    line_start = diag;
    p = diag;
    while (*p != '\0') {
        if (*p == '\n') {
            if (p > line_start) {
                fprintf(stderr, "%s: %.*s\n", g_progname, (int)(p - line_start), line_start);
            }
            line_start = p + 1;
        }
        ++p;
    }
    if (*line_start != '\0') {
        fprintf(stderr, "%s: %s\n", g_progname, line_start);
    }
}

static int run_strict_validation(elfobj_t *obj) {
    elf_err_t err;

    (void)elf_set_validation_mode(obj, ELF_VALIDATE_STRICT);
    err = elf_validate(obj, NULL);
    print_validation_diagnostics(obj);
    return err == ELF_OK ? 0 : -1;
}

static size_t requested_edit_count(const elfedit_ctx_t *ctx) {
    size_t n = 0;

    n += ctx->have_output_type ? 1u : 0u;
    n += ctx->have_output_machine ? 1u : 0u;
    n += ctx->have_output_osabi ? 1u : 0u;
    n += ctx->have_output_abiversion ? 1u : 0u;
    n += ctx->have_output_flags ? 1u : 0u;
    n += ctx->have_output_entry ? 1u : 0u;
    n += ctx->set_section_type_specs.count;
    n += ctx->set_section_flags_specs.count;
    n += ctx->set_section_align_specs.count;
    n += ctx->rename_section_specs.count;
    n += ctx->set_segment_type_specs.count;
    n += ctx->set_segment_flags_specs.count;
    n += ctx->set_segment_align_specs.count;
    return n;
}

static void print_dry_run_summary(const elfedit_ctx_t *ctx, int valid) {
    printf("dry-run: %s\n", valid ? "validation passed" : "validation failed");
    printf("dry-run: %lu edit(s) would be applied\n", (unsigned long)requested_edit_count(ctx));
}

static int write_output(const elfedit_ctx_t *ctx, elfobj_t *obj) {
    const char *target = ctx->output_path != NULL ? ctx->output_path : ctx->input_path;
    int in_place = paths_same_file(target, ctx->input_path);
    int have_target_stat = 0;
    struct stat st_target;
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
    if (stat(target, &st_target) == 0) {
        have_target_stat = 1;
    }

    if (elf_write_file(obj, tmp_path) != ELF_OK) {
        warnf("%s: write failed: %s", tmp_path, elf_errstr(elf_last_error(obj)));
        unlink(tmp_path);
        return -1;
    }
    if (have_target_stat) {
        if (chmod(tmp_path, st_target.st_mode & 07777) != 0) {
            warnf("%s: failed to preserve mode bits: %s", tmp_path, strerror(errno));
            unlink(tmp_path);
            return -1;
        }
        if (chown(tmp_path, st_target.st_uid, st_target.st_gid) != 0 && errno != EPERM) {
            warnf("%s: failed to preserve ownership: %s", tmp_path, strerror(errno));
            unlink(tmp_path);
            return -1;
        }
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
    strlist_free(&ctx->set_segment_type_specs);
    strlist_free(&ctx->set_segment_flags_specs);
    strlist_free(&ctx->set_segment_align_specs);
}

int main(int argc, char **argv) {
    elfedit_ctx_t ctx;
    elfobj_t *obj = NULL;
    elf_err_t open_err;
    int valid = 0;
    int rc = 1;

    if (argv[0] != NULL && argv[0][0] != '\0') {
        const char *slash = strrchr(argv[0], '/');
        g_progname = slash != NULL ? slash + 1 : argv[0];
    }

    if (parse_args(argc, argv, &ctx) != 0) {
        ctx_cleanup(&ctx);
        return 1;
    }
    g_verbose = ctx.verbose;

    open_err = ctx.input_mmap ? elf_open_with_options(ctx.input_path, ELFOBJ_OPEN_USE_MMAP, &obj)
                              : elf_open(ctx.input_path, &obj);
    if (open_err != ELF_OK) {
        if (open_err == ELF_ERR_FORMAT) {
            fprintf(stderr, "%s: %s: not an ELF file\n", g_progname, ctx.input_path);
        } else {
            warnf("%s: failed to open ELF object", ctx.input_path);
        }
        goto out;
    }
    if (ctx.input_mmap) {
        verbf("input mmap requested: %s", elf_uses_mmap(obj) ? "active" : "not active");
    }

    if (!ctx.force && elf_type(obj) == ET_CORE) {
        warnf("refusing to edit core files without --force");
        goto out;
    }
    if (requested_edit_count(&ctx) == 0) {
        warnf("no edits requested");
        rc = 0;
        goto out;
    }

    if (apply_mutations(&ctx, obj) != 0) {
        goto out;
    }
    valid = run_strict_validation(obj) == 0;

    if (ctx.dry_run) {
        print_dry_run_summary(&ctx, valid);
        rc = valid ? 0 : 1;
        goto out;
    }

    if (!valid) {
        if (!ctx.force) {
            goto out;
        }
        fprintf(stderr, "WARNING: writing structurally invalid ELF\n");
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
