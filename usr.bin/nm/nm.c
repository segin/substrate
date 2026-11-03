/*
 * nm - list symbols from object files
 *
 * Displays the symbol table of each ELF object file given on the command
 * line.  Supports BSD, POSIX, and SysV output formats, multiple sort and
 * filter modes, C++/Rust/D demangling, archive handling, and dynamic
 * symbol tables.
 */

#include <elfobj.h>
#include <demangle.h>
#include <elf.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NM_VERSION "1.0.0"
#define AR_MAGIC "!<arch>\n"
#define AR_THIN_MAGIC "!<thin>\n"
#define AR_MAGIC_LEN 8
#define AR_FMAG "`\n"
#define AR_HDR_LEN 60

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

struct ar_raw_header {
    char name[16];
    char mtime[12];
    char uid[6];
    char gid[6];
    char mode[8];
    char size[10];
    char fmag[2];
};

typedef struct {
    char *name;
    uint64_t value;
    uint64_t sz;
    char type_char;
    uint8_t bind;
    uint8_t stype;
    uint8_t visibility;
    uint32_t shndx;
    char *sec_name;
    size_t orig_index;
} nm_sym_t;

typedef struct {
    nm_sym_t *syms;
    size_t count;
    size_t cap;
} nm_symtab_t;

enum nm_format  { FMT_BSD = 0, FMT_POSIX = 1, FMT_SYSV = 2 };
enum nm_sort    { SORT_ALPHA = 0, SORT_NUMERIC = 1, SORT_NONE = 2, SORT_SIZE = 3 };

/* ------------------------------------------------------------------ */
/* Options                                                             */
/* ------------------------------------------------------------------ */

static struct {
    enum nm_format format;
    enum nm_sort   sort_mode;
    int reverse;
    int undefined_only;
    int extern_only;
    int debug_syms;
    int defined_only;
    int no_weak;
    int print_file;
    int print_size;
    int dynamic;
    int do_demangle;
    int radix;
    int special_syms;
    int print_armap;
    int print_line;
} opts;

static const char *progname = "nm";

/* ------------------------------------------------------------------ */
/* Utility helpers                                                     */
/* ------------------------------------------------------------------ */

static void usage(FILE *out)
{
    fprintf(out,
        "usage: %s [-aABCDglnoPprSsuVv] [-f {bsd,sysv,posix}] [-t {d,o,x}]\n"
        "       [--format={bsd,sysv,posix}] [--radix={d,o,x}] [--line-numbers]\n"
        "       [--demangle] [--defined-only] [--no-weak]\n"
        "       [--extern-only] [--undefined-only] [--debug-syms]\n"
        "       [--numeric-sort] [--no-sort] [--reverse-sort] [--size-sort]\n"
        "       [--print-file-name] [--print-size] [--dynamic]\n"
        "       [--print-armap] [--special-syms] [-h|--help] [-V|--version]\n"
        "       [--] <file>...\n",
        progname);
}

static char *xstrdup(const char *s)
{
    size_t n;
    char *out;

    if (s == NULL)
        return NULL;
    n = strlen(s) + 1;
    out = malloc(n);
    if (out == NULL)
        return NULL;
    memcpy(out, s, n);
    return out;
}

static void nm_symtab_init(nm_symtab_t *tab)
{
    tab->syms = NULL;
    tab->count = 0;
    tab->cap = 0;
}

static void nm_symtab_free(nm_symtab_t *tab)
{
    size_t i;

    for (i = 0; i < tab->count; i++) {
        free(tab->syms[i].name);
        free(tab->syms[i].sec_name);
    }
    free(tab->syms);
    tab->syms = NULL;
    tab->count = 0;
    tab->cap = 0;
}

static int nm_symtab_push(nm_symtab_t *tab, const nm_sym_t *sym)
{
    if (tab->count == tab->cap) {
        size_t newcap = tab->cap == 0 ? 64 : tab->cap * 2;
        nm_sym_t *next = realloc(tab->syms, newcap * sizeof(*next));
        if (next == NULL)
            return -1;
        tab->syms = next;
        tab->cap = newcap;
    }
    tab->syms[tab->count++] = *sym;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Endian-aware read helpers (for manual dynsym parsing)               */
/* ------------------------------------------------------------------ */

static uint16_t rd16(const uint8_t *p, int be)
{
    if (be)
        return ((uint16_t)p[0] << 8) | p[1];
    return p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p, int be)
{
    if (be)
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | p[3];
    return p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p, int be)
{
    if (be)
        return ((uint64_t)rd32(p, 1) << 32) | rd32(p + 4, 1);
    return rd32(p, 0) | ((uint64_t)rd32(p + 4, 0) << 32);
}

/* ------------------------------------------------------------------ */
/* Symbol type classification                                          */
/* ------------------------------------------------------------------ */

static char classify_symbol(elfobj_t *obj, uint32_t shndx, uint8_t bind,
                            uint8_t stype, uint64_t value, uint64_t sz,
                            const char **sec_name_out)
{
    elf_section_t *sec;
    uint32_t sh_type;
    uint64_t sh_flags;
    const char *sname;
    char base;

    (void)value;
    (void)sz;

    if (sec_name_out)
        *sec_name_out = NULL;

    /* Undefined */
    if (shndx == SHN_UNDEF) {
        if (sec_name_out)
            *sec_name_out = "*UND*";
        if (bind == STB_WEAK)
            return 'w';
        return 'U';
    }

    /* Absolute */
    if (shndx == SHN_ABS) {
        if (sec_name_out)
            *sec_name_out = "*ABS*";
        return (bind == STB_LOCAL) ? 'a' : 'A';
    }

    /* Common */
    if (shndx == SHN_COMMON) {
        if (sec_name_out)
            *sec_name_out = "*COM*";
        return (bind == STB_LOCAL) ? 'c' : 'C';
    }

    /*
     * GNU extensions, checked in the order GNU nm checks them: an
     * IFUNC is 'i' whatever its binding (local, global or weak), and a
     * unique global is 'u' whatever section it lives in.
     */
    if (stype == STT_GNU_IFUNC || bind == STB_GNU_UNIQUE) {
        sec = elf_section_get(obj, shndx);
        if (sec && sec_name_out) {
            sname = elf_section_name(sec);
            *sec_name_out = sname ? sname : "";
        }
        return (stype == STT_GNU_IFUNC) ? 'i' : 'u';
    }

    /* Weak defined symbols get W/V regardless of section */
    if (bind == STB_WEAK) {
        sec = elf_section_get(obj, shndx);
        if (sec && sec_name_out) {
            sname = elf_section_name(sec);
            *sec_name_out = sname ? sname : "";
        }
        return (stype == STT_OBJECT) ? 'V' : 'W';
    }

    /* Look up the section for defined symbols */
    sec = elf_section_get(obj, shndx);
    if (sec == NULL) {
        if (sec_name_out)
            *sec_name_out = "?";
        return '?';
    }

    sname = elf_section_name(sec);
    if (sec_name_out)
        *sec_name_out = sname ? sname : "";

    sh_type = elf_section_type(sec);
    sh_flags = elf_section_flags(sec);

    /* Debug / non-alloc sections */
    if (elf_section_is_debug(sec)) {
        return 'N';
    }

    if ((sh_flags & SHF_ALLOC) == 0) {
        return (bind == STB_LOCAL) ? 'n' : 'N';
    }

    /* Text (executable) */
    if (sh_flags & SHF_EXECINSTR) {
        base = 'T';
        goto apply_case;
    }

    /* BSS (uninitialized, writable) */
    if (sh_type == SHT_NOBITS && (sh_flags & SHF_WRITE)) {
        base = 'B';
        goto apply_case;
    }

    /* Data (initialized, writable) */
    if (sh_flags & SHF_WRITE) {
        base = 'D';
        goto apply_case;
    }

    /* Read-only data */
    base = 'R';

apply_case:
    return (bind == STB_LOCAL) ? (char)tolower((unsigned char)base) : base;
}

/* ------------------------------------------------------------------ */
/* Filter                                                              */
/* ------------------------------------------------------------------ */

/**
 * @brief Tell whether a name is an assembler-local label.
 *
 * Matches the names GNU BFD treats as local labels in ELF objects:
 * ".L" and ".." prefixes, gcc's "_.L_" prefix, and the "L<digit>"
 * names gas gives dollar and forward/backward labels, which carry a
 * \\001 or \\002 byte.
 *
 * @param name Symbol name; never NULL.
 * @return 1 if the name is a local label, 0 otherwise.
 */
static int is_local_label_name(const char *name)
{
    if (name[0] == '.' && (name[1] == 'L' || name[1] == '.'))
        return 1;
    if (strncmp(name, "_.L_", 4) == 0)
        return 1;
    if (name[0] == 'L' && isdigit((unsigned char)name[1]))
        return strchr(name, '\001') != NULL || strchr(name, '\002') != NULL;
    return 0;
}

/**
 * @brief Tell whether a symbol is a target-specific special symbol.
 *
 * These are the symbols GNU nm hides unless --special-syms is given:
 * ARM mapping and tag symbols ("$" followed by a lower-case letter,
 * optionally followed by "." and a suffix, e.g. "$a", "$t", "$d.1"),
 * AArch64 mapping symbols ("$x", "$d", optionally with a "." suffix),
 * and on RISC-V the mapping symbols ("$d", "$x", "$xrv..."), local
 * labels and symbols with empty names.  No other machine has any.
 *
 * @param machine e_machine of the object the symbol came from.
 * @param name    Symbol name; may be NULL.
 * @return 1 if the symbol is special for @p machine, 0 otherwise.
 */
static int is_target_special_symbol(uint16_t machine, const char *name)
{
    if (name == NULL)
        name = "";

    switch (machine) {
    case EM_ARM:
        return name[0] == '$' && name[1] >= 'a' && name[1] <= 'z' &&
               (name[2] == '\0' || name[2] == '.');
    case EM_AARCH64:
        return name[0] == '$' && (name[1] == 'd' || name[1] == 'x') &&
               (name[2] == '\0' || name[2] == '.');
    case EM_RISCV:
        return name[0] == '\0' || is_local_label_name(name) ||
               strcmp(name, "$d") == 0 || strcmp(name, "$x") == 0 ||
               strncmp(name, "$xrv", 4) == 0;
    default:
        return 0;
    }
}

/**
 * @brief Decide whether a collected symbol is printed.
 *
 * @param sym     The symbol.
 * @param machine e_machine of the object it came from, for the
 *                target-specific special symbols --special-syms shows.
 * @return 1 to print the symbol, 0 to drop it.
 */
static int filter_symbol(const nm_sym_t *sym, uint16_t machine)
{
    const char *special_name;

    /* Skip the null symbol (index 0, empty name, SHN_UNDEF) */
    if (sym->orig_index == 0 && sym->shndx == SHN_UNDEF &&
        (sym->name == NULL || sym->name[0] == '\0'))
        return 0;

    /*
     * Mapping symbols and the like are hidden unless --special-syms
     * asks for them; -a does not show them, as with GNU nm.  A section
     * symbol has no name of its own in the string table, but BFD names
     * it after its section, and that is the name GNU tests: on RISC-V,
     * where an empty name is special, ".text" and ".debug_info" are not.
     */
    special_name = sym->name;
    if (sym->stype == STT_SECTION &&
        (special_name == NULL || special_name[0] == '\0') && sym->sec_name)
        special_name = sym->sec_name;
    if (!opts.special_syms && is_target_special_symbol(machine, special_name))
        return 0;

    /* By default, skip STT_FILE and STT_SECTION unless -a */
    if (!opts.debug_syms) {
        if (sym->stype == STT_FILE || sym->stype == STT_SECTION)
            return 0;
        /* Skip symbols with empty names (typically compiler-generated) */
        if (sym->name == NULL || sym->name[0] == '\0')
            return 0;
    }

    if (opts.undefined_only && sym->shndx != SHN_UNDEF)
        return 0;

    if (opts.defined_only && sym->shndx == SHN_UNDEF)
        return 0;

    if (opts.extern_only && sym->bind == STB_LOCAL)
        return 0;

    if (opts.no_weak && sym->bind == STB_WEAK)
        return 0;

    /* Sorting by size only makes sense for symbols that have one */
    if (opts.sort_mode == SORT_SIZE &&
        (sym->shndx == SHN_UNDEF || sym->sz == 0))
        return 0;

    return 1;
}

/* ------------------------------------------------------------------ */
/* Sort comparators                                                    */
/* ------------------------------------------------------------------ */

static int cmp_alpha(const void *a, const void *b)
{
    const nm_sym_t *sa = a;
    const nm_sym_t *sb = b;
    const char *na = sa->name ? sa->name : "";
    const char *nb = sb->name ? sb->name : "";
    int rc = strcmp(na, nb);

    if (rc != 0)
        return opts.reverse ? -rc : rc;
    /* Tie-break: by value, then by original index */
    if (sa->value != sb->value)
        return opts.reverse
            ? (sa->value > sb->value ? -1 : 1)
            : (sa->value < sb->value ? -1 : 1);
    return 0;
}

static int cmp_numeric(const void *a, const void *b)
{
    const nm_sym_t *sa = a;
    const nm_sym_t *sb = b;
    int rc;

    /* Undefined symbols sort to the front (no value) */
    if (sa->shndx == SHN_UNDEF && sb->shndx != SHN_UNDEF)
        return opts.reverse ? 1 : -1;
    if (sa->shndx != SHN_UNDEF && sb->shndx == SHN_UNDEF)
        return opts.reverse ? -1 : 1;

    if (sa->value != sb->value) {
        rc = (sa->value < sb->value) ? -1 : 1;
        return opts.reverse ? -rc : rc;
    }
    /* Tie-break by name */
    {
        const char *na = sa->name ? sa->name : "";
        const char *nb = sb->name ? sb->name : "";
        rc = strcmp(na, nb);
        return opts.reverse ? -rc : rc;
    }
}

static int cmp_size(const void *a, const void *b)
{
    const nm_sym_t *sa = a;
    const nm_sym_t *sb = b;
    int rc;

    if (sa->sz != sb->sz) {
        rc = (sa->sz < sb->sz) ? -1 : 1;
        return opts.reverse ? -rc : rc;
    }
    /* Tie-break by name */
    {
        const char *na = sa->name ? sa->name : "";
        const char *nb = sb->name ? sb->name : "";
        rc = strcmp(na, nb);
        return opts.reverse ? -rc : rc;
    }
}

static int cmp_none(const void *a, const void *b)
{
    const nm_sym_t *sa = a;
    const nm_sym_t *sb = b;
    int rc;

    if (sa->orig_index < sb->orig_index)
        rc = -1;
    else if (sa->orig_index > sb->orig_index)
        rc = 1;
    else
        rc = 0;
    return opts.reverse ? -rc : rc;
}

static void sort_symbols(nm_symtab_t *tab)
{
    int (*cmpfn)(const void *, const void *);

    if (tab->count <= 1)
        return;

    switch (opts.sort_mode) {
    case SORT_NUMERIC: cmpfn = cmp_numeric; break;
    case SORT_SIZE:    cmpfn = cmp_size;    break;
    case SORT_NONE:    cmpfn = cmp_none;    break;
    default:           cmpfn = cmp_alpha;   break;
    }
    qsort(tab->syms, tab->count, sizeof(tab->syms[0]), cmpfn);
}

/* ------------------------------------------------------------------ */
/* Output helpers                                                      */
/* ------------------------------------------------------------------ */

static const char *display_name(const nm_sym_t *sym)
{
    static char *prev_demangled;

    if (prev_demangled) {
        demangle_free(prev_demangled);
        prev_demangled = NULL;
    }

    if (opts.do_demangle && sym->name) {
        char *d = demangle(sym->name, DEMANGLE_AUTO);
        if (d) {
            prev_demangled = d;
            return d;
        }
    }
    return sym->name ? sym->name : "";
}

/* (format_value removed — all output uses inline printf with radix) */

static void print_bsd_line(const nm_sym_t *sym, elfobj_class_t cls, const char *file, elfobj_t *obj)
{
    int w = (cls == ELFOBJ_CLASS_64) ? 16 : 8;
    const char *name = display_name(sym);
    /* --size-sort without -S shows the size in place of the value */
    unsigned long long first = (opts.sort_mode == SORT_SIZE && !opts.print_size)
                             ? (unsigned long long)sym->sz
                             : (unsigned long long)sym->value;

    if (opts.print_file && file)
        printf("%s:", file);

    if (sym->shndx == SHN_UNDEF) {
        printf("%*s %c", w, "", sym->type_char);
    } else {
        if (opts.radix == 8)
            printf("%0*llo %c", w, first, sym->type_char);
        else if (opts.radix == 16)
            printf("%0*llx %c", w, first, sym->type_char);
        else
            printf("%*llu %c", w, first, sym->type_char);
    }

    if (opts.print_size && sym->sz > 0) {
        if (opts.radix == 8)
            printf(" %0*llo", w, (unsigned long long)sym->sz);
        else if (opts.radix == 16)
            printf(" %0*llx", w, (unsigned long long)sym->sz);
        else
            printf(" %*llu", w, (unsigned long long)sym->sz);
    }

    printf(" %s", name);
    if (opts.print_line && obj && sym->shndx != SHN_UNDEF) {
        char *dwarf_file = NULL;
        int dwarf_line = 0;
        if (elf_dwarf_get_line_info(obj, sym->value, &dwarf_file, &dwarf_line) == ELF_OK && dwarf_file) {
            printf("\t%s:%d", dwarf_file, dwarf_line);
            free(dwarf_file);
        }
    }
    printf("\n");
}

static void print_posix_line(const nm_sym_t *sym, elfobj_class_t cls, const char *file, elfobj_t *obj)
{
    const char *name = display_name(sym);
    int w = (cls == ELFOBJ_CLASS_64) ? 16 : 8;

    if (opts.print_file && file)
        printf("%s:", file);

    if (sym->shndx == SHN_UNDEF) {
        printf("%s %c", name, sym->type_char);
    } else {
        if (opts.radix == 8) {
            printf("%s %c %0*llo %0*llo", name, sym->type_char,
                   w, (unsigned long long)sym->value,
                   w, (unsigned long long)sym->sz);
        } else if (opts.radix == 16) {
            printf("%s %c %0*llx %0*llx", name, sym->type_char,
                   w, (unsigned long long)sym->value,
                   w, (unsigned long long)sym->sz);
        } else {
            printf("%s %c %*llu %*llu", name, sym->type_char,
                   w, (unsigned long long)sym->value,
                   w, (unsigned long long)sym->sz);
        }
    }
    if (opts.print_line && obj && sym->shndx != SHN_UNDEF) {
        char *dwarf_file = NULL;
        int dwarf_line = 0;
        if (elf_dwarf_get_line_info(obj, sym->value, &dwarf_file, &dwarf_line) == ELF_OK && dwarf_file) {
            printf("\t%s:%d", dwarf_file, dwarf_line);
            free(dwarf_file);
        }
    }
    printf("\n");
}

static void print_sysv_header(const char *file)
{
    printf("\n\nSymbols from %s:\n\n", file);
    printf("%-20s|%18s|%5s|%8s|%18s|     |%s\n",
           "Name", "Value", "Class", "Type", "Size", "Section");
}

static const char *stt_name(uint8_t stype)
{
    switch (stype) {
    case STT_NOTYPE:  return "NOTYPE";
    case STT_OBJECT:  return "OBJECT";
    case STT_FUNC:    return "FUNC";
    case STT_SECTION: return "SECTION";
    case STT_FILE:    return "FILE";
    case STT_COMMON:  return "COMMON";
    case STT_TLS:     return "TLS";
    default:          return "?";
    }
}

static void print_sysv_line(const nm_sym_t *sym, elfobj_class_t cls, const char *file, elfobj_t *obj)
{
    const char *name = display_name(sym);
    int w = (cls == ELFOBJ_CLASS_64) ? 16 : 8;
    const char *sec = sym->sec_name ? sym->sec_name : "*UND*";
    char vbuf[32];
    char sbuf[32];

    (void)file;

    if (sym->shndx == SHN_UNDEF) {
        snprintf(vbuf, sizeof(vbuf), "%*s", w, "");
        snprintf(sbuf, sizeof(sbuf), "%*s", w, "");
    } else {
        if (opts.radix == 8) {
            snprintf(vbuf, sizeof(vbuf), "%0*llo", w, (unsigned long long)sym->value);
            snprintf(sbuf, sizeof(sbuf), "%0*llo", w, (unsigned long long)sym->sz);
        } else if (opts.radix == 16) {
            snprintf(vbuf, sizeof(vbuf), "%0*llx", w, (unsigned long long)sym->value);
            snprintf(sbuf, sizeof(sbuf), "%0*llx", w, (unsigned long long)sym->sz);
        } else {
            snprintf(vbuf, sizeof(vbuf), "%*llu", w, (unsigned long long)sym->value);
            snprintf(sbuf, sizeof(sbuf), "%*llu", w, (unsigned long long)sym->sz);
        }
    }

    printf("%-20s|%*s|  %c  |%-8s|%*s|     |%s",
           name,
           w, vbuf,
           sym->type_char,
           stt_name(sym->stype),
           w, sbuf,
           sec);
    if (opts.print_line && obj && sym->shndx != SHN_UNDEF) {
        char *dwarf_file = NULL;
        int dwarf_line = 0;
        if (elf_dwarf_get_line_info(obj, sym->value, &dwarf_file, &dwarf_line) == ELF_OK && dwarf_file) {
            printf("\t%s:%d", dwarf_file, dwarf_line);
            free(dwarf_file);
        }
    }
    printf("\n");
}

/* ------------------------------------------------------------------ */
/* Collect symbols from elfobj's built-in symbol table (.symtab)       */
/* ------------------------------------------------------------------ */

static int collect_from_elfobj(elfobj_t *obj, nm_symtab_t *tab)
{
    size_t n = elf_symbol_count(obj);
    size_t i;

    for (i = 0; i < n; i++) {
        elf_symbol_t *sym = elf_symbol_at(obj, i);
        nm_sym_t entry;
        const char *sec_name = NULL;

        if (sym == NULL)
            continue;

        memset(&entry, 0, sizeof(entry));
        entry.name = xstrdup(elf_symbol_name(sym));
        entry.value = elf_symbol_value(sym);
        entry.sz = elf_symbol_size(sym);
        entry.bind = elf_symbol_bind(sym);
        entry.stype = elf_symbol_type(sym);
        entry.visibility = elf_symbol_visibility(sym);
        entry.shndx = elf_symbol_shndx(sym);
        entry.orig_index = i;

        entry.type_char = classify_symbol(obj, entry.shndx, entry.bind,
                                          entry.stype, entry.value,
                                          entry.sz, &sec_name);
        entry.sec_name = xstrdup(sec_name);

        if (nm_symtab_push(tab, &entry) != 0) {
            free(entry.name);
            free(entry.sec_name);
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Collect symbols from .dynsym (manual parse for -D flag)             */
/* ------------------------------------------------------------------ */

static int collect_from_dynsym(elfobj_t *obj, nm_symtab_t *tab)
{
    size_t nsec = elf_section_count(obj);
    size_t i;
    elf_section_t *dynsym_sec = NULL;
    elf_section_t *dynstr_sec = NULL;
    const uint8_t *sym_data;
    const uint8_t *str_data;
    size_t sym_data_sz, str_data_sz;
    size_t ent_size;
    size_t nsyms;
    int be = (elf_endian(obj) == ELFOBJ_ENDIAN_BE);
    int is64 = (elf_class(obj) == ELFOBJ_CLASS_64);

    /* Find .dynsym section */
    for (i = 0; i < nsec; i++) {
        elf_section_t *sec = elf_section_get(obj, i);
        if (sec && elf_section_type(sec) == SHT_DYNSYM) {
            dynsym_sec = sec;
            break;
        }
    }
    if (dynsym_sec == NULL)
        return 0; /* No dynamic symbols */

    sym_data = elf_section_data(dynsym_sec, &sym_data_sz);
    if (sym_data == NULL || sym_data_sz == 0)
        return 0;

    /* Find the linked string table */
    /* The link field is available through section internals; we find .dynstr by name */
    dynstr_sec = elf_find_section(obj, ".dynstr");
    if (dynstr_sec == NULL) {
        /* Try by iterating to find a strtab linked from dynsym */
        for (i = 0; i < nsec; i++) {
            elf_section_t *sec = elf_section_get(obj, i);
            if (sec && elf_section_type(sec) == SHT_STRTAB) {
                const char *sn = elf_section_name(sec);
                if (sn && strcmp(sn, ".dynstr") == 0) {
                    dynstr_sec = sec;
                    break;
                }
            }
        }
    }
    if (dynstr_sec == NULL)
        return 0;

    str_data = elf_section_data(dynstr_sec, &str_data_sz);
    if (str_data == NULL)
        return 0;

    ent_size = is64 ? 24 : 16; /* sizeof(Elf64_Sym) or sizeof(Elf32_Sym) */
    nsyms = sym_data_sz / ent_size;

    for (i = 0; i < nsyms; i++) {
        const uint8_t *p = sym_data + i * ent_size;
        nm_sym_t entry;
        uint32_t st_name_off;
        uint8_t st_info, st_other;
        const char *sec_name = NULL;
        const char *sym_name;

        memset(&entry, 0, sizeof(entry));

        if (is64) {
            st_name_off = rd32(p + 0, be);
            st_info = p[4];
            st_other = p[5];
            entry.shndx = rd16(p + 6, be);
            entry.value = rd64(p + 8, be);
            entry.sz = rd64(p + 16, be);
        } else {
            st_name_off = rd32(p + 0, be);
            entry.value = rd32(p + 4, be);
            entry.sz = rd32(p + 8, be);
            st_info = p[12];
            st_other = p[13];
            entry.shndx = rd16(p + 14, be);
        }

        entry.bind = ELF32_ST_BIND(st_info);
        entry.stype = ELF32_ST_TYPE(st_info);
        entry.visibility = st_other & 0x3;
        entry.orig_index = i;

        /* Resolve symbol name from string table */
        if (st_name_off < str_data_sz) {
            sym_name = (const char *)(str_data + st_name_off);
            /* Ensure null-terminated within bounds */
            {
                size_t maxlen = str_data_sz - st_name_off;
                size_t j;
                int found_null = 0;
                for (j = 0; j < maxlen; j++) {
                    if (sym_name[j] == '\0') {
                        found_null = 1;
                        break;
                    }
                }
                if (!found_null)
                    sym_name = "";
            }
        } else {
            sym_name = "";
        }
        entry.name = xstrdup(sym_name);

        entry.type_char = classify_symbol(obj, entry.shndx, entry.bind,
                                          entry.stype, entry.value,
                                          entry.sz, &sec_name);
        entry.sec_name = xstrdup(sec_name);

        if (nm_symtab_push(tab, &entry) != 0) {
            free(entry.name);
            free(entry.sec_name);
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Process a single ELF object                                         */
/* ------------------------------------------------------------------ */

static int process_elf(elfobj_t *obj, const char *display_name_str)
{
    nm_symtab_t tab;
    elfobj_class_t cls;
    uint16_t machine;
    size_t i;
    int printed = 0;

    nm_symtab_init(&tab);
    cls = elf_class(obj);
    machine = elf_machine(obj);

    if (opts.dynamic) {
        if (collect_from_dynsym(obj, &tab) != 0) {
            nm_symtab_free(&tab);
            return -1;
        }
    } else {
        if (collect_from_elfobj(obj, &tab) != 0) {
            nm_symtab_free(&tab);
            return -1;
        }
    }

    /* Check for "no symbols" */
    {
        int has_displayable = 0;
        for (i = 0; i < tab.count; i++) {
            if (filter_symbol(&tab.syms[i], machine)) {
                has_displayable = 1;
                break;
            }
        }
        if (tab.count == 0 || !has_displayable) {
            /* Check if the table is truly empty (not just filtered out) */
            if (tab.count == 0 ||
                (tab.count == 1 && tab.syms[0].shndx == SHN_UNDEF &&
                 (tab.syms[0].name == NULL || tab.syms[0].name[0] == '\0'))) {
                fprintf(stderr, "%s: %s: no symbols\n", progname, display_name_str);
                nm_symtab_free(&tab);
                return 0;
            }
        }
    }

    sort_symbols(&tab);

    if (opts.format == FMT_SYSV) {
        print_sysv_header(display_name_str);
    }

    for (i = 0; i < tab.count; i++) {
        if (!filter_symbol(&tab.syms[i], machine))
            continue;

        switch (opts.format) {
        case FMT_POSIX:
            print_posix_line(&tab.syms[i], cls, display_name_str, obj);
            break;
        case FMT_SYSV:
            print_sysv_line(&tab.syms[i], cls, display_name_str, obj);
            break;
        default:
            print_bsd_line(&tab.syms[i], cls, display_name_str, obj);
            break;
        }
        printed++;
    }

    if (printed == 0 && tab.count > 0) {
        /* All symbols filtered out — not an error */
    }

    nm_symtab_free(&tab);
    return 0;
}

/* ------------------------------------------------------------------ */
/* File I/O                                                            */
/* ------------------------------------------------------------------ */

static int read_file_bytes(const char *path, uint8_t **buf_out, size_t *size_out)
{
    FILE *fp;
    long sz;
    uint8_t *buf;

    *buf_out = NULL;
    *size_out = 0;

    fp = fopen(path, "rb");
    if (fp == NULL)
        return -1;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    sz = ftell(fp);
    if (sz < 0) { fclose(fp); return -1; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return -1; }
    if ((unsigned long)sz > SIZE_MAX) { fclose(fp); return -1; }

    buf = malloc((size_t)sz);
    if (buf == NULL && sz != 0) { fclose(fp); return -1; }
    if (sz != 0 && fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf); fclose(fp); return -1;
    }
    fclose(fp);
    *buf_out = buf;
    *size_out = (size_t)sz;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Archive support (replicates pattern from size utility)               */
/* ------------------------------------------------------------------ */

static int is_archive_buffer(const uint8_t *buf, size_t size, int *is_thin)
{
    if (is_thin)
        *is_thin = 0;
    if (buf == NULL || size < AR_MAGIC_LEN)
        return 0;
    if (memcmp(buf, AR_MAGIC, AR_MAGIC_LEN) == 0)
        return 1;
    if (memcmp(buf, AR_THIN_MAGIC, AR_MAGIC_LEN) == 0) {
        if (is_thin)
            *is_thin = 1;
        return 1;
    }
    return 0;
}

static int parse_decimal_field(const char *field, size_t len, uint64_t *out)
{
    char tmp[32];
    uint64_t v = 0;
    size_t n = 0;
    size_t i;

    if (field == NULL || out == NULL || len == 0 || len >= sizeof(tmp))
        return -1;
    while (n < len && field[n] != '\0') {
        tmp[n] = field[n];
        n++;
    }
    while (n > 0 && (tmp[n - 1] == ' ' || tmp[n - 1] == '/'))
        n--;
    tmp[n] = '\0';
    while (*tmp == ' ')
        memmove(tmp, tmp + 1, strlen(tmp));
    if (tmp[0] == '\0') { *out = 0; return 0; }
    for (i = 0; tmp[i] != '\0'; i++) {
        uint8_t digit;
        if (tmp[i] < '0' || tmp[i] > '9')
            return -1;
        digit = (uint8_t)(tmp[i] - '0');
        if (v > (UINT64_MAX - (uint64_t)digit) / 10u)
            return -1;
        v = v * 10u + (uint64_t)digit;
    }
    *out = v;
    return 0;
}

static char *dup_member_name_simple(const char name_field[16])
{
    char tmp[17];
    size_t len = 16;

    memcpy(tmp, name_field, 16);
    tmp[16] = '\0';
    while (len > 0 && (tmp[len - 1] == ' ' || tmp[len - 1] == '\0'))
        len--;
    tmp[len] = '\0';
    if (!(len > 0 && tmp[0] == '/')) {
        while (len > 0 && tmp[len - 1] == '/')
            len--;
    }
    while (len > 0 && tmp[len - 1] == ' ')
        len--;
    tmp[len] = '\0';
    return xstrdup(tmp);
}

static char *dup_gnu_long_name(const uint8_t *table, size_t table_size, uint64_t off)
{
    size_t start, end, i, n;
    char *out;

    if (table == NULL || off >= table_size)
        return NULL;
    start = (size_t)off;
    end = start;
    for (i = start; i < table_size; i++) {
        if (table[i] == '\n' || table[i] == '\0')
            break;
        end++;
    }
    while (end > start && table[end - 1] == '/')
        end--;
    if (end <= start)
        return NULL;
    n = end - start;
    out = malloc(n + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, table + start, n);
    out[n] = '\0';
    return out;
}

static char *path_dirname_dup(const char *path)
{
    const char *slash;
    size_t n;
    char *out;

    if (path == NULL)
        return xstrdup(".");
    slash = strrchr(path, '/');
    if (slash == NULL)
        return xstrdup(".");
    if (slash == path)
        return xstrdup("/");
    n = (size_t)(slash - path);
    out = malloc(n + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, path, n);
    out[n] = '\0';
    return out;
}

static char *join_paths(const char *dir, const char *leaf)
{
    size_t a, b;
    int need_sep;
    char *out;

    if (leaf == NULL)
        return NULL;
    if (leaf[0] == '/')
        return xstrdup(leaf);
    if (dir == NULL || dir[0] == '\0')
        return xstrdup(leaf);
    a = strlen(dir);
    b = strlen(leaf);
    need_sep = (a > 0 && dir[a - 1] != '/');
    out = malloc(a + (size_t)need_sep + b + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, dir, a);
    if (need_sep) {
        out[a] = '/';
        memcpy(out + a + 1, leaf, b);
        out[a + 1 + b] = '\0';
    } else {
        memcpy(out + a, leaf, b);
        out[a + b] = '\0';
    }
    return out;
}

static int process_elf_path(const char *path, const char *display)
{
    elfobj_t *obj = NULL;
    elf_err_t err;
    int rc;

    err = elf_open(path, &obj);
    if (err != ELF_OK || obj == NULL) {
        if (err == ELF_ERR_FORMAT)
            fprintf(stderr, "%s: %s: file format not recognized\n", progname, display);
        else
            fprintf(stderr, "%s: %s: %s\n", progname, display, elf_errstr(err));
        return -1;
    }
    rc = process_elf(obj, display);
    elf_close(obj);
    return rc;
}

static int process_elf_memory(const uint8_t *buf, size_t sz, const char *display)
{
    elfobj_t *obj = NULL;
    elf_err_t err;
    int rc;

    err = elf_open_memory(buf, sz, &obj);
    if (err != ELF_OK || obj == NULL) {
        if (err == ELF_ERR_FORMAT)
            fprintf(stderr, "%s: %s: file format not recognized\n", progname, display);
        else
            fprintf(stderr, "%s: %s: %s\n", progname, display, elf_errstr(err));
        return -1;
    }
    rc = process_elf(obj, display);
    elf_close(obj);
    return rc;
}

/**
 * @brief Name the archive member whose header starts at @p hoff.
 *
 * Resolves the three spellings an ar header can use: a GNU "/N"
 * reference into the long-name table, a BSD "#1/N" name stored in
 * front of the member's data, and a plain name in the 16-byte field.
 *
 * @param buf           The whole archive.
 * @param size          Size of @p buf in bytes.
 * @param hoff          Offset of the member's ar header in @p buf.
 * @param nametab       The GNU "//" long-name table, or NULL.
 * @param nametab_size  Size of @p nametab in bytes.
 * @return A malloc'd name, or NULL when @p hoff is not a valid header
 *         or memory runs out.
 */
static char *archive_member_name_at(const uint8_t *buf, size_t size, size_t hoff,
                                    const uint8_t *nametab, size_t nametab_size)
{
    const struct ar_raw_header *hdr;
    char *name;

    if (hoff < AR_MAGIC_LEN || hoff > size || size - hoff < AR_HDR_LEN)
        return NULL;
    hdr = (const struct ar_raw_header *)(const void *)(buf + hoff);
    if (memcmp(hdr->fmag, AR_FMAG, 2) != 0)
        return NULL;

    if (memcmp(hdr->name, "#1/", 3) == 0) {
        uint64_t extlen = 0;
        size_t n;

        if (parse_decimal_field(hdr->name + 3, 13, &extlen) != 0 ||
            extlen > size - hoff - AR_HDR_LEN)
            return NULL;
        n = (size_t)extlen;
        while (n > 0 && buf[hoff + AR_HDR_LEN + n - 1] == '\0')
            n--;
        name = malloc(n + 1);
        if (name == NULL)
            return NULL;
        memcpy(name, buf + hoff + AR_HDR_LEN, n);
        name[n] = '\0';
        return name;
    }

    name = dup_member_name_simple(hdr->name);
    if (name != NULL && name[0] == '/' && name[1] >= '0' && name[1] <= '9') {
        uint64_t noff = 0;
        char *long_name = NULL;

        if (parse_decimal_field(name + 1, strlen(name) - 1, &noff) == 0)
            long_name = dup_gnu_long_name(nametab, nametab_size, noff);
        free(name);
        name = long_name;
    }
    return name;
}

/**
 * @brief Print one "<symbol> in <member>" line of the archive index.
 *
 * The first call for an archive also prints the "Archive index:"
 * heading, so an archive whose index is empty prints nothing, as GNU
 * nm does.  The symbol is demangled under -C.
 *
 * @param sym       Symbol name from the index (NUL-terminated).
 * @param member    Name of the member defining it.
 * @param printed   In/out: whether the heading has been printed.
 */
static void print_armap_entry(const char *sym, const char *member, int *printed)
{
    char *dem = NULL;

    if (!*printed) {
        printf("\nArchive index:\n");
        *printed = 1;
    }
    if (opts.do_demangle)
        dem = demangle(sym, DEMANGLE_AUTO);
    printf("%s in %s\n", dem ? dem : sym, member);
    if (dem)
        demangle_free(dem);
}

/**
 * @brief Print an archive's symbol index, as GNU nm -s does.
 *
 * Reads the GNU "/" (32-bit offsets) or "/SYM64/" (64-bit offsets)
 * table, both big-endian, or a BSD "__.SYMDEF" ranlib table in
 * little-endian order, and prints "Archive index:" followed by one
 * "<symbol> in <member>" line per entry, in index order.  The index and
 * the GNU long-name table always precede the ordinary members, so only
 * the leading special members are scanned.  An archive without an
 * index prints nothing.
 *
 * @param archive_path  Archive name, for diagnostics.
 * @param buf           The whole archive.
 * @param size          Size of @p buf in bytes.
 * @return 0 on success, -1 when the index is malformed.
 */
static int print_archive_index(const char *archive_path, const uint8_t *buf, size_t size)
{
    const uint8_t *map = NULL, *nametab = NULL;
    size_t map_size = 0, nametab_size = 0;
    int map_kind = 0;           /* 1 = GNU 32-bit, 2 = GNU 64-bit, 3 = BSD */
    size_t off = AR_MAGIC_LEN;
    int printed = 0;

    while (off <= size && size - off >= AR_HDR_LEN) {
        const struct ar_raw_header *hdr =
            (const struct ar_raw_header *)(const void *)(buf + off);
        uint64_t msize = 0, extlen = 0;
        char *name;
        int special = 1;

        if (memcmp(hdr->fmag, AR_FMAG, 2) != 0 ||
            parse_decimal_field(hdr->size, sizeof(hdr->size), &msize) != 0 ||
            msize > size - off - AR_HDR_LEN)
            break;
        if (memcmp(hdr->name, "#1/", 3) == 0) {
            /* 4.4BSD: the name sits in front of the data. */
            if (parse_decimal_field(hdr->name + 3, 13, &extlen) != 0 ||
                extlen > msize)
                break;
            name = archive_member_name_at(buf, size, off, NULL, 0);
        } else {
            name = dup_member_name_simple(hdr->name);
        }
        if (name == NULL)
            return -1;
        if (strcmp(name, "/") == 0 && map == NULL)
            map_kind = 1;
        else if ((strcmp(name, "/SYM64") == 0 || strcmp(name, "/SYM64/") == 0) &&
                 map == NULL)
            map_kind = 2;
        else if ((strcmp(name, "__.SYMDEF") == 0 ||
                  strcmp(name, "__.SYMDEF SORTED") == 0) && map == NULL)
            map_kind = 3;
        else if (strcmp(name, "//") == 0) {
            nametab = buf + off + AR_HDR_LEN;
            nametab_size = (size_t)msize;
        } else
            special = 0;
        if (special && map == NULL && map_kind != 0) {
            map = buf + off + AR_HDR_LEN + (size_t)extlen;
            map_size = (size_t)(msize - extlen);
        }
        free(name);
        if (!special)
            break;
        off += AR_HDR_LEN + (size_t)msize + (size_t)(msize & 1u);
    }

    if (map == NULL)
        return 0;

    if (map_kind == 1 || map_kind == 2) {
        size_t w = map_kind == 2 ? 8 : 4;
        uint64_t count;
        size_t i, strpos;

        if (map_size < w)
            goto bad;
        count = w == 8 ? rd64(map, 1) : rd32(map, 1);
        if (count > (map_size - w) / w)
            goto bad;
        strpos = w + (size_t)count * w;
        for (i = 0; i < (size_t)count; i++) {
            const uint8_t *p = map + w + i * w;
            uint64_t moff = w == 8 ? rd64(p, 1) : rd32(p, 1);
            const char *sym = (const char *)(map + strpos);
            const uint8_t *nul;
            char *member;

            if (strpos >= map_size)
                goto bad;
            nul = memchr(map + strpos, '\0', map_size - strpos);
            if (nul == NULL)
                goto bad;
            member = moff <= SIZE_MAX ?
                archive_member_name_at(buf, size, (size_t)moff, nametab, nametab_size) :
                NULL;
            if (member == NULL)
                goto bad;
            print_armap_entry(sym, member, &printed);
            free(member);
            strpos = (size_t)(nul - map) + 1;
        }
        return 0;
    }

    /* BSD: ranlib array byte count, {strx, off} pairs, string size, strings. */
    {
        uint32_t ranlib_bytes, strsize;
        const uint8_t *strs;
        size_t i, n;

        if (map_size < 4)
            goto bad;
        ranlib_bytes = rd32(map, 0);
        if (ranlib_bytes % 8 != 0 || ranlib_bytes > map_size - 4 ||
            map_size - 4 - ranlib_bytes < 4)
            goto bad;
        strsize = rd32(map + 4 + ranlib_bytes, 0);
        if (strsize > map_size - 8 - ranlib_bytes)
            goto bad;
        strs = map + 8 + ranlib_bytes;
        n = ranlib_bytes / 8;
        for (i = 0; i < n; i++) {
            uint32_t strx = rd32(map + 4 + i * 8, 0);
            uint32_t moff = rd32(map + 8 + i * 8, 0);
            char *member;

            if (strx >= strsize || memchr(strs + strx, '\0', strsize - strx) == NULL)
                goto bad;
            member = archive_member_name_at(buf, size, moff, nametab, nametab_size);
            if (member == NULL)
                goto bad;
            print_armap_entry((const char *)(strs + strx), member, &printed);
            free(member);
        }
        return 0;
    }

bad:
    fprintf(stderr, "%s: %s: malformed archive index\n", progname, archive_path);
    return -1;
}

static int process_archive(const char *archive_path, const uint8_t *buf, size_t size,
                           int is_thin, int *any_fail)
{
    size_t off = AR_MAGIC_LEN;
    const uint8_t *gnu_name_table = NULL;
    size_t gnu_name_table_size = 0;
    char *archive_dir = NULL;

    if (opts.print_armap && print_archive_index(archive_path, buf, size) != 0)
        *any_fail = 1;

    if (is_thin) {
        archive_dir = path_dirname_dup(archive_path);
        if (archive_dir == NULL) {
            fprintf(stderr, "%s: %s: out of memory\n", progname, archive_path);
            *any_fail = 1;
            return -1;
        }
    }

    while (off + AR_HDR_LEN <= size) {
        const struct ar_raw_header *hdr =
            (const struct ar_raw_header *)(const void *)(buf + off);
        uint64_t msize_u64 = 0;
        size_t msize, payload_size, payload_off, next_off;
        char *member_name = NULL;
        char *member_label = NULL;
        char *thin_ref_path = NULL;
        const uint8_t *member_data;
        size_t member_size;
        int special_member = 0;
        int thin_ref = 0;

        if (memcmp(hdr->fmag, AR_FMAG, 2) != 0) {
            fprintf(stderr, "%s: %s: malformed archive header\n", progname, archive_path);
            *any_fail = 1;
            free(archive_dir);
            return -1;
        }

        if (parse_decimal_field(hdr->size, sizeof(hdr->size), &msize_u64) != 0 ||
            msize_u64 > SIZE_MAX) {
            fprintf(stderr, "%s: %s: malformed archive member size\n", progname, archive_path);
            *any_fail = 1;
            free(archive_dir);
            return -1;
        }
        msize = (size_t)msize_u64;
        payload_size = msize;

        if (is_thin) {
            if (memcmp(hdr->name, "#1/", 3) == 0) {
                uint64_t extlen_u64 = 0;
                if (parse_decimal_field(hdr->name + 3, 13, &extlen_u64) != 0 ||
                    extlen_u64 > msize)
                    payload_size = msize;
                else
                    payload_size = (size_t)extlen_u64;
            } else {
                char *raw_name = dup_member_name_simple(hdr->name);
                if (raw_name && (strcmp(raw_name, "/") == 0 ||
                    strcmp(raw_name, "//") == 0 ||
                    strcmp(raw_name, "/SYM64") == 0 ||
                    strcmp(raw_name, "/SYM64/") == 0 ||
                    strcmp(raw_name, "__.SYMDEF") == 0 ||
                    strcmp(raw_name, "__.SYMDEF SORTED") == 0))
                    payload_size = msize;
                else
                    payload_size = 0;
                free(raw_name);
            }
        }

        payload_off = off + AR_HDR_LEN;
        if (payload_off > size || payload_size > size - payload_off) {
            fprintf(stderr, "%s: %s: truncated archive member\n", progname, archive_path);
            *any_fail = 1;
            free(archive_dir);
            return -1;
        }
        member_data = buf + payload_off;
        member_size = payload_size;
        /*
         * Advance past the data actually stored in this archive.  A thin
         * archive member's size field is the external file's size and no
         * data follows its header, so skipping by that field would land
         * past the next header (or wrap on ILP32).  payload_size is
         * bounded by size - payload_off above, so the sum cannot wrap;
         * the pad byte only matters when room for it remains.
         */
        next_off = payload_off + payload_size;
        if ((payload_size & 1u) != 0 && next_off < size)
            next_off++;

        /* Parse member name */
        if (memcmp(hdr->name, "#1/", 3) == 0) {
            uint64_t extlen_u64 = 0;
            size_t extlen;

            if (parse_decimal_field(hdr->name + 3, 13, &extlen_u64) != 0 ||
                extlen_u64 > member_size) {
                free(archive_dir);
                *any_fail = 1;
                return -1;
            }
            extlen = (size_t)extlen_u64;
            member_name = malloc(extlen + 1);
            if (member_name == NULL) { free(archive_dir); *any_fail = 1; return -1; }
            memcpy(member_name, member_data, extlen);
            member_name[extlen] = '\0';
            member_data += extlen;
            member_size -= extlen;
            if (is_thin) {
                thin_ref = 1;
                thin_ref_path = xstrdup(member_name);
            }
        } else {
            member_name = dup_member_name_simple(hdr->name);
            if (member_name == NULL) { free(archive_dir); *any_fail = 1; return -1; }

            if (strcmp(member_name, "//") == 0) {
                gnu_name_table = member_data;
                gnu_name_table_size = member_size;
                special_member = 1;
            } else if (strcmp(member_name, "/") == 0 ||
                       strcmp(member_name, "__.SYMDEF") == 0 ||
                       strcmp(member_name, "__.SYMDEF SORTED") == 0 ||
                       strcmp(member_name, "/SYM64") == 0 ||
                       strcmp(member_name, "/SYM64/") == 0) {
                special_member = 1;
            } else if (member_name[0] == '/' &&
                       member_name[1] >= '0' && member_name[1] <= '9') {
                uint64_t noff = 0;
                char *long_name = NULL;

                if (parse_decimal_field(member_name + 1,
                        strlen(member_name) - 1, &noff) == 0)
                    long_name = dup_gnu_long_name(gnu_name_table,
                                                  gnu_name_table_size, noff);
                free(member_name);
                member_name = long_name ? long_name : xstrdup("<bad-name>");
                if (member_name == NULL) { free(archive_dir); *any_fail = 1; return -1; }
            }

            if (is_thin && !special_member) {
                if (member_size > 0 && member_size < (size_t)(PATH_MAX * 4)) {
                    size_t trim = member_size;
                    thin_ref_path = malloc(member_size + 1);
                    if (thin_ref_path) {
                        memcpy(thin_ref_path, member_data, member_size);
                        while (trim > 0 && (thin_ref_path[trim - 1] == '\n' ||
                                            thin_ref_path[trim - 1] == '\0'))
                            trim--;
                        thin_ref_path[trim] = '\0';
                        if (trim == 0) {
                            free(thin_ref_path);
                            thin_ref_path = xstrdup(member_name);
                        }
                    }
                } else {
                    thin_ref_path = xstrdup(member_name);
                }
                thin_ref = 1;
            }
        }

        if (!special_member) {
            size_t labelsz = strlen(archive_path) + strlen(member_name) + 4;
            member_label = malloc(labelsz);
            if (member_label)
                snprintf(member_label, labelsz, "%s[%s]", archive_path, member_name);

            printf("\n%s:\n", member_label ? member_label : member_name);

            if (thin_ref) {
                char *resolved = join_paths(archive_dir,
                    thin_ref_path ? thin_ref_path : member_name);
                if (resolved) {
                    if (process_elf_path(resolved, member_label ? member_label : member_name) != 0)
                        *any_fail = 1;
                    free(resolved);
                } else {
                    *any_fail = 1;
                }
            } else {
                if (process_elf_memory(member_data, member_size,
                        member_label ? member_label : member_name) != 0)
                    *any_fail = 1;
            }

            free(member_label);
        }

        free(member_name);
        free(thin_ref_path);
        off = next_off;
    }

    free(archive_dir);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Top-level file processor                                            */
/* ------------------------------------------------------------------ */

static int process_file(const char *path, int multi_file)
{
    uint8_t *buf = NULL;
    size_t sz = 0;
    int is_thin = 0;
    int any_fail = 0;

    if (read_file_bytes(path, &buf, &sz) == 0 && is_archive_buffer(buf, sz, &is_thin)) {
        int rc = process_archive(path, buf, sz, is_thin, &any_fail);
        free(buf);
        return (rc != 0 || any_fail) ? -1 : 0;
    }
    free(buf);

    if (multi_file)
        printf("\n%s:\n", path);

    return process_elf_path(path, path);
}

/* ------------------------------------------------------------------ */
/* Argument parsing helpers                                            */
/* ------------------------------------------------------------------ */

static int starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/**
 * @brief Set the value radix from the argument of -t or --radix.
 *
 * @param r  "d", "o" or "x".
 * @return 0 on success, -1 (after a diagnostic) for any other value.
 */
static int set_radix(const char *r)
{
    if (strcmp(r, "d") == 0) opts.radix = 10;
    else if (strcmp(r, "o") == 0) opts.radix = 8;
    else if (strcmp(r, "x") == 0) opts.radix = 16;
    else {
        fprintf(stderr, "%s: %s: invalid radix\n", progname, r);
        return -1;
    }
    return 0;
}

/**
 * @brief Set the output format from the argument of -f or --format.
 *
 * @param f  "bsd", "posix" or "sysv".
 * @return 0 on success, -1 (after a diagnostic) for any other value.
 */
static int set_format(const char *f)
{
    if (strcmp(f, "bsd") == 0) opts.format = FMT_BSD;
    else if (strcmp(f, "posix") == 0) opts.format = FMT_POSIX;
    else if (strcmp(f, "sysv") == 0) opts.format = FMT_SYSV;
    else {
        fprintf(stderr, "%s: %s: invalid output format\n", progname, f);
        return -1;
    }
    return 0;
}

/**
 * @brief Fetch the argument of an option that requires one, as getopt
 *        does: the rest of the current word if any, else the next word.
 *
 * @param attached  Text following the option in the same word ("" if none).
 * @param name      The option, for the diagnostic ("-t", "--format", ...).
 * @param argc      Argument count.
 * @param argv      Argument vector.
 * @param i         Index of the current word; advanced past the next
 *                  word when that word is consumed.
 * @return The argument, or NULL (after a diagnostic) when it is missing.
 */
static const char *option_arg(const char *attached, const char *name,
                              int argc, char **argv, int *i)
{
    if (*attached)
        return attached;
    if (*i + 1 < argc)
        return argv[++*i];
    fprintf(stderr, "%s: option '%s' requires an argument\n", progname, name);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    int i;
    int rc = 0;
    int file_count = 0;
    int end_of_opts = 0;
    const char **files;

    /* Defaults */
    opts.format = FMT_BSD;
    opts.sort_mode = SORT_ALPHA;
    opts.radix = 16;

    if (argc > 0 && argv[0])
        progname = argv[0];

    /*
     * Parse the options and collect the file names in one pass, so an
     * option's argument is consumed exactly once and never mistaken for
     * a file.  Options and files may be interleaved; "--" ends the
     * options and a lone "-" is a file name, as with getopt.
     */
    files = malloc((size_t)(argc > 0 ? argc : 1) * sizeof(*files));
    if (!files) {
        fprintf(stderr, "%s: out of memory\n", progname);
        return 1;
    }

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *val;

        if (end_of_opts || arg[0] != '-' || arg[1] == '\0') {
            files[file_count++] = arg;
            continue;
        }

        if (strcmp(arg, "--") == 0) { end_of_opts = 1; continue; }

        /* Long options */
        if (arg[1] == '-') {
            if (strcmp(arg, "--help") == 0) { usage(stdout); goto out; }
            if (strcmp(arg, "--version") == 0) { printf("nm %s\n", NM_VERSION); goto out; }
            if (strcmp(arg, "--format") == 0 || starts_with(arg, "--format=")) {
                val = arg[8] ? arg + 9 : option_arg("", "--format", argc, argv, &i);
                if (!val || set_format(val) != 0) goto bad;
                continue;
            }
            if (strcmp(arg, "--radix") == 0 || starts_with(arg, "--radix=")) {
                val = arg[7] ? arg + 8 : option_arg("", "--radix", argc, argv, &i);
                if (!val || set_radix(val) != 0) goto bad;
                continue;
            }
            if (strcmp(arg, "--numeric-sort") == 0) { opts.sort_mode = SORT_NUMERIC; continue; }
            if (strcmp(arg, "--no-sort") == 0) { opts.sort_mode = SORT_NONE; continue; }
            if (strcmp(arg, "--reverse-sort") == 0) { opts.reverse = 1; continue; }
            if (strcmp(arg, "--size-sort") == 0) { opts.sort_mode = SORT_SIZE; continue; }
            if (strcmp(arg, "--undefined-only") == 0) { opts.undefined_only = 1; continue; }
            if (strcmp(arg, "--extern-only") == 0) { opts.extern_only = 1; continue; }
            if (strcmp(arg, "--debug-syms") == 0) { opts.debug_syms = 1; continue; }
            if (strcmp(arg, "--defined-only") == 0) { opts.defined_only = 1; continue; }
            if (strcmp(arg, "--no-weak") == 0) { opts.no_weak = 1; continue; }
            if (strcmp(arg, "--print-file-name") == 0) { opts.print_file = 1; continue; }
            if (strcmp(arg, "--print-size") == 0) { opts.print_size = 1; continue; }
            if (strcmp(arg, "--dynamic") == 0) { opts.dynamic = 1; continue; }
            if (strcmp(arg, "--demangle") == 0) { opts.do_demangle = 1; continue; }
            if (strcmp(arg, "--print-armap") == 0) { opts.print_armap = 1; continue; }
            if (strcmp(arg, "--special-syms") == 0) { opts.special_syms = 1; continue; }
            if (strcmp(arg, "--portability") == 0) { opts.format = FMT_POSIX; continue; }
            if (strcmp(arg, "--line-numbers") == 0) { opts.print_line = 1; continue; }

            fprintf(stderr, "%s: unrecognized option '%s'\n", progname, arg);
            goto bad;
        }

        /* Short options (may be combined, e.g. -agn, -at d, -fsysv) */
        {
            const char *p;
            for (p = arg + 1; *p; p++) {
                switch (*p) {
                case 'a': opts.debug_syms = 1; break;
                case 'A': opts.print_file = 1; break;
                case 'B': opts.format = FMT_BSD; break;
                case 'C': opts.do_demangle = 1; break;
                case 'D': opts.dynamic = 1; break;
                case 'g': opts.extern_only = 1; break;
                case 'h': usage(stdout); goto out;
                case 'l': opts.print_line = 1; break;
                case 'n': opts.sort_mode = SORT_NUMERIC; break;
                case 'o': opts.print_file = 1; break;
                case 'p': opts.sort_mode = SORT_NONE; break;
                case 'P': opts.format = FMT_POSIX; break;
                case 'r': opts.reverse = 1; break;
                case 's': opts.print_armap = 1; break;
                case 'S': opts.print_size = 1; break;
                case 'u': opts.undefined_only = 1; break;
                case 'v': opts.sort_mode = SORT_NUMERIC; break;
                case 'V': printf("nm %s\n", NM_VERSION); goto out;
                case 'f':
                    /* The rest of the word, or the next word, is the format */
                    val = option_arg(p + 1, "-f", argc, argv, &i);
                    if (!val || set_format(val) != 0) goto bad;
                    goto next_arg;
                case 't':
                    /* The rest of the word, or the next word, is the radix */
                    val = option_arg(p + 1, "-t", argc, argv, &i);
                    if (!val || set_radix(val) != 0) goto bad;
                    goto next_arg;
                default:
                    fprintf(stderr, "%s: unrecognized option '-%c'\n", progname, *p);
                    goto bad;
                }
            }
        }
    next_arg:
        ;
    }

    /* Default to a.out if no files given */
    if (file_count == 0) {
        if (process_file("a.out", 0) != 0)
            rc = 1;
        goto out;
    }

    for (i = 0; i < file_count; i++) {
        if (process_file(files[i], file_count > 1) != 0)
            rc = 1;
    }
    goto out;

bad:
    usage(stderr);
    rc = 1;
out:
    free(files);
    return rc;
}
