#include "as_data.h"
#include "as_elf_emit.h"
#include "as_lexer.h"
#include "as_parser.h"
#include "as_sections.h"
#include "as_symtab.h"
#include "elfobj.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef EM_X86_64
#define EM_X86_64 62
#endif

#define SUBSTRATE_TC_DEPTH_ENV "SUBSTRATE_TC_DEPTH"
#define SUBSTRATE_TC_TRACE_ENV "SUBSTRATE_TC_TRACE"
#define SUBSTRATE_TC_MAX_ENV "SUBSTRATE_TC_MAX_DEPTH"
#define SUBSTRATE_TC_DEFAULT_MAX 64

#define AS_MODE_AUTO (-1)
#define AS_MODE_32 0
#define AS_MODE_64 1

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} strvec_t;

typedef enum {
    AS_OUTPUT_ELF = 0,
    AS_OUTPUT_BINARY,
} as_output_t;

typedef struct {
    int mode;
    int syntax_intel;
    as_output_t output;
    int emit_listing;
    int statistics;
    int target_help;
    int warn_enabled;
    int fatal_warnings;
    const char *in_path;
    const char *out_path;
    const char *listing_path;
    const char *self_path;
    const char *march;
    unsigned long long max_input_bytes;
    unsigned long long max_line_bytes;
    unsigned long long max_token_length;
    unsigned long long max_macro_depth;
    unsigned long long max_include_depth;
    strvec_t gcc_opts;
    strvec_t as_opts;
} as_ctx_t;

typedef enum {
    AS_E_USAGE,
    AS_E_INTERNAL,
    AS_E_BACKEND,
    AS_E_VALIDATE,
    AS_E_LIMIT,
} as_error_code_t;

static int g_emit_error_codes = 0;
static void as_diag(as_error_code_t code, const char *fmt, ...);

static long parse_env_long(const char *s, long fallback) {
    char *end;
    long v;

    if (s == NULL || s[0] == '\0') {
        return fallback;
    }
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return fallback;
    }
    return v;
}

static int toolchain_guard_enter(const char *tool) {
    long depth;
    long max_depth;
    char depth_buf[32];
    char trace_buf[256];
    const char *trace;
    int n;

    depth = parse_env_long(getenv(SUBSTRATE_TC_DEPTH_ENV), 0);
    if (depth < 0) {
        depth = 0;
    }
    max_depth = parse_env_long(getenv(SUBSTRATE_TC_MAX_ENV), SUBSTRATE_TC_DEFAULT_MAX);
    if (max_depth < 4) {
        max_depth = 4;
    }
    if (depth >= max_depth) {
        trace = getenv(SUBSTRATE_TC_TRACE_ENV);
        as_diag(AS_E_INTERNAL,
                "toolchain recursion guard hit at depth %ld (max %ld)%s%s",
                depth,
                max_depth,
                trace != NULL && trace[0] != '\0' ? ", trace: " : "",
                trace != NULL && trace[0] != '\0' ? trace : "");
        return -1;
    }

    snprintf(depth_buf, sizeof(depth_buf), "%ld", depth + 1);
    if (setenv(SUBSTRATE_TC_DEPTH_ENV, depth_buf, 1) != 0) {
        as_diag(AS_E_INTERNAL, "failed to set recursion depth env");
        return -1;
    }
    trace = getenv(SUBSTRATE_TC_TRACE_ENV);
    if (trace == NULL || trace[0] == '\0') {
        n = snprintf(trace_buf, sizeof(trace_buf), "%s", tool);
    } else {
        n = snprintf(trace_buf, sizeof(trace_buf), "%s->%s", trace, tool);
    }
    if (n < 0) {
        as_diag(AS_E_INTERNAL, "failed to format recursion trace");
        return -1;
    }
    if ((size_t)n >= sizeof(trace_buf)) {
        trace_buf[sizeof(trace_buf) - 1] = '\0';
    }
    if (setenv(SUBSTRATE_TC_TRACE_ENV, trace_buf, 1) != 0) {
        as_diag(AS_E_INTERNAL, "failed to set recursion trace env");
        return -1;
    }
    return 0;
}

static const char *as_error_code_name(as_error_code_t code) {
    switch (code) {
    case AS_E_USAGE:
        return "AS_E_USAGE";
    case AS_E_INTERNAL:
        return "AS_E_INTERNAL";
    case AS_E_BACKEND:
        return "AS_E_BACKEND";
    case AS_E_VALIDATE:
        return "AS_E_VALIDATE";
    case AS_E_LIMIT:
        return "AS_E_LIMIT";
    default:
        return "AS_E_UNKNOWN";
    }
}

static void as_diag(as_error_code_t code, const char *fmt, ...) {
    va_list ap;

    if (g_emit_error_codes) {
        fprintf(stderr, "[%s] ", as_error_code_name(code));
    }
    fprintf(stderr, "as: error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [-32|-64] [-c] [-g] [-I dir] [-D name[=value]] [-march cpu] [-mtune cpu] "
            "[-O elf|binary] "
            "[-msyntax=att|intel] [-W|--warn|--no-warn|--fatal-warnings] "
            "[-al[=file]] [--defsym sym=val] [--statistics] [--target-help] "
            "[-Wa opts] [--max-input-bytes N] [--max-line-bytes N] [--max-token-length N] "
            "[--max-macro-depth N] [--max-include-depth N] [-o output] input.s|input.S\n",
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

static int parse_u64(const char *s, unsigned long long *out) {
    char *end = NULL;
    unsigned long long v;

    if (s == NULL || s[0] == '\0' || out == NULL) {
        return -1;
    }
    errno = 0;
    v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return -1;
    }
    *out = v;
    return 0;
}

static unsigned long long limit_from_env(const char *name) {
    const char *v = getenv(name);
    unsigned long long out = 0;

    if (v == NULL || v[0] == '\0') {
        return 0;
    }
    if (parse_u64(v, &out) != 0) {
        return 0;
    }
    return out;
}

static int count_max_token_len(const char *line) {
    size_t i;
    size_t cur = 0;
    size_t max = 0;

    if (line == NULL) {
        return 0;
    }
    for (i = 0; line[i] != '\0'; ++i) {
        if (isspace((unsigned char)line[i])) {
            cur = 0;
            continue;
        }
        cur++;
        if (cur > max) {
            max = cur;
        }
    }
    return (int)max;
}

static const char *line_start_directive(const char *line) {
    while (line != NULL && *line != '\0' && isspace((unsigned char)*line)) {
        line++;
    }
    return line;
}

static int preflight_source_limits(const as_ctx_t *ctx) {
    FILE *fp;
    char *line = NULL;
    size_t cap = 0;
    ssize_t nread;
    unsigned long long total = 0;
    unsigned long long macro_depth = 0;
    unsigned long long lineno = 0;

    if (ctx == NULL || ctx->in_path == NULL) {
        return -1;
    }

    fp = fopen(ctx->in_path, "rb");
    if (fp == NULL) {
        as_diag(AS_E_USAGE, "failed to open input %s: %s", ctx->in_path, strerror(errno));
        return -1;
    }

    while ((nread = getline(&line, &cap, fp)) >= 0) {
        const char *d;
        int tlen;

        lineno++;
        total += (unsigned long long)nread;
        if (ctx->max_input_bytes > 0 && total > ctx->max_input_bytes) {
            as_diag(AS_E_LIMIT, "%s:%llu: input exceeds --max-input-bytes=%llu", ctx->in_path, lineno,
                    ctx->max_input_bytes);
            free(line);
            fclose(fp);
            return -1;
        }
        if (ctx->max_line_bytes > 0 && (unsigned long long)nread > ctx->max_line_bytes) {
            as_diag(AS_E_LIMIT, "%s:%llu: line exceeds --max-line-bytes=%llu", ctx->in_path, lineno,
                    ctx->max_line_bytes);
            free(line);
            fclose(fp);
            return -1;
        }
        tlen = count_max_token_len(line);
        if (ctx->max_token_length > 0 && (unsigned long long)tlen > ctx->max_token_length) {
            as_diag(AS_E_LIMIT, "%s:%llu: token exceeds --max-token-length=%llu", ctx->in_path, lineno,
                    ctx->max_token_length);
            free(line);
            fclose(fp);
            return -1;
        }

        d = line_start_directive(line);
        if (strncmp(d, ".macro", 6) == 0 && (d[6] == '\0' || isspace((unsigned char)d[6]))) {
            macro_depth++;
            if (ctx->max_macro_depth > 0 && macro_depth > ctx->max_macro_depth) {
                as_diag(AS_E_LIMIT, "%s:%llu: macro depth exceeds --max-macro-depth=%llu", ctx->in_path, lineno,
                        ctx->max_macro_depth);
                free(line);
                fclose(fp);
                return -1;
            }
        } else if (strncmp(d, ".endm", 5) == 0 && (d[5] == '\0' || isspace((unsigned char)d[5]))) {
            if (macro_depth > 0) {
                macro_depth--;
            }
        }
    }

    free(line);
    fclose(fp);
    return 0;
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

static int source_needs_cpp(const char *path) {
    const char *dot;

    if (path == NULL) {
        return 0;
    }
    dot = strrchr(path, '.');
    if (dot == NULL) {
        return 0;
    }
    return strcmp(dot, ".S") == 0;
}

static int x64_isa_level_from_march(const char *march) {
    if (march == NULL || march[0] == '\0') {
        return 1;
    }
    if (strcmp(march, "x86-64-v4") == 0) {
        return 4;
    }
    if (strcmp(march, "x86-64-v3") == 0) {
        return 3;
    }
    if (strcmp(march, "x86-64-v2") == 0) {
        return 2;
    }
    return 1;
}

static const char *resolve_cpp_tool(const as_ctx_t *ctx, char *buf, size_t bufsz) {
    const char *override;
    const char *self;
    const char *slash;
    size_t dirlen;

    override = getenv("AS_CPP");
    if (override != NULL && override[0] != '\0') {
        return override;
    }
    if (ctx == NULL || ctx->self_path == NULL || buf == NULL || bufsz == 0) {
        return "cpp";
    }
    self = ctx->self_path;
    slash = strrchr(self, '/');
    if (slash == NULL) {
        return "cpp";
    }
    dirlen = (size_t)(slash - self);

    if (snprintf(buf, bufsz, "%.*s/../cc/cpp", (int)dirlen, self) > 0 && access(buf, X_OK) == 0) {
        return buf;
    }
    if (snprintf(buf, bufsz, "%.*s/cpp", (int)dirlen, self) > 0 && access(buf, X_OK) == 0) {
        return buf;
    }
    return "cpp";
}

static int run_cpp_stage(const as_ctx_t *ctx, const char *in_path, const char *out_path) {
    size_t i;
    size_t argc;
    char **argv;
    size_t at = 0;
    char cpp_path[PATH_MAX];
    const char *cpp_prog;
    pid_t pid;
    int status;

    cpp_prog = resolve_cpp_tool(ctx, cpp_path, sizeof(cpp_path));
    argc = 5 + ctx->gcc_opts.count;
    argv = (char **)calloc(argc + 1, sizeof(*argv));
    if (argv == NULL) {
        return -1;
    }

    argv[at++] = (char *)cpp_prog;
    for (i = 0; i < ctx->gcc_opts.count; ++i) {
        argv[at++] = ctx->gcc_opts.items[i];
    }
    argv[at++] = "-o";
    argv[at++] = (char *)out_path;
    argv[at++] = (char *)in_path;
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

static int collect_include_dirs(const as_ctx_t *ctx, const char ***dirs_out, size_t *count_out) {
    const char **dirs = NULL;
    size_t i;
    size_t count = 0;

    if (dirs_out == NULL || count_out == NULL) {
        return -1;
    }
    *dirs_out = NULL;
    *count_out = 0;

    if (ctx == NULL || ctx->as_opts.count == 0) {
        return 0;
    }

    dirs = (const char **)calloc(ctx->as_opts.count, sizeof(*dirs));
    if (dirs == NULL) {
        return -1;
    }
    for (i = 0; i < ctx->as_opts.count; ++i) {
        const char *opt = ctx->as_opts.items[i];
        if (strncmp(opt, "-I", 2) != 0) {
            continue;
        }
        if (opt[2] == '\0') {
            continue;
        }
        dirs[count++] = opt + 2;
    }
    *dirs_out = dirs;
    *count_out = count;
    return 0;
}

static int run_native_backend(const as_ctx_t *ctx) {
    as_token_vec_t toks;
    as_parse_result_t parsed;
    as_symtab_t syms;
    as_section_state_t secs;
    as_data_program_t data;
    as_lexer_cfg_t lcfg;
    as_parser_cfg_t pcfg;
    as_elf_cfg_t ecfg;
    const char **include_dirs = NULL;
    size_t include_dir_count = 0;
    char errbuf[512];
    char *temp_pp = NULL;
    const char *src_path = NULL;
    int rc = -1;

    memset(&lcfg, 0, sizeof(lcfg));
    memset(&pcfg, 0, sizeof(pcfg));
    memset(&ecfg, 0, sizeof(ecfg));
    as_token_vec_init(&toks);
    as_parse_result_init(&parsed);
    as_symtab_init(&syms);
    as_section_state_init(&secs);
    as_data_program_init(&data);

    if (source_needs_cpp(ctx->in_path)) {
        char tmp_template[] = "/tmp/aspp_XXXXXX";
        int fd = mkstemp(tmp_template);
        if (fd < 0) {
            as_diag(AS_E_INTERNAL, "failed to create temp preprocessed file: %s", strerror(errno));
            goto out;
        }
        close(fd);
        temp_pp = xstrdup(tmp_template);
        if (temp_pp == NULL) {
            as_diag(AS_E_INTERNAL, "out of memory");
            goto out;
        }
        if (run_cpp_stage(ctx, ctx->in_path, temp_pp) != 0) {
            as_diag(AS_E_BACKEND, "preprocessor stage failed");
            goto out;
        }
        src_path = temp_pp;
    } else {
        src_path = ctx->in_path;
    }

    if (collect_include_dirs(ctx, &include_dirs, &include_dir_count) != 0) {
        as_diag(AS_E_INTERNAL, "failed to build include dir list");
        goto out;
    }

    lcfg.include_dirs = include_dirs;
    lcfg.include_dir_count = include_dir_count;
    lcfg.intel_syntax = ctx->syntax_intel;
    lcfg.max_include_depth = (unsigned)ctx->max_include_depth;

    pcfg.intel_syntax = ctx->syntax_intel;
    pcfg.arch = AS_PARSER_ARCH_X86;

    ecfg.machine = ctx->mode == AS_MODE_64 ? EM_X86_64 : EM_386;
    ecfg.is_64 = ctx->mode == AS_MODE_64 ? 1u : 0u;
    ecfg.use_rela = ctx->mode == AS_MODE_64 ? 1u : 0u;
    ecfg.x86_64_isa_level = (unsigned)x64_isa_level_from_march(ctx->march);
    ecfg.intel_syntax = (unsigned)(ctx->syntax_intel ? 1 : 0);

    if (as_lex_file(src_path, &lcfg, &toks, errbuf, sizeof(errbuf)) != 0) {
        as_diag(AS_E_BACKEND, "%s", errbuf);
        goto out;
    }
    if (as_parse_tokens(&toks, &pcfg, &parsed, errbuf, sizeof(errbuf)) != 0) {
        as_diag(AS_E_BACKEND, "%s", errbuf);
        goto out;
    }
    if (as_symtab_build(&parsed, &syms, errbuf, sizeof(errbuf)) != 0) {
        as_diag(AS_E_BACKEND, "%s", errbuf);
        goto out;
    }
    if (as_sections_build(&parsed, &secs, errbuf, sizeof(errbuf)) != 0) {
        as_diag(AS_E_BACKEND, "%s", errbuf);
        goto out;
    }
    if (as_data_build(&parsed, &data, errbuf, sizeof(errbuf)) != 0) {
        as_diag(AS_E_BACKEND, "%s", errbuf);
        goto out;
    }
    if (ctx->output == AS_OUTPUT_BINARY) {
        if (as_elf_emit_binary_file(&parsed, &secs, &ecfg, ctx->out_path, errbuf, sizeof(errbuf)) != 0) {
            as_diag(AS_E_BACKEND, "%s", errbuf);
            goto out;
        }
    } else {
        if (as_elf_emit_file(&parsed, &secs, &syms, &data, &ecfg, ctx->out_path, errbuf, sizeof(errbuf)) != 0) {
            as_diag(AS_E_BACKEND, "%s", errbuf);
            goto out;
        }
    }

    rc = 0;

out:
    if (temp_pp != NULL) {
        unlink(temp_pp);
    }
    free(temp_pp);
    free(include_dirs);
    as_data_program_free(&data);
    as_section_state_free(&secs);
    as_symtab_free(&syms);
    as_parse_result_free(&parsed);
    as_token_vec_free(&toks);
    return rc;
}


static int validate_output_file(const as_ctx_t *ctx) {
    elfobj_t *check = NULL;
    elf_err_t err;

    if (ctx->output == AS_OUTPUT_BINARY) {
        return 0;
    }

    err = elf_open(ctx->out_path, &check);
    if (err != ELF_OK) {
        as_diag(AS_E_VALIDATE, "failed to open output %s: %s", ctx->out_path, elf_errstr(err));
        return -1;
    }

    if (elf_type(check) != ET_REL) {
        as_diag(AS_E_VALIDATE, "output is not ET_REL");
        elf_close(check);
        return -1;
    }

    if (ctx->mode == AS_MODE_64) {
        if (elf_class(check) != ELFOBJ_CLASS_64 || elf_machine(check) != EM_X86_64) {
            as_diag(AS_E_VALIDATE, "output is not x86_64 ELF64");
            elf_close(check);
            return -1;
        }
    } else {
        if (elf_class(check) != ELFOBJ_CLASS_32 || elf_machine(check) != EM_386) {
            as_diag(AS_E_VALIDATE, "output is not i386 ELF32");
            elf_close(check);
            return -1;
        }
    }

    elf_close(check);
    return 0;
}

static unsigned long long wallclock_us(void) {
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0) {
        return 0;
    }
    return (unsigned long long)tv.tv_sec * 1000000ULL + (unsigned long long)tv.tv_usec;
}

static void print_target_help(const as_ctx_t *ctx) {
    (void)ctx;
    puts("Substrate assembler target help:");
    puts("  x86/i386:");
    puts("    core integer ops, jumps/calls/returns, basic x87/MMX/SSE names");
    puts("    prefixes: lock, rep/repe/repne, segment overrides, rex");
    puts("    syntax: -msyntax=att|intel, .intel_syntax/.att_syntax");
    puts("    Intel memory qualifiers parsed: byte/word/dword/qword/xmmword/ymmword/zmmword ptr");
    puts("  x86-64:");
    puts("    baseline x86-64 plus ISA levels x86-64-v2/v3/v4");
    puts("  ARMv7 / AArch64:");
    puts("    baseline branch/arithmetic syntax, register lists, condition codes");
    puts("  output:");
    puts("    -O elf (default) or -O binary (flat image: .text/.rodata/.data/.bss)");
}

static char *default_listing_path(const as_ctx_t *ctx) {
    size_t n;
    char *p;

    if (ctx == NULL || ctx->out_path == NULL) {
        return NULL;
    }
    n = strlen(ctx->out_path);
    p = (char *)malloc(n + 5);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, ctx->out_path, n);
    memcpy(p + n, ".lst", 5);
    return p;
}

static int emit_listing_file(const as_ctx_t *ctx) {
    FILE *in;
    FILE *out;
    char *line = NULL;
    size_t cap = 0;
    ssize_t nread;
    unsigned long long line_no = 0;
    char *owned_path = NULL;
    const char *path;

    if (ctx == NULL || ctx->in_path == NULL) {
        return -1;
    }
    path = ctx->listing_path;
    if (path == NULL || path[0] == '\0') {
        owned_path = default_listing_path(ctx);
        path = owned_path;
    }
    if (path == NULL) {
        as_diag(AS_E_INTERNAL, "failed to allocate listing path");
        return -1;
    }

    in = fopen(ctx->in_path, "rb");
    if (in == NULL) {
        as_diag(AS_E_INTERNAL, "failed to open source for listing: %s", ctx->in_path);
        free(owned_path);
        return -1;
    }
    out = fopen(path, "wb");
    if (out == NULL) {
        as_diag(AS_E_INTERNAL, "failed to open listing output: %s", path);
        fclose(in);
        free(owned_path);
        return -1;
    }

    while ((nread = getline(&line, &cap, in)) >= 0) {
        line_no++;
        if (fprintf(out, "%6llu  %s", line_no, line) < 0) {
            as_diag(AS_E_INTERNAL, "failed writing listing output: %s", path);
            free(line);
            fclose(in);
            fclose(out);
            free(owned_path);
            return -1;
        }
        (void)nread;
    }

    free(line);
    fclose(in);
    fclose(out);
    free(owned_path);
    return 0;
}

static void print_statistics(unsigned long long start_us, unsigned long long end_us) {
    struct rusage ru;
    unsigned long long elapsed = 0;

    if (end_us >= start_us) {
        elapsed = end_us - start_us;
    }
    if (getrusage(RUSAGE_SELF, &ru) != 0) {
        fprintf(stderr, "as: statistics: wall_us=%llu\n", elapsed);
        return;
    }
    fprintf(stderr, "as: statistics: wall_us=%llu maxrss_kb=%ld user_us=%ld sys_us=%ld\n", elapsed, ru.ru_maxrss,
            (long)ru.ru_utime.tv_sec * 1000000L + (long)ru.ru_utime.tv_usec,
            (long)ru.ru_stime.tv_sec * 1000000L + (long)ru.ru_stime.tv_usec);
}

int main(int argc, char **argv) {
    as_ctx_t ctx;
    int i;
    int query_version = 0;
    unsigned long long t_start_us;
    unsigned long long t_end_us;
    char *owned_listing_path = NULL;

    memset(&ctx, 0, sizeof(ctx));
    ctx.mode = AS_MODE_AUTO;
    ctx.out_path = "a.out.o";
    ctx.output = AS_OUTPUT_ELF;
    ctx.self_path = argv[0];
    ctx.warn_enabled = 1;
    ctx.max_input_bytes = limit_from_env("AS_MAX_INPUT_BYTES");
    ctx.max_line_bytes = limit_from_env("AS_MAX_LINE_BYTES");
    ctx.max_token_length = limit_from_env("AS_MAX_TOKEN_LENGTH");
    ctx.max_macro_depth = limit_from_env("AS_MAX_MACRO_DEPTH");
    ctx.max_include_depth = limit_from_env("AS_MAX_INCLUDE_DEPTH");
    g_emit_error_codes = getenv("AS_ERROR_CODES") != NULL ? 1 : 0;
    t_start_us = wallclock_us();
    if (toolchain_guard_enter("as") != 0) {
        return 1;
    }
    setenv("SUBSTRATE_AS_ACTIVE", "1", 1);

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const char *val = NULL;
        unsigned long long parsed = 0;

        if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
            query_version = 1;
            continue;
        }
        if (strcmp(arg, "--target-help") == 0) {
            ctx.target_help = 1;
            continue;
        }
        if (strcmp(arg, "--statistics") == 0) {
            ctx.statistics = 1;
            continue;
        }
        if (strcmp(arg, "-c") == 0) {
            continue;
        }
        if (strcmp(arg, "-O") == 0) {
            const char *value;
            if (i + 1 >= argc) {
                usage(argv[0]);
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 2;
            }
            value = argv[++i];
            if (strcmp(value, "elf") == 0) {
                ctx.output = AS_OUTPUT_ELF;
            } else if (strcmp(value, "binary") == 0 || strcmp(value, "bin") == 0) {
                ctx.output = AS_OUTPUT_BINARY;
            } else if (isdigit((unsigned char)value[0])) {
                if (push_opt_with_value(&ctx.as_opts, "-O", value) != 0) {
                    strvec_free(&ctx.gcc_opts);
                    strvec_free(&ctx.as_opts);
                    return 1;
                }
            } else {
                as_diag(AS_E_USAGE, "unsupported output format '%s' for -O (expected elf|binary)", value);
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 2;
            }
            continue;
        }
        if (strncmp(arg, "-O", 2) == 0 && arg[2] != '\0') {
            const char *value = arg + 2;
            if (strcmp(value, "elf") == 0) {
                ctx.output = AS_OUTPUT_ELF;
            } else if (strcmp(value, "binary") == 0 || strcmp(value, "bin") == 0) {
                ctx.output = AS_OUTPUT_BINARY;
            } else if (isdigit((unsigned char)value[0])) {
                if (strvec_push(&ctx.as_opts, arg) != 0) {
                    strvec_free(&ctx.gcc_opts);
                    strvec_free(&ctx.as_opts);
                    return 1;
                }
            } else {
                as_diag(AS_E_USAGE, "unsupported output format '%s' for -O (expected elf|binary)", value);
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 2;
            }
            continue;
        }
        if (strcmp(arg, "-W") == 0 || strcmp(arg, "--warn") == 0) {
            ctx.warn_enabled = 1;
            continue;
        }
        if (strcmp(arg, "--no-warn") == 0) {
            ctx.warn_enabled = 0;
            continue;
        }
        if (strcmp(arg, "--fatal-warnings") == 0) {
            ctx.fatal_warnings = 1;
            continue;
        }
        if (strcmp(arg, "-al") == 0) {
            ctx.emit_listing = 1;
            continue;
        }
        if (strncmp(arg, "-al=", 4) == 0) {
            ctx.emit_listing = 1;
            ctx.listing_path = arg + 4;
            continue;
        }
        if (strcmp(arg, "--defsym") == 0) {
            if (i + 1 >= argc || push_opt_with_value(&ctx.as_opts, "--defsym=", argv[++i]) != 0) {
                usage(argv[0]);
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 2;
            }
            continue;
        }
        if (strncmp(arg, "--defsym=", 9) == 0) {
            if (strvec_push(&ctx.as_opts, arg) != 0) {
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 1;
            }
            continue;
        }
        if (strcmp(arg, "-msyntax") == 0) {
            const char *value;

            if (i + 1 >= argc) {
                usage(argv[0]);
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 2;
            }
            value = argv[++i];
            if (strcmp(value, "intel") == 0) {
                ctx.syntax_intel = 1;
            } else if (strcmp(value, "att") == 0) {
                ctx.syntax_intel = 0;
            } else {
                as_diag(AS_E_USAGE, "unsupported -msyntax=%s (expected att|intel)", value);
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 2;
            }
            continue;
        }
        if (strncmp(arg, "-msyntax=", 9) == 0) {
            const char *value = arg + 9;

            if (strcmp(value, "intel") == 0) {
                ctx.syntax_intel = 1;
            } else if (strcmp(value, "att") == 0) {
                ctx.syntax_intel = 0;
            } else {
                as_diag(AS_E_USAGE, "unsupported -msyntax=%s (expected att|intel)", value);
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 2;
            }
            continue;
        }
        if (strcmp(arg, "--max-input-bytes") == 0 || strcmp(arg, "--max-line-bytes") == 0 ||
            strcmp(arg, "--max-token-length") == 0 || strcmp(arg, "--max-macro-depth") == 0 ||
            strcmp(arg, "--max-include-depth") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 2;
            }
            val = argv[++i];
        } else if (strncmp(arg, "--max-input-bytes=", 18) == 0) {
            val = arg + 18;
        } else if (strncmp(arg, "--max-line-bytes=", 17) == 0) {
            val = arg + 17;
        } else if (strncmp(arg, "--max-token-length=", 19) == 0) {
            val = arg + 19;
        } else if (strncmp(arg, "--max-macro-depth=", 18) == 0) {
            val = arg + 18;
        } else if (strncmp(arg, "--max-include-depth=", 20) == 0) {
            val = arg + 20;
        }
        if (val != NULL) {
            if (parse_u64(val, &parsed) != 0) {
                as_diag(AS_E_USAGE, "invalid numeric value: %s", val);
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 2;
            }
            if (strncmp(arg, "--max-input-bytes", 17) == 0) {
                ctx.max_input_bytes = parsed;
            } else if (strncmp(arg, "--max-line-bytes", 16) == 0) {
                ctx.max_line_bytes = parsed;
            } else if (strncmp(arg, "--max-token-length", 18) == 0) {
                ctx.max_token_length = parsed;
            } else if (strncmp(arg, "--max-macro-depth", 17) == 0) {
                ctx.max_macro_depth = parsed;
            } else {
                ctx.max_include_depth = parsed;
            }
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
            if (strvec_push(&ctx.gcc_opts, arg) != 0 || strvec_push(&ctx.as_opts, arg) != 0) {
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
            if (push_opt_with_value(&ctx.gcc_opts, strcmp(arg, "-I") == 0 ? "-I" : "-D", value) != 0 ||
                (strcmp(arg, "-I") == 0 && push_opt_with_value(&ctx.as_opts, "-I", value) != 0)) {
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
                as_diag(AS_E_USAGE, "invalid -Wa argument");
                strvec_free(&ctx.gcc_opts);
                strvec_free(&ctx.as_opts);
                return 2;
            }
            continue;
        }
        if (strncmp(arg, "-Wa,", 4) == 0) {
            if (strvec_push_csv(&ctx.as_opts, arg + 4) != 0) {
                as_diag(AS_E_USAGE, "invalid -Wa argument");
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
            if (strvec_push(&ctx.gcc_opts, arg) != 0 || (strncmp(arg, "-I", 2) == 0 && strvec_push(&ctx.as_opts, arg) != 0)) {
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
            as_diag(AS_E_USAGE, "multiple input files are not supported");
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

    if (ctx.target_help) {
        print_target_help(&ctx);
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
        as_diag(AS_E_USAGE, "unsupported -march=%s for %s mode", ctx.march,
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
    if (push_opt_with_value(&ctx.as_opts, "-msyntax=", ctx.syntax_intel ? "intel" : "att") != 0) {
        strvec_free(&ctx.gcc_opts);
        strvec_free(&ctx.as_opts);
        return 1;
    }
    if (ctx.warn_enabled) {
        if (strvec_push(&ctx.as_opts, "--warn") != 0) {
            strvec_free(&ctx.gcc_opts);
            strvec_free(&ctx.as_opts);
            return 1;
        }
    } else {
        if (strvec_push(&ctx.as_opts, "--no-warn") != 0) {
            strvec_free(&ctx.gcc_opts);
            strvec_free(&ctx.as_opts);
            return 1;
        }
    }
    if (ctx.fatal_warnings) {
        if (strvec_push(&ctx.as_opts, "--fatal-warnings") != 0) {
            strvec_free(&ctx.gcc_opts);
            strvec_free(&ctx.as_opts);
            return 1;
        }
    }
    if (ctx.max_include_depth > 0) {
        char lim[32];
        if (snprintf(lim, sizeof(lim), "%llu", ctx.max_include_depth) >= (int)sizeof(lim) ||
            push_opt_with_value(&ctx.gcc_opts, "-fmax-include-depth=", lim) != 0) {
            strvec_free(&ctx.gcc_opts);
            strvec_free(&ctx.as_opts);
            return 1;
        }
    }
    if (preflight_source_limits(&ctx) != 0) {
        strvec_free(&ctx.gcc_opts);
        strvec_free(&ctx.as_opts);
        return 1;
    }
    if (run_native_backend(&ctx) != 0) {
        strvec_free(&ctx.gcc_opts);
        strvec_free(&ctx.as_opts);
        return 1;
    }
    if (validate_output_file(&ctx) != 0) {
        strvec_free(&ctx.gcc_opts);
        strvec_free(&ctx.as_opts);
        return 1;
    }
    if (ctx.emit_listing) {
        if (ctx.listing_path != NULL) {
            owned_listing_path = NULL;
        } else {
            owned_listing_path = default_listing_path(&ctx);
            ctx.listing_path = owned_listing_path;
        }
        if (emit_listing_file(&ctx) != 0) {
            strvec_free(&ctx.gcc_opts);
            strvec_free(&ctx.as_opts);
            free(owned_listing_path);
            return 1;
        }
    }
    t_end_us = wallclock_us();
    if (ctx.statistics) {
        print_statistics(t_start_us, t_end_us);
    }

    strvec_free(&ctx.gcc_opts);
    strvec_free(&ctx.as_opts);
    free(owned_listing_path);
    return 0;
}
