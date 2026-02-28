#include "cc_driver.h"
#include "cc_frontend.h"
#include "cc_pipeline.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} strvec_t;

typedef struct {
    int invoked_as_cpp;
    int mode_E;
    int mode_S;
    int mode_c;
    int verbose;
    int dry_run;
    int wall;
    int werror;
    int pedantic;
    int pedantic_errors;
    int debug;
    int shared;
    int pic;
    int pthread_flag;
    int emit_ssa;
    int bootstrap_gcc;
    int query_print_file_name;
    int query_print_libgcc_file_name;
    int nostdlib;
    int nodefaultlibs;
    int gnu89_inline_mode;
    int gnu89_inline_override;
    int i386_isa_level;
    int i386_has_sse;
    int i386_has_sse2;
    int i386_has_mmx;
    int i386_supports_sse;
    int i386_supports_sse2;
    int i386_supports_mmx;
    int i386_fp_math_mode;
    int i386_arch_explicit;
    cc_target_t target;

    const char *std;
    const char *opt_level;
    const char *output;
    const char *forced_lang;
    const char *print_file_name;
    const char *parse_bad_arg;

    strvec_t cpp_flags;
    strvec_t c_flags;
    strvec_t as_flags;
    strvec_t ld_flags;
    strvec_t inputs;
    strvec_t temp_files;
} cc_opts_t;

enum {
    I386_FPMATH_AUTO = 0,
    I386_FPMATH_SSE = 1,
    I386_FPMATH_387 = 2
};

static const char *target_gcc_flag(cc_target_t target) {
    return target == CC_TARGET_I386 ? "-m32" : "-m64";
}

static int opt_level_num(const cc_opts_t *o) {
    if (o->opt_level == NULL || o->opt_level[0] == '\0') {
        return 0;
    }
    if (o->opt_level[0] < '0' || o->opt_level[0] > '3') {
        return 0;
    }
    return o->opt_level[0] - '0';
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

static void opts_free(cc_opts_t *o) {
    strvec_free(&o->cpp_flags);
    strvec_free(&o->c_flags);
    strvec_free(&o->as_flags);
    strvec_free(&o->ld_flags);
    strvec_free(&o->inputs);
    strvec_free(&o->temp_files);
}

static int run_cmd(const cc_opts_t *o, char **argv) {
    pid_t pid;
    int status;
    size_t i;

    if (o->verbose || o->dry_run) {
        for (i = 0; argv[i] != NULL; ++i) {
            fprintf(stderr, "%s%s", i == 0 ? "" : " ", argv[i]);
        }
        fputc('\n', stderr);
    }

    if (o->dry_run) {
        return 0;
    }

    pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }

    return 0;
}

static int capture_cmd_output(char *const argv[], char *out, size_t outsz) {
    int pipefd[2];
    pid_t pid;
    int status;
    ssize_t nread;
    size_t used = 0;

    if (pipe(pipefd) != 0) {
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);
    while ((nread = read(pipefd[0], out + used, outsz - used - 1)) > 0) {
        used += (size_t)nread;
        if (used + 1 >= outsz) {
            break;
        }
    }
    close(pipefd[0]);

    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }

    out[used] = '\0';
    while (used > 0 && (out[used - 1] == '\n' || out[used - 1] == '\r' || out[used - 1] == ' ' ||
                        out[used - 1] == '\t')) {
        out[--used] = '\0';
    }
    return 0;
}

static int gcc_print_file_name(const char *name, cc_target_t target, char out[PATH_MAX]) {
    char opt[PATH_MAX];
    char *argv[4];

    if (snprintf(opt, sizeof(opt), "-print-file-name=%s", name) >= (int)sizeof(opt)) {
        return -1;
    }
    argv[0] = "gcc";
    argv[1] = (char *)target_gcc_flag(target);
    argv[2] = opt;
    argv[3] = NULL;
    if (capture_cmd_output(argv, out, PATH_MAX) != 0) {
        return -1;
    }
    if (strcmp(out, name) == 0) {
        return -1;
    }
    return 0;
}

static int gcc_print_libgcc(cc_target_t target, char out[PATH_MAX]) {
    char *argv[4];

    argv[0] = "gcc";
    argv[1] = (char *)target_gcc_flag(target);
    argv[2] = "-print-libgcc-file-name";
    argv[3] = NULL;
    if (capture_cmd_output(argv, out, PATH_MAX) != 0) {
        return -1;
    }
    return 0;
}

static int has_ext(const char *path, const char *ext) {
    size_t pn;
    size_t en;

    pn = strlen(path);
    en = strlen(ext);
    if (pn < en) {
        return 0;
    }
    return strcmp(path + pn - en, ext) == 0;
}

static const char *prog_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    if (slash == NULL || slash[1] == '\0') {
        return path;
    }
    return slash + 1;
}

static int strvec_has_flag(const strvec_t *v, const char *flag) {
    size_t i;
    if (v == NULL || flag == NULL) {
        return 0;
    }
    for (i = 0; i < v->count; ++i) {
        if (strcmp(v->items[i], flag) == 0) {
            return 1;
        }
    }
    return 0;
}

static int make_temp_path(cc_opts_t *o, const char *prefix, const char *suffix, char out[PATH_MAX]) {
    char templ[PATH_MAX];
    int fd;

    if (snprintf(templ, sizeof(templ), "/tmp/%sXXXXXX", prefix) >= (int)sizeof(templ)) {
        return -1;
    }

    fd = mkstemp(templ);
    if (fd < 0) {
        return -1;
    }
    close(fd);

    if (suffix != NULL) {
        char with_suffix[PATH_MAX];
        if (snprintf(with_suffix, sizeof(with_suffix), "%s%s", templ, suffix) >= (int)sizeof(with_suffix)) {
            unlink(templ);
            return -1;
        }
        if (rename(templ, with_suffix) != 0) {
            unlink(templ);
            return -1;
        }
        if (snprintf(out, PATH_MAX, "%s", with_suffix) >= PATH_MAX) {
            return -1;
        }
    } else {
        if (snprintf(out, PATH_MAX, "%s", templ) >= PATH_MAX) {
            return -1;
        }
    }

    return strvec_push(&o->temp_files, out);
}

static void cleanup_temp_files(const cc_opts_t *o) {
    size_t i;

    for (i = 0; i < o->temp_files.count; ++i) {
        unlink(o->temp_files.items[i]);
    }
}

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [options] file...\n"
            "  -E                 preprocess only\n"
            "  -S                 compile to assembly\n"
            "  -c                 compile/assemble only\n"
            "  -o <file>          output file\n"
            "  -I/-D/-U           preprocessor options\n"
            "  -include/-imacros  forced preprocessor include inputs\n"
            "  -P -dM             preprocess formatting/macro dump modes\n"
            "  -M/-MM/-MD/-MMD    dependency generation modes\n"
            "  -MF/-MT/-MQ        dependency output/target controls\n"
            "  -std=c90/c95/c99/c11/c17/c23   language mode\n"
            "  -O0..-O3           optimization level\n"
            "  -m32/-m64          target ABI (i386 or x86_64)\n"
            "  -march=<arch>      ISA level control (e.g. i386/i486/i586/i686)\n"
            "  -msse2/-mno-sse2   i386 SSE2 opcode enable/disable\n"
            "  -msse/-mno-sse     i386 SSE opcode enable/disable\n"
            "  -mmmx/-mno-mmx     i386 MMX opcode enable/disable\n"
            "  -mfpmath=sse|387   i386 scalar FP lowering mode\n"
            "  -g                 debug info\n"
            "  -Wall -Werror      warnings\n"
            "  -pedantic          warn on non-C99 extensions\n"
            "  -pedantic-errors   reject non-C99 extensions\n"
            "  -fPIC              position-independent code\n"
            "  -fgnu89-inline     GNU89 inline mode semantics\n"
            "  -fno-gnu89-inline  disable GNU89 inline mode override\n"
            "  -shared            create shared object (link)\n"
            "  -pthread           enable thread options\n"
            "  -v                 verbose stages\n"
            "  -###               print commands without executing\n"
            "  -emit-ssa          run IR verification utility on input .ir\n"
            "  -print-file-name=<name>   print discovered runtime/include path\n"
            "  -print-libgcc-file-name   print discovered libgcc path\n"
            "  --bootstrap-gcc    temporary fallback C->.s via host gcc\n",
            prog);
}

static int i386_features_from_arch_name(const char *name, int *out_level, int *out_has_sse, int *out_has_sse2,
                                        int *out_has_mmx, int *out_supports_sse, int *out_supports_sse2,
                                        int *out_supports_mmx) {
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (strcmp(name, "i386") == 0) {
        *out_level = 3;
        *out_has_sse = 0;
        *out_has_sse2 = 0;
        *out_has_mmx = 0;
        *out_supports_sse = 0;
        *out_supports_sse2 = 0;
        *out_supports_mmx = 0;
        return 1;
    }
    if (strcmp(name, "i486") == 0) {
        *out_level = 4;
        *out_has_sse = 0;
        *out_has_sse2 = 0;
        *out_has_mmx = 0;
        *out_supports_sse = 0;
        *out_supports_sse2 = 0;
        *out_supports_mmx = 0;
        return 1;
    }
    if (strcmp(name, "i586") == 0 || strcmp(name, "pentium") == 0) {
        *out_level = 5;
        *out_has_sse = 0;
        *out_has_sse2 = 0;
        *out_has_mmx = 0;
        *out_supports_sse = 0;
        *out_supports_sse2 = 0;
        *out_supports_mmx = 1;
        return 1;
    }
    if (strcmp(name, "pentium-mmx") == 0) {
        *out_level = 5;
        *out_has_sse = 0;
        *out_has_sse2 = 0;
        *out_has_mmx = 1;
        *out_supports_sse = 0;
        *out_supports_sse2 = 0;
        *out_supports_mmx = 1;
        return 1;
    }
    if (strcmp(name, "i686") == 0 || strcmp(name, "pentiumpro") == 0 || strcmp(name, "pentium2") == 0) {
        *out_level = 6;
        *out_has_sse = 0;
        *out_has_sse2 = 0;
        *out_has_mmx = 1;
        *out_supports_sse = 0;
        *out_supports_sse2 = 0;
        *out_supports_mmx = 1;
        return 1;
    }
    if (strcmp(name, "pentium3") == 0) {
        *out_level = 6;
        *out_has_sse = 1;
        *out_has_sse2 = 0;
        *out_has_mmx = 1;
        *out_supports_sse = 1;
        *out_supports_sse2 = 0;
        *out_supports_mmx = 1;
        return 1;
    }
    if (strcmp(name, "pentium4") == 0 || strcmp(name, "prescott") == 0 || strcmp(name, "nocona") == 0 ||
        strcmp(name, "core2") == 0 || strcmp(name, "x86-64") == 0 || strcmp(name, "x86-64-v1") == 0 ||
        strcmp(name, "x86-64-v2") == 0 || strcmp(name, "x86-64-v3") == 0 || strcmp(name, "x86-64-v4") == 0) {
        *out_level = 7;
        *out_has_sse = 1;
        *out_has_sse2 = 0;
        *out_has_mmx = 1;
        *out_supports_sse = 1;
        *out_supports_sse2 = 1;
        *out_supports_mmx = 1;
        return 1;
    }
    return 0;
}

static int parse_args(int argc, char **argv, cc_opts_t *o) {
    int i;

    o->parse_bad_arg = NULL;
    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        o->parse_bad_arg = a;

        if (strcmp(a, "-E") == 0) {
            o->mode_E = 1;
            continue;
        }
        if (strcmp(a, "-S") == 0) {
            o->mode_S = 1;
            continue;
        }
        if (strcmp(a, "-c") == 0) {
            o->mode_c = 1;
            continue;
        }
        if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
            o->verbose = 1;
            if (strcmp(a, "-v") == 0) {
                if (strvec_push(&o->cpp_flags, a) != 0) {
                    return -1;
                }
            }
            continue;
        }
        if (strcmp(a, "-###") == 0) {
            o->dry_run = 1;
            continue;
        }
        if (strcmp(a, "-Wall") == 0) {
            o->wall = 1;
            strvec_push(&o->c_flags, a);
            continue;
        }
        if (strcmp(a, "-Werror") == 0) {
            o->werror = 1;
            strvec_push(&o->c_flags, a);
            continue;
        }
        if (strcmp(a, "-pedantic") == 0) {
            o->pedantic = 1;
            strvec_push(&o->c_flags, a);
            continue;
        }
        if (strcmp(a, "-pedantic-errors") == 0) {
            o->pedantic = 1;
            o->pedantic_errors = 1;
            strvec_push(&o->c_flags, a);
            continue;
        }
        if (strcmp(a, "-g") == 0) {
            o->debug = 1;
            strvec_push(&o->c_flags, a);
            strvec_push(&o->as_flags, a);
            strvec_push(&o->ld_flags, a);
            continue;
        }
        if (strcmp(a, "-fPIC") == 0) {
            o->pic = 1;
            strvec_push(&o->c_flags, a);
            strvec_push(&o->ld_flags, a);
            continue;
        }
        if (strcmp(a, "-fgnu89-inline") == 0) {
            o->gnu89_inline_mode = 1;
            o->gnu89_inline_override = 1;
            if (strvec_push(&o->c_flags, a) != 0) {
                return -1;
            }
            continue;
        }
        if (strcmp(a, "-fno-gnu89-inline") == 0) {
            o->gnu89_inline_mode = 0;
            o->gnu89_inline_override = 1;
            if (strvec_push(&o->c_flags, a) != 0) {
                return -1;
            }
            continue;
        }
        if (strcmp(a, "-shared") == 0) {
            o->shared = 1;
            strvec_push(&o->ld_flags, a);
            continue;
        }
        if (strcmp(a, "-pthread") == 0) {
            o->pthread_flag = 1;
            strvec_push(&o->c_flags, a);
            strvec_push(&o->ld_flags, a);
            continue;
        }
        if (strcmp(a, "-emit-ssa") == 0) {
            o->emit_ssa = 1;
            continue;
        }
        if (strncmp(a, "-print-file-name=", 17) == 0) {
            o->query_print_file_name = 1;
            o->print_file_name = a + 17;
            continue;
        }
        if (strcmp(a, "-print-file-name") == 0) {
            if (i + 1 >= argc) {
                return -1;
            }
            o->query_print_file_name = 1;
            o->print_file_name = argv[++i];
            continue;
        }
        if (strcmp(a, "-print-libgcc-file-name") == 0) {
            o->query_print_libgcc_file_name = 1;
            continue;
        }
        if (strcmp(a, "--bootstrap-gcc") == 0) {
            o->bootstrap_gcc = 1;
            continue;
        }
        if (strcmp(a, "-nostdlib") == 0) {
            o->nostdlib = 1;
            strvec_push(&o->c_flags, a);
            continue;
        }
        if (strcmp(a, "-nostdinc") == 0) {
            if (strvec_push(&o->cpp_flags, a) != 0 || strvec_push(&o->c_flags, a) != 0) {
                return -1;
            }
            continue;
        }
        if (strcmp(a, "-P") == 0 || strcmp(a, "-dM") == 0) {
            if (strvec_push(&o->cpp_flags, a) != 0) {
                return -1;
            }
            if (strcmp(a, "-dM") == 0) {
                o->mode_E = 1;
            }
            continue;
        }
        if (strcmp(a, "-M") == 0 || strcmp(a, "-MM") == 0 || strcmp(a, "-MD") == 0 || strcmp(a, "-MMD") == 0) {
            if (strvec_push(&o->cpp_flags, a) != 0) {
                return -1;
            }
            if (strcmp(a, "-M") == 0 || strcmp(a, "-MM") == 0) {
                o->mode_E = 1;
            }
            continue;
        }
        if (strcmp(a, "-MF") == 0 || strcmp(a, "-MT") == 0 || strcmp(a, "-MQ") == 0 || strcmp(a, "-include") == 0 ||
            strcmp(a, "-imacros") == 0) {
            if (i + 1 >= argc) {
                return -1;
            }
            if (strvec_push(&o->cpp_flags, a) != 0 || strvec_push(&o->cpp_flags, argv[i + 1]) != 0) {
                return -1;
            }
            ++i;
            continue;
        }
        if (strncmp(a, "-MF", 3) == 0 || strncmp(a, "-MT", 3) == 0 || strncmp(a, "-MQ", 3) == 0 ||
            strncmp(a, "-include", 8) == 0 || strncmp(a, "-imacros", 8) == 0) {
            if (strvec_push(&o->cpp_flags, a) != 0) {
                return -1;
            }
            continue;
        }
        if (strcmp(a, "-nodefaultlibs") == 0) {
            o->nodefaultlibs = 1;
            strvec_push(&o->c_flags, a);
            continue;
        }

        if (strcmp(a, "-o") == 0) {
            if (i + 1 >= argc) {
                return -1;
            }
            o->output = argv[++i];
            continue;
        }
        if (strcmp(a, "-x") == 0) {
            if (i + 1 >= argc) {
                return -1;
            }
            o->forced_lang = argv[++i];
            continue;
        }
        if (strncmp(a, "-x", 2) == 0 && a[2] != '\0') {
            o->forced_lang = a + 2;
            continue;
        }

        if (strncmp(a, "-std=", 5) == 0) {
            o->std = a + 5;
            strvec_push(&o->c_flags, a);
            continue;
        }
        if (strncmp(a, "-O", 2) == 0) {
            o->opt_level = a + 2;
            strvec_push(&o->c_flags, a);
            continue;
        }
        if (strcmp(a, "-m32") == 0) {
            o->target = CC_TARGET_I386;
            if (!o->i386_arch_explicit) {
                o->i386_isa_level = 6;
                o->i386_has_sse = 0;
                o->i386_has_sse2 = 0;
                o->i386_has_mmx = 1;
                o->i386_supports_sse = 0;
                o->i386_supports_sse2 = 0;
                o->i386_supports_mmx = 1;
            }
            if (strvec_push(&o->c_flags, a) != 0 || strvec_push(&o->cpp_flags, a) != 0) {
                return -1;
            }
            continue;
        }
        if (strcmp(a, "-m64") == 0) {
            o->target = CC_TARGET_X86_64;
            if (strvec_push(&o->c_flags, a) != 0 || strvec_push(&o->cpp_flags, a) != 0) {
                return -1;
            }
            continue;
        }
        if (strcmp(a, "-march") == 0 || strcmp(a, "-mcpu") == 0 || strcmp(a, "-mtune") == 0) {
            int level = 0;
            int has_sse = 0;
            int has_sse2 = 0;
            int has_mmx = 0;
            int supports_sse = 0;
            int supports_sse2 = 0;
            int supports_mmx = 0;
            int is_tune = strcmp(a, "-mtune") == 0;
            if (i + 1 >= argc) {
                return -1;
            }
            if (strvec_push(&o->c_flags, a) != 0 || strvec_push(&o->c_flags, argv[i + 1]) != 0 ||
                strvec_push(&o->cpp_flags, a) != 0 || strvec_push(&o->cpp_flags, argv[i + 1]) != 0) {
                return -1;
            }
            if (!is_tune && i386_features_from_arch_name(argv[i + 1], &level, &has_sse, &has_sse2, &has_mmx,
                                                         &supports_sse, &supports_sse2, &supports_mmx)) {
                o->i386_isa_level = level;
                o->i386_has_sse = has_sse;
                o->i386_has_sse2 = has_sse2;
                o->i386_has_mmx = has_mmx;
                o->i386_supports_sse = supports_sse;
                o->i386_supports_sse2 = supports_sse2;
                o->i386_supports_mmx = supports_mmx;
                o->i386_arch_explicit = 1;
            }
            ++i;
            continue;
        }
        if (strncmp(a, "-march=", 7) == 0 || strncmp(a, "-mcpu=", 6) == 0 || strncmp(a, "-mtune=", 7) == 0) {
            int level = 0;
            int has_sse = 0;
            int has_sse2 = 0;
            int has_mmx = 0;
            int supports_sse = 0;
            int supports_sse2 = 0;
            int supports_mmx = 0;
            const char *eq = strchr(a, '=');
            int is_tune = strncmp(a, "-mtune=", 7) == 0;
            if (strvec_push(&o->c_flags, a) != 0 || strvec_push(&o->cpp_flags, a) != 0) {
                return -1;
            }
            if (!is_tune &&
                i386_features_from_arch_name(eq != NULL ? eq + 1 : NULL, &level, &has_sse, &has_sse2, &has_mmx,
                                             &supports_sse, &supports_sse2, &supports_mmx)) {
                o->i386_isa_level = level;
                o->i386_has_sse = has_sse;
                o->i386_has_sse2 = has_sse2;
                o->i386_has_mmx = has_mmx;
                o->i386_supports_sse = supports_sse;
                o->i386_supports_sse2 = supports_sse2;
                o->i386_supports_mmx = supports_mmx;
                o->i386_arch_explicit = 1;
            }
            continue;
        }
        if (strcmp(a, "-msse") == 0 || strcmp(a, "-mno-sse") == 0) {
            if (strvec_push(&o->c_flags, a) != 0 || strvec_push(&o->cpp_flags, a) != 0) {
                return -1;
            }
            if (strcmp(a, "-msse") == 0) {
                if (o->target == CC_TARGET_I386 && !o->i386_supports_sse) {
                    fprintf(stderr, "cc: error: -msse is not supported for selected -march level\n");
                    return -1;
                }
                o->i386_has_sse = 1;
            } else {
                o->i386_has_sse = 0;
            }
            continue;
        }
        if (strcmp(a, "-msse2") == 0 || strcmp(a, "-mno-sse2") == 0) {
            if (strvec_push(&o->c_flags, a) != 0 || strvec_push(&o->cpp_flags, a) != 0) {
                return -1;
            }
            if (strcmp(a, "-msse2") == 0) {
                if (o->target == CC_TARGET_I386 && !o->i386_supports_sse2) {
                    fprintf(stderr, "cc: error: -msse2 is not supported for selected -march level\n");
                    return -1;
                }
                o->i386_has_sse2 = 1;
            } else {
                o->i386_has_sse2 = 0;
            }
            continue;
        }
        if (strcmp(a, "-mmmx") == 0 || strcmp(a, "-mno-mmx") == 0) {
            if (strvec_push(&o->c_flags, a) != 0 || strvec_push(&o->cpp_flags, a) != 0) {
                return -1;
            }
            if (strcmp(a, "-mmmx") == 0) {
                if (o->target == CC_TARGET_I386 && !o->i386_supports_mmx) {
                    fprintf(stderr, "cc: error: -mmmx is not supported for selected -march level\n");
                    return -1;
                }
                o->i386_has_mmx = 1;
            } else {
                o->i386_has_mmx = 0;
            }
            continue;
        }
        if (strcmp(a, "-mfpmath=sse") == 0 || strcmp(a, "-mfpmath=387") == 0) {
            if (strvec_push(&o->c_flags, a) != 0) {
                return -1;
            }
            o->i386_fp_math_mode = strcmp(a, "-mfpmath=387") == 0 ? I386_FPMATH_387 : I386_FPMATH_SSE;
            continue;
        }
        if (strcmp(a, "-mfpmath") == 0) {
            if (i + 1 >= argc) {
                return -1;
            }
            if (strvec_push(&o->c_flags, a) != 0 || strvec_push(&o->c_flags, argv[i + 1]) != 0) {
                return -1;
            }
            if (strcmp(argv[i + 1], "387") == 0) {
                o->i386_fp_math_mode = I386_FPMATH_387;
            } else if (strcmp(argv[i + 1], "sse") == 0) {
                o->i386_fp_math_mode = I386_FPMATH_SSE;
            }
            ++i;
            continue;
        }

        if (strcmp(a, "-I") == 0 || strcmp(a, "-D") == 0 || strcmp(a, "-U") == 0 || strcmp(a, "-iquote") == 0 ||
            strcmp(a, "-isystem") == 0) {
            if (i + 1 >= argc) {
                return -1;
            }
            if (strvec_push(&o->cpp_flags, a) != 0 || strvec_push(&o->cpp_flags, argv[i + 1]) != 0 ||
                strvec_push(&o->c_flags, a) != 0 || strvec_push(&o->c_flags, argv[i + 1]) != 0) {
                return -1;
            }
            ++i;
            continue;
        }

        if (strncmp(a, "-I", 2) == 0 || strncmp(a, "-D", 2) == 0 || strncmp(a, "-U", 2) == 0 ||
            strncmp(a, "-iquote", 7) == 0 || strncmp(a, "-isystem", 8) == 0) {
            if (strvec_push(&o->cpp_flags, a) != 0 || strvec_push(&o->c_flags, a) != 0) {
                return -1;
            }
            continue;
        }
        if (strncmp(a, "-L", 2) == 0 || strncmp(a, "-l", 2) == 0) {
            if (strvec_push(&o->ld_flags, a) != 0) {
                return -1;
            }
            continue;
        }
        if (strcmp(a, "-L") == 0 || strcmp(a, "-l") == 0) {
            if (i + 1 >= argc) {
                return -1;
            }
            if (strvec_push(&o->ld_flags, a) != 0 || strvec_push(&o->ld_flags, argv[i + 1]) != 0) {
                return -1;
            }
            ++i;
            continue;
        }
        if (strncmp(a, "-Wl,", 4) == 0) {
            char *csv = xstrdup(a + 4);
            char *tok;
            if (csv == NULL) {
                return -1;
            }
            tok = strtok(csv, ",");
            while (tok != NULL) {
                if (strvec_push(&o->ld_flags, tok) != 0) {
                    free(csv);
                    return -1;
                }
                tok = strtok(NULL, ",");
            }
            free(csv);
            continue;
        }
        if (strncmp(a, "-Wp,", 4) == 0) {
            char *csv = xstrdup(a + 4);
            char *tok;
            if (csv == NULL) {
                return -1;
            }
            tok = strtok(csv, ",");
            while (tok != NULL) {
                if (strvec_push(&o->cpp_flags, tok) != 0) {
                    free(csv);
                    return -1;
                }
                tok = strtok(NULL, ",");
            }
            free(csv);
            continue;
        }
        if (strcmp(a, "-Wp") == 0) {
            char *csv;
            char *tok;
            if (i + 1 >= argc) {
                return -1;
            }
            csv = xstrdup(argv[++i]);
            if (csv == NULL) {
                return -1;
            }
            tok = strtok(csv, ",");
            while (tok != NULL) {
                if (strvec_push(&o->cpp_flags, tok) != 0) {
                    free(csv);
                    return -1;
                }
                tok = strtok(NULL, ",");
            }
            free(csv);
            continue;
        }
        if (strncmp(a, "-Wa,", 4) == 0) {
            char *csv = xstrdup(a + 4);
            char *tok;
            if (csv == NULL) {
                return -1;
            }
            tok = strtok(csv, ",");
            while (tok != NULL) {
                if (strvec_push(&o->as_flags, tok) != 0) {
                    free(csv);
                    return -1;
                }
                tok = strtok(NULL, ",");
            }
            free(csv);
            continue;
        }

        if (strcmp(a, "-") == 0) {
            if (strvec_push(&o->inputs, a) != 0) {
                return -1;
            }
            continue;
        }
        if (a[0] == '-') {
            if (strvec_push(&o->c_flags, a) != 0) {
                return -1;
            }
            continue;
        }

        if (strvec_push(&o->inputs, a) != 0) {
            return -1;
        }
    }

    if (o->mode_E + o->mode_S + o->mode_c > 1) {
        o->parse_bad_arg = "-E/-S/-c";
        fprintf(stderr, "cc: -E, -S, and -c are mutually exclusive\n");
        return -1;
    }

    if (o->inputs.count == 0 && !o->query_print_file_name && !o->query_print_libgcc_file_name) {
        if (o->parse_bad_arg == NULL) {
            o->parse_bad_arg = "<no-input>";
        }
        return -1;
    }

    return 0;
}

static int run_preprocess(const cc_opts_t *o, const char *in, const char *out) {
    cc_diag_t diag;
    const char *std_mode = o->std != NULL ? o->std : "gnu89";
    const char *const *pp_flags = (const char *const *)o->cpp_flags.items;
    size_t pp_count = o->cpp_flags.count;
    const char *pp_in = in;
    int use_stdin_tmp = 0;
    char stdin_tmp[PATH_MAX];
    FILE *tmpfp;
    int tfd;
    char buf[4096];
    size_t nread;

    memset(&diag, 0, sizeof(diag));
    if (strcmp(in, "-") == 0) {
        snprintf(stdin_tmp, sizeof(stdin_tmp), "/tmp/ccstdinXXXXXX");
        tfd = mkstemp(stdin_tmp);
        if (tfd < 0) {
            fprintf(stderr, "cpp: error: failed to create temporary stdin file\n");
            return -1;
        }
        tmpfp = fdopen(tfd, "wb");
        if (tmpfp == NULL) {
            close(tfd);
            unlink(stdin_tmp);
            fprintf(stderr, "cpp: error: failed to open temporary stdin file\n");
            return -1;
        }
        while ((nread = fread(buf, 1, sizeof(buf), stdin)) > 0) {
            if (fwrite(buf, 1, nread, tmpfp) != nread) {
                fclose(tmpfp);
                unlink(stdin_tmp);
                fprintf(stderr, "cpp: error: failed writing temporary stdin file\n");
                return -1;
            }
        }
        fclose(tmpfp);
        pp_in = stdin_tmp;
        use_stdin_tmp = 1;
    }

    if (cc_preprocess_file(pp_in, out, std_mode, pp_flags, pp_count, &diag) != 0) {
        if (use_stdin_tmp) {
            unlink(stdin_tmp);
        }
        if (diag.line != 0) {
            if (diag.path[0] != '\0') {
                fprintf(stderr, "%s:%zu:%zu: error: %s\n", diag.path, diag.line, diag.col, diag.message);
            } else {
                fprintf(stderr, "cpp:%zu:%zu: error: %s\n", diag.line, diag.col, diag.message);
            }
        } else if (diag.message[0] != '\0') {
            fprintf(stderr, "cpp: error: %s\n", diag.message);
        } else {
            fprintf(stderr, "cpp: error: preprocess failed\n");
        }
        return -1;
    }
    if (use_stdin_tmp) {
        unlink(stdin_tmp);
    }
    return 0;
}

static int run_bootstrap_frontend(const cc_opts_t *o, const char *in_c, const char *out_s) {
    char stdflag[64];
    int want_trigraphs = 1;
    size_t argc = 13 + o->c_flags.count;
    char **argv;
    size_t i;
    size_t at = 0;

    argv = (char **)calloc(argc + 1, sizeof(*argv));
    if (argv == NULL) {
        return -1;
    }

    argv[at++] = "gcc";
    argv[at++] = "-S";
    argv[at++] = (char *)target_gcc_flag(o->target);
    /* Force C language mode so bootstrap .i with #line markers is accepted. */
    argv[at++] = "-x";
    argv[at++] = "c";
    snprintf(stdflag, sizeof(stdflag), "-std=%s", o->std != NULL ? o->std : "gnu99");
    argv[at++] = stdflag;
    if (want_trigraphs) {
        argv[at++] = "-trigraphs";
    }
    if (!o->pic && !o->shared) {
        argv[at++] = "-fno-pie";
        argv[at++] = "-no-pie";
    }
    for (i = 0; i < o->c_flags.count; ++i) {
        argv[at++] = o->c_flags.items[i];
    }
    argv[at++] = "-o";
    argv[at++] = (char *)out_s;
    argv[at++] = (char *)in_c;
    argv[at] = NULL;

    i = run_cmd(o, argv);
    free(argv);
    return (int)i;
}

static int run_as(const cc_opts_t *o, const char *in_s, const char *out_o) {
    const char *as_prog = getenv("AS");
    size_t argc = 5 + o->as_flags.count;
    char **argv;
    size_t i;
    size_t at = 0;

    argv = (char **)calloc(argc + 1, sizeof(*argv));
    if (argv == NULL) {
        return -1;
    }

    if (as_prog == NULL || as_prog[0] == '\0') {
        as_prog = "as";
    }
    argv[at++] = (char *)as_prog;
    argv[at++] = o->target == CC_TARGET_I386 ? "--32" : "--64";
    for (i = 0; i < o->as_flags.count; ++i) {
        argv[at++] = o->as_flags.items[i];
    }
    argv[at++] = "-o";
    argv[at++] = (char *)out_o;
    argv[at++] = (char *)in_s;
    argv[at] = NULL;

    i = run_cmd(o, argv);
    free(argv);
    return (int)i;
}

static int run_ld(const cc_opts_t *o, const strvec_t *objs, const char *out) {
    const char *ld_prog = getenv("LD");
    char crt1[PATH_MAX];
    char crti[PATH_MAX];
    char crtbegin[PATH_MAX];
    char crtend[PATH_MAX];
    char crtn[PATH_MAX];
    char libgcc[PATH_MAX];
    const int want_default_runtime = !o->shared && !o->nostdlib && !o->nodefaultlibs;
    size_t argc = 25 + o->ld_flags.count + objs->count;
    char **argv;
    size_t i;
    size_t at = 0;

    argv = (char **)calloc(argc + 1, sizeof(*argv));
    if (argv == NULL) {
        return -1;
    }

    if (ld_prog == NULL || ld_prog[0] == '\0') {
        ld_prog = "ld";
    }
    argv[at++] = (char *)ld_prog;
    argv[at++] = "-m";
    argv[at++] = o->target == CC_TARGET_I386 ? "elf_i386" : "elf_x86_64";
    if (o->shared) {
        argv[at++] = "-shared";
    }
    if (want_default_runtime) {
        if (gcc_print_file_name("crt1.o", o->target, crt1) != 0 ||
            gcc_print_file_name("crti.o", o->target, crti) != 0 ||
            gcc_print_file_name("crtbegin.o", o->target, crtbegin) != 0 ||
            gcc_print_file_name("crtend.o", o->target, crtend) != 0 ||
            gcc_print_file_name("crtn.o", o->target, crtn) != 0 ||
            gcc_print_libgcc(o->target, libgcc) != 0) {
            fprintf(stderr, "cc: failed to discover runtime crt/libgcc paths via gcc\n");
            free(argv);
            return -1;
        }
        argv[at++] = "-dynamic-linker";
        argv[at++] = o->target == CC_TARGET_I386 ? "/lib/ld-linux.so.2" : "/lib64/ld-linux-x86-64.so.2";
        argv[at++] = crt1;
        argv[at++] = crti;
        argv[at++] = crtbegin;
    }
    argv[at++] = "-o";
    argv[at++] = (char *)out;
    for (i = 0; i < objs->count; ++i) {
        argv[at++] = objs->items[i];
    }
    for (i = 0; i < o->ld_flags.count; ++i) {
        argv[at++] = o->ld_flags.items[i];
    }
    if (want_default_runtime) {
        argv[at++] = "-lc";
        argv[at++] = "-lm";
        argv[at++] = libgcc;
        argv[at++] = crtend;
        argv[at++] = crtn;
    }
    argv[at] = NULL;

    i = run_cmd(o, argv);
    free(argv);
    return (int)i;
}

static int run_emit_ssa_tool(const cc_opts_t *o, const char *self, const char *ir_file) {
    char *argv[3];
    char tool_path[PATH_MAX];
    const char *slash;

    slash = strrchr(self, '/');
    if (slash != NULL) {
        size_t dlen = (size_t)(slash - self);
        if (dlen + strlen("/ir-verifier") + 1 < sizeof(tool_path)) {
            memcpy(tool_path, self, dlen);
            tool_path[dlen] = '\0';
            strcat(tool_path, "/ir-verifier");
            argv[0] = tool_path;
        } else {
            argv[0] = "ir-verifier";
        }
    } else {
        argv[0] = "ir-verifier";
    }
    argv[1] = (char *)ir_file;
    argv[2] = NULL;

    return run_cmd(o, argv);
}

static int derive_out(const char *in, const char *ext, char out[PATH_MAX]) {
    const char *base_in = strrchr(in, '/');
    const char *dot;
    size_t base;

    if (base_in != NULL) {
        base_in++;
    } else {
        base_in = in;
    }
    dot = strrchr(base_in, '.');
    if (dot == NULL) {
        base = strlen(base_in);
    } else {
        base = (size_t)(dot - base_in);
    }

    if (base + strlen(ext) + 1 > PATH_MAX) {
        return -1;
    }

    memcpy(out, base_in, base);
    out[base] = '\0';
    strcat(out, ext);
    return 0;
}

static int maybe_print_info_and_exit(int argc, char **argv) {
    int i;
    cc_target_t target = CC_TARGET_X86_64;
    const char *info = NULL;
    int has_other = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-m32") == 0) {
            target = CC_TARGET_I386;
        } else if (strcmp(argv[i], "-m64") == 0) {
            target = CC_TARGET_X86_64;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            info = argv[i];
        } else if (strcmp(argv[i], "-dumpversion") == 0 || strcmp(argv[i], "-dumpfullversion") == 0) {
            info = argv[i];
        } else if (strcmp(argv[i], "-dumpmachine") == 0) {
            info = argv[i];
        } else {
            has_other = 1;
        }
    }
    if (info == NULL || has_other) {
        return 0;
    }
    if (strcmp(info, "--version") == 0 || strcmp(info, "-v") == 0) {
        printf("cc (Substrate GCC-compatible) 13.2.0\n");
        return 1;
    }
    if (strcmp(info, "-dumpversion") == 0 || strcmp(info, "-dumpfullversion") == 0) {
        printf("13.2.0\n");
        return 1;
    }
    if (strcmp(info, "-dumpmachine") == 0) {
        printf("%s\n", target == CC_TARGET_I386 ? "i386-pc-linux-gnu" : "x86_64-pc-linux-gnu");
        return 1;
    }
    return 0;
}

int cc_main(int argc, char **argv) {
    cc_opts_t o;
    strvec_t obj_files;
    size_t i;
    int rc = 1;

    memset(&o, 0, sizeof(o));
    memset(&obj_files, 0, sizeof(obj_files));
    o.i386_isa_level = 6;
    o.i386_has_sse = 0;
    o.i386_has_sse2 = 1;
    o.i386_has_mmx = 1;
    o.i386_supports_sse = 1;
    o.i386_supports_sse2 = 1;
    o.i386_supports_mmx = 1;
    o.i386_fp_math_mode = I386_FPMATH_AUTO;
    o.target = CC_TARGET_X86_64;
    if (strcmp(prog_basename(argv[0]), "cpp") == 0) {
        o.invoked_as_cpp = 1;
        o.mode_E = 1;
    }
    if (maybe_print_info_and_exit(argc, argv)) {
        return 0;
    }

    if (parse_args(argc, argv, &o) != 0) {
        if (o.parse_bad_arg != NULL && o.parse_bad_arg[0] != '\0') {
            fprintf(stderr, "cc: invalid or unsupported parameter: %s\n", o.parse_bad_arg);
        }
        usage(argv[0]);
        goto out;
    }

    if (o.query_print_file_name) {
        char path[PATH_MAX];
        const char *name = o.print_file_name != NULL ? o.print_file_name : "";
        if (name[0] == '\0') {
            printf("\n");
        } else if (gcc_print_file_name(name, o.target, path) == 0) {
            printf("%s\n", path);
        } else {
            printf("%s\n", name);
        }
        rc = 0;
        goto out;
    }
    if (o.query_print_libgcc_file_name) {
        char path[PATH_MAX];
        if (gcc_print_libgcc(o.target, path) == 0) {
            printf("%s\n", path);
        } else {
            printf("libgcc.a\n");
        }
        rc = 0;
        goto out;
    }

    if (o.mode_E && o.output != NULL && o.inputs.count > 1) {
        fprintf(stderr, "cc: -E with -o requires exactly one input\n");
        goto out;
    }

    if (o.emit_ssa) {
        if (o.inputs.count != 1 || !has_ext(o.inputs.items[0], ".ir")) {
            fprintf(stderr, "cc: -emit-ssa currently expects exactly one .ir input\n");
            goto out;
        }
        if (run_emit_ssa_tool(&o, argv[0], o.inputs.items[0]) != 0) {
            goto out;
        }
        rc = 0;
        goto out;
    }

    for (i = 0; i < o.inputs.count; ++i) {
        const char *in = o.inputs.items[i];
        int input_is_c = has_ext(in, ".c");
        int input_is_S = has_ext(in, ".S");
        int input_is_s = has_ext(in, ".s");

        if (o.forced_lang != NULL && strcmp(o.forced_lang, "none") != 0) {
            if (strcmp(o.forced_lang, "c") == 0) {
                input_is_c = 1;
                input_is_S = 0;
                input_is_s = 0;
            } else if (strcmp(o.forced_lang, "assembler-with-cpp") == 0) {
                input_is_c = 0;
                input_is_S = 1;
                input_is_s = 0;
            } else if (strcmp(o.forced_lang, "assembler") == 0) {
                input_is_c = 0;
                input_is_S = 0;
                input_is_s = 1;
            }
        }

        if (o.mode_E) {
            const char *pp_out;

            if (has_ext(in, ".o") || has_ext(in, ".a") || has_ext(in, ".so")) {
                fprintf(stderr, "cc: -E is not valid for object/archive input %s\n", in);
                goto out;
            }

            pp_out = o.output != NULL ? o.output : "/dev/stdout";
            if (run_preprocess(&o, in, pp_out) != 0) {
                goto out;
            }
            continue;
        }

        if (input_is_S) {
            char out_s[PATH_MAX];
            char out_o[PATH_MAX];
            char tmp_s[PATH_MAX];

            if (o.mode_S) {
                if (o.output != NULL) {
                    snprintf(out_s, sizeof(out_s), "%s", o.output);
                } else if (derive_out(in, ".s", out_s) != 0) {
                    fprintf(stderr, "cc: failed to derive .s output name\n");
                    goto out;
                }
                if (run_preprocess(&o, in, out_s) != 0) {
                    goto out;
                }
                continue;
            }

            if (make_temp_path(&o, "ccspp_", ".s", tmp_s) != 0) {
                fprintf(stderr, "cc: failed to create temporary preprocessed assembly file\n");
                goto out;
            }
            if (run_preprocess(&o, in, tmp_s) != 0) {
                goto out;
            }

            if (o.mode_c) {
                if (o.output != NULL) {
                    snprintf(out_o, sizeof(out_o), "%s", o.output);
                } else if (derive_out(in, ".o", out_o) != 0) {
                    fprintf(stderr, "cc: failed to derive .o output name\n");
                    goto out;
                }
            } else {
                if (make_temp_path(&o, "cco_", ".o", out_o) != 0) {
                    fprintf(stderr, "cc: failed to create temporary object file\n");
                    goto out;
                }
            }

            if (run_as(&o, tmp_s, out_o) != 0) {
                goto out;
            }
            if (strvec_push(&obj_files, out_o) != 0) {
                goto out;
            }
            continue;
        }

        if (input_is_c) {
            char out_pp[PATH_MAX];
            char out_s[PATH_MAX];
            char out_o[PATH_MAX];

            if (make_temp_path(&o, "ccpp_", ".i", out_pp) != 0) {
                fprintf(stderr, "cc: failed to create temporary preprocessed file\n");
                goto out;
            }
            if (run_preprocess(&o, in, out_pp) != 0) {
                goto out;
            }

            if (o.mode_S) {
                if (o.output != NULL) {
                    snprintf(out_s, sizeof(out_s), "%s", o.output);
                } else if (derive_out(in, ".s", out_s) != 0) {
                    fprintf(stderr, "cc: failed to derive .s output name\n");
                    goto out;
                }
            } else {
                if (make_temp_path(&o, "ccs_", ".s", out_s) != 0) {
                    fprintf(stderr, "cc: failed to create temporary assembly file\n");
                    goto out;
                }
            }

            if (o.bootstrap_gcc) {
                if (run_bootstrap_frontend(&o, out_pp, out_s) != 0) {
                    goto out;
                }
            } else {
                cc_diag_t diag;
                memset(&diag, 0, sizeof(diag));
                if (strvec_has_flag(&o.c_flags, "-ffreestanding")) {
                    setenv("CC_FREESTANDING", "1", 1);
                } else {
                    unsetenv("CC_FREESTANDING");
                }
                if (cc_compile_c_to_s(out_pp, in, out_s, o.std != NULL ? o.std : "gnu89", o.debug, o.target,
                                      opt_level_num(&o), o.wall,
                                      o.werror, o.pedantic, o.pedantic_errors, o.gnu89_inline_mode,
                                      o.gnu89_inline_override, o.i386_isa_level, o.i386_has_sse2, o.i386_has_mmx,
                                      o.i386_fp_math_mode, &diag) != 0) {
                    if (diag.line != 0) {
                        if (diag.path[0] != '\0') {
                            fprintf(stderr, "%s:%zu:%zu: error: %s\n", diag.path, diag.line, diag.col, diag.message);
                        } else {
                            fprintf(stderr, "cc:%zu:%zu: error: %s\n", diag.line, diag.col, diag.message);
                        }
                    } else if (diag.message[0] != '\0') {
                        fprintf(stderr, "cc: error: %s\n", diag.message);
                    } else {
                        fprintf(stderr, "cc: error: native C pipeline failed\n");
                    }
                    fprintf(stderr,
                            "cc: note: current native pipeline supports a strict subset "
                            "(int/bool/char/unsigned-char/short/unsigned-short/unsigned-int/long-long/unsigned-long-long/float/double scalar + one/two/three/four-level pointer functions/prototypes, typedef aliases, declarations (including C99 for-init declarations), assignments/calls, unary address-of/dereference pointer ops, pointer arithmetic/indexing (`ptr +/- int`, `int + ptr`, compatible `ptr - ptr` for non-void pointers, and `ptr[idx]`/`idx[ptr]`), compound assignments, ++/--, if/else, while/do/for, switch/case/default, goto/labels, break/continue, empty statements (`;`), C95 digraph/trigraph lexical forms, arithmetic/logical/bitwise/shift/comma operators (including &&/|| short-circuit), ternary `?:`, scalar casts, pointer/integer casts, `sizeof` on supported scalar types, numeric comparisons, and returns)\n");
                    goto out;
                }
            }

            if (o.mode_S) {
                continue;
            }

            if (o.mode_c) {
                if (o.output != NULL) {
                    snprintf(out_o, sizeof(out_o), "%s", o.output);
                } else if (derive_out(in, ".o", out_o) != 0) {
                    fprintf(stderr, "cc: failed to derive .o output name\n");
                    goto out;
                }
            } else {
                if (make_temp_path(&o, "cco_", ".o", out_o) != 0) {
                    fprintf(stderr, "cc: failed to create temporary object file\n");
                    goto out;
                }
            }

            if (run_as(&o, out_s, out_o) != 0) {
                goto out;
            }
            if (strvec_push(&obj_files, out_o) != 0) {
                goto out;
            }
            continue;
        }

        if (input_is_s) {
            char out_o[PATH_MAX];

            if (o.mode_S) {
                if (o.output != NULL && strcmp(o.output, in) != 0) {
                    char *cp_argv[4];
                    cp_argv[0] = "cp";
                    cp_argv[1] = (char *)in;
                    cp_argv[2] = (char *)o.output;
                    cp_argv[3] = NULL;
                    if (run_cmd(&o, cp_argv) != 0) {
                        goto out;
                    }
                }
                continue;
            }

            if (o.mode_c) {
                if (o.output != NULL) {
                    snprintf(out_o, sizeof(out_o), "%s", o.output);
                } else if (derive_out(in, ".o", out_o) != 0) {
                    fprintf(stderr, "cc: failed to derive .o output name\n");
                    goto out;
                }
            } else {
                if (make_temp_path(&o, "cco_", ".o", out_o) != 0) {
                    fprintf(stderr, "cc: failed to create temporary object file\n");
                    goto out;
                }
            }

            if (run_as(&o, in, out_o) != 0) {
                goto out;
            }
            if (strvec_push(&obj_files, out_o) != 0) {
                goto out;
            }
            continue;
        }

        if (has_ext(in, ".o") || has_ext(in, ".a") || has_ext(in, ".so")) {
            if (strvec_push(&obj_files, in) != 0) {
                goto out;
            }
            continue;
        }

        fprintf(stderr, "cc: unsupported input type: %s\n", in);
        goto out;
    }

    if (o.mode_E || o.mode_S || o.mode_c) {
        rc = 0;
        goto out;
    }

    if (obj_files.count == 0) {
        fprintf(stderr, "cc: no objects to link\n");
        goto out;
    }

    {
        const char *out = o.output != NULL ? o.output : "a.out";
        if (run_ld(&o, &obj_files, out) != 0) {
            goto out;
        }
    }

    rc = 0;
out:
    cleanup_temp_files(&o);
    opts_free(&o);
    strvec_free(&obj_files);
    return rc;
}

int main(int argc, char **argv) {
    return cc_main(argc, argv);
}
