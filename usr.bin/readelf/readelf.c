#include <elfobj.h>

#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READELF_VERSION "0.1.0"

typedef struct {
    int wide;
    int show_file_header;
    int show_program_headers;
    int show_section_headers;
} readelf_opts_t;

static const char *g_progname = "readelf";

static void warnf(const char *fmt, ...) {
    va_list ap;

    fprintf(stderr, "%s: Error: ", g_progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void usage(FILE *out) {
    fprintf(out,
            "usage: %s [options] <file>...\n"
            "  -h, --file-header       Display the ELF file header\n"
            "  -l, --program-headers   Display program headers\n"
            "  -S, --section-headers   Display section headers\n"
            "  -e, --headers           Equivalent to -h -l -S\n"
            "  -a, --all               Display all core information\n"
            "  -W, --wide              Do not limit output width\n"
            "      --help              Display this help and exit\n"
            "      --version           Display version information and exit\n",
            g_progname);
}

static void print_version(void) {
    printf("%s %s\n", g_progname, READELF_VERSION);
}

static int parse_options(int argc, char **argv, readelf_opts_t *opts) {
    static const struct option longopts[] = {
        {"file-header", no_argument, NULL, 'h'},
        {"program-headers", no_argument, NULL, 'l'},
        {"segments", no_argument, NULL, 'l'},
        {"section-headers", no_argument, NULL, 'S'},
        {"sections", no_argument, NULL, 'S'},
        {"headers", no_argument, NULL, 'e'},
        {"all", no_argument, NULL, 'a'},
        {"wide", no_argument, NULL, 'W'},
        {"help", no_argument, NULL, 1},
        {"version", no_argument, NULL, 2},
        {0, 0, 0, 0},
    };
    int ch;

    if (opts == NULL) {
        return -1;
    }
    memset(opts, 0, sizeof(*opts));

    optind = 1;
    while ((ch = getopt_long(argc, argv, "hlSeaW", longopts, NULL)) != -1) {
        switch (ch) {
            case 'h':
                opts->show_file_header = 1;
                break;
            case 'l':
                opts->show_program_headers = 1;
                break;
            case 'S':
                opts->show_section_headers = 1;
                break;
            case 'e':
                opts->show_file_header = 1;
                opts->show_program_headers = 1;
                opts->show_section_headers = 1;
                break;
            case 'a':
                opts->show_file_header = 1;
                opts->show_program_headers = 1;
                opts->show_section_headers = 1;
                break;
            case 'W':
                opts->wide = 1;
                break;
            case 1:
                usage(stdout);
                return 1;
            case 2:
                print_version();
                return 1;
            default:
                usage(stderr);
                return -1;
        }
    }
    return 0;
}

static int open_elf(const char *path, elfobj_t **out_obj) {
    elf_err_t err;

    if (path == NULL || out_obj == NULL) {
        return -1;
    }
    *out_obj = NULL;
    err = elf_open(path, out_obj);
    if (err != ELF_OK || *out_obj == NULL) {
        if (err == ELF_ERR_FORMAT) {
            warnf("'%s': Not an ELF file", path);
        } else {
            warnf("'%s': %s", path, elf_errstr(err));
        }
        return -1;
    }
    return 0;
}

static int process_file(const char *path, const readelf_opts_t *opts, int multiple_files) {
    elfobj_t *obj = NULL;
    int rc = 0;

    (void)opts;

    if (path == NULL) {
        return -1;
    }
    if (open_elf(path, &obj) != 0) {
        return -1;
    }

    if (multiple_files) {
        printf("\nFile: %s\n", path);
    }

    printf("ELF file '%s' opened successfully (class=%s, endian=%s).\n",
           path,
           elf_class(obj) == ELFOBJ_CLASS_64 ? "ELF64" :
           (elf_class(obj) == ELFOBJ_CLASS_32 ? "ELF32" : "unknown"),
           elf_endian(obj) == ELFOBJ_ENDIAN_BE ? "big" :
           (elf_endian(obj) == ELFOBJ_ENDIAN_LE ? "little" : "unknown"));

    elf_close(obj);
    return rc;
}

int main(int argc, char **argv) {
    readelf_opts_t opts;
    int parse_rc;
    int rc = 0;
    int i;
    int input_count;

    if (argv != NULL && argv[0] != NULL && argv[0][0] != '\0') {
        g_progname = argv[0];
    }

    parse_rc = parse_options(argc, argv, &opts);
    if (parse_rc > 0) {
        return 0;
    }
    if (parse_rc < 0) {
        return 1;
    }

    input_count = argc - optind;
    if (input_count <= 0) {
        usage(stderr);
        return 1;
    }

    for (i = optind; i < argc; ++i) {
        if (process_file(argv[i], &opts, input_count > 1) != 0) {
            rc = 1;
        }
    }

    return rc;
}
