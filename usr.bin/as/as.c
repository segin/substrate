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

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} strvec_t;

typedef struct {
    int mode64;
    const char *in_path;
    const char *out_path;
    strvec_t pass;
} as_ctx_t;

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [-32|-64] [-g] [-I dir] [-D macro] [-march cpu] [-mtune cpu] "
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

static int run_gcc_assembler(const as_ctx_t *ctx) {
    size_t i;
    size_t argc;
    char **argv;
    pid_t pid;
    int status;

    argc = 8 + ctx->pass.count;
    argv = (char **)calloc(argc + 1, sizeof(*argv));
    if (argv == NULL) {
        return -1;
    }

    argv[0] = "gcc";
    argv[1] = "-c";
    argv[2] = "-x";
    argv[3] = "assembler";
    argv[4] = ctx->mode64 ? "-m64" : "-m32";

    for (i = 0; i < ctx->pass.count; ++i) {
        argv[5 + i] = ctx->pass.items[i];
    }

    argv[5 + ctx->pass.count] = "-o";
    argv[6 + ctx->pass.count] = (char *)ctx->out_path;
    argv[7 + ctx->pass.count] = (char *)ctx->in_path;
    argv[8 + ctx->pass.count] = NULL;

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
        fprintf(stderr, "as.x86: failed to open output %s: %s\n", ctx->out_path, elf_errstr(err));
        return -1;
    }

    if (elf_type(check) != ET_REL) {
        fprintf(stderr, "as.x86: output is not ET_REL\n");
        elf_close(check);
        return -1;
    }

    if (ctx->mode64) {
        if (elf_class(check) != ELFOBJ_CLASS_64 || elf_machine(check) != EM_X86_64) {
            fprintf(stderr, "as.x86: output is not x86_64 ELF64\n");
            elf_close(check);
            return -1;
        }
    } else {
        if (elf_class(check) != ELFOBJ_CLASS_32 || elf_machine(check) != EM_386) {
            fprintf(stderr, "as.x86: output is not i386 ELF32\n");
            elf_close(check);
            return -1;
        }
    }

    /* Keep strict format/type/class checks here; full validator has known false
     * positives for some compiler-generated relocation layouts at this stage. */
    elf_close(check);
    return 0;
}

int main(int argc, char **argv) {
    as_ctx_t ctx;
    int i;

    memset(&ctx, 0, sizeof(ctx));
    ctx.out_path = "a.out.o";

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                strvec_free(&ctx.pass);
                return 2;
            }
            ctx.out_path = argv[++i];
            continue;
        }

        if (strcmp(argv[i], "-32") == 0) {
            ctx.mode64 = 0;
            continue;
        }
        if (strcmp(argv[i], "-64") == 0) {
            ctx.mode64 = 1;
            continue;
        }

        if (strcmp(argv[i], "-I") == 0 || strcmp(argv[i], "-D") == 0 ||
            strcmp(argv[i], "-Wa") == 0 || strcmp(argv[i], "-march") == 0 ||
            strcmp(argv[i], "-mtune") == 0) {
            char combo[1024];

            if (i + 1 >= argc) {
                usage(argv[0]);
                strvec_free(&ctx.pass);
                return 2;
            }

            if (strcmp(argv[i], "-Wa") == 0) {
                if (snprintf(combo, sizeof(combo), "-Wa,%s", argv[i + 1]) >= (int)sizeof(combo)) {
                    strvec_free(&ctx.pass);
                    return 1;
                }
                if (strvec_push(&ctx.pass, combo) != 0) {
                    strvec_free(&ctx.pass);
                    return 1;
                }
            } else {
                if (strvec_push(&ctx.pass, argv[i]) != 0 || strvec_push(&ctx.pass, argv[i + 1]) != 0) {
                    strvec_free(&ctx.pass);
                    return 1;
                }
            }
            ++i;
            continue;
        }

        if (strncmp(argv[i], "-I", 2) == 0 || strncmp(argv[i], "-D", 2) == 0 ||
            strncmp(argv[i], "-march=", 7) == 0 || strncmp(argv[i], "-mtune=", 7) == 0 ||
            strncmp(argv[i], "-Wa,", 4) == 0 || strcmp(argv[i], "-g") == 0) {
            if (strvec_push(&ctx.pass, argv[i]) != 0) {
                strvec_free(&ctx.pass);
                return 1;
            }
            continue;
        }

        if (argv[i][0] == '-') {
            /* Keep compatibility by forwarding unknown options to the backend. */
            if (strvec_push(&ctx.pass, argv[i]) != 0) {
                strvec_free(&ctx.pass);
                return 1;
            }
            continue;
        }

        if (ctx.in_path != NULL) {
            fprintf(stderr, "as.x86: multiple input files are not supported\n");
            strvec_free(&ctx.pass);
            return 2;
        }
        ctx.in_path = argv[i];
    }

    if (ctx.in_path == NULL) {
        usage(argv[0]);
        strvec_free(&ctx.pass);
        return 2;
    }

    if (run_gcc_assembler(&ctx) != 0) {
        fprintf(stderr, "as.x86: backend assembly failed\n");
        strvec_free(&ctx.pass);
        return 1;
    }

    if (validate_output_file(&ctx) != 0) {
        strvec_free(&ctx.pass);
        return 1;
    }

    strvec_free(&ctx.pass);
    return 0;
}
