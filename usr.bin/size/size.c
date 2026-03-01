#include <elfobj.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SIZE_VERSION "0.1.0"

typedef struct {
    uint64_t text;
    uint64_t data;
    uint64_t bss;
    elfobj_class_t cls;
} size_totals_t;

typedef enum {
    SIZE_CLASSIFY_BERKELEY = 0,
    SIZE_CLASSIFY_SYSV = 1
} size_classify_mode_t;

static const char *progname = "size";

static void usage(FILE *out) {
    fprintf(out, "usage: %s <file>...\n", progname);
}

static void classify_section(const elf_section_t *sec, size_totals_t *totals,
                             size_classify_mode_t mode) {
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_size;
    int is_alloc;
    int is_exec;
    int is_write;
    int is_tls;
    int is_nobits;

    if (sec == NULL || totals == NULL) {
        return;
    }

    sh_type = elf_section_type(sec);
    sh_flags = elf_section_flags(sec);
    sh_size = elf_section_size(sec);

    is_alloc = ((sh_flags & SHF_ALLOC) != 0);
    is_exec = ((sh_flags & SHF_EXECINSTR) != 0);
    is_write = ((sh_flags & SHF_WRITE) != 0);
    is_tls = ((sh_flags & SHF_TLS) != 0);
    is_nobits = (sh_type == SHT_NOBITS);

    if (!is_alloc) {
        return;
    }

    if (is_exec) {
        totals->text += sh_size;
        return;
    }

    if (is_write || is_tls) {
        if (is_nobits) {
            totals->bss += sh_size;
        } else {
            totals->data += sh_size;
        }
        return;
    }

    if (!is_nobits && mode == SIZE_CLASSIFY_SYSV) {
        totals->data += sh_size;
    } else {
        totals->text += sh_size;
    }
}

static int analyze_elf(const char *path, size_totals_t *totals, size_classify_mode_t mode) {
    elfobj_t *obj = NULL;
    size_t i;
    size_t nsec;
    elf_err_t err;

    if (path == NULL || totals == NULL) {
        return -1;
    }

    err = elf_open(path, &obj);
    if (err != ELF_OK || obj == NULL) {
        fprintf(stderr, "%s: %s: %s\n", progname, path, elf_errstr(err));
        return -1;
    }

    totals->cls = elf_class(obj);
    nsec = elf_section_count(obj);
    for (i = 0; i < nsec; ++i) {
        elf_section_t *sec = elf_section_get(obj, i);
        classify_section(sec, totals, mode);
    }

    elf_close(obj);
    return 0;
}

static void print_row(const char *path, const size_totals_t *totals) {
    uint64_t total;
    int hex_width;

    total = totals->text + totals->data + totals->bss;
    hex_width = (totals->cls == ELFOBJ_CLASS_64) ? 16 : 8;
    printf("%7llu %7llu %7llu %7llu %0*llx %s\n",
           (unsigned long long)totals->text,
           (unsigned long long)totals->data,
           (unsigned long long)totals->bss,
           (unsigned long long)total,
           hex_width,
           (unsigned long long)total,
           path);
}

int main(int argc, char **argv) {
    int i;
    int any_fail = 0;
    int printed = 0;
    size_totals_t grand;

    (void)SIZE_VERSION;

    if (argc < 2) {
        usage(stderr);
        return 1;
    }

    grand.text = 0;
    grand.data = 0;
    grand.bss = 0;
    grand.cls = ELFOBJ_CLASS_32;

    printf("   text    data     bss     dec     hex filename\n");
    for (i = 1; i < argc; ++i) {
        size_totals_t totals;

        totals.text = 0;
        totals.data = 0;
        totals.bss = 0;
        totals.cls = ELFOBJ_CLASS_32;

        if (analyze_elf(argv[i], &totals, SIZE_CLASSIFY_BERKELEY) != 0) {
            any_fail = 1;
            continue;
        }
        print_row(argv[i], &totals);
        printed++;

        grand.text += totals.text;
        grand.data += totals.data;
        grand.bss += totals.bss;
        if (totals.cls == ELFOBJ_CLASS_64) {
            grand.cls = ELFOBJ_CLASS_64;
        }
    }

    if (printed > 1) {
        print_row("total", &grand);
    }

    return any_fail ? 1 : 0;
}
