#include <elfobj.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SIZE_VERSION "0.1.0"
#define AR_MAGIC "!<arch>\n"
#define AR_THIN_MAGIC "!<thin>\n"
#define AR_MAGIC_LEN 8
#define AR_FMAG "`\n"
#define AR_HDR_LEN 60

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
            "[-d|-o|-x|--radix={8,10,16}] [-t|--totals] [--common] "
            "[--target=bfdname] [-V|--version] [-h|--help] <file>...\n",
            progname);
}

static void print_version(void) {
    printf("%s %s\n", progname, SIZE_VERSION);
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

static int parse_decimal_field(const char *field, size_t len, uint64_t *out) {
    char tmp[32];
    uint64_t v = 0;
    size_t n = 0;
    size_t i;

    if (field == NULL || out == NULL || len == 0 || len >= sizeof(tmp)) {
        return -1;
    }
    while (n < len && field[n] != '\0') {
        tmp[n] = field[n];
        n++;
    }
    while (n > 0 && (tmp[n - 1] == ' ' || tmp[n - 1] == '/')) {
        n--;
    }
    tmp[n] = '\0';
    while (*tmp == ' ') {
        memmove(tmp, tmp + 1, strlen(tmp));
    }
    if (tmp[0] == '\0') {
        *out = 0;
        return 0;
    }
    for (i = 0; tmp[i] != '\0'; ++i) {
        uint8_t digit;
        if (tmp[i] < '0' || tmp[i] > '9') {
            return -1;
        }
        digit = (uint8_t)(tmp[i] - '0');
        if (v > (UINT64_MAX - (uint64_t)digit) / 10u) {
            return -1;
        }
        v = v * 10u + (uint64_t)digit;
    }
    *out = v;
    return 0;
}

static int read_file_bytes(const char *path, uint8_t **buf_out, size_t *size_out) {
    FILE *fp;
    long sz;
    uint8_t *buf;

    if (path == NULL || buf_out == NULL || size_out == NULL) {
        return -1;
    }
    *buf_out = NULL;
    *size_out = 0;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    if ((unsigned long)sz > SIZE_MAX) {
        fclose(fp);
        return -1;
    }
    buf = (uint8_t *)malloc((size_t)sz);
    if (buf == NULL && sz != 0) {
        fclose(fp);
        return -1;
    }
    if (sz != 0 && fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    *buf_out = buf;
    *size_out = (size_t)sz;
    return 0;
}

static int is_archive_buffer(const uint8_t *buf, size_t size, int *is_thin) {
    if (is_thin != NULL) {
        *is_thin = 0;
    }
    if (buf == NULL || size < AR_MAGIC_LEN) {
        return 0;
    }
    if (memcmp(buf, AR_MAGIC, AR_MAGIC_LEN) == 0) {
        return 1;
    }
    if (memcmp(buf, AR_THIN_MAGIC, AR_MAGIC_LEN) == 0) {
        if (is_thin != NULL) {
            *is_thin = 1;
        }
        return 1;
    }
    return 0;
}

static char *dup_member_name_simple(const char name_field[16]) {
    char tmp[17];
    size_t len = 16;

    memcpy(tmp, name_field, 16);
    tmp[16] = '\0';
    while (len > 0 && (tmp[len - 1] == ' ' || tmp[len - 1] == '\0')) {
        len--;
    }
    tmp[len] = '\0';
    if (!(len > 0 && tmp[0] == '/')) {
        while (len > 0 && tmp[len - 1] == '/') {
            len--;
        }
    }
    while (len > 0 && tmp[len - 1] == ' ') {
        len--;
    }
    tmp[len] = '\0';
    return xstrdup(tmp);
}

static char *dup_gnu_long_name(const uint8_t *table, size_t table_size, uint64_t off) {
    size_t i;
    size_t start;
    size_t end;

    if (table == NULL || off >= table_size || off > SIZE_MAX) {
        return NULL;
    }
    start = (size_t)off;
    end = start;
    for (i = start; i < table_size; ++i) {
        if (table[i] == '\n' || table[i] == '\0') {
            break;
        }
        end++;
    }
    while (end > start && table[end - 1] == '/') {
        end--;
    }
    if (end <= start) {
        return NULL;
    }
    {
        size_t n = end - start;
        char *out = (char *)malloc(n + 1);
        if (out == NULL) {
            return NULL;
        }
        memcpy(out, table + start, n);
        out[n] = '\0';
        return out;
    }
}

static char *path_dirname_dup(const char *path) {
    const char *slash;
    size_t n;
    char *out;

    if (path == NULL) {
        return xstrdup(".");
    }
    slash = strrchr(path, '/');
    if (slash == NULL) {
        return xstrdup(".");
    }
    if (slash == path) {
        return xstrdup("/");
    }
    n = (size_t)(slash - path);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, path, n);
    out[n] = '\0';
    return out;
}

static char *join_paths(const char *dir, const char *leaf) {
    size_t a;
    size_t b;
    char *out;
    int need_sep;

    if (leaf == NULL) {
        return NULL;
    }
    if (leaf[0] == '/') {
        return xstrdup(leaf);
    }
    if (dir == NULL || dir[0] == '\0') {
        return xstrdup(leaf);
    }
    a = strlen(dir);
    b = strlen(leaf);
    need_sep = (a > 0 && dir[a - 1] != '/');
    out = (char *)malloc(a + (size_t)need_sep + b + 1);
    if (out == NULL) {
        return NULL;
    }
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

static int analyze_obj(elfobj_t *obj, size_report_t *report, size_classify_mode_t mode,
                       int keep_rows, int include_common) {
    size_t i;
    size_t nsec;
    size_totals_t *totals;

    if (obj == NULL || report == NULL) {
        return -1;
    }
    totals = &report->totals;

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
            return -1;
        }
    }

    if (include_common) {
        size_t nsym = elf_symbol_count(obj);
        for (i = 0; i < nsym; ++i) {
            elf_symbol_t *sym = elf_symbol_at(obj, i);
            if (sym != NULL && elf_symbol_shndx(sym) == SHN_COMMON) {
                totals->bss += elf_symbol_size(sym);
            }
        }
    }

    return 0;
}

static elf_err_t analyze_elf_path(const char *path, size_report_t *report,
                                  size_classify_mode_t mode, int keep_rows,
                                  int include_common) {
    elfobj_t *obj = NULL;
    elf_err_t err;
    int rc;

    err = elf_open(path, &obj);
    if (err != ELF_OK || obj == NULL) {
        return err;
    }
    rc = analyze_obj(obj, report, mode, keep_rows, include_common);
    elf_close(obj);
    if (rc != 0) {
        return ELF_ERR_OOM;
    }
    return ELF_OK;
}

static elf_err_t analyze_elf_memory(const uint8_t *buf, size_t size, size_report_t *report,
                                    size_classify_mode_t mode, int keep_rows,
                                    int include_common) {
    elfobj_t *obj = NULL;
    elf_err_t err;
    int rc;

    err = elf_open_memory(buf, size, &obj);
    if (err != ELF_OK || obj == NULL) {
        return err;
    }
    rc = analyze_obj(obj, report, mode, keep_rows, include_common);
    elf_close(obj);
    if (rc != 0) {
        return ELF_ERR_OOM;
    }
    return ELF_OK;
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

static void print_sysv_totals(const size_totals_t *totals, int radix) {
    char text_buf[32];
    char data_buf[32];
    char bss_buf[32];
    char total_buf[32];
    uint64_t total;

    if (totals == NULL) {
        return;
    }
    total = totals->text + totals->data + totals->bss;
    format_value(totals->text, radix, text_buf, sizeof(text_buf));
    format_value(totals->data, radix, data_buf, sizeof(data_buf));
    format_value(totals->bss, radix, bss_buf, sizeof(bss_buf));
    format_value(total, radix, total_buf, sizeof(total_buf));

    printf("total  :\n");
    printf("section              size             addr\n");
    printf("%-18s %10s %16s\n", ".text", text_buf, "0");
    printf("%-18s %10s %16s\n", ".data", data_buf, "0");
    printf("%-18s %10s %16s\n", ".bss", bss_buf, "0");
    printf("Total                %10s\n", total_buf);
}

static void accumulate_grand(size_totals_t *grand, const size_report_t *report) {
    if (grand == NULL || report == NULL) {
        return;
    }
    grand->text += report->totals.text;
    grand->data += report->totals.data;
    grand->bss += report->totals.bss;
    if (report->totals.cls == ELFOBJ_CLASS_64) {
        grand->cls = ELFOBJ_CLASS_64;
    }
}

static void emit_report_row(const char *display_name, const size_report_t *report, size_format_t format,
                            int radix, int radix_explicit, int *first_file) {
    if (format == SIZE_FORMAT_SYSV) {
        if (first_file != NULL && !*first_file) {
            printf("\n");
        }
        print_sysv_table(display_name, report, radix);
        if (first_file != NULL) {
            *first_file = 0;
        }
    } else if (format == SIZE_FORMAT_GNU) {
        print_gnu_row(display_name, &report->totals, radix);
    } else {
        print_row(display_name, &report->totals, radix, radix_explicit);
    }
}

static void print_member_diag(const char *archive_path, const char *member_name, elf_err_t err) {
    if (err == ELF_ERR_FORMAT) {
        fprintf(stderr, "%s: %s(%s): file format not recognized\n",
                progname, archive_path, member_name);
    } else {
        fprintf(stderr, "%s: %s(%s): %s\n",
                progname, archive_path, member_name, elf_errstr(err));
    }
}

static int process_archive(const char *archive_path, const uint8_t *buf, size_t size, int is_thin,
                           size_format_t format, size_classify_mode_t classify_mode, int radix,
                           int radix_explicit, int include_common, size_totals_t *grand,
                           int *printed, int *first_file, int *any_fail) {
    size_t off = AR_MAGIC_LEN;
    const uint8_t *gnu_name_table = NULL;
    size_t gnu_name_table_size = 0;
    char *archive_dir = NULL;

    if (is_thin) {
        archive_dir = path_dirname_dup(archive_path);
        if (archive_dir == NULL) {
            fprintf(stderr, "%s: %s: out of memory\n", progname, archive_path);
            if (any_fail != NULL) {
                *any_fail = 1;
            }
            return -1;
        }
    }

    while (off + AR_HDR_LEN <= size) {
        const struct ar_raw_header *hdr = (const struct ar_raw_header *)(const void *)(buf + off);
        uint64_t msize_u64 = 0;
        size_t msize;
        size_t payload_size;
        size_t payload_off;
        size_t next_off;
        char *member_name = NULL;
        char *member_label = NULL;
        char *thin_ref_path = NULL;
        const uint8_t *member_data;
        size_t member_size;
        int special_member = 0;
        int thin_ref = 0;

        if (memcmp(hdr->fmag, AR_FMAG, 2) != 0) {
            fprintf(stderr, "%s: %s: malformed archive header\n", progname, archive_path);
            if (any_fail != NULL) {
                *any_fail = 1;
            }
            free(archive_dir);
            return -1;
        }
        if (parse_decimal_field(hdr->size, sizeof(hdr->size), &msize_u64) != 0 ||
            msize_u64 > SIZE_MAX) {
            fprintf(stderr, "%s: %s: malformed archive member size\n", progname, archive_path);
            if (any_fail != NULL) {
                *any_fail = 1;
            }
            free(archive_dir);
            return -1;
        }
        msize = (size_t)msize_u64;
        payload_size = msize;
        if (is_thin) {
            if (memcmp(hdr->name, "#1/", 3) == 0) {
                uint64_t extlen_u64 = 0;
                if (parse_decimal_field(hdr->name + 3, 13, &extlen_u64) != 0 ||
                    extlen_u64 > msize) {
                    fprintf(stderr, "%s: %s: malformed extended member name\n", progname, archive_path);
                    if (any_fail != NULL) {
                        *any_fail = 1;
                    }
                    free(archive_dir);
                    return -1;
                }
                payload_size = (size_t)extlen_u64;
            } else {
                char *raw_name = dup_member_name_simple(hdr->name);
                if (raw_name == NULL) {
                    fprintf(stderr, "%s: %s: out of memory\n", progname, archive_path);
                    if (any_fail != NULL) {
                        *any_fail = 1;
                    }
                    free(archive_dir);
                    return -1;
                }
                if (strcmp(raw_name, "/") == 0 ||
                    strcmp(raw_name, "//") == 0 ||
                    strcmp(raw_name, "__.SYMDEF") == 0 ||
                    strcmp(raw_name, "__.SYMDEF SORTED") == 0 ||
                    strcmp(raw_name, "/SYM64") == 0 ||
                    strcmp(raw_name, "/SYM64/") == 0) {
                    payload_size = msize;
                } else {
                    payload_size = 0;
                }
                free(raw_name);
            }
        }
        payload_off = off + AR_HDR_LEN;
        if (payload_off > size || payload_size > size - payload_off) {
            fprintf(stderr, "%s: %s: truncated archive member\n", progname, archive_path);
            if (any_fail != NULL) {
                *any_fail = 1;
            }
            free(archive_dir);
            return -1;
        }
        member_data = buf + payload_off;
        member_size = payload_size;
        next_off = payload_off + payload_size + (payload_size & 1u);

        if (memcmp(hdr->name, "#1/", 3) == 0) {
            uint64_t extlen_u64 = 0;
            size_t extlen;

            if (parse_decimal_field(hdr->name + 3, 13, &extlen_u64) != 0 ||
                extlen_u64 > member_size) {
                fprintf(stderr, "%s: %s: malformed extended member name\n", progname, archive_path);
                if (any_fail != NULL) {
                    *any_fail = 1;
                }
                free(archive_dir);
                return -1;
            }
            extlen = (size_t)extlen_u64;
            member_name = (char *)malloc(extlen + 1);
            if (member_name == NULL) {
                fprintf(stderr, "%s: %s: out of memory\n", progname, archive_path);
                if (any_fail != NULL) {
                    *any_fail = 1;
                }
                free(archive_dir);
                return -1;
            }
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
            if (member_name == NULL) {
                fprintf(stderr, "%s: %s: out of memory\n", progname, archive_path);
                if (any_fail != NULL) {
                    *any_fail = 1;
                }
                free(archive_dir);
                return -1;
            }

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

                if (parse_decimal_field(member_name + 1, strlen(member_name) - 1, &noff) == 0) {
                    long_name = dup_gnu_long_name(gnu_name_table, gnu_name_table_size, noff);
                }
                free(member_name);
                member_name = (long_name != NULL) ? long_name : xstrdup("<bad-name>");
                if (member_name == NULL) {
                    fprintf(stderr, "%s: %s: out of memory\n", progname, archive_path);
                    if (any_fail != NULL) {
                        *any_fail = 1;
                    }
                    free(archive_dir);
                    return -1;
                }
            }

            if (is_thin && !special_member) {
                if (member_size > 0 && member_size < (size_t)(PATH_MAX * 4)) {
                    size_t trim = member_size;
                    thin_ref_path = (char *)malloc(member_size + 1);
                    if (thin_ref_path == NULL) {
                        fprintf(stderr, "%s: %s: out of memory\n", progname, archive_path);
                        if (any_fail != NULL) {
                            *any_fail = 1;
                        }
                        free(member_name);
                        free(archive_dir);
                        return -1;
                    }
                    memcpy(thin_ref_path, member_data, member_size);
                    while (trim > 0 &&
                           (thin_ref_path[trim - 1] == '\n' ||
                            thin_ref_path[trim - 1] == '\0')) {
                        trim--;
                    }
                    thin_ref_path[trim] = '\0';
                    if (trim == 0) {
                        free(thin_ref_path);
                        thin_ref_path = xstrdup(member_name);
                    }
                } else {
                    thin_ref_path = xstrdup(member_name);
                }
                thin_ref = 1;
            }
        }
        if (!special_member) {
            size_report_t report;
            elf_err_t err;

            memset(&report, 0, sizeof(report));
            report.totals.cls = ELFOBJ_CLASS_32;

            member_label = (char *)malloc(strlen(archive_path) + strlen(member_name) + 3);
            if (member_label == NULL) {
                fprintf(stderr, "%s: %s: out of memory\n", progname, archive_path);
                if (any_fail != NULL) {
                    *any_fail = 1;
                }
                free(member_name);
                free(thin_ref_path);
                free(archive_dir);
                return -1;
            }
            (void)snprintf(member_label,
                           strlen(archive_path) + strlen(member_name) + 3,
                           "%s(%s)",
                           archive_path,
                           member_name);

            if (thin_ref) {
                char *resolved = join_paths(archive_dir, thin_ref_path != NULL ? thin_ref_path : member_name);
                if (resolved == NULL) {
                    fprintf(stderr, "%s: %s: out of memory\n", progname, archive_path);
                    if (any_fail != NULL) {
                        *any_fail = 1;
                    }
                    free(member_label);
                    free(member_name);
                    free(thin_ref_path);
                    free(archive_dir);
                    return -1;
                }
                err = analyze_elf_path(resolved, &report, classify_mode,
                                       format == SIZE_FORMAT_SYSV, include_common);
                free(resolved);
            } else {
                err = analyze_elf_memory(member_data, member_size, &report, classify_mode,
                                         format == SIZE_FORMAT_SYSV, include_common);
            }

            if (err == ELF_OK) {
                emit_report_row(member_label, &report, format, radix, radix_explicit, first_file);
                accumulate_grand(grand, &report);
                if (printed != NULL) {
                    (*printed)++;
                }
            } else {
                print_member_diag(archive_path, member_name, err);
                if (err != ELF_ERR_FORMAT && any_fail != NULL) {
                    *any_fail = 1;
                }
            }
            free_report(&report);
        }

        free(member_label);
        free(member_name);
        free(thin_ref_path);
        off = next_off;
    }

    free(archive_dir);
    return 0;
}

static int process_operand(const char *path, size_format_t format, size_classify_mode_t classify_mode,
                           int radix, int radix_explicit, int include_common, size_totals_t *grand,
                           int *printed, int *first_file, int *any_fail) {
    uint8_t *buf = NULL;
    size_t sz = 0;
    int is_thin = 0;
    elf_err_t err;
    size_report_t report;

    if (read_file_bytes(path, &buf, &sz) == 0 && is_archive_buffer(buf, sz, &is_thin)) {
        int rc = process_archive(path, buf, sz, is_thin, format, classify_mode, radix, radix_explicit,
                                 include_common,
                                 grand, printed, first_file, any_fail);
        free(buf);
        return rc;
    }
    free(buf);

    memset(&report, 0, sizeof(report));
    report.totals.cls = ELFOBJ_CLASS_32;
    err = analyze_elf_path(path, &report, classify_mode, format == SIZE_FORMAT_SYSV,
                           include_common);
    if (err != ELF_OK) {
        if (err == ELF_ERR_FORMAT) {
            fprintf(stderr, "%s: %s: file format not recognized\n", progname, path);
        } else if (err == ELF_ERR_IO) {
            if (access(path, R_OK) != 0 && errno == EACCES) {
                fprintf(stderr, "%s: %s: Permission denied\n", progname, path);
            } else {
                fprintf(stderr, "%s: %s: %s\n", progname, path, elf_errstr(err));
            }
        } else {
            fprintf(stderr, "%s: %s: %s\n", progname, path, elf_errstr(err));
        }
        if (any_fail != NULL) {
            *any_fail = 1;
        }
        free_report(&report);
        return -1;
    }

    emit_report_row(path, &report, format, radix, radix_explicit, first_file);
    accumulate_grand(grand, &report);
    if (printed != NULL) {
        (*printed)++;
    }
    free_report(&report);
    return 0;
}

int main(int argc, char **argv) {
    int i;
    int any_fail = 0;
    int printed = 0;
    int file_count = 0;
    int first_file = 1;
    int radix = 10;
    int radix_explicit = 0;
    int totals_forced = 0;
    int include_common = 0;
    size_format_t format = SIZE_FORMAT_BERKELEY;
    size_classify_mode_t classify_mode = SIZE_CLASSIFY_BERKELEY;
    size_totals_t grand;

    (void)SIZE_VERSION;

    if (argc < 2) {
        usage(stderr);
        return 1;
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
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
        if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--totals") == 0) {
            totals_forced = 1;
            continue;
        }
        if (strcmp(argv[i], "--common") == 0) {
            include_common = 1;
            continue;
        }
        if (strncmp(argv[i], "--target=", 9) == 0) {
            continue;
        }
        if (strcmp(argv[i], "--target") == 0) {
            if (i + 1 < argc) {
                i++;
                continue;
            }
            usage(stderr);
            return 1;
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
        if (argv[i][0] == '-') {
            continue;
        }
        (void)process_operand(argv[i], format, classify_mode, radix, radix_explicit,
                              include_common,
                              &grand, &printed, &first_file, &any_fail);
    }

    if ((totals_forced && printed > 0) || printed > 1) {
        if (format == SIZE_FORMAT_BERKELEY) {
            print_row("total", &grand, radix, radix_explicit);
        } else if (format == SIZE_FORMAT_GNU) {
            print_gnu_row("total", &grand, radix);
        } else if (format == SIZE_FORMAT_SYSV) {
            if (!first_file) {
                printf("\n");
            }
            print_sysv_totals(&grand, radix);
        }
    }

    return any_fail ? 1 : 0;
}
