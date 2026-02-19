#include "../../include/elfobj.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ET_NONE
#define ET_NONE 0
#define ET_REL 1
#define ET_EXEC 2
#define ET_DYN 3
#endif

#define AR_MAGIC "!<arch>\n"
#define AR_HDRSZ 60

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} strvec_t;

typedef struct {
    elfobj_t **objs;
    size_t count;
    size_t cap;
} objvec_t;

typedef struct {
    const char *out_path;
    int out_type;
    int allow_undefined;
    int strip_all;
    int gc_sections;
    int incremental_rel;
    strvec_t inputs;
    strvec_t lib_paths;
} ld_ctx_t;

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [-r|-shared|-pie|-static] [-o output] [-L dir] [-l name] "
            "[-T script] [--gc-sections] [--strip-all] [--allow-undefined] input...\n",
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
}

static int objvec_push(objvec_t *v, elfobj_t *obj) {
    elfobj_t **next;
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 16 : v->cap * 2;
        next = (elfobj_t **)realloc(v->objs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->objs = next;
        v->cap = ncap;
    }
    v->objs[v->count++] = obj;
    return 0;
}

static void objvec_close_all(objvec_t *v) {
    size_t i;
    for (i = 0; i < v->count; ++i) {
        elf_close(v->objs[i]);
    }
    free(v->objs);
}

static int ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s);
    size_t b = strlen(suffix);
    return a >= b && strcmp(s + (a - b), suffix) == 0;
}

static int file_exists(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static int slurp_file(const char *path, unsigned char **buf, size_t *sz) {
    FILE *fp = fopen(path, "rb");
    long end;
    size_t got;
    unsigned char *p;
    if (fp == NULL) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    end = ftell(fp);
    if (end < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    p = (unsigned char *)malloc((size_t)end);
    if (p == NULL && end != 0) {
        fclose(fp);
        return -1;
    }
    got = fread(p, 1, (size_t)end, fp);
    fclose(fp);
    if (got != (size_t)end) {
        free(p);
        return -1;
    }
    *buf = p;
    *sz = (size_t)end;
    return 0;
}

static int load_elf_object(const char *path, objvec_t *objs) {
    elfobj_t *o = NULL;
    elf_err_t err = elf_open(path, &o);
    if (err != ELF_OK) {
        fprintf(stderr, "ld.x86: %s: %s\n", path, elf_errstr(err));
        return -1;
    }
    if (objvec_push(objs, o) != 0) {
        elf_close(o);
        return -1;
    }
    return 0;
}

static int load_archive(const char *path, objvec_t *objs) {
    unsigned char *buf = NULL;
    size_t sz = 0;
    size_t off = 0;

    if (slurp_file(path, &buf, &sz) != 0) {
        fprintf(stderr, "ld.x86: cannot read archive %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (sz < 8 || memcmp(buf, AR_MAGIC, 8) != 0) {
        fprintf(stderr, "ld.x86: %s: invalid archive magic\n", path);
        free(buf);
        return -1;
    }
    off = 8;
    while (off + AR_HDRSZ <= sz) {
        size_t size_field_off = off + 48;
        size_t data_off = off + AR_HDRSZ;
        char size_txt[11];
        long memb_sz;

        memcpy(size_txt, buf + size_field_off, 10);
        size_txt[10] = '\0';
        memb_sz = strtol(size_txt, NULL, 10);
        if (memb_sz < 0) {
            free(buf);
            return -1;
        }
        if (data_off + (size_t)memb_sz > sz) {
            free(buf);
            return -1;
        }

        if ((size_t)memb_sz >= 4 &&
            buf[data_off + 0] == 0x7f &&
            buf[data_off + 1] == 'E' &&
            buf[data_off + 2] == 'L' &&
            buf[data_off + 3] == 'F') {
            elfobj_t *o = NULL;
            if (elf_open_memory(buf + data_off, (size_t)memb_sz, &o) == ELF_OK) {
                if (objvec_push(objs, o) != 0) {
                    elf_close(o);
                    free(buf);
                    return -1;
                }
            }
        }

        off = data_off + (size_t)memb_sz;
        if (off & 1u) {
            off++;
        }
    }

    free(buf);
    return 0;
}

static char *resolve_lib(const ld_ctx_t *ctx, const char *name) {
    size_t i;
    char path[1024];
    for (i = 0; i < ctx->lib_paths.count; ++i) {
        snprintf(path, sizeof(path), "%s/lib%s.a", ctx->lib_paths.items[i], name);
        if (file_exists(path)) {
            return xstrdup(path);
        }
        snprintf(path, sizeof(path), "%s/lib%s.so", ctx->lib_paths.items[i], name);
        if (file_exists(path)) {
            return xstrdup(path);
        }
    }
    snprintf(path, sizeof(path), "/usr/lib/lib%s.a", name);
    if (file_exists(path)) {
        return xstrdup(path);
    }
    return NULL;
}

static int load_inputs(ld_ctx_t *ctx, objvec_t *objs) {
    size_t i;
    for (i = 0; i < ctx->inputs.count; ++i) {
        const char *in = ctx->inputs.items[i];
        if (ends_with(in, ".a")) {
            if (load_archive(in, objs) != 0) {
                return -1;
            }
        } else {
            if (load_elf_object(in, objs) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int write_output(ld_ctx_t *ctx, objvec_t *objs) {
    elfobj_t *out = NULL;
    elfobj_t *check = NULL;
    elf_err_t err;
    char *diag = NULL;

    if (objs->count == 0) {
        fprintf(stderr, "ld.x86: no input objects\n");
        return -1;
    }
    err = elf_link(objs->objs, objs->count, &out);
    if (err != ELF_OK) {
        fprintf(stderr, "ld.x86: link failed: %s\n", elf_errstr(err));
        return -1;
    }

    if (ctx->out_type != ET_NONE) {
        err = elf_set_type(out, (uint16_t)ctx->out_type);
        if (err != ELF_OK) {
            fprintf(stderr, "ld.x86: cannot set output type: %s\n", elf_errstr(err));
            elf_close(out);
            return -1;
        }
    }

    err = elf_write_file(out, ctx->out_path);
    elf_close(out);
    if (err != ELF_OK) {
        fprintf(stderr, "ld.x86: cannot write %s: %s\n", ctx->out_path, elf_errstr(err));
        return -1;
    }

    err = elf_open(ctx->out_path, &check);
    if (err != ELF_OK) {
        fprintf(stderr, "ld.x86: reopen failed for %s: %s\n", ctx->out_path, elf_errstr(err));
        return -1;
    }
    err = elf_validate(check, &diag);
    elf_close(check);
    if (err != ELF_OK) {
        fprintf(stderr, "ld.x86: post-write validation failed: %s\n",
                diag ? diag : elf_errstr(err));
        free(diag);
        return -1;
    }
    free(diag);
    return 0;
}

int main(int argc, char **argv) {
    ld_ctx_t ctx;
    objvec_t objs;
    int i;

    memset(&ctx, 0, sizeof(ctx));
    memset(&objs, 0, sizeof(objs));
    ctx.out_path = "a.out";
    ctx.out_type = ET_REL;
    ctx.incremental_rel = 1;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            ctx.out_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "-r") == 0) {
            ctx.out_type = ET_REL;
            ctx.incremental_rel = 1;
            continue;
        }
        if (strcmp(argv[i], "-shared") == 0) {
            ctx.out_type = ET_DYN;
            continue;
        }
        if (strcmp(argv[i], "-pie") == 0) {
            ctx.out_type = ET_DYN;
            continue;
        }
        if (strcmp(argv[i], "-static") == 0) {
            ctx.out_type = ET_EXEC;
            continue;
        }
        if (strcmp(argv[i], "-L") == 0) {
            if (i + 1 >= argc || strvec_push(&ctx.lib_paths, argv[++i]) != 0) {
                return 2;
            }
            continue;
        }
        if (strncmp(argv[i], "-L", 2) == 0 && argv[i][2] != '\0') {
            if (strvec_push(&ctx.lib_paths, argv[i] + 2) != 0) {
                return 2;
            }
            continue;
        }
        if (strcmp(argv[i], "-l") == 0) {
            char *resolved;
            if (i + 1 >= argc) {
                return 2;
            }
            resolved = resolve_lib(&ctx, argv[++i]);
            if (resolved == NULL) {
                fprintf(stderr, "ld.x86: cannot find -l%s\n", argv[i]);
                return 1;
            }
            if (strvec_push(&ctx.inputs, resolved) != 0) {
                free(resolved);
                return 1;
            }
            free(resolved);
            continue;
        }
        if (strncmp(argv[i], "-l", 2) == 0 && argv[i][2] != '\0') {
            char *resolved = resolve_lib(&ctx, argv[i] + 2);
            if (resolved == NULL) {
                fprintf(stderr, "ld.x86: cannot find %s\n", argv[i]);
                return 1;
            }
            if (strvec_push(&ctx.inputs, resolved) != 0) {
                free(resolved);
                return 1;
            }
            free(resolved);
            continue;
        }
        if (strcmp(argv[i], "-T") == 0) {
            if (i + 1 >= argc) {
                return 2;
            }
            i++;
            continue;
        }
        if (strcmp(argv[i], "--gc-sections") == 0) {
            ctx.gc_sections = 1;
            continue;
        }
        if (strcmp(argv[i], "--strip-all") == 0) {
            ctx.strip_all = 1;
            continue;
        }
        if (strcmp(argv[i], "--allow-undefined") == 0) {
            ctx.allow_undefined = 1;
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "ld.x86: unsupported option: %s\n", argv[i]);
            return 2;
        }
        if (strvec_push(&ctx.inputs, argv[i]) != 0) {
            return 1;
        }
    }

    if (ctx.inputs.count == 0) {
        usage(argv[0]);
        return 2;
    }

    if (load_inputs(&ctx, &objs) != 0) {
        objvec_close_all(&objs);
        strvec_free(&ctx.inputs);
        strvec_free(&ctx.lib_paths);
        return 1;
    }
    if (write_output(&ctx, &objs) != 0) {
        objvec_close_all(&objs);
        strvec_free(&ctx.inputs);
        strvec_free(&ctx.lib_paths);
        return 1;
    }

    objvec_close_all(&objs);
    strvec_free(&ctx.inputs);
    strvec_free(&ctx.lib_paths);
    return 0;
}
