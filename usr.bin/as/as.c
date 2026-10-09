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
    int invoked_from_cc;
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

static int as_set_env_owned(const char *name, const char *value) {
    if (name == NULL || value == NULL) {
        errno = EINVAL;
        return -1;
    }
    return setenv(name, value, 1);
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
    if (as_set_env_owned(SUBSTRATE_TC_DEPTH_ENV, depth_buf) != 0) {
        as_diag(AS_E_INTERNAL, "failed to set recursion depth env: %s", strerror(errno));
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
    if (as_set_env_owned(SUBSTRATE_TC_TRACE_ENV, trace_buf) != 0) {
        as_diag(AS_E_INTERNAL, "failed to set recursion trace env: %s", strerror(errno));
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
            "[--from-cc] "
            "[-al[=file]] [--defsym sym=val] [--statistics] [--target-help] "
            "[-Wa opts] [--max-input-bytes N] [--max-line-bytes N] [--max-token-length N] "
            "[--max-macro-depth N] [--max-include-depth N] [-o output] input.s|input.S\n",
            prog);
}

static int directive_is_explicitly_unsupported(const char *name) {
    if (name == NULL) {
        return 0;
    }
    return strcmp(name, ".if") == 0 || strcmp(name, ".ifdef") == 0 || strcmp(name, ".ifndef") == 0 ||
           strcmp(name, ".else") == 0 || strcmp(name, ".elseif") == 0 || strcmp(name, ".endif") == 0 ||
           strcmp(name, ".macro") == 0 || strcmp(name, ".endm") == 0 || strcmp(name, ".rept") == 0 ||
           strcmp(name, ".endr") == 0 || strcmp(name, ".irp") == 0 || strcmp(name, ".irpc") == 0;
}

static int validate_directives(const as_parse_result_t *parsed, char *errbuf, size_t errbuf_sz) {
    size_t i;

    if (parsed == NULL || errbuf == NULL || errbuf_sz == 0) {
        return -1;
    }
    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];

        if (st->kind != AS_STMT_DIRECTIVE) {
            continue;
        }
        if (!directive_is_explicitly_unsupported(st->u.directive.name)) {
            continue;
        }
        snprintf(errbuf, errbuf_sz, "%s:%u: unsupported directive %s",
                 st->file != NULL ? st->file : "<input>",
                 st->line,
                 st->u.directive.name != NULL ? st->u.directive.name : "<null>");
        return -1;
    }
    return 0;
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

static unsigned long long wallclock_us(void);
static void print_phase_stat(const as_ctx_t *ctx, const char *phase, unsigned long long start_us);

#define AS_PHASE_BEGIN() unsigned long long phase_start_us = wallclock_us()
#define AS_PHASE_END(ctx_, name_) print_phase_stat((ctx_), (name_), phase_start_us)

static int line_starts_with_directive(const char *line, const char *name) {
    size_t n;

    while (line != NULL && isspace((unsigned char)*line)) {
        line++;
    }
    if (line == NULL) {
        return 0;
    }
    n = strlen(name);
    return strncmp(line, name, n) == 0 && (line[n] == '\0' || isspace((unsigned char)line[n]) ||
                                           line[n] == ',' || line[n] == ';');
}

static char *trim_in_place(char *s) {
    char *end;

    while (s != NULL && isspace((unsigned char)*s)) {
        s++;
    }
    if (s == NULL || *s == '\0') {
        return s;
    }
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return s;
}

static int write_substituted_line(FILE *out, const char *line, const char *var, const char *value) {
    size_t vlen;
    const char *p;
    int quote = 0;
    int escaped = 0;

    if (out == NULL || line == NULL || var == NULL || value == NULL) {
        return -1;
    }
    vlen = strlen(var);
    for (p = line; *p != '\0'; ++p) {
        if (quote != 0) {
            if (!escaped && *p == quote) {
                quote = 0;
            }
            escaped = (!escaped && *p == '\\') ? 1 : 0;
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == ';') {
            fputc('\n', out);
            continue;
        }
        if (*p == '\\' && vlen > 0 && strncmp(p + 1, var, vlen) == 0) {
            fputs(value, out);
            p += vlen;
            continue;
        }
        fputc((unsigned char)*p, out);
    }
    if (p == line || p[-1] != '\n') {
        fputc('\n', out);
    }
    return ferror(out) ? -1 : 0;
}

static int write_split_asm_line(FILE *out, const char *line) {
    return write_substituted_line(out, line, "", "");
}

static int expand_irp_block(FILE *in, FILE *out, const char *header, int chars_mode) {
    char *spec = NULL;
    char *var;
    char *vals;
    char *comma;
    char *line = NULL;
    size_t cap = 0;
    strvec_t body = {0};
    int rc = -1;

    spec = xstrdup(header);
    if (spec == NULL) {
        return -1;
    }
    var = spec;
    while (*var != '\0' && !isspace((unsigned char)*var)) {
        var++;
    }
    var = trim_in_place(var);
    comma = strchr(var, ',');
    if (comma == NULL) {
        goto out;
    }
    *comma = '\0';
    vals = trim_in_place(comma + 1);
    var = trim_in_place(var);
    if (var == NULL || *var == '\0' || vals == NULL) {
        goto out;
    }
    if (*vals == '"' || *vals == '\'') {
        char quote = *vals++;
        char *end = strrchr(vals, quote);
        if (end != NULL) {
            *end = '\0';
        }
    }

    while (getline(&line, &cap, in) >= 0) {
        if (line_starts_with_directive(line, ".endr")) {
            break;
        }
        if (strvec_push(&body, line) != 0) {
            goto out;
        }
    }

    if (chars_mode) {
        char value[2];
        const char *p;
        value[1] = '\0';
        for (p = vals; *p != '\0'; ++p) {
            size_t i;
            if (isspace((unsigned char)*p) || *p == ',') {
                continue;
            }
            value[0] = *p;
            for (i = 0; i < body.count; ++i) {
                if (write_substituted_line(out, body.items[i], var, value) != 0) {
                    goto out;
                }
            }
        }
    } else {
        char *save = NULL;
        char *tok;
        for (tok = strtok_r(vals, ",", &save); tok != NULL; tok = strtok_r(NULL, ",", &save)) {
            size_t i;
            tok = trim_in_place(tok);
            for (i = 0; i < body.count; ++i) {
                if (write_substituted_line(out, body.items[i], var, tok) != 0) {
                    goto out;
                }
            }
        }
    }
    rc = 0;

out:
    free(line);
    strvec_free(&body);
    free(spec);
    return rc;
}

static int eval_rept_count(const char *s, long *out) {
    char *tmp;
    char *p;
    long acc = 0;
    long cur = 0;
    int have = 0;
    int sign = 1;

    if (s == NULL || out == NULL) {
        return -1;
    }
    tmp = xstrdup(s);
    if (tmp == NULL) {
        return -1;
    }
    for (p = tmp; *p != '\0'; ++p) {
        if (*p == '(' || *p == ')') {
            *p = ' ';
        }
    }
    p = trim_in_place(tmp);
    while (*p != '\0') {
        char *endp;
        long v;
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '+') {
            sign = 1;
            p++;
            continue;
        }
        if (*p == '-') {
            sign = -1;
            p++;
            continue;
        }
        v = strtol(p, &endp, 0);
        if (endp == p) {
            free(tmp);
            return -1;
        }
        cur = sign * v;
        p = endp;
        while (isspace((unsigned char)*p)) {
            p++;
        }
        while (*p == '*') {
            long rhs;
            p++;
            while (isspace((unsigned char)*p)) {
                p++;
            }
            rhs = strtol(p, &endp, 0);
            if (endp == p) {
                free(tmp);
                return -1;
            }
            cur *= rhs;
            p = endp;
            while (isspace((unsigned char)*p)) {
                p++;
            }
        }
        acc += cur;
        have = 1;
        sign = 1;
    }
    free(tmp);
    if (!have || acc < 0) {
        return -1;
    }
    *out = acc;
    return 0;
}

static int expand_rept_block(FILE *in, FILE *out, const char *header) {
    char *line = NULL;
    size_t cap = 0;
    strvec_t body = {0};
    long count;
    long iter;
    int nested = 0;
    int rc = -1;

    if (eval_rept_count(header, &count) != 0) {
        return -1;
    }
    while (getline(&line, &cap, in) >= 0) {
        const char *d = line_start_directive(line);
        if (line_starts_with_directive(d, ".rept")) {
            nested++;
        } else if (line_starts_with_directive(d, ".endr")) {
            if (nested == 0) {
                break;
            }
            nested--;
        }
        if (strvec_push(&body, line) != 0) {
            goto out;
        }
    }
    for (iter = 0; iter < count; ++iter) {
        size_t i;
        for (i = 0; i < body.count; ++i) {
            if (write_split_asm_line(out, body.items[i]) != 0) {
                goto out;
            }
        }
    }
    rc = 0;

out:
    free(line);
    strvec_free(&body);
    return rc;
}

static int expand_rept_file(const char *in_path, char **out_path) {
    FILE *in = NULL;
    FILE *out = NULL;
    char tmp_template[] = "/tmp/asrept_XXXXXX";
    char *line = NULL;
    size_t cap = 0;
    int fd = -1;
    int rc = -1;

    if (in_path == NULL || out_path == NULL) {
        return -1;
    }
    *out_path = NULL;
    in = fopen(in_path, "rb");
    if (in == NULL) {
        return -1;
    }
    fd = mkstemp(tmp_template);
    if (fd < 0) {
        goto out;
    }
    out = fdopen(fd, "wb");
    if (out == NULL) {
        close(fd);
        fd = -1;
        goto out;
    }
    fd = -1;

    while (getline(&line, &cap, in) >= 0) {
        const char *d = line_start_directive(line);
        if (line_starts_with_directive(d, ".rept")) {
            if (expand_rept_block(in, out, d + 5) != 0) {
                goto out;
            }
            continue;
        }
        if (line_starts_with_directive(d, ".irpc")) {
            if (expand_irp_block(in, out, d + 5, 1) != 0) {
                goto out;
            }
            continue;
        }
        if (line_starts_with_directive(d, ".irp")) {
            if (expand_irp_block(in, out, d + 4, 0) != 0) {
                goto out;
            }
            continue;
        }
        fputs(line, out);
    }
    if (fflush(out) != 0) {
        goto out;
    }
    *out_path = xstrdup(tmp_template);
    if (*out_path == NULL) {
        goto out;
    }
    rc = 0;

out:
    free(line);
    if (fd >= 0) {
        close(fd);
    }
    if (out != NULL) {
        fclose(out);
    }
    if (in != NULL) {
        fclose(in);
    }
    if (rc != 0) {
        unlink(tmp_template);
    }
    return rc;
}

typedef struct {
    char *name;
    strvec_t params;
    strvec_t defaults;
    strvec_t body;
} gas_macro_t;

typedef struct {
    gas_macro_t *items;
    size_t count;
    size_t cap;
    unsigned long serial;
} gas_macro_vec_t;

static void gas_macro_free(gas_macro_t *m) {
    if (m == NULL) {
        return;
    }
    free(m->name);
    strvec_free(&m->params);
    strvec_free(&m->defaults);
    strvec_free(&m->body);
    memset(m, 0, sizeof(*m));
}

static void gas_macro_vec_free(gas_macro_vec_t *v) {
    size_t i;

    if (v == NULL) {
        return;
    }
    for (i = 0; i < v->count; ++i) {
        gas_macro_free(&v->items[i]);
    }
    free(v->items);
    memset(v, 0, sizeof(*v));
}

static gas_macro_t *find_gas_macro(gas_macro_vec_t *v, const char *name) {
    size_t i;

    if (v == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < v->count; ++i) {
        if (v->items[i].name != NULL && strcmp(v->items[i].name, name) == 0) {
            return &v->items[i];
        }
    }
    return NULL;
}

static int gas_macro_vec_push(gas_macro_vec_t *v, gas_macro_t *m) {
    gas_macro_t *next;

    if (v == NULL || m == NULL || m->name == NULL) {
        return -1;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 32 : v->cap * 2;
        next = (gas_macro_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }
    v->items[v->count++] = *m;
    memset(m, 0, sizeof(*m));
    return 0;
}

static int split_macro_list(const char *s, strvec_t *out, int split_ws) {
    const char *start;
    size_t i;
    int quote = 0;
    int depth = 0;

    if (s == NULL || out == NULL) {
        return -1;
    }
    start = s;
    for (i = 0;; ++i) {
        char ch = s[i];
        int sep = 0;

        if (quote != 0) {
            if (ch == quote) {
                quote = 0;
            }
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '(' || ch == '[' || ch == '{') {
            depth++;
        } else if ((ch == ')' || ch == ']' || ch == '}') && depth > 0) {
            depth--;
        } else if ((ch == ',' || ch == '\0' || ch == '\n' ||
                    (split_ws && isspace((unsigned char)ch))) &&
                   depth == 0) {
            sep = 1;
        }

        if (sep) {
            size_t n = (size_t)(s + i - start);
            char *tmp = (char *)malloc(n + 1);
            char *trimmed;
            if (tmp == NULL) {
                return -1;
            }
            memcpy(tmp, start, n);
            tmp[n] = '\0';
            trimmed = trim_in_place(tmp);
            if (trimmed != NULL && *trimmed != '\0' && strvec_push(out, trimmed) != 0) {
                free(tmp);
                return -1;
            }
            free(tmp);
            if (ch == '\0' || ch == '\n') {
                break;
            }
            start = s + i + 1;
        }
    }
    return 0;
}

static int push_macro_arg_slice(strvec_t *out, const char *start, const char *end) {
    size_t n;
    char *tmp;
    char *trimmed;

    if (out == NULL || start == NULL || end == NULL || end < start) {
        return -1;
    }
    n = (size_t)(end - start);
    tmp = (char *)malloc(n + 1);
    if (tmp == NULL) {
        return -1;
    }
    memcpy(tmp, start, n);
    tmp[n] = '\0';
    trimmed = trim_in_place(tmp);
    if (trimmed != NULL && *trimmed != '\0' && strvec_push(out, trimmed) != 0) {
        free(tmp);
        return -1;
    }
    free(tmp);
    return 0;
}

static int split_named_macro_arg_piece(const char *s, strvec_t *out) {
    const char *start;
    size_t i;
    int quote = 0;
    int depth = 0;

    if (s == NULL || out == NULL) {
        return -1;
    }
    start = s;
    for (i = 0;; ++i) {
        char ch = s[i];

        if (quote != 0) {
            if (ch == quote) {
                quote = 0;
            }
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '(' || ch == '[' || ch == '{') {
            depth++;
        } else if ((ch == ')' || ch == ']' || ch == '}') && depth > 0) {
            depth--;
        } else if (depth == 0 && ch == '#') {
            /* GAS treats '#' as a line comment on x86. Stop here so
             * trailing comments do not become spurious positional macro
             * arguments and overwrite earlier named values. */
            return push_macro_arg_slice(out, start, s + i);
        } else if (depth == 0 && isspace((unsigned char)ch)) {
            const char *next = s + i + 1;
            while (isspace((unsigned char)*next)) {
                next++;
            }
            if (*next == '#') {
                if (push_macro_arg_slice(out, start, s + i) != 0) {
                    return -1;
                }
                return 0;
            }
            if (*next != '\0' && *next != '\n') {
                if (push_macro_arg_slice(out, start, s + i) != 0) {
                    return -1;
                }
                start = next;
                i = (size_t)(next - s) - 1;
            }
        }
        if (ch == '\0' || ch == '\n') {
            return push_macro_arg_slice(out, start, s + i);
        }
    }
}

static int split_macro_arglist(const char *s, strvec_t *out) {
    strvec_t comma_args = {0};
    size_t i;
    int rc = -1;

    if (split_macro_list(s, &comma_args, 0) != 0) {
        return -1;
    }
    for (i = 0; i < comma_args.count; ++i) {
        if (split_named_macro_arg_piece(comma_args.items[i], out) != 0) {
            goto out;
        }
    }
    rc = 0;

out:
    strvec_free(&comma_args);
    return rc;
}

static int split_macro_param_specs(const char *s, strvec_t *out) {
    return split_macro_list(s, out, 1);
}

static int parse_gas_macro_header(const char *header, gas_macro_t *m) {
    char *tmp;
    char *p;
    char *rest;
    strvec_t specs = {0};
    size_t i;

    if (header == NULL || m == NULL) {
        return -1;
    }
    memset(m, 0, sizeof(*m));
    tmp = xstrdup(header);
    if (tmp == NULL) {
        return -1;
    }
    p = trim_in_place(tmp);
    while (*p != '\0' && !isspace((unsigned char)*p) && *p != ',') {
        p++;
    }
    if (*p != '\0') {
        *p++ = '\0';
    }
    m->name = xstrdup(trim_in_place(tmp));
    rest = trim_in_place(p);
    if (m->name == NULL || m->name[0] == '\0') {
        free(tmp);
        gas_macro_free(m);
        return -1;
    }
    if (rest != NULL && *rest != '\0' && split_macro_param_specs(rest, &specs) != 0) {
        free(tmp);
        gas_macro_free(m);
        return -1;
    }
    for (i = 0; i < specs.count; ++i) {
        char *spec = specs.items[i];
        char *eq = strchr(spec, '=');
        char *colon;
        char *def = "";
        if (eq != NULL) {
            *eq = '\0';
            def = trim_in_place(eq + 1);
        }
        colon = strchr(spec, ':');
        if (colon != NULL) {
            *colon = '\0';
        }
        spec = trim_in_place(spec);
        if (spec != NULL && *spec != '\0') {
            if (strvec_push(&m->params, spec) != 0 || strvec_push(&m->defaults, def) != 0) {
                free(tmp);
                strvec_free(&specs);
                gas_macro_free(m);
                return -1;
            }
        }
    }
    strvec_free(&specs);
    free(tmp);
    return 0;
}

static char *expand_gas_macro_line_text(const char *line, const gas_macro_t *m, char **values, unsigned long serial) {
    size_t cap = 0;
    size_t len = 0;
    char *out = NULL;
    const char *p;

    if (line == NULL || m == NULL) {
        return NULL;
    }
    cap = strlen(line) + 64;
    out = (char *)malloc(cap);
    if (out == NULL) {
        return NULL;
    }
    out[0] = '\0';
    for (p = line; *p != '\0'; ++p) {
        const char *subst = NULL;
        char serial_buf[32];
        size_t subst_len = 0;
        size_t i;

        if (*p == '\\') {
            if (p[1] == '@') {
                snprintf(serial_buf, sizeof(serial_buf), "%lu", serial);
                subst = serial_buf;
                subst_len = strlen(subst);
                p++;
            } else if (p[1] == '(' && p[2] == ')') {
                p += 2;
                continue;
            } else {
                for (i = 0; i < m->params.count; ++i) {
                    size_t n = strlen(m->params.items[i]);
                    if (n > 0 && strncmp(p + 1, m->params.items[i], n) == 0 &&
                        !(isalnum((unsigned char)p[1 + n]) || p[1 + n] == '_')) {
                        subst = values[i] != NULL ? values[i] : "";
                        subst_len = strlen(subst);
                        p += n;
                        break;
                    }
                }
            }
        }
        if (subst == NULL) {
            subst = p;
            subst_len = 1;
        }
        if (len + subst_len + 1 > cap) {
            size_t ncap = (len + subst_len + 1) * 2;
            char *next = (char *)realloc(out, ncap);
            if (next == NULL) {
                free(out);
                return NULL;
            }
            out = next;
            cap = ncap;
        }
        memcpy(out + len, subst, subst_len);
        len += subst_len;
        out[len] = '\0';
    }
    return out;
}

static char *macro_arg_value_copy(const char *raw) {
    char *tmp;
    char *s;
    size_t n;

    tmp = xstrdup(raw != NULL ? raw : "");
    if (tmp == NULL) {
        return NULL;
    }
    s = trim_in_place(tmp);
    n = strlen(s);
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') || (s[0] == '\'' && s[n - 1] == '\''))) {
        s[n - 1] = '\0';
        s++;
    }
    if (s != tmp) {
        char *out = xstrdup(s);
        free(tmp);
        return out;
    }
    return tmp;
}

static int macro_arg_assign(char **slot, const char *raw) {
    char *copy = macro_arg_value_copy(raw);
    if (copy == NULL) {
        return -1;
    }
    free(*slot);
    *slot = copy;
    return 0;
}

static char *macro_named_arg_eq(char *arg) {
    char *p;

    if (arg == NULL) {
        return NULL;
    }
    p = trim_in_place(arg);
    if (!(isalpha((unsigned char)*p) || *p == '_')) {
        return NULL;
    }
    while (isalnum((unsigned char)*p) || *p == '_') {
        p++;
    }
    return *p == '=' ? p : NULL;
}

static int expand_gas_macro_statement(FILE *out, gas_macro_vec_t *macros, const char *line, unsigned depth);

static int expand_split_statement(FILE *out, gas_macro_vec_t *macros, const char *line, unsigned depth) {
    size_t i;
    const char *start = line;
    int quote = 0;
    int split = 0;

    for (i = 0; line != NULL && line[i] != '\0'; ++i) {
        char ch = line[i];
        if (quote != 0) {
            if (ch == quote) {
                quote = 0;
            }
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == ';') {
            size_t n = (size_t)(line + i - start);
            char *part = (char *)malloc(n + 2);
            if (part == NULL) {
                return -1;
            }
            memcpy(part, start, n);
            part[n] = '\n';
            part[n + 1] = '\0';
            if (expand_gas_macro_statement(out, macros, part, depth) != 0) {
                free(part);
                return -1;
            }
            free(part);
            start = line + i + 1;
            split = 1;
        }
    }
    if (split) {
        if (start != NULL && *trim_in_place((char *)start) != '\0') {
            char *tail = xstrdup(start);
            int rc;
            if (tail == NULL) {
                return -1;
            }
            rc = expand_gas_macro_statement(out, macros, tail, depth);
            free(tail);
            return rc;
        }
        return 0;
    }
    return -2;
}

static int expand_gas_macro_statement(FILE *out, gas_macro_vec_t *macros, const char *line, unsigned depth) {
    const char *p;
    const char *name_start;
    char name[128];
    size_t n = 0;
    gas_macro_t *m;
    char **values = NULL;
    strvec_t args = {0};
    size_t i;
    int rc = -1;

    if (depth > 64) {
        return -1;
    }
    {
        int split_rc = expand_split_statement(out, macros, line, depth);
        if (split_rc != -2) {
            return split_rc;
        }
    }
    p = line;
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    {
        const char *label_start = p;
        const char *q = p;
        while (*q != '\0' && (isalnum((unsigned char)*q) || *q == '_' || *q == '.')) {
            q++;
        }
        if (q > label_start && *q == ':') {
            const char *rest = q + 1;
            const char *rp;
            char rname[128];
            size_t rn = 0;

            while (*rest != '\0' && isspace((unsigned char)*rest)) {
                rest++;
            }
            rp = rest;
            while (*rp != '\0' && (isalnum((unsigned char)*rp) || *rp == '_' || *rp == '.')) {
                if (rn + 1 < sizeof(rname)) {
                    rname[rn++] = *rp;
                }
                rp++;
            }
            rname[rn] = '\0';
            if (rn > 0 && find_gas_macro(macros, rname) != NULL) {
                if (fprintf(out, "%.*s:\n", (int)(q - label_start), label_start) < 0) {
                    return -1;
                }
                return expand_gas_macro_statement(out, macros, rest, depth + 1);
            }
        }
    }
    name_start = p;
    while (*p != '\0' && (isalnum((unsigned char)*p) || *p == '_' || *p == '.')) {
        if (n + 1 < sizeof(name)) {
            name[n++] = *p;
        }
        p++;
    }
    name[n] = '\0';
    if (n == 0 || (m = find_gas_macro(macros, name)) == NULL) {
        return write_split_asm_line(out, line);
    }
    values = (char **)calloc(m->params.count, sizeof(*values));
    if (values == NULL) {
        return -1;
    }
    for (i = 0; i < m->params.count; ++i) {
        values[i] = xstrdup(i < m->defaults.count ? m->defaults.items[i] : "");
        if (values[i] == NULL) {
            goto out;
        }
    }
    if (split_macro_arglist(p, &args) != 0) {
        goto out;
    }
    {
        size_t pos = 0;
        for (i = 0; i < args.count; ++i) {
            char *eq = macro_named_arg_eq(args.items[i]);
            if (eq != NULL) {
                size_t k;
                *eq = '\0';
                for (k = 0; k < m->params.count; ++k) {
                    if (strcmp(trim_in_place(args.items[i]), m->params.items[k]) == 0) {
                        if (macro_arg_assign(&values[k], trim_in_place(eq + 1)) != 0) {
                            goto out;
                        }
                        break;
                    }
                }
            } else if (pos < m->params.count) {
                if (macro_arg_assign(&values[pos], trim_in_place(args.items[i])) != 0) {
                    goto out;
                }
                pos++;
            }
        }
    }
    {
        unsigned long macro_serial = ++macros->serial;
        for (i = 0; i < m->body.count; ++i) {
            char *expanded = expand_gas_macro_line_text(m->body.items[i], m, values, macro_serial);
        if (expanded == NULL) {
            goto out;
        }
        if (expand_gas_macro_statement(out, macros, expanded, depth + 1) != 0) {
            free(expanded);
            goto out;
        }
        free(expanded);
        }
    }
    rc = 0;

out:
    if (values != NULL) {
        for (i = 0; i < m->params.count; ++i) {
            free(values[i]);
        }
    }
    free(values);
    strvec_free(&args);
    (void)name_start;
    return rc;
}

static int expand_rept_block_with_macros(FILE *in, FILE *out, gas_macro_vec_t *macros, const char *header) {
    char *line = NULL;
    size_t cap = 0;
    strvec_t body = {0};
    long count;
    long iter;
    int nested = 0;
    int rc = -1;

    if (eval_rept_count(header, &count) != 0) {
        return -1;
    }
    while (getline(&line, &cap, in) >= 0) {
        const char *d = line_start_directive(line);
        if (line_starts_with_directive(d, ".rept")) {
            nested++;
        } else if (line_starts_with_directive(d, ".endr")) {
            if (nested == 0) {
                break;
            }
            nested--;
        }
        if (strvec_push(&body, line) != 0) {
            goto out;
        }
    }
    for (iter = 0; iter < count; ++iter) {
        size_t i;
        for (i = 0; i < body.count; ++i) {
            if (expand_gas_macro_statement(out, macros, body.items[i], 0) != 0) {
                goto out;
            }
        }
    }
    rc = 0;

out:
    free(line);
    strvec_free(&body);
    return rc;
}

static int expand_gas_source_controls(const char *in_path, char **out_path) {
    FILE *in = NULL;
    FILE *out = NULL;
    char tmp_template[] = "/tmp/asexp_XXXXXX";
    char *line = NULL;
    size_t cap = 0;
    int fd = -1;
    gas_macro_vec_t macros = {0};
    int rc = -1;

    if (in_path == NULL || out_path == NULL) {
        return -1;
    }
    *out_path = NULL;
    in = fopen(in_path, "rb");
    if (in == NULL) {
        return -1;
    }
    fd = mkstemp(tmp_template);
    if (fd < 0) {
        goto out;
    }
    out = fdopen(fd, "wb");
    if (out == NULL) {
        close(fd);
        fd = -1;
        goto out;
    }
    fd = -1;

    while (getline(&line, &cap, in) >= 0) {
        const char *d = line_start_directive(line);

        if (line_starts_with_directive(d, ".macro")) {
            gas_macro_t m;
            if (parse_gas_macro_header(d + 6, &m) != 0) {
                goto out;
            }
            while (getline(&line, &cap, in) >= 0) {
                if (line_starts_with_directive(line_start_directive(line), ".endm")) {
                    break;
                }
                if (strvec_push(&m.body, line) != 0) {
                    gas_macro_free(&m);
                    goto out;
                }
            }
            if (gas_macro_vec_push(&macros, &m) != 0) {
                gas_macro_free(&m);
                goto out;
            }
            continue;
        }
        if (line_starts_with_directive(d, ".rept")) {
            if (expand_rept_block_with_macros(in, out, &macros, d + 5) != 0) {
                goto out;
            }
            continue;
        }
        if (line_starts_with_directive(d, ".irpc")) {
            if (expand_irp_block(in, out, d + 5, 1) != 0) {
                goto out;
            }
            continue;
        }
        if (line_starts_with_directive(d, ".irp")) {
            if (expand_irp_block(in, out, d + 4, 0) != 0) {
                goto out;
            }
            continue;
        }
        if (expand_gas_macro_statement(out, &macros, line, 0) != 0) {
            goto out;
        }
    }

    if (fflush(out) != 0) {
        goto out;
    }
    *out_path = xstrdup(tmp_template);
    if (*out_path == NULL) {
        goto out;
    }
    rc = 0;

out:
    free(line);
    gas_macro_vec_free(&macros);
    if (fd >= 0) {
        close(fd);
    }
    if (out != NULL) {
        fclose(out);
    }
    if (in != NULL) {
        fclose(in);
    }
    if (rc != 0) {
        unlink(tmp_template);
    }
    return rc;
}

typedef struct {
    int parent_active;
    int branch_taken;
    int active;
    int else_seen;
} gas_cond_frame_t;

static int parse_cond_operand(const char *s, long long *num, char *buf, size_t bufsz) {
    char *tmp;
    char *p;
    char *endp;

    if (s == NULL || num == NULL || buf == NULL || bufsz == 0) {
        return -1;
    }
    tmp = xstrdup(s);
    if (tmp == NULL) {
        return -1;
    }
    p = trim_in_place(tmp);
    if (*p == '$') {
        p++;
    }
    *num = strtoll(p, &endp, 0);
    if (endp != p && *trim_in_place(endp) == '\0') {
        free(tmp);
        buf[0] = '\0';
        return 1;
    }
    snprintf(buf, bufsz, "%s", p);
    free(tmp);
    return 0;
}

static int eval_gas_cond_expr(const char *expr) {
    char *tmp;
    char *p;
    char *op;
    long long ln = 0;
    long long rn = 0;
    char lb[128];
    char rb[128];
    int lt;
    int rt;
    int result;
    int op_kind = -1; /* 0:==, 1:!=, 2:<, 3:>, 4:<=, 5:>= */
    size_t op_len = 0;

    if (expr == NULL) {
        return 0;
    }
    tmp = xstrdup(expr);
    if (tmp == NULL) {
        return 0;
    }
    p = trim_in_place(tmp);
    /* Order matters: check 2-char operators before single-char ones so
     * `<=` is not classified as `<` followed by `=`. */
    if ((op = strstr(p, "==")) != NULL) {
        op_kind = 0; op_len = 2;
    } else if ((op = strstr(p, "!=")) != NULL) {
        op_kind = 1; op_len = 2;
    } else if ((op = strstr(p, "<=")) != NULL) {
        op_kind = 4; op_len = 2;
    } else if ((op = strstr(p, ">=")) != NULL) {
        op_kind = 5; op_len = 2;
    } else if ((op = strchr(p, '<')) != NULL && op[1] != '<') {
        op_kind = 2; op_len = 1;
    } else if ((op = strchr(p, '>')) != NULL && op[1] != '>') {
        op_kind = 3; op_len = 1;
    } else {
        op = NULL;
    }
    if (op != NULL) {
        *op = '\0';
        lt = parse_cond_operand(p, &ln, lb, sizeof(lb));
        rt = parse_cond_operand(op + op_len, &rn, rb, sizeof(rb));
        if (lt == 1 && rt == 1) {
            switch (op_kind) {
            case 0: result = (ln == rn); break;
            case 1: result = (ln != rn); break;
            case 2: result = (ln < rn); break;
            case 3: result = (ln > rn); break;
            case 4: result = (ln <= rn); break;
            case 5: result = (ln >= rn); break;
            default: result = 0; break;
            }
        } else {
            int cmp = strcmp(trim_in_place(lb), trim_in_place(rb));
            switch (op_kind) {
            case 0: result = (cmp == 0); break;
            case 1: result = (cmp != 0); break;
            case 2: result = (cmp < 0); break;
            case 3: result = (cmp > 0); break;
            case 4: result = (cmp <= 0); break;
            case 5: result = (cmp >= 0); break;
            default: result = 0; break;
            }
        }
        free(tmp);
        return result;
    }
    lt = parse_cond_operand(p, &ln, lb, sizeof(lb));
    free(tmp);
    if (lt == 1) {
        return ln != 0;
    }
    return lb[0] != '\0';
}

static int filter_gas_conditionals(const char *in_path, char **out_path) {
    FILE *in = NULL;
    FILE *out = NULL;
    char tmp_template[] = "/tmp/ascond_XXXXXX";
    char *line = NULL;
    size_t cap = 0;
    int fd = -1;
    gas_cond_frame_t stack[1024];
    size_t depth = 0;
    int active = 1;
    int rc = -1;

    if (in_path == NULL || out_path == NULL) {
        return -1;
    }
    *out_path = NULL;
    in = fopen(in_path, "rb");
    if (in == NULL) {
        return -1;
    }
    fd = mkstemp(tmp_template);
    if (fd < 0) {
        goto out;
    }
    out = fdopen(fd, "wb");
    if (out == NULL) {
        close(fd);
        fd = -1;
        goto out;
    }
    fd = -1;

    while (getline(&line, &cap, in) >= 0) {
        const char *d = line_start_directive(line);

        if (line_starts_with_directive(d, ".ifdef") || line_starts_with_directive(d, ".ifndef")) {
            gas_cond_frame_t *f;
            int is_ifndef = line_starts_with_directive(d, ".ifndef");
            if (depth >= sizeof(stack) / sizeof(stack[0])) {
                goto out;
            }
            f = &stack[depth++];
            f->parent_active = active;
            f->branch_taken = f->parent_active && is_ifndef;
            f->active = f->branch_taken;
            f->else_seen = 0;
            active = f->active;
            fputc('\n', out);
            continue;
        }
        if (line_starts_with_directive(d, ".ifb") || line_starts_with_directive(d, ".ifnb")) {
            gas_cond_frame_t *f;
            int is_ifnb = line_starts_with_directive(d, ".ifnb");
            const char *arg = d + (is_ifnb ? 5 : 4);
            char *tmp = xstrdup(arg);
            char *trimmed;
            int nonblank;
            if (tmp == NULL || depth >= sizeof(stack) / sizeof(stack[0])) {
                free(tmp);
                goto out;
            }
            trimmed = trim_in_place(tmp);
            nonblank = trimmed != NULL && *trimmed != '\0';
            f = &stack[depth++];
            f->parent_active = active;
            f->branch_taken = f->parent_active && (is_ifnb ? nonblank : !nonblank);
            f->active = f->branch_taken;
            f->else_seen = 0;
            active = f->active;
            free(tmp);
            fputc('\n', out);
            continue;
        }
        if (line_starts_with_directive(d, ".ifc") || line_starts_with_directive(d, ".ifnc")) {
            /* GAS string-equality conditional: .ifc str1, str2 */
            int is_negated = line_starts_with_directive(d, ".ifnc");
            gas_cond_frame_t *f;
            const char *args = d + (is_negated ? 5 : 4);
            char *tmp = xstrdup(args);
            char *trimmed;
            char *comma;
            int eq = 0;
            if (tmp == NULL || depth >= sizeof(stack) / sizeof(stack[0])) {
                free(tmp);
                goto out;
            }
            trimmed = trim_in_place(tmp);
            comma = strchr(trimmed, ',');
            if (comma != NULL) {
                char *lhs;
                char *rhs;
                *comma = '\0';
                lhs = trim_in_place(trimmed);
                rhs = trim_in_place(comma + 1);
                eq = strcmp(lhs != NULL ? lhs : "", rhs != NULL ? rhs : "") == 0;
            }
            f = &stack[depth++];
            f->parent_active = active;
            f->branch_taken = f->parent_active && (is_negated ? !eq : eq);
            f->active = f->branch_taken;
            f->else_seen = 0;
            active = f->active;
            free(tmp);
            fputc('\n', out);
            continue;
        }
        if (line_starts_with_directive(d, ".ifeq") || line_starts_with_directive(d, ".ifne") ||
            line_starts_with_directive(d, ".ifgt") || line_starts_with_directive(d, ".iflt") ||
            line_starts_with_directive(d, ".ifge") || line_starts_with_directive(d, ".ifle")) {
            /* GAS arithmetic conditionals: .ifeq/.ifne/.ifgt/.iflt/.ifge/.ifle EXPR
             * compare EXPR against zero. */
            gas_cond_frame_t *f;
            const char *args = d + 5;
            long long v = 0;
            int truth = 0;
            if (depth >= sizeof(stack) / sizeof(stack[0])) {
                goto out;
            }
            {
                char *endp;
                while (*args == ' ' || *args == '\t') args++;
                v = strtoll(args, &endp, 0);
                (void)endp;
            }
            if (line_starts_with_directive(d, ".ifeq")) truth = (v == 0);
            else if (line_starts_with_directive(d, ".ifne")) truth = (v != 0);
            else if (line_starts_with_directive(d, ".ifgt")) truth = (v > 0);
            else if (line_starts_with_directive(d, ".iflt")) truth = (v < 0);
            else if (line_starts_with_directive(d, ".ifge")) truth = (v >= 0);
            else if (line_starts_with_directive(d, ".ifle")) truth = (v <= 0);
            f = &stack[depth++];
            f->parent_active = active;
            f->branch_taken = f->parent_active && truth;
            f->active = f->branch_taken;
            f->else_seen = 0;
            active = f->active;
            fputc('\n', out);
            continue;
        }
        if (line_starts_with_directive(d, ".if")) {
            gas_cond_frame_t *f;
            if (depth >= sizeof(stack) / sizeof(stack[0])) {
                goto out;
            }
            f = &stack[depth++];
            f->parent_active = active;
            f->branch_taken = f->parent_active && eval_gas_cond_expr(d + 3);
            f->active = f->branch_taken;
            f->else_seen = 0;
            active = f->active;
            fputc('\n', out);
            continue;
        }
        if (line_starts_with_directive(d, ".elseif")) {
            gas_cond_frame_t *f;
            if (depth == 0) {
                goto out;
            }
            f = &stack[depth - 1];
            if (f->else_seen || f->branch_taken || !f->parent_active) {
                f->active = 0;
            } else {
                f->active = eval_gas_cond_expr(d + 7);
                if (f->active) {
                    f->branch_taken = 1;
                }
            }
            active = f->active;
            fputc('\n', out);
            continue;
        }
        if (line_starts_with_directive(d, ".else")) {
            gas_cond_frame_t *f;
            if (depth == 0) {
                goto out;
            }
            f = &stack[depth - 1];
            f->active = f->parent_active && !f->branch_taken && !f->else_seen;
            f->branch_taken = f->branch_taken || f->active;
            f->else_seen = 1;
            active = f->active;
            fputc('\n', out);
            continue;
        }
        if (line_starts_with_directive(d, ".endif")) {
            if (depth == 0) {
                goto out;
            }
            depth--;
            active = depth == 0 ? 1 : stack[depth - 1].active;
            fputc('\n', out);
            continue;
        }
        if (active) {
            fputs(line, out);
        } else {
            fputc('\n', out);
        }
    }
    if (depth != 0 || fflush(out) != 0) {
        goto out;
    }
    *out_path = xstrdup(tmp_template);
    if (*out_path == NULL) {
        goto out;
    }
    rc = 0;

out:
    free(line);
    if (fd >= 0) {
        close(fd);
    }
    if (out != NULL) {
        fclose(out);
    }
    if (in != NULL) {
        fclose(in);
    }
    if (rc != 0) {
        unlink(tmp_template);
    }
    return rc;
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
    char *temp_expanded = NULL;
    char *temp_rept = NULL;
    char *temp_cond = NULL;
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

    if (expand_gas_source_controls(src_path, &temp_expanded) != 0) {
        as_diag(AS_E_BACKEND, "macro/control expansion stage failed");
        goto out;
    }
    src_path = temp_expanded;
    if (expand_rept_file(src_path, &temp_rept) != 0) {
        as_diag(AS_E_BACKEND, "repeat expansion stage failed");
        goto out;
    }
    src_path = temp_rept;
    if (filter_gas_conditionals(src_path, &temp_cond) != 0) {
        as_diag(AS_E_BACKEND, "conditional assembly stage failed");
        goto out;
    }
    src_path = temp_cond;

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
    ecfg.x86_code_bits = ctx->mode == AS_MODE_64 ? 64u : 32u;
    ecfg.use_rela = ctx->mode == AS_MODE_64 ? 1u : 0u;
    ecfg.x86_64_isa_level = (unsigned)x64_isa_level_from_march(ctx->march);
    ecfg.intel_syntax = (unsigned)(ctx->syntax_intel ? 1 : 0);

    {
        AS_PHASE_BEGIN();
        if (as_lex_file(src_path, &lcfg, &toks, errbuf, sizeof(errbuf)) != 0) {
            as_diag(AS_E_BACKEND, "%s", errbuf);
            goto out;
        }
        AS_PHASE_END(ctx, "lex");
    }
    {
        AS_PHASE_BEGIN();
        if (as_parse_tokens(&toks, &pcfg, &parsed, errbuf, sizeof(errbuf)) != 0) {
            as_diag(AS_E_BACKEND, "%s", errbuf);
            goto out;
        }
        AS_PHASE_END(ctx, "parse");
    }
    {
        AS_PHASE_BEGIN();
        if (validate_directives(&parsed, errbuf, sizeof(errbuf)) != 0) {
            as_diag(AS_E_BACKEND, "%s", errbuf);
            goto out;
        }
        AS_PHASE_END(ctx, "validate");
    }
    {
        AS_PHASE_BEGIN();
        if (as_symtab_build(&parsed, &syms, errbuf, sizeof(errbuf)) != 0) {
            as_diag(AS_E_BACKEND, "%s", errbuf);
            goto out;
        }
        AS_PHASE_END(ctx, "symtab");
    }
    {
        AS_PHASE_BEGIN();
        if (as_sections_build(&parsed, &secs, errbuf, sizeof(errbuf)) != 0) {
            as_diag(AS_E_BACKEND, "%s", errbuf);
            goto out;
        }
        AS_PHASE_END(ctx, "sections");
    }
    {
        AS_PHASE_BEGIN();
        if (as_data_build(&parsed, &data, errbuf, sizeof(errbuf)) != 0) {
            as_diag(AS_E_BACKEND, "%s", errbuf);
            goto out;
        }
        AS_PHASE_END(ctx, "data");
    }
    if (ctx->output == AS_OUTPUT_BINARY) {
        AS_PHASE_BEGIN();
        if (as_elf_emit_binary_file(&parsed, &secs, &ecfg, ctx->out_path, errbuf, sizeof(errbuf)) != 0) {
            as_diag(AS_E_BACKEND, "%s", errbuf);
            goto out;
        }
        AS_PHASE_END(ctx, "emit-binary");
    } else {
        AS_PHASE_BEGIN();
        if (as_elf_emit_file(&parsed, &secs, &syms, &data, &ecfg, ctx->out_path, errbuf, sizeof(errbuf)) != 0) {
            as_diag(AS_E_BACKEND, "%s", errbuf);
            goto out;
        }
        AS_PHASE_END(ctx, "emit-elf");
    }

    rc = 0;

out:
    if (temp_pp != NULL && getenv("AS_KEEP_TEMPS") == NULL) {
        unlink(temp_pp);
    }
    if (temp_expanded != NULL && getenv("AS_KEEP_TEMPS") == NULL) {
        unlink(temp_expanded);
    }
    if (temp_rept != NULL && getenv("AS_KEEP_TEMPS") == NULL) {
        unlink(temp_rept);
    }
    if (temp_cond != NULL && getenv("AS_KEEP_TEMPS") == NULL) {
        unlink(temp_cond);
    }
    free(temp_pp);
    free(temp_expanded);
    free(temp_rept);
    free(temp_cond);
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

#ifdef AS_SUBSTRATE_BUILD
    return 0;
#endif

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

static void print_phase_stat(const as_ctx_t *ctx, const char *phase, unsigned long long start_us) {
    unsigned long long now;

    if (ctx == NULL || !ctx->statistics || phase == NULL) {
        return;
    }
    now = wallclock_us();
    fprintf(stderr, "as: statistics: phase=%s wall_us=%llu\n", phase,
            now >= start_us ? now - start_us : 0ULL);
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
        if (strcmp(arg, "--from-cc") == 0) {
            ctx.invoked_from_cc = 1;
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
