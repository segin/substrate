/*
 * size - list section sizes of ELF object files.
 *
 *   size [file...]
 *
 * Prints the Berkeley-format text/data/bss totals (and their decimal and
 * hex sum) for each ELF object, summing allocatable sections:
 *   text = ALLOC sections that are executable or read-only (with contents)
 *   data = ALLOC + WRITE sections that occupy file space
 *   bss  = ALLOC sections of type SHT_NOBITS
 *
 * The old stub printed "(stub)" and exited 0 without reading the file at
 * all.  This parses the ELF section headers for real (ELF32 and ELF64,
 * little-endian) and reports a per-file error + non-zero exit when a file
 * is missing or not an ELF object.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elf.h"

static const char *prog = "size";

#define SHF_ALLOC_     0x2
#define SHF_WRITE_     0x1
#define SHF_EXEC_      0x4
#define SHT_NOBITS_    8

/* Classify and accumulate one section's flags/type/size. */
static void
tally(uint32_t type, uint64_t flags, uint64_t sz,
      uint64_t *text, uint64_t *data, uint64_t *bss)
{
    if (!(flags & SHF_ALLOC_))
        return;
    if (type == SHT_NOBITS_)
        *bss += sz;
    else if (flags & SHF_WRITE_)
        *data += sz;
    else
        *text += sz;                 /* executable or read-only with contents */
}

static int
size_file(const char *path, int print_name)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(errno));
        return 1;
    }

    unsigned char ident[EI_NIDENT];
    if (fread(ident, 1, EI_NIDENT, f) != EI_NIDENT ||
        memcmp(ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "%s: %s: not an ELF object\n", prog, path);
        fclose(f);
        return 1;
    }
    if (ident[EI_DATA] != ELFDATA2LSB) {
        fprintf(stderr, "%s: %s: unsupported (big-endian) ELF\n", prog, path);
        fclose(f);
        return 1;
    }

    uint64_t text = 0, data = 0, bss = 0;
    int is64 = (ident[EI_CLASS] == ELFCLASS64);

    rewind(f);
    if (is64) {
        Elf64_Ehdr eh;
        if (fread(&eh, sizeof eh, 1, f) != 1 || eh.e_shentsize < sizeof(Elf64_Shdr))
            goto malformed;
        if (fseek(f, (long)eh.e_shoff, SEEK_SET) != 0) goto malformed;
        for (unsigned i = 0; i < eh.e_shnum; i++) {
            Elf64_Shdr sh;
            if (fread(&sh, sizeof sh, 1, f) != 1) goto malformed;
            tally(sh.sh_type, sh.sh_flags, sh.sh_size, &text, &data, &bss);
        }
    } else {
        Elf32_Ehdr eh;
        if (fread(&eh, sizeof eh, 1, f) != 1 || eh.e_shentsize < sizeof(Elf32_Shdr))
            goto malformed;
        if (fseek(f, (long)eh.e_shoff, SEEK_SET) != 0) goto malformed;
        for (unsigned i = 0; i < eh.e_shnum; i++) {
            Elf32_Shdr sh;
            if (fread(&sh, sizeof sh, 1, f) != 1) goto malformed;
            tally(sh.sh_type, sh.sh_flags, sh.sh_size, &text, &data, &bss);
        }
    }

    unsigned long long total = (unsigned long long)text + data + bss;
    printf("%7llu\t%7llu\t%7llu\t%7llu\t%7llx",
        (unsigned long long)text, (unsigned long long)data,
        (unsigned long long)bss, total, total);
    if (print_name)
        printf("\t%s", path);
    printf("\n");

    fclose(f);
    return 0;

malformed:
    fprintf(stderr, "%s: %s: malformed ELF section headers\n", prog, path);
    fclose(f);
    return 1;
}

int
main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s file...\n", prog);
        return 1;
    }

    printf("   text\t   data\t    bss\t    dec\t    hex\tfilename\n");

    int rc = 0;
    for (int i = 1; i < argc; i++)
        if (size_file(argv[i], 1) != 0)
            rc = 1;
    return rc;
}
