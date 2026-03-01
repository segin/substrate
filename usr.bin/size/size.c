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

static const char *progname = "size";

static void usage(FILE *out) {
    fprintf(out, "usage: %s <file>...\n", progname);
}

static void classify_section(const elf_section_t *sec, size_totals_t *totals) {
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_size;

    if (sec == NULL || totals == NULL) {
        return;
    }

    sh_type = elf_section_type(sec);
    sh_flags = elf_section_flags(sec);
    sh_size = elf_section_size(sec);

    if ((sh_flags & SHF_ALLOC) == 0) {
        return;
    }

    if ((sh_flags & SHF_EXECINSTR) != 0) {
        totals->text += sh_size;
        return;
    }

    if ((sh_flags & SHF_WRITE) != 0) {
        if (sh_type == SHT_NOBITS) {
            totals->bss += sh_size;
        } else {
            totals->data += sh_size;
        }
        return;
    }

    totals->text += sh_size;
}

static int analyze_elf(const char *path, size_totals_t *totals) {
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
        classify_section(sec, totals);
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

    (void)SIZE_VERSION;

    if (argc < 2) {
        usage(stderr);
        return 1;
    }

    printf("   text    data     bss     dec     hex filename\n");
    for (i = 1; i < argc; ++i) {
        size_totals_t totals;

        totals.text = 0;
        totals.data = 0;
        totals.bss = 0;
        totals.cls = ELFOBJ_CLASS_32;

        if (analyze_elf(argv[i], &totals) != 0) {
            any_fail = 1;
            continue;
        }
        print_row(argv[i], &totals);
    }

    return any_fail ? 1 : 0;
}
