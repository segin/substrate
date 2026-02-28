#include <elfobj.h>

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const char *input_path;
    const char *output_path;
} elfedit_ctx_t;

static const char *g_progname = "elfedit";

static void warnf(const char *fmt, ...) {
    va_list ap;

    fprintf(stderr, "%s: ", g_progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void usage(FILE *out) {
    fprintf(out, "usage: %s [-o output] <input>\n", g_progname);
}

static int parse_args(int argc, char **argv, elfedit_ctx_t *ctx) {
    int ch;

    memset(ctx, 0, sizeof(*ctx));

    while ((ch = getopt(argc, argv, "ho:")) != -1) {
        switch (ch) {
        case 'h':
            usage(stdout);
            exit(0);
        case 'o':
            ctx->output_path = optarg;
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
    (void)ctx;
    (void)obj;
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

int main(int argc, char **argv) {
    elfedit_ctx_t ctx;
    elfobj_t *obj = NULL;
    int rc = 1;

    if (argv[0] != NULL && argv[0][0] != '\0') {
        const char *slash = strrchr(argv[0], '/');
        g_progname = slash != NULL ? slash + 1 : argv[0];
    }

    if (parse_args(argc, argv, &ctx) != 0) {
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
    return rc;
}
