#include "elfobj.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef EM_X86_64
#define EM_X86_64 62
#endif

#define AS_MODE_AUTO (-1)
#define AS_MODE_32 0
#define AS_MODE_64 1

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} strvec_t;

typedef struct {
    int mode;
    const char *in_path;
    const char *out_path;
    const char *self_path;
    const char *march;
    strvec_t gcc_opts;
    strvec_t as_opts;
} as_ctx_t;

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [-32|-64] [-g] [-I dir] [-D name[=value]] [-march cpu] [-mtune cpu] "
            "[-Wa opts] [-o output] input.s\n",
            prog);
}

static char *xstrdup(const char *s) {
    size_t n;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

static int strvec_push(strvec_t *v, const char *s) {
    char **next;

    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 16 : v->cap * 2;
        next = (char **)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }

    v->items[v->count] = xstrdup(s);
    if (v->items[v->count] == NULL) {
        return -1;
    }
    v->count++;
    return 0;
}

static void strvec_free(strvec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        free(v->items[i]);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static int push_opt_with_value(strvec_t *v, const char *opt, const char *value) {
    size_t olen;
    size_t vlen;
    char *buf;
    int rc;

    if (opt == NULL || value == NULL) {
        return -1;
    }
    olen = strlen(opt);
    vlen = strlen(value);
    buf = (char *)malloc(olen + vlen + 1);
    if (buf == NULL) {
        return -1;
    }
    memcpy(buf, opt, olen);
    memcpy(buf + olen, value, vlen);
    buf[olen + vlen] = '\0';

    rc = strvec_push(v, buf);
    free(buf);
    return rc;
}

static int strvec_push_csv(strvec_t *v, const char *csv) {
    char *tmp;
    char *tok;
    char *saveptr;
    int pushed = 0;

    if (csv == NULL) {
        return -1;
    }
    tmp = xstrdup(csv);
    if (tmp == NULL) {
        return -1;
    }

    for (tok = strtok_r(tmp, ",", &saveptr); tok != NULL; tok = strtok_r(NULL, ",", &saveptr)) {
        if (tok[0] == '\0') {
            continue;
        }
        if (strvec_push(v, tok) != 0) {
            free(tmp);
            return -1;
        }
        pushed = 1;
    }

    free(tmp);
    return pushed ? 0 : -1;
}

static int infer_mode_from_prog(const char *prog) {
    const char *base;

    if (prog == NULL) {
        return AS_MODE_AUTO;
    }

    base = strrchr(prog, '/');
    base = base == NULL ? prog : base + 1;

    if (strcmp(base, "as.x64") == 0) {
        return AS_MODE_64;
    }
    if (strcmp(base, "as.x86") == 0) {
        return AS_MODE_32;
    }
    return AS_MODE_AUTO;
}

static int infer_mode_from_march(const char *march) {
    if (march == NULL || march[0] == '\0') {
        return AS_MODE_AUTO;
    }
    if (strncmp(march, "x86-64", 6) == 0 || strcmp(march, "amd64") == 0 || strcmp(march, "generic64") == 0) {
        return AS_MODE_64;
    }
    if (strcmp(march, "i386") == 0 || strcmp(march, "i486") == 0 || strcmp(march, "i586") == 0 ||
        strcmp(march, "i686") == 0 || strcmp(march, "generic32") == 0) {
        return AS_MODE_32;
    }
    return AS_MODE_AUTO;
}

static int infer_mode(const as_ctx_t *ctx) {
    int mode;

    mode = infer_mode_from_prog(ctx->self_path);
    if (mode != AS_MODE_AUTO) {
        return mode;
    }

    mode = infer_mode_from_march(ctx->march);
    if (mode != AS_MODE_AUTO) {
        return mode;
    }

#if defined(__x86_64__) || defined(__amd64__)
    return AS_MODE_64;
#else
    return AS_MODE_32;
#endif
}

static int is_march_supported_64(const char *march) {
    static const char *const allow[] = {
        "x86-64",
        "x86-64-v1",
        "x86-64-v2",
        "x86-64-v3",
        "x86-64-v4",
        "generic",
        "generic64",
        "native",
        "amd64",
    };
    size_t i;

    for (i = 0; i < sizeof(allow) / sizeof(allow[0]); ++i) {
        if (strcmp(march, allow[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_march_supported_32(const char *march) {
    static const char *const allow[] = {
        "i386",
        "i486",
        "i586",
        "i686",
        "pentium",
        "pentiumpro",
        "pentium4",
        "athlon",
        "generic",
        "generic32",
        "native",
    };
    size_t i;

    for (i = 0; i < sizeof(allow) / sizeof(allow[0]); ++i) {
        if (strcmp(march, allow[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int validate_march(int mode, const char *march) {
    if (march == NULL || march[0] == '\0') {
        return 0;
    }
    if (mode == AS_MODE_64) {
        return is_march_supported_64(march) ? 0 : -1;
    }
    return is_march_supported_32(march) ? 0 : -1;
}

static const char *march_to_gas(int mode, const char *march) {
    if (march == NULL || march[0] == '\0') {
        return NULL;
    }

    if (mode == AS_MODE_64) {
        if (strcmp(march, "x86-64") == 0 || strcmp(march, "x86-64-v1") == 0 || strcmp(march, "amd64") == 0) {
            return "x86-64";
        }
        if (strcmp(march, "x86-64-v2") == 0) {
            return "core2";
        }
        if (strcmp(march, "x86-64-v3") == 0 || strcmp(march, "x86-64-v4") == 0) {
            return "znver1";
        }
        if (strcmp(march, "generic") == 0 || strcmp(march, "generic64") == 0 || strcmp(march, "native") == 0) {
            return NULL;
        }
        return march;
    }

    if (strcmp(march, "generic") == 0 || strcmp(march, "generic32") == 0 || strcmp(march, "native") == 0) {
        return NULL;
    }
    return march;
}

static const char *backend_compiler(void) {
    const char *override = getenv("AS_BACKEND");
    const char *cc = getenv("CC");

    if (override != NULL && override[0] != '\0') {
        return override;
    }
    if (cc != NULL && cc[0] != '\0') {
        return cc;
    }
    return "gcc";
}

static int run_backend(const as_ctx_t *ctx) {
    size_t i;
    size_t argc;
    char **argv;
    size_t at = 0;
    const char *cc_prog;
    pid_t pid;
    int status;

    cc_prog = backend_compiler();
    argc = 8 + ctx->gcc_opts.count + (ctx->as_opts.count * 2);
    argv = (char **)calloc(argc + 1, sizeof(*argv));
    if (argv == NULL) {
        return -1;
    }

    argv[at++] = (char *)cc_prog;
    argv[at++] = "-c";
    argv[at++] = "-x";
    argv[at++] = "assembler-with-cpp";
    argv[at++] = ctx->mode == AS_MODE_64 ? "-m64" : "-m32";

    for (i = 0; i < ctx->gcc_opts.count; ++i) {
        argv[at++] = ctx->gcc_opts.items[i];
    }
    for (i = 0; i < ctx->as_opts.count; ++i) {
        argv[at++] = "-Xassembler";
        argv[at++] = ctx->as_opts.items[i];
    }

    argv[at++] = "-o";
    argv[at++] = (char *)ctx->out_path;
    argv[at++] = (char *)ctx->in_path;
    argv[at] = NULL;

    pid = fork();
    if (pid < 0) {
        free(argv);
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) {
        free(argv);
        return -1;
    }
    free(argv);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }
    return 0;
}

static int validate_output_file(const as_ctx_t *ctx) {
    elfobj_t *check = NULL;
    elf_err_t err;

    err = elf_open(ctx->out_path, &check);
    if (err != ELF_OK) {
        fprintf(stderr, "as: failed to open output %s: %s\n", ctx->out_path, elf_errstr(err));
        return -1;
    }

    if (elf_type(check) != ET_REL) {
        fprintf(stderr, "as: output is not ET_REL\n");
        elf_close(check);
        return -1;
    }

    if (ctx->mode == AS_MODE_64) {
        if (elf_class(check) != ELFOBJ_CLASS_64 || elf_machine(check) != EM_X86_64) {
            fprintf(stderr, "as: output is not x86_64 ELF64\n");
            elf_close(check);
            return -1;
        }
    } else {
        if (elf_class(check) != ELFOBJ_CLASS_32 || elf_machine(check) != EM_386) {
            fprintf(stderr, "as: output is not i386 ELF32\n");
            elf_close(check);
            return -1;
        }
    }

    elf_close(check);
    return 0;
}

int main(int argc, char **argv) {
    as_ctx_t ctx;
    int i;
    int query_version = 0;

    memset(&ctx, 0, sizeof(ctx));
    ctx.mode = AS_MODE_AUTO;
    ctx.out_path = "a.out.o";
    ctx.self_path = argv[0];

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
            query_version = 1;
            continue;
        }
        if (strcmp(arg, "-o") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 2;
            }
            ctx.out_path = argv[++i];
            continue;
        }
        if (strcmp(arg, "-32") == 0 || strcmp(arg, "--32") == 0) {
            ctx.mode = AS_MODE_32;
            continue;
        }
        if (strcmp(arg, "-64") == 0 || strcmp(arg, "--64") == 0) {
            ctx.mode = AS_MODE_64;
            continue;
        }
        if (strcmp(arg, "-g") == 0) {
            if (strvec_push(&ctx.gcc_opts, arg) != 0) {
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 1;
            }
            continue;
        }
        if (strcmp(arg, "-I") == 0 || strcmp(arg, "-D") == 0 || strcmp(arg, "-march") == 0 ||
            strcmp(arg, "-mtune") == 0) {
            const char *value;

            if (i + 1 >= argc) {
                usage(argv[0]);
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 2;
            }
            value = argv[++i];
            if (strcmp(arg, "-march") == 0) {
                ctx.march = value;
                if (push_opt_with_value(&ctx.gcc_opts, "-march=", value) != 0) {
                    strvec_free(&ctx.gcc_opts);
                    strvec_free(&ctx.as_opts);
                    return 1;
                }
                continue;
            }
            if (strcmp(arg, "-mtune") == 0) {
                if (push_opt_with_value(&ctx.gcc_opts, "-mtune=", value) != 0) {
                    strvec_free(&ctx.gcc_opts);
                    strvec_free(&ctx.as_opts);
                    return 1;
                }
                continue;
            }
            if (push_opt_with_value(&ctx.gcc_opts, strcmp(arg, "-I") == 0 ? "-I" : "-D", value) != 0) {
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 1;
            }
            continue;
        }
        if (strcmp(arg, "-Wa") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 2;
            }
            if (strvec_push_csv(&ctx.as_opts, argv[++i]) != 0) {
                fprintf(stderr, "as: invalid -Wa argument\n");
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 2;
            }
            continue;
        }
        if (strncmp(arg, "-Wa,", 4) == 0) {
            if (strvec_push_csv(&ctx.as_opts, arg + 4) != 0) {
                fprintf(stderr, "as: invalid -Wa argument\n");
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 2;
            }
            continue;
        }
        if (strncmp(arg, "-march=", 7) == 0) {
            ctx.march = arg + 7;
            if (strvec_push(&ctx.gcc_opts, arg) != 0) {
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 1;
            }
            continue;
        }
        if (strncmp(arg, "-mtune=", 7) == 0) {
            if (strvec_push(&ctx.gcc_opts, arg) != 0) {
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 1;
            }
            continue;
        }
        if (strncmp(arg, "-I", 2) == 0 || strncmp(arg, "-D", 2) == 0) {
            if (strvec_push(&ctx.gcc_opts, arg) != 0) {
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 1;
            }
            continue;
        }

        if (arg[0] == '-') {
            if (strvec_push(&ctx.as_opts, arg) != 0) {
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 1;
            }
            continue;
        }

        if (ctx.in_path != NULL) {
            fprintf(stderr, "as: multiple input files are not supported\n");
            strvec_free(&ctx.gcc_opts);
            strvec_free(&ctx.as_opts);
            return 2;
        }
        ctx.in_path = arg;
    }

    if (query_version) {
        printf("GNU assembler (GNU Binutils) 2.40\n");
        strvec_free(&ctx.gcc_opts);
        strvec_free(&ctx.as_opts);
        return 0;
    }

    if (ctx.in_path == NULL) {
        usage(argv[0]);
        strvec_free(&ctx.gcc_opts);
        strvec_free(&ctx.as_opts);
        return 2;
    }
    if (ctx.mode == AS_MODE_AUTO) {
        ctx.mode = infer_mode(&ctx);
    }
    if (validate_march(ctx.mode, ctx.march) != 0) {
        fprintf(stderr, "as: unsupported -march=%s for %s mode\n", ctx.march,
                ctx.mode == AS_MODE_64 ? "64-bit" : "32-bit");
        strvec_free(&ctx.gcc_opts);
        strvec_free(&ctx.as_opts);
        return 2;
    }
    if (ctx.march != NULL) {
        const char *gas_march = march_to_gas(ctx.mode, ctx.march);
        if (gas_march != NULL && push_opt_with_value(&ctx.as_opts, "-march=", gas_march) != 0) {
            strvec_free(&ctx.gcc_opts);
            strvec_free(&ctx.as_opts);
            return 1;
        }
    }
    if (run_backend(&ctx) != 0) {
        fprintf(stderr, "as: backend assembly failed\n");
        strvec_free(&ctx.gcc_opts);
        strvec_free(&ctx.as_opts);
        return 1;
    }
    if (validate_output_file(&ctx) != 0) {
        strvec_free(&ctx.gcc_opts);
        strvec_free(&ctx.as_opts);
        return 1;
    }

    strvec_free(&ctx.gcc_opts);
    strvec_free(&ctx.as_opts);
    return 0;
}
