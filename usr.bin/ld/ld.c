#include "elfobj.h"
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
    int mode; /* 32 or 64 */
    uint16_t expect_type;
    const char *out_path;
    strvec_t pass;
    strvec_t inputs;
} ld_ctx_t;

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [-m32|-m64] [-r|-shared|-pie|-static] [-o output] "
            "[-L dir] [-l name] [-T script] [--gc-sections] [--strip-all] "
            "[--allow-undefined] input...\n",
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

static int infer_mode_from_inputs(ld_ctx_t *ctx) {
    size_t i;

    for (i = 0; i < ctx->inputs.count; ++i) {
        elfobj_t *obj = NULL;
        if (elf_open(ctx->inputs.items[i], &obj) != ELF_OK) {
            continue;
        }
        if (elf_class(obj) == ELFOBJ_CLASS_64 || elf_machine(obj) == EM_X86_64) {
            ctx->mode = 64;
            elf_close(obj);
            return 0;
        }
        if (elf_class(obj) == ELFOBJ_CLASS_32 || elf_machine(obj) == EM_386) {
            ctx->mode = 32;
            elf_close(obj);
            return 0;
        }
        elf_close(obj);
    }

    ctx->mode = 32;
    return 0;
}

static int run_ld_backend(const ld_ctx_t *ctx) {
    const char *emu = ctx->mode == 64 ? "elf_x86_64" : "elf_i386";
    size_t argc = 4 + ctx->pass.count;
    char **argv = (char **)calloc(argc + 1, sizeof(*argv));
    pid_t pid;
    int status;
    size_t i;

    if (argv == NULL) {
        return -1;
    }

    argv[0] = "ld";
    argv[1] = "-m";
    argv[2] = (char *)emu;

    for (i = 0; i < ctx->pass.count; ++i) {
        argv[3 + i] = ctx->pass.items[i];
    }
    argv[3 + ctx->pass.count] = NULL;

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

static int validate_output(const ld_ctx_t *ctx) {
    elfobj_t *obj = NULL;

    if (elf_open(ctx->out_path, &obj) != ELF_OK) {
        fprintf(stderr, "ld.x86: failed to open output %s\n", ctx->out_path);
        return -1;
    }

    if (ctx->expect_type != 0 && elf_type(obj) != ctx->expect_type) {
        fprintf(stderr, "ld.x86: wrong output ELF type\n");
        elf_close(obj);
        return -1;
    }

    if (ctx->mode == 64) {
        if (elf_class(obj) != ELFOBJ_CLASS_64 || elf_machine(obj) != EM_X86_64) {
            fprintf(stderr, "ld.x86: expected x86_64 ELF64 output\n");
            elf_close(obj);
            return -1;
        }
    } else {
        if (elf_class(obj) != ELFOBJ_CLASS_32 || elf_machine(obj) != EM_386) {
            fprintf(stderr, "ld.x86: expected i386 ELF32 output\n");
            elf_close(obj);
            return -1;
        }
    }

    elf_close(obj);
    return 0;
}

int main(int argc, char **argv) {
    ld_ctx_t ctx;
    int i;

    memset(&ctx, 0, sizeof(ctx));
    ctx.out_path = "a.out";

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-m32") == 0) {
            ctx.mode = 32;
            continue;
        }
        if (strcmp(argv[i], "-m64") == 0) {
            ctx.mode = 64;
            continue;
        }

        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                strvec_free(&ctx.pass);
                strvec_free(&ctx.inputs);
                return 2;
            }
            ctx.out_path = argv[i + 1];
            if (strvec_push(&ctx.pass, argv[i]) != 0 || strvec_push(&ctx.pass, argv[i + 1]) != 0) {
                strvec_free(&ctx.pass);
                strvec_free(&ctx.inputs);
                return 1;
            }
            ++i;
            continue;
        }

        if (strcmp(argv[i], "-r") == 0) {
            ctx.expect_type = ET_REL;
        } else if (strcmp(argv[i], "-shared") == 0 || strcmp(argv[i], "-pie") == 0) {
            ctx.expect_type = ET_DYN;
        } else if (strcmp(argv[i], "-static") == 0) {
            ctx.expect_type = ET_EXEC;
        }

        if (argv[i][0] != '-') {
            if (strvec_push(&ctx.inputs, argv[i]) != 0) {
                strvec_free(&ctx.pass);
                strvec_free(&ctx.inputs);
                return 1;
            }
        }

        if (strvec_push(&ctx.pass, argv[i]) != 0) {
            strvec_free(&ctx.pass);
            strvec_free(&ctx.inputs);
            return 1;
        }
    }

    if (ctx.inputs.count == 0) {
        usage(argv[0]);
        strvec_free(&ctx.pass);
        strvec_free(&ctx.inputs);
        return 2;
    }

    if (ctx.mode == 0) {
        infer_mode_from_inputs(&ctx);
    }

    if (run_ld_backend(&ctx) != 0) {
        fprintf(stderr, "ld.x86: backend link failed\n");
        strvec_free(&ctx.pass);
        strvec_free(&ctx.inputs);
        return 1;
    }

    if (validate_output(&ctx) != 0) {
        strvec_free(&ctx.pass);
        strvec_free(&ctx.inputs);
        return 1;
    }

    strvec_free(&ctx.pass);
    strvec_free(&ctx.inputs);
    return 0;
}
