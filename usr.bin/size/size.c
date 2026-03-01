#include <elfobj.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIZE_VERSION "0.1.0"

typedef struct {
    uint64_t text;
    uint64_t data;
    uint64_t bss;
    elfobj_class_t cls;
} size_totals_t;

typedef struct {
    char *name;
    uint64_t size;
    uint64_t addr;
} size_section_row_t;

typedef struct {
    size_totals_t totals;
    size_section_row_t *rows;
    size_t row_count;
    size_t row_cap;
} size_report_t;

typedef enum {
    SIZE_CLASSIFY_BERKELEY = 0,
    SIZE_CLASSIFY_SYSV = 1
} size_classify_mode_t;

typedef enum {
    SIZE_FORMAT_BERKELEY = 0,
    SIZE_FORMAT_SYSV = 1,
    SIZE_FORMAT_GNU = 2
} size_format_t;

static const char *progname = "size";

static void usage(FILE *out) {
    fprintf(out,
            "usage: %s [-A|-B|--format={sysv,berkeley,gnu}] "
            "[-d|-o|-x|--radix={8,10,16}] <file>...\n",
            progname);
}

static char *xstrdup(const char *s) {
    size_t n;
    char *out;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    out = (char *)malloc(n);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, s, n);
    return out;
}

static void free_report(size_report_t *report) {
    size_t i;

    if (report == NULL) {
        return;
    }
    for (i = 0; i < report->row_count; ++i) {
        free(report->rows[i].name);
    }
    free(report->rows);
    report->rows = NULL;
    report->row_count = 0;
    report->row_cap = 0;
}

static int push_row(size_report_t *report, const char *name, uint64_t size, uint64_t addr) {
    size_section_row_t *next;
    size_t cap;

    if (report == NULL || name == NULL) {
        return -1;
    }
    if (report->row_count == report->row_cap) {
        cap = (report->row_cap == 0) ? 16 : report->row_cap * 2;
        next = (size_section_row_t *)realloc(report->rows, cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        report->rows = next;
        report->row_cap = cap;
    }
    report->rows[report->row_count].name = xstrdup(name);
    if (report->rows[report->row_count].name == NULL) {
        return -1;
    }
    report->rows[report->row_count].size = size;
    report->rows[report->row_count].addr = addr;
    report->row_count++;
    return 0;
}

static int parse_radix(const char *arg) {
    if (arg == NULL) {
        return -1;
    }
    if (strcmp(arg, "8") == 0) {
        return 8;
    }
    if (strcmp(arg, "10") == 0) {
        return 10;
    }
    if (strcmp(arg, "16") == 0) {
        return 16;
    }
    return -1;
}

static void format_value(uint64_t value, int radix, char *buf, size_t buflen) {
    if (buf == NULL || buflen == 0) {
        return;
    }
    if (radix == 8) {
        (void)snprintf(buf, buflen, "%llo", (unsigned long long)value);
    } else if (radix == 16) {
        (void)snprintf(buf, buflen, "%llx", (unsigned long long)value);
    } else {
        (void)snprintf(buf, buflen, "%llu", (unsigned long long)value);
    }
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

static int analyze_elf(const char *path, size_report_t *report, size_classify_mode_t mode,
                       int keep_rows) {
    elfobj_t *obj = NULL;
    size_t i;
    size_t nsec;
    elf_err_t err;
    size_totals_t *totals;

    if (path == NULL || report == NULL) {
        return -1;
    }
    totals = &report->totals;

    err = elf_open(path, &obj);
    if (err != ELF_OK || obj == NULL) {
        fprintf(stderr, "%s: %s: %s\n", progname, path, elf_errstr(err));
        return -1;
    }

    totals->cls = elf_class(obj);
    nsec = elf_section_count(obj);
    for (i = 0; i < nsec; ++i) {
        elf_section_t *sec = elf_section_get(obj, i);
        uint64_t flags;
        const char *name;

        classify_section(sec, totals, mode);
        if (!keep_rows || sec == NULL) {
            continue;
        }
        flags = elf_section_flags(sec);
        if ((flags & SHF_ALLOC) == 0) {
            continue;
        }
        name = elf_section_name(sec);
        if (name == NULL || name[0] == '\0') {
            name = "<unnamed>";
        }
        if (push_row(report, name, elf_section_size(sec), elf_section_addr(sec)) != 0) {
            fprintf(stderr, "%s: %s: out of memory\n", progname, path);
            elf_close(obj);
            return -1;
        }
    }

    elf_close(obj);
    return 0;
}

static void print_row(const char *path, const size_totals_t *totals, int radix, int radix_explicit) {
    uint64_t total;
    int hex_width;
    char text_buf[32];
    char data_buf[32];
    char bss_buf[32];
    char total_a_buf[32];
    char total_b_buf[32];

    total = totals->text + totals->data + totals->bss;
    hex_width = (totals->cls == ELFOBJ_CLASS_64) ? 16 : 8;
    format_value(totals->text, radix, text_buf, sizeof(text_buf));
    format_value(totals->data, radix, data_buf, sizeof(data_buf));
    format_value(totals->bss, radix, bss_buf, sizeof(bss_buf));
    format_value(total, radix, total_a_buf, sizeof(total_a_buf));
    if (radix_explicit) {
        format_value(total, radix, total_b_buf, sizeof(total_b_buf));
        printf("%7s %7s %7s %7s %*s %s\n",
               text_buf,
               data_buf,
               bss_buf,
               total_a_buf,
               hex_width,
               total_b_buf,
               path);
    } else {
        (void)snprintf(total_b_buf, sizeof(total_b_buf), "%0*llx",
                       hex_width,
                       (unsigned long long)total);
        printf("%7s %7s %7s %7s %*s %s\n",
               text_buf,
               data_buf,
               bss_buf,
               total_a_buf,
               hex_width,
               total_b_buf,
               path);
    }
}

static void print_gnu_row(const char *path, const size_totals_t *totals, int radix) {
    uint64_t total;
    char text_buf[32];
    char data_buf[32];
    char bss_buf[32];
    char total_buf[32];

    total = totals->text + totals->data + totals->bss;
    format_value(totals->text, radix, text_buf, sizeof(text_buf));
    format_value(totals->data, radix, data_buf, sizeof(data_buf));
    format_value(totals->bss, radix, bss_buf, sizeof(bss_buf));
    format_value(total, radix, total_buf, sizeof(total_buf));
    printf("%10s %10s %10s %10s %s\n",
           text_buf,
           data_buf,
           bss_buf,
           total_buf,
           path);
}

static void print_sysv_table(const char *path, const size_report_t *report, int radix) {
    size_t i;
    uint64_t total = 0;
    char size_buf[32];
    char addr_buf[32];
    char total_buf[32];

    printf("%s  :\n", path);
    printf("section              size             addr\n");
    for (i = 0; i < report->row_count; ++i) {
        const size_section_row_t *row = &report->rows[i];
        total += row->size;
        format_value(row->size, radix, size_buf, sizeof(size_buf));
        format_value(row->addr, radix, addr_buf, sizeof(addr_buf));
        printf("%-18s %10s %16s\n",
               row->name,
               size_buf,
               addr_buf);
    }
    format_value(total, radix, total_buf, sizeof(total_buf));
    printf("Total                %10s\n\n", total_buf);
}

int main(int argc, char **argv) {
    int i;
    int any_fail = 0;
    int printed = 0;
    int file_count = 0;
    int first_file = 1;
    int radix = 10;
    int radix_explicit = 0;
    size_format_t format = SIZE_FORMAT_BERKELEY;
    size_classify_mode_t classify_mode = SIZE_CLASSIFY_BERKELEY;
    size_totals_t grand;

    (void)SIZE_VERSION;

    if (argc < 2) {
        usage(stderr);
        return 1;
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-A") == 0 || strcmp(argv[i], "--format=sysv") == 0) {
            format = SIZE_FORMAT_SYSV;
            classify_mode = SIZE_CLASSIFY_SYSV;
            continue;
        }
        if (strcmp(argv[i], "--format=gnu") == 0) {
            format = SIZE_FORMAT_GNU;
            classify_mode = SIZE_CLASSIFY_BERKELEY;
            continue;
        }
        if (strcmp(argv[i], "-d") == 0) {
            radix = 10;
            radix_explicit = 1;
            continue;
        }
        if (strcmp(argv[i], "-o") == 0) {
            radix = 8;
            radix_explicit = 1;
            continue;
        }
        if (strcmp(argv[i], "-x") == 0) {
            radix = 16;
            radix_explicit = 1;
            continue;
        }
        if (strncmp(argv[i], "--radix=", 8) == 0) {
            int parsed = parse_radix(argv[i] + 8);
            if (parsed < 0) {
                usage(stderr);
                return 1;
            }
            radix = parsed;
            radix_explicit = 1;
            continue;
        }
        if (strcmp(argv[i], "-B") == 0 || strcmp(argv[i], "--format=berkeley") == 0) {
            format = SIZE_FORMAT_BERKELEY;
            classify_mode = SIZE_CLASSIFY_BERKELEY;
            continue;
        }
        if (argv[i][0] == '-') {
            usage(stderr);
            return 1;
        }
        file_count++;
    }
    if (file_count == 0) {
        usage(stderr);
        return 1;
    }

    grand.text = 0;
    grand.data = 0;
    grand.bss = 0;
    grand.cls = ELFOBJ_CLASS_32;

    if (format == SIZE_FORMAT_BERKELEY) {
        printf("   text    data     bss     dec     hex filename\n");
    } else if (format == SIZE_FORMAT_GNU) {
        printf("      text       data        bss      total filename\n");
    }

    for (i = 1; i < argc; ++i) {
        size_report_t report;

        if (argv[i][0] == '-') {
            continue;
        }

        memset(&report, 0, sizeof(report));
        report.totals.cls = ELFOBJ_CLASS_32;

        if (analyze_elf(argv[i], &report, classify_mode, format == SIZE_FORMAT_SYSV) != 0) {
            any_fail = 1;
            free_report(&report);
            continue;
        }

        if (format == SIZE_FORMAT_SYSV) {
            if (!first_file) {
                printf("\n");
            }
            print_sysv_table(argv[i], &report, radix);
            first_file = 0;
        } else if (format == SIZE_FORMAT_GNU) {
            print_gnu_row(argv[i], &report.totals, radix);
        } else {
            print_row(argv[i], &report.totals, radix, radix_explicit);
        }
        printed++;

        grand.text += report.totals.text;
        grand.data += report.totals.data;
        grand.bss += report.totals.bss;
        if (report.totals.cls == ELFOBJ_CLASS_64) {
            grand.cls = ELFOBJ_CLASS_64;
        }

        free_report(&report);
    }

    if (printed > 1) {
        if (format == SIZE_FORMAT_BERKELEY) {
            print_row("total", &grand, radix, radix_explicit);
        } else if (format == SIZE_FORMAT_GNU) {
            print_gnu_row("total", &grand, radix);
        }
    }

    return any_fail ? 1 : 0;
}
