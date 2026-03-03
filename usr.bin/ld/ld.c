#include "elfobj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    int explicit_mode;
    uint16_t expect_type;
    const char *out_path;
    const char *self_path;
    strvec_t pass;
    strvec_t inputs;
} ld_ctx_t;

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [-m32|-m64|-m <emulation>] [-r|-shared|-pie|-static] [-o output] "
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

static int same_file(const char *a, const char *b) {
    struct stat sa;
    struct stat sb;

    if (a == NULL || b == NULL) {
        return 0;
    }
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0) {
        return 0;
    }
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static char *path_join(const char *dir, const char *leaf) {
    size_t dlen;
    size_t llen;
    size_t need_slash;
    char *out;

    if (dir == NULL || leaf == NULL) {
        return NULL;
    }
    dlen = strlen(dir);
    llen = strlen(leaf);
    need_slash = dlen > 0 && dir[dlen - 1] != '/';
    out = (char *)malloc(dlen + need_slash + llen + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, dir, dlen);
    if (need_slash) {
        out[dlen++] = '/';
    }
    memcpy(out + dlen, leaf, llen);
    out[dlen + llen] = '\0';
    return out;
}

static char *find_backend_ld(const ld_ctx_t *ctx) {
    const char *override = getenv("LD_BACKEND");
    const char *path_env;
    char *dup;
    char *tok;
    char *saveptr;

    if (override != NULL && override[0] != '\0') {
        if (strchr(override, '/') != NULL && same_file(override, ctx->self_path)) {
            fprintf(stderr, "ld: LD_BACKEND resolves to this linker binary (%s)\n", override);
            return NULL;
        }
        return xstrdup(override);
    }

    path_env = getenv("PATH");
    if (path_env != NULL && path_env[0] != '\0') {
        dup = xstrdup(path_env);
        if (dup == NULL) {
            return NULL;
        }
        for (tok = strtok_r(dup, ":", &saveptr); tok != NULL; tok = strtok_r(NULL, ":", &saveptr)) {
            char *cand;

            if (tok[0] == '\0') {
                continue;
            }
            cand = path_join(tok, "ld");
            if (cand == NULL) {
                free(dup);
                return NULL;
            }
            if (access(cand, X_OK) == 0) {
                if (!same_file(cand, ctx->self_path)) {
                    free(dup);
                    return cand;
                }
            }
            free(cand);
        }
        free(dup);
    }

    if (access("/usr/bin/ld", X_OK) == 0 && !same_file("/usr/bin/ld", ctx->self_path)) {
        return xstrdup("/usr/bin/ld");
    }
    if (access("/bin/ld", X_OK) == 0 && !same_file("/bin/ld", ctx->self_path)) {
        return xstrdup("/bin/ld");
    }

    fprintf(stderr, "ld: no backend linker found (set LD_BACKEND or adjust PATH)\n");
    return NULL;
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

    ctx->mode = 64;
    return 0;
}

static int infer_mode_from_program_name(ld_ctx_t *ctx) {
    const char *base;

    if (ctx == NULL || ctx->self_path == NULL) {
        return 0;
    }
    base = strrchr(ctx->self_path, '/');
    if (base != NULL) {
        base++;
    } else {
        base = ctx->self_path;
    }
    if (strstr(base, "x86_64") != NULL || strstr(base, "amd64") != NULL || strstr(base, "x64") != NULL) {
        ctx->mode = 64;
        return 1;
    }
    if (strstr(base, "i386") != NULL || strstr(base, "x86") != NULL || strstr(base, "32") != NULL) {
        ctx->mode = 32;
        return 1;
    }
    return 0;
}

static int default_mode(void) {
#if defined(__x86_64__) || defined(__amd64__)
    return 64;
#else
    return 32;
#endif
}

static int parse_mode_token(const char *tok) {
    if (tok == NULL) {
        return 0;
    }
    if (strcmp(tok, "elf_x86_64") == 0 || strcmp(tok, "elf64-x86-64") == 0 ||
        strcmp(tok, "x86_64") == 0 || strcmp(tok, "amd64") == 0 || strcmp(tok, "64") == 0) {
        return 64;
    }
    if (strcmp(tok, "elf_i386") == 0 || strcmp(tok, "elf32-i386") == 0 ||
        strcmp(tok, "i386") == 0 || strcmp(tok, "x86") == 0 || strcmp(tok, "32") == 0) {
        return 32;
    }
    return 0;
}

static int run_internal_rel_link(const ld_ctx_t *ctx) {
    elfobj_t **objs = NULL;
    size_t obj_count = 0;
    elfobj_t *out = NULL;
    elf_err_t err;

    err = elf_link_load_objects((const char **)ctx->inputs.items, ctx->inputs.count, &objs, &obj_count);
    if (err != ELF_OK) {
        fprintf(stderr, "ld: failed to load input objects: %s\n", elf_errstr(err));
        return -1;
    }
    err = elf_link(objs, obj_count, &out);
    if (err != ELF_OK || out == NULL) {
        fprintf(stderr, "ld: internal relocatable link failed: %s\n", out ? elf_last_diagnostics(out) : elf_errstr(err));
        elf_link_unload_objects(objs, obj_count);
        if (out != NULL) {
            elf_close(out);
        }
        return -1;
    }
    err = elf_write_file(out, ctx->out_path);
    if (err != ELF_OK) {
        fprintf(stderr, "ld: failed to write output %s: %s\n", ctx->out_path, elf_errstr(err));
        elf_link_unload_objects(objs, obj_count);
        elf_close(out);
        return -1;
    }

    elf_link_unload_objects(objs, obj_count);
    elf_close(out);
    return 0;
}

static int run_ld_backend(const ld_ctx_t *ctx) {
    const char *emu = ctx->mode == 64 ? "elf_x86_64" : "elf_i386";
    size_t argc = 4 + ctx->pass.count;
    char **argv = (char **)calloc(argc + 1, sizeof(*argv));
    char *backend;
    pid_t pid;
    int status;
    size_t i;

    backend = find_backend_ld(ctx);
    if (backend == NULL) {
        return -1;
    }

    if (argv == NULL) {
        free(backend);
        return -1;
    }

    argv[0] = backend;
    argv[1] = "-m";
    argv[2] = (char *)emu;

    for (i = 0; i < ctx->pass.count; ++i) {
        argv[3 + i] = ctx->pass.items[i];
    }
    argv[3 + ctx->pass.count] = NULL;

    pid = fork();
    if (pid < 0) {
        free(backend);
        free(argv);
        return -1;
    }
    if (pid == 0) {
        if (strchr(argv[0], '/') != NULL) {
            execv(argv[0], argv);
        } else {
            execvp(argv[0], argv);
        }
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) {
        free(backend);
        free(argv);
        return -1;
    }

    free(backend);
    free(argv);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }
    return 0;
}

static int validate_output(const ld_ctx_t *ctx) {
    elfobj_t *obj = NULL;

    if (elf_open(ctx->out_path, &obj) != ELF_OK) {
        fprintf(stderr, "ld: failed to open output %s\n", ctx->out_path);
        return -1;
    }

    if (ctx->expect_type != 0 && elf_type(obj) != ctx->expect_type) {
        fprintf(stderr, "ld: wrong output ELF type\n");
        elf_close(obj);
        return -1;
    }

    if (ctx->mode == 64) {
        if (elf_class(obj) != ELFOBJ_CLASS_64 || elf_machine(obj) != EM_X86_64) {
            fprintf(stderr, "ld: expected x86_64 ELF64 output\n");
            elf_close(obj);
            return -1;
        }
    } else {
        if (elf_class(obj) != ELFOBJ_CLASS_32 || elf_machine(obj) != EM_386) {
            fprintf(stderr, "ld: expected i386 ELF32 output\n");
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
    int query_version = 0;

    memset(&ctx, 0, sizeof(ctx));
    ctx.out_path = "a.out";
    ctx.self_path = argv[0];

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            query_version = 1;
            continue;
        }
        if (strcmp(argv[i], "-m32") == 0) {
            ctx.mode = 32;
            ctx.explicit_mode = 1;
            continue;
        }
        if (strcmp(argv[i], "-m64") == 0) {
            ctx.mode = 64;
            ctx.explicit_mode = 1;
            continue;
        }
        if (strcmp(argv[i], "-m") == 0) {
            int m;
            if (i + 1 >= argc) {
                usage(argv[0]);
                strvec_free(&ctx.pass);
                strvec_free(&ctx.inputs);
                return 2;
            }
            m = parse_mode_token(argv[i + 1]);
            if (m == 0) {
                fprintf(stderr, "ld: unsupported emulation '%s'\n", argv[i + 1]);
                strvec_free(&ctx.pass);
                strvec_free(&ctx.inputs);
                return 2;
            }
            ctx.mode = m;
            ctx.explicit_mode = 1;
            if (strvec_push(&ctx.pass, argv[i]) != 0 || strvec_push(&ctx.pass, argv[i + 1]) != 0) {
                strvec_free(&ctx.pass);
                strvec_free(&ctx.inputs);
                return 1;
            }
            ++i;
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

    if (query_version && ctx.inputs.count == 0) {
        printf("GNU ld (GNU Binutils) 2.40\n");
        strvec_free(&ctx.pass);
        strvec_free(&ctx.inputs);
        return 0;
    }

    if (ctx.inputs.count == 0) {
        usage(argv[0]);
        strvec_free(&ctx.pass);
        strvec_free(&ctx.inputs);
        return 2;
    }

    if (ctx.mode == 0) {
        if (!infer_mode_from_program_name(&ctx)) {
            infer_mode_from_inputs(&ctx);
        }
    }
    if (ctx.mode == 0) {
        ctx.mode = default_mode();
    }

    if (ctx.expect_type == ET_REL) {
        if (run_internal_rel_link(&ctx) != 0) {
            strvec_free(&ctx.pass);
            strvec_free(&ctx.inputs);
            return 1;
        }
    } else if (run_ld_backend(&ctx) != 0) {
        fprintf(stderr, "ld: backend link failed\n");
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
