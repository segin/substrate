#include "as_data.h"
#include "as_elf_emit.h"
#include "as_lexer.h"
#include "as_parser.h"
#include "as_lower.h"
#include "as_sections.h"
#include "as_symtab.h"
#include "as_util.h"
#include "elfobj.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
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

#define SUBSTRATE_TC_DEPTH_ENV "SUBSTRATE_TC_DEPTH"
#define SUBSTRATE_TC_TRACE_ENV "SUBSTRATE_TC_TRACE"
#define SUBSTRATE_TC_MAX_ENV "SUBSTRATE_TC_MAX_DEPTH"
#define SUBSTRATE_TC_DEFAULT_MAX 64

/* How large a source may grow as its macros and repeats are expanded,
 * where --max-expansion-bytes does not say: 256 MiB. */
#define AS_EXPANSION_BYTES_DEFAULT (256ull * 1024 * 1024)

/* How deep files may include files, where --max-include-depth does not
 * say. */
#define AS_INCLUDE_DEPTH_DEFAULT 64

/* How deep macros may be expanded, where --max-macro-depth does not say. */
#define AS_MACRO_DEPTH_DEFAULT 64

/* What the standard input is called in a message, as GNU as calls it. */
#define AS_STDIN_NAME "{standard input}"

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
    /* 0 for x86; EM_ARM or EM_AARCH64. */
    unsigned target;
    /* ARM: the source begins as Thumb (-mthumb). */
    int arm_thumb;
    int syntax_intel;
    as_output_t output;
    int emit_listing;
    int statistics;
    int target_help;
    int invoked_from_cc;
    int warn_enabled;
    int fatal_warnings;
    /* -g: a line table of the source itself, if it has none of its own. */
    int gen_debug;
    const char *in_path;
    /* The source, where it is not the one file in_path names: several
     * files one after another, or the standard input. */
    struct as_text *gathered;
    const char *out_path;
    const char *listing_path;
    /* The listing's path where the assembler made it up, to be freed. */
    char *owned_listing_path;
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

/*
 * Every error is written here.  A statement's place is a line of the
 * expanded source under an internal name (as_lexer.h); it is given as
 * the file and line the user wrote it at.
 */
static void as_diag(as_error_code_t code, const char *fmt, ...) {
    char said[1024];
    char shown[1024];
    va_list ap;

    if (g_emit_error_codes) {
        fprintf(stderr, "[%s] ", as_error_code_name(code));
    }
    va_start(ap, fmt);
    vsnprintf(said, sizeof(said), fmt, ap);
    va_end(ap);
    as_source_positions_in_text(said, shown, sizeof(shown));
    /*
     * What is wrong with a line of the source is said as every
     * assembler and compiler says it, `file:line: Error: text`, which is
     * what an editor or a build looks for.  What is wrong with the run
     * -- an option, a file that will not open -- has no line, and is
     * the program's to say.
     */
    {
        const char *p;

        for (p = shown; (p = strchr(p, ':')) != NULL; ++p) {
            const char *q = p + 1;

            while (isdigit((unsigned char)*q)) {
                q++;
            }
            if (q > p + 1 && q[0] == ':' && q[1] == ' ' && q[2] != '\0') {
                fprintf(stderr, "%.*s Error: %s\n", (int)(q + 1 - shown), shown, q + 2);
                return;
            }
        }
    }
    fprintf(stderr, "as: error: %s\n", shown);
}

/*
 * The options, for --help: every one that is taken, and none that is
 * not.  tests/usr.bin/as/test_cli_help.sh holds the two to each other
 * -- it tries each line of this, and looks for each option the
 * argument loop compares with in what this prints.
 */
static const struct {
    const char *option; /* as it is written, with what follows it */
    const char *what;
} as_options[] = {
    {"-o FILE", "the object to write (a.out)"},
    {"-32, --32", "assemble for i386"},
    {"-64, --64", "assemble for x86-64"},
    {"--target=NAME", "the machine: i386, x86_64, arm, aarch64 (--target NAME)"},
    {"-march=CPU[+EXT...]", "the processor whose instructions are allowed (-march CPU)"},
    {"-mtune=CPU", "taken, and changes nothing (-mtune CPU)"},
    {"-msyntax=att|intel", "the syntax the source begins in (-msyntax NAME)"},
    {"-mthumb, -marm", "ARM: the instruction set the source begins in"},
    {"-mcpu=NAME, -mfpu=NAME, -mfloat-abi=NAME", "ARM and AArch64: taken, and allow everything"},
    {"-O elf|binary", "an ELF object, or a flat image of its sections (-Obinary)"},
    {"-g, --gen-debug, --gdwarf-N", "a line table for the source itself"},
    {"-I DIR", "where .include and .incbin look (-IDIR)"},
    {"-D NAME[=VALUE]", "a macro for the C preprocessor, for a .S source (-DNAME)"},
    {"--defsym SYM=VALUE", "define a symbol before the source is read"},
    {"-al[=FILE]", "a listing, on the standard output or in FILE"},
    {"-c", "taken, and changes nothing"},
    {"--warn", "warnings are written (so they are unless -W is given)"},
    {"-W, --no-warn", "warnings are not written"},
    {"--fatal-warnings", "a warning is an error"},
    {"--noexecstack", "taken: the stack is never marked executable"},
    {"--from-cc", "run by the compiler driver, which this then does not run"},
    {"-Wa,OPTS", "options handed on by a compiler driver (-Wa OPTS)"},
    {"--statistics", "the time each pass took, on the standard error"},
    {"--max-input-bytes N", "refuse a source longer than this"},
    {"--max-line-bytes N", "refuse a line longer than this"},
    {"--max-token-length N", "refuse a token longer than this"},
    {"--max-macro-depth N", "refuse macros nested deeper than this"},
    {"--max-include-depth N", "refuse files included deeper than this"},
    {"--max-expansion-bytes N", "refuse a source that expands to more than this"},
    {"--version, -v", "say what this is, and assemble nothing"},
    {"--help", "this"},
    {"--target-help", "what is assembled for each machine"},
};

static void print_help(FILE *out, const char *prog) {
    size_t i;

    fprintf(out, "usage: %s [options] [-o output] [file ...]\n", prog);
    fprintf(out, "With no file, or for the file `-', the source is the standard input.\n\n");
    for (i = 0; i < sizeof(as_options) / sizeof(as_options[0]); ++i) {
        fprintf(out, "  %-24s   %s\n", as_options[i].option, as_options[i].what);
    }
}

static void usage(const char *prog) {
    fprintf(stderr, "usage: %s [options] [-o output] [file ...]\n%s --help lists the options\n", prog, prog);
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

/*
 * Whether a directive is one some part of the assembler acts on, by the
 * time the conditionals, the macros and the directives that are other
 * directives rewritten are gone.  One that is not was passed over in
 * silence: whatever it was to store or to change was not, and the object
 * was written as if the line had not been there.
 */
static int directive_is_known(const char *name) {
    static const char *const known[] = {
        /* data */
        ".byte", ".short", ".word", ".hword", ".2byte", ".long", ".int", ".4byte", ".quad", ".8byte",
        ".ascii", ".asciz", ".string", ".float", ".double", ".fill", ".skip", ".space", ".zero", ".org",
        ".incbin", ".align", ".balign", ".p2align", ".uleb128", ".sleb128",
        /* sections */
        ".text", ".data", ".bss", ".rodata", ".section", ".pushsection", ".popsection", ".previous",
        ".subsection", ".group",
        /* symbols */
        ".globl", ".global", ".local", ".weak", ".hidden", ".internal", ".protected", ".type", ".size",
        ".comm", ".lcomm", ".symver", ".set", ".equ", ".equiv",
        /* modes */
        ".code16", ".code32", ".code64", ".intel_syntax", ".att_syntax",
        /* debugging */
        ".file", ".loc", ".ident",
        /*
         * And those GNU as takes that say nothing of the bytes: a
         * declaration it ignores too, the instruction sets to allow,
         * listings, and debugging formats that are not written.
         */
        ".extern", ".arch", ".loc_mark_labels", ".line", ".ln", ".eject", ".list", ".nolist", ".title",
        ".sbttl", ".psize", ".stabs", ".stabn", ".stabd", ".version", ".gnu_attribute", ".addrsig",
        ".addrsig_sym", ".sse_check", ".operand_check",
        /* The way macros are read unless .altmacro says otherwise, and
         * .altmacro is not taken. */
        ".noaltmacro",
    };
    size_t i;

    if (name == NULL) {
        return 0;
    }
    if (strncmp(name, ".cfi_", 5) == 0) {
        return 1;
    }
    for (i = 0; i < sizeof(known) / sizeof(known[0]); ++i) {
        if (strcmp(name, known[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The text of a directive that says something: its one argument, a
 * string, or `fallback` if it has none. */
static char *message_text(const as_directive_t *d, const char *fallback) {
    char *bytes = NULL;
    size_t len = 0;
    char *text;

    if (d->arg_count < 1 || d->args[0] == NULL || d->args[0][0] == '\0' ||
        as_decode_string_literal(d->args[0], &bytes, &len) != 0) {
        len = strlen(fallback);
        text = (char *)malloc(len + 1);
        if (text != NULL) {
            memcpy(text, fallback, len + 1);
        }
        return text;
    }
    text = (char *)malloc(len + 1);
    if (text != NULL) {
        memcpy(text, bytes, len);
        text[len] = '\0';
    }
    free(bytes);
    return text;
}

/*
 * The directives that say something to whoever is assembling, and those
 * that are not directives at all.
 *
 * .error and .err are errors, each reported and the assembly failed when
 * all have been; .abort is one that ends it there; .warning is a warning
 * and .print a line on standard output; .fail is an error below 500 and
 * a warning from there.  They are done here, after the conditionals,
 * so that one in a branch not taken says nothing.  A directive that is
 * not known is an error, named with its line.
 *
 * 0, or -1 with every message written already.
 */
static int run_message_directives(as_parse_result_t *parsed, as_elf_warnings_t *warnings) {
    unsigned errors = 0;
    size_t i;

    for (i = 0; i < parsed->count; ++i) {
        as_stmt_t *st = &parsed->items[i];
        as_directive_t *d = &st->u.directive;
        const char *file = st->file != NULL ? st->file : "<input>";
        const char *said = NULL;
        char *text = NULL;
        int is_error = 0;
        size_t k;

        if (st->kind != AS_STMT_DIRECTIVE || d->name == NULL) {
            continue;
        }
        if (strcmp(d->name, ".error") == 0) {
            text = message_text(d, ".error directive invoked in source file");
            is_error = 1;
        } else if (strcmp(d->name, ".err") == 0) {
            said = ".err encountered";
            is_error = 1;
        } else if (strcmp(d->name, ".abort") == 0) {
            as_diag(AS_E_BACKEND, "%s:%u: .abort detected: the assembly is abandoned", file, st->line);
            return -1;
        } else if (strcmp(d->name, ".warning") == 0) {
            text = message_text(d, ".warning directive invoked in source file");
        } else if (strcmp(d->name, ".print") == 0) {
            text = message_text(d, "");
            if (text != NULL) {
                puts(text);
            }
            free(text);
            text = NULL;
            said = "";
        } else if (strcmp(d->name, ".fail") == 0) {
            long long n = 0;
            static char fail_text[64];

            if (d->arg_count >= 1 && d->args[0] != NULL) {
                (void)as_expr_eval_string(d->args[0], NULL, NULL, &n);
            }
            snprintf(fail_text, sizeof(fail_text), ".fail %lld encountered", n);
            said = fail_text;
            is_error = n < 500;
        } else if (!directive_is_known(d->name)) {
            as_diag(AS_E_BACKEND, "%s:%u: unknown directive %s", file, st->line, d->name);
            errors++;
            continue;
        } else {
            continue;
        }
        if (text != NULL) {
            said = text;
        }
        if (said != NULL && said[0] != '\0') {
            if (is_error) {
                as_diag(AS_E_BACKEND, "%s:%u: %s", file, st->line, said);
                errors++;
            } else if (!warnings->suppress) {
                /* Where the user wrote it, and the macro it is of the
                 * expansion of. */
                char written[1024];
                char shown[1024];

                snprintf(written, sizeof(written), "%s:%u: Warning: %s\n", file, st->line, said);
                as_source_positions_in_text(written, shown, sizeof(shown));
                fputs(shown, stderr);
                warnings->count++;
            }
        }
        free(text);
        /* Said; what is left of the statement is its labels. */
        for (k = 0; k < d->arg_count; ++k) {
            free(d->args[k]);
        }
        free(d->args);
        free(d->name);
        memset(&st->u, 0, sizeof(st->u));
        st->kind = st->label_count != 0 ? AS_STMT_LABEL_ONLY : AS_STMT_EMPTY;
    }
    return errors != 0 ? -1 : 0;
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

static ssize_t text_getline(char **line, size_t *cap, struct as_text *t);
static void text_rewind(struct as_text *t);

static void preflight_done(FILE *fp, const as_ctx_t *ctx) {
    if (fp != NULL) {
        fclose(fp);
    }
    if (ctx->gathered != NULL) {
        text_rewind(ctx->gathered);
    }
}

static int preflight_source_limits(const as_ctx_t *ctx) {
    FILE *fp = NULL;
    char *line = NULL;
    size_t cap = 0;
    ssize_t nread;
    unsigned long long total = 0;
    unsigned long long macro_depth = 0;
    unsigned long long lineno = 0;

    if (ctx == NULL || ctx->in_path == NULL) {
        return -1;
    }

    /* The file, or what was gathered of several or of the standard
     * input, which is read here and then again from its beginning. */
    if (ctx->gathered == NULL) {
        fp = fopen(ctx->in_path, "rb");
        if (fp == NULL) {
            as_diag(AS_E_USAGE, "failed to open input %s: %s", ctx->in_path, strerror(errno));
            return -1;
        }
    }

    while ((nread = fp != NULL ? getline(&line, &cap, fp) : text_getline(&line, &cap, ctx->gathered)) >= 0) {
        const char *d;
        int tlen;

        lineno++;
        total += (unsigned long long)nread;
        if (ctx->max_input_bytes > 0 && total > ctx->max_input_bytes) {
            as_diag(AS_E_LIMIT, "%s:%llu: input exceeds --max-input-bytes=%llu", ctx->in_path, lineno,
                    ctx->max_input_bytes);
            free(line);
            preflight_done(fp, ctx);
            return -1;
        }
        /* The bytes of the line, without the newline that ends it: a
         * line of exactly the limit passes. */
        if (ctx->max_line_bytes > 0 &&
            (unsigned long long)(nread > 0 && line[nread - 1] == '\n' ? nread - 1 : nread) > ctx->max_line_bytes) {
            as_diag(AS_E_LIMIT, "%s:%llu: line exceeds --max-line-bytes=%llu", ctx->in_path, lineno,
                    ctx->max_line_bytes);
            free(line);
            preflight_done(fp, ctx);
            return -1;
        }
        tlen = count_max_token_len(line);
        if (ctx->max_token_length > 0 && (unsigned long long)tlen > ctx->max_token_length) {
            as_diag(AS_E_LIMIT, "%s:%llu: token exceeds --max-token-length=%llu", ctx->in_path, lineno,
                    ctx->max_token_length);
            free(line);
            preflight_done(fp, ctx);
            return -1;
        }

        d = line_start_directive(line);
        if (strncmp(d, ".macro", 6) == 0 && (d[6] == '\0' || isspace((unsigned char)d[6]))) {
            macro_depth++;
            if (ctx->max_macro_depth > 0 && macro_depth > ctx->max_macro_depth) {
                as_diag(AS_E_LIMIT, "%s:%llu: macro depth exceeds --max-macro-depth=%llu", ctx->in_path, lineno,
                        ctx->max_macro_depth);
                free(line);
                preflight_done(fp, ctx);
                return -1;
            }
        } else if (strncmp(d, ".endm", 5) == 0 && (d[5] == '\0' || isspace((unsigned char)d[5]))) {
            if (macro_depth > 0) {
                macro_depth--;
            }
        }
    }

    free(line);
    preflight_done(fp, ctx);
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

/*
 * The processors -march= names, which are GNU as's, and whether each
 * has 64-bit mode; then this assembler's own names for the levels of
 * the x86-64 psABI, which GNU as has not, and which are 64-bit only.
 */
static const struct {
    const char *name;
    unsigned char has_32;
    unsigned char has_64;
} march_cpus[] = {
    {"generic32", 1, 0},  {"generic64", 1, 1}, {"i386", 1, 0},       {"i486", 1, 0},     {"i586", 1, 0},
    {"pentium", 1, 0},    {"i686", 1, 0},      {"pentiumpro", 1, 0}, {"pentiumii", 1, 0}, {"pentiumiii", 1, 0},
    {"pentium4", 1, 0},   {"prescott", 1, 0},  {"nocona", 1, 1},     {"core", 1, 0},     {"core2", 1, 1},
    {"corei7", 1, 1},     {"k6", 1, 0},        {"k6_2", 1, 0},       {"athlon", 1, 0},   {"opteron", 1, 1},
    {"k8", 1, 1},         {"amdfam10", 1, 1},  {"bdver1", 1, 1},     {"bdver2", 1, 1},   {"bdver3", 1, 1},
    {"bdver4", 1, 1},     {"znver1", 1, 1},    {"znver2", 1, 1},     {"znver3", 1, 1},   {"znver4", 1, 1},
    {"znver5", 1, 1},     {"znver6", 1, 1},    {"btver1", 1, 1},     {"btver2", 1, 1},
    {"x86-64", 0, 1},     {"x86-64-v1", 0, 1}, {"x86-64-v2", 0, 1},  {"x86-64-v3", 0, 1}, {"x86-64-v4", 0, 1},
    {"amd64", 0, 1},      {"generic", 1, 1},   {"native", 1, 1},
};

/* The extensions that may follow, each after a `+` and each with `no`
 * before it or not. */
static const char *const march_extensions[] = {
    "8087", "287", "387", "687", "cmov", "fxsr", "mmx", "sse", "sse2", "sse3", "sse4a", "ssse3", "sse4.1",
    "sse4.2", "sse4", "avx", "avx2", "avx512f", "avx512cd", "avx512er", "avx512pf", "avx512dq", "avx512bw",
    "avx512vl", "monitor", "vmx", "vmfunc", "smx", "xsave", "xsaveopt", "xsavec", "xsaves", "aes", "pclmul",
    "fsgsbase", "rdrnd", "f16c", "bmi2", "fma", "fma4", "xop", "lwp", "movbe", "cx16", "lahf_sahf", "ept",
    "lzcnt", "popcnt", "hle", "rtm", "tsx", "invpcid", "clflush", "nop", "syscall", "rdtscp", "3dnow",
    "3dnowa", "padlock", "svme", "abm", "bmi", "tbm", "adx", "rdseed", "prfchw", "smap", "mpx", "sha",
    "clflushopt", "prefetchwt1", "se1", "clwb", "avx512ifma", "avx512vbmi", "avx512_4fmaps", "avx512_4vnniw",
    "avx512_vpopcntdq", "avx512_vbmi2", "avx512_vnni", "avx512_bitalg", "avx_vnni", "clzero", "mwaitx",
    "ospke", "rdpid", "ptwrite", "ibt", "shstk", "gfni", "vaes", "vpclmulqdq", "wbnoinvd", "pconfig",
    "waitpkg", "cldemote", "amx_int8", "amx_bf16", "amx_fp16", "amx_complex", "amx_tile", "movdiri",
    "movdir64b", "avx512_bf16", "avx512_vp2intersect", "tdx", "enqcmd", "serialize", "rdpru", "mcommit",
    "sev_es", "tsxldtrk", "kl", "widekl", "uintr", "hreset", "avx512_fp16", "prefetchi", "avx_ifma",
    "avx_vnni_int8", "cmpccxadd", "wrmsrns", "msrlist", "avx_ne_convert", "rao_int", "rmpquery", "fred",
    "lkgs", "avx_vnni_int16", "sha512", "sm3", "sm4", "pbndkb", "avx10.1", "user_msr", "apx_f",
};

/*
 * 0 if MARCH is a processor there is, with extensions there are, that
 * has the mode asked for; -1 if it names none, and -2 if the processor
 * is one without 64-bit mode and that is the mode.
 */
static int validate_march(int mode, const char *march) {
    size_t cpu_len;
    size_t i;
    const char *p;
    int found = 0;

    if (march == NULL) {
        return 0;
    }
    cpu_len = strcspn(march, "+");
    for (i = 0; i < sizeof(march_cpus) / sizeof(march_cpus[0]); ++i) {
        if (strlen(march_cpus[i].name) == cpu_len && strncmp(march, march_cpus[i].name, cpu_len) == 0) {
            if (mode == AS_MODE_64 ? !march_cpus[i].has_64 : !march_cpus[i].has_32) {
                return mode == AS_MODE_64 ? -2 : -1;
            }
            found = 1;
            break;
        }
    }
    if (!found) {
        return -1;
    }
    for (p = march + cpu_len; *p == '+';) {
        size_t len;

        p++;
        len = strcspn(p, "+");
        if (len > 2 && p[0] == 'n' && p[1] == 'o') {
            p += 2;
            len -= 2;
        }
        found = 0;
        for (i = 0; i < sizeof(march_extensions) / sizeof(march_extensions[0]); ++i) {
            if (strlen(march_extensions[i]) == len && strncmp(p, march_extensions[i], len) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return -1;
        }
        p += len;
    }
    return 0;
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
    /* With no -march nothing is refused for the processor it needs:
     * what a compiler given -mavx2 writes is assembled with no option,
     * as GNU as assembles it. */
    if (march == NULL || march[0] == '\0') {
        return 4;
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

/*
 * Run the C preprocessor over a source and take what it writes: its
 * standard output, read here through a pipe, in *text_out (the caller's
 * to free) and *len_out.  It was told to write a file in /tmp.
 */
static int run_cpp_capture(const as_ctx_t *ctx, const char *in_path, char **text_out, size_t *len_out) {
    size_t i;
    size_t argc;
    char **argv;
    size_t at = 0;
    char cpp_path[PATH_MAX];
    const char *cpp_prog;
    pid_t pid;
    int status;
    int fds[2];
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    int failed = 0;

    *text_out = NULL;
    *len_out = 0;
    cpp_prog = resolve_cpp_tool(ctx, cpp_path, sizeof(cpp_path));
    argc = 3 + ctx->gcc_opts.count;
    argv = (char **)calloc(argc + 1, sizeof(*argv));
    if (argv == NULL) {
        return -1;
    }

    argv[at++] = (char *)cpp_prog;
    for (i = 0; i < ctx->gcc_opts.count; ++i) {
        argv[at++] = ctx->gcc_opts.items[i];
    }
    argv[at++] = (char *)in_path;
    argv[at] = NULL;

    if (pipe(fds) != 0) {
        free(argv);
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        free(argv);
        return -1;
    }
    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(fds[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    free(argv);

    for (;;) {
        ssize_t got;

        if (cap - len < 65536) {
            size_t ncap = cap == 0 ? 131072 : cap * 2;
            char *next = (char *)realloc(buf, ncap + 1);

            if (next == NULL) {
                failed = 1;
                break;
            }
            buf = next;
            cap = ncap;
        }
        got = read(fds[0], buf + len, cap - len);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got < 0) {
            failed = 1;
        }
        if (got <= 0) {
            break;
        }
        len += (size_t)got;
    }
    close(fds[0]);
    if (waitpid(pid, &status, 0) < 0 || failed || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        free(buf);
        return -1;
    }
    if (buf != NULL) {
        buf[len] = '\0';
    }
    *text_out = buf;
    *len_out = len;
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

/*
 * The source on its way from its file to the tokenizer.
 *
 * Macros, repeats and conditionals are expanded as text, line by line,
 * each pass reading one text and writing the next.  The texts were
 * files in /tmp -- four of them for a source with no macro in it -- and
 * a statement had the name of the last and its line there, which is
 * what every message said: "/tmp/ascond_dlnR1j:5".  They are in memory,
 * and each line knows the file and line it came from: a writer notes
 * where its reader stands whenever a line is begun, and where the count
 * a later reader would keep goes wrong -- lines left out, or several
 * made of one -- it writes a mark (AS_LEX_MARK) that puts it right.
 */
typedef struct as_text {
    char *data;
    size_t len;
    size_t cap;
    int failed;
    /* As what is read: the next byte, and the file and line of the line
     * last read. */
    size_t pos;
    char *file;
    unsigned line;
    /* As what is written: the text its lines are made from, where a
     * reader of it will take the next line to be, and whether a line is
     * part written. */
    const struct as_text *origin;
    char *out_file;
    unsigned out_line;
    int mid_line;
    /* The macro whose expansion is being written, or NULL: its name
     * goes in the marks of the lines of it (AS_LEX_MACRO_SEP). */
    const char *macro;
} as_text_t;

/* The expansion of macros and repeats says what is wrong here, for
 * whoever called it to report: a pass has no other way to. */
static char expand_err[256];

/* The most bytes a source may come to, expanded (--max-expansion-bytes). */
static unsigned long long text_limit = AS_EXPANSION_BYTES_DEFAULT;

static void text_free(as_text_t *t) {
    if (t == NULL) {
        return;
    }
    free(t->data);
    free(t->file);
    free(t->out_file);
    memset(t, 0, sizeof(*t));
}

static int text_set_file(char **slot, const char *name) {
    char *copy = xstrdup(name != NULL ? name : "<input>");

    if (copy == NULL) {
        return -1;
    }
    free(*slot);
    *slot = copy;
    return 0;
}

static int text_reserve(as_text_t *t, size_t more) {
    if (t->failed) {
        return -1;
    }
    /* `.rept 0x7fffffff` and a repeat inside a repeat ask for more than
     * there is, and were given it until the machine had none. */
    if (t->origin != NULL && (unsigned long long)t->len + more > text_limit) {
        if (expand_err[0] == '\0') {
            snprintf(expand_err, sizeof(expand_err),
                     "the source expands to more than %llu bytes (--max-expansion-bytes)", text_limit);
        }
        t->failed = 1;
        return -1;
    }
    if (t->len + more + 1 > t->cap) {
        size_t ncap = t->cap == 0 ? 4096 : t->cap;
        char *next;

        while (ncap < t->len + more + 1) {
            ncap *= 2;
        }
        next = (char *)realloc(t->data, ncap);
        if (next == NULL) {
            t->failed = 1;
            return -1;
        }
        t->data = next;
        t->cap = ncap;
    }
    return 0;
}

static void text_put_raw(as_text_t *t, const char *s, size_t n) {
    if (text_reserve(t, n) != 0) {
        return;
    }
    memcpy(t->data + t->len, s, n);
    t->len += n;
    t->data[t->len] = '\0';
}

/* A line is about to be written: see that whoever reads it will know
 * the line it is made from. */
static void text_begin_line(as_text_t *t) {
    const as_text_t *from = t->origin;
    char mark[32];
    char with_macro[PATH_MAX + 160];
    const char *file;

    if (from == NULL || from->file == NULL || from->line == 0) {
        return;
    }
    /* The file as the mark has it: with the macro being expanded after
     * it, unless what is read says one already -- a later pass, which
     * is copying an expansion and making none. */
    file = from->file;
    if (t->macro != NULL && strchr(from->file, AS_LEX_MACRO_SEP) == NULL &&
        snprintf(with_macro, sizeof(with_macro), "%s%c%s", from->file, AS_LEX_MACRO_SEP, t->macro) <
            (int)sizeof(with_macro)) {
        file = with_macro;
    }
    if (t->out_file != NULL && strcmp(t->out_file, file) == 0 && t->out_line + 1 == from->line) {
        return;
    }
    snprintf(mark, sizeof(mark), "%c%u ", AS_LEX_MARK, from->line);
    text_put_raw(t, mark, strlen(mark));
    text_put_raw(t, file, strlen(file));
    text_put_raw(t, "\n", 1);
    if (t->out_file == NULL || strcmp(t->out_file, file) != 0) {
        if (text_set_file(&t->out_file, file) != 0) {
            t->failed = 1;
        }
    }
    t->out_line = from->line - 1;
}

static void text_putc(int c, as_text_t *t) {
    char ch = (char)c;

    if (!t->mid_line) {
        text_begin_line(t);
    }
    text_put_raw(t, &ch, 1);
    if (ch == '\n') {
        t->out_line++;
        t->mid_line = 0;
    } else {
        t->mid_line = 1;
    }
}

static void text_puts(const char *s, as_text_t *t) {
    /* A line at a time, not a character: an expansion may be of many
     * millions of them. */
    while (*s != '\0') {
        const char *nl = strchr(s, '\n');
        size_t n = nl != NULL ? (size_t)(nl - s) + 1 : strlen(s);

        if (!t->mid_line) {
            text_begin_line(t);
        }
        text_put_raw(t, s, n);
        if (nl != NULL) {
            t->out_line++;
            t->mid_line = 0;
        } else {
            t->mid_line = 1;
        }
        s += n;
    }
}

/*
 * The next line of a text, as getline(3) gives one of a file: with its
 * newline, in *line, and its length returned; -1 at the end.  A mark is
 * taken in and not given, and so is the line a C preprocessor leaves to
 * say the same thing, `# 12 "file.S"`.
 */
static ssize_t text_getline(char **line, size_t *cap, as_text_t *t) {
    for (;;) {
        const char *start;
        size_t n = 0;

        if (t->pos >= t->len) {
            return -1;
        }
        start = t->data + t->pos;
        while (t->pos + n < t->len && start[n] != '\n') {
            n++;
        }
        if (t->pos + n < t->len) {
            n++;
        }
        t->pos += n;
        if (start[0] == AS_LEX_MARK ||
            (start[0] == '#' && n > 3 && start[1] == ' ' && isdigit((unsigned char)start[2]))) {
            const char *p = start + (start[0] == '#' ? 2 : 1);
            const char *end = start + n;
            unsigned long at = 0;
            const char *name;
            const char *name_end;

            while (p < end && isdigit((unsigned char)*p)) {
                at = at * 10 + (unsigned long)(*p - '0');
                p++;
            }
            while (p < end && *p == ' ') {
                p++;
            }
            name = p;
            name_end = end;
            while (name_end > name && (name_end[-1] == '\n' || name_end[-1] == '\r')) {
                name_end--;
            }
            if (start[0] == '#') {
                /* "file" and then flags that are of no use here. */
                if (name < name_end && *name == '"') {
                    const char *close = name + 1;

                    while (close < name_end && *close != '"') {
                        close++;
                    }
                    name++;
                    name_end = close;
                } else if (name < name_end) {
                    /* `# 12 something else`: a comment that begins with
                     * a number, and a line like any other. */
                    goto ordinary;
                }
            }
            if (name < name_end) {
                char *copy = (char *)malloc((size_t)(name_end - name) + 1);

                if (copy == NULL) {
                    return -1;
                }
                memcpy(copy, name, (size_t)(name_end - name));
                copy[name_end - name] = '\0';
                free(t->file);
                t->file = copy;
            }
            t->line = at > 0 ? (unsigned)(at - 1) : 0;
            continue;
        }
    ordinary:
        if (*cap < n + 1) {
            char *next = (char *)realloc(*line, n + 1);

            if (next == NULL) {
                return -1;
            }
            *line = next;
            *cap = n + 1;
        }
        memcpy(*line, start, n);
        (*line)[n] = '\0';
        t->line++;
        return (ssize_t)n;
    }
}


/* Read it again from its first line. */
static void text_rewind(as_text_t *t) {
    t->pos = 0;
    t->line = 0;
}

/* A file, read whole, as a text whose first line is its line 1. */
static int text_read_file(as_text_t *t, const char *path) {
    int fd = open(path, O_RDONLY);

    memset(t, 0, sizeof(*t));
    if (fd < 0) {
        return -1;
    }
    for (;;) {
        ssize_t got;

        if (text_reserve(t, 65536) != 0) {
            close(fd);
            return -1;
        }
        got = read(fd, t->data + t->len, 65536);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return -1;
        }
        if (got == 0) {
            break;
        }
        t->len += (size_t)got;
    }
    close(fd);
    if (t->data != NULL) {
        t->data[t->len] = '\0';
    }
    return text_set_file(&t->file, path);
}

static int write_substituted_line(as_text_t *out, const char *line, const char *var, const char *value) {
    size_t vlen;
    const char *p;
    as_scan_t scan;
    int comment = 0;

    if (out == NULL || line == NULL || var == NULL || value == NULL) {
        return -1;
    }
    memset(&scan, 0, sizeof(scan));
    vlen = strlen(var);
    for (p = line; *p != '\0'; ++p) {
        size_t more;
        int kind = comment ? AS_SCAN_COMMENT_TO_EOL : as_scan_step(&scan, line, (size_t)(p - line), &more);

        /* A `;` ends a statement where it is text: not in a string, and
         * not in a comment. */
        if (kind == AS_SCAN_COMMENT_TO_EOL) {
            comment = 1;
        } else if (kind == AS_SCAN_TEXT && *p == ';') {
            text_putc('\n', out);
            continue;
        }
        if (*p == '\\' && vlen > 0 && strncmp(p + 1, var, vlen) == 0) {
            text_puts(value, out);
            p += vlen;
            continue;
        }
        text_putc((unsigned char)*p, out);
    }
    if (p == line || p[-1] != '\n') {
        text_putc('\n', out);
    }
    return out->failed ? -1 : 0;
}

static int write_split_asm_line(as_text_t *out, const char *line) {
    return write_substituted_line(out, line, "", "");
}

static int expand_irp_block(as_text_t *in, as_text_t *out, const char *header, int chars_mode) {
    char *spec = NULL;
    char *var;
    char *vals;
    char *comma;
    char *line = NULL;
    size_t cap = 0;
    strvec_t body = {0};
    int rc = -1;
    int closed = 0;
    unsigned begun = in->line;

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
    /* With no values the body is assembled once, the name standing for
     * nothing. */
    if (comma == NULL) {
        vals = var + strlen(var);
    } else {
        *comma = '\0';
        vals = trim_in_place(comma + 1);
    }
    var = trim_in_place(var);
    if (var == NULL || *var == '\0' || vals == NULL) {
        snprintf(expand_err, sizeof(expand_err), "%s:%u: %s: no name to stand for the values",
                 in->file != NULL ? in->file : "<input>", in->line, chars_mode ? ".irpc" : ".irp");
        goto out;
    }
    if (*vals == '"' || *vals == '\'') {
        char quote = *vals++;
        char *end = strrchr(vals, quote);
        if (end != NULL) {
            *end = '\0';
        }
    }

    while (text_getline(&line, &cap, in) >= 0) {
        if (line_starts_with_directive(line, ".endr")) {
            closed = 1;
            break;
        }
        if (strvec_push(&body, line) != 0) {
            goto out;
        }
    }
    if (!closed) {
        snprintf(expand_err, sizeof(expand_err), "%s:%u: the %s here has no .endr",
                 in->file != NULL ? in->file : "<input>", begun, chars_mode ? ".irpc" : ".irp");
        goto out;
    }
    if (!chars_mode && *vals == '\0') {
        size_t i;

        for (i = 0; i < body.count; ++i) {
            if (write_substituted_line(out, body.items[i], var, "") != 0) {
                goto out;
            }
        }
        rc = 0;
        goto out;
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
    long long v;

    /* An expression of numbers alone -- no symbol has a value while the
     * source is still being expanded -- and not negative. */
    if (s == NULL || out == NULL ||
        as_expr_eval_string(s, NULL, NULL, &v) != AS_EXPR_EVAL_OK ||
        v < 0 || v > (long long)LONG_MAX) {
        return -1;
    }
    *out = (long)v;
    return 0;
}

/*
 * How many bytes the body of a repeat comes to, the repeats inside it
 * counted as many times as they say: what two nested `.rept 200000`
 * would be is known from their two lines, not from writing it.  Capped,
 * so that it cannot wrap.
 */
static unsigned long long rept_body_bytes(const strvec_t *body) {
    const unsigned long long cap = 1ull << 60;
    unsigned long long times[32];
    size_t depth = 0;
    unsigned long long factor = 1;
    unsigned long long bytes = 0;
    size_t k;

    for (k = 0; k < body->count; ++k) {
        const char *d = line_start_directive(body->items[k]);
        unsigned long long len = strlen(body->items[k]);
        long n;

        if (line_starts_with_directive(d, ".endr")) {
            if (depth > 0) {
                factor = times[--depth];
            }
            continue;
        }
        if (line_starts_with_directive(d, ".rept") && depth < sizeof(times) / sizeof(times[0])) {
            times[depth++] = factor;
            if (eval_rept_count(d + 5, &n) == 0 && n >= 0) {
                factor = (unsigned long long)n > cap / (factor != 0 ? factor : 1) ? cap : factor * (unsigned long long)n;
            }
            continue;
        }
        bytes += len > cap / (factor != 0 ? factor : 1) ? cap : len * factor;
        if (bytes > cap) {
            bytes = cap;
        }
    }
    return bytes;
}

static int expand_rept_block(as_text_t *in, as_text_t *out, const char *header) {
    char *line = NULL;
    size_t cap = 0;
    strvec_t body = {0};
    long count;
    long iter;
    int nested = 0;
    int rc = -1;
    int closed = 0;
    unsigned first_line = in->line + 1;
    unsigned end_line;

    if (eval_rept_count(header, &count) != 0) {
        snprintf(expand_err, sizeof(expand_err), "%s:%u: .rept: the count is not a number that is known here",
                 in->file != NULL ? in->file : "<input>", in->line);
        return -1;
    }
    while (text_getline(&line, &cap, in) >= 0) {
        const char *d = line_start_directive(line);
        if (line_starts_with_directive(d, ".rept")) {
            nested++;
        } else if (line_starts_with_directive(d, ".endr")) {
            if (nested == 0) {
                closed = 1;
                break;
            }
            nested--;
        }
        if (strvec_push(&body, line) != 0) {
            goto out;
        }
    }
    /* Each copy of a line is of the line it was written on, which is
     * where a message about it should point. */
    if (!closed) {
        snprintf(expand_err, sizeof(expand_err), "%s:%u: the .rept here has no .endr",
                 in->file != NULL ? in->file : "<input>", first_line - 1);
        goto out;
    }
    end_line = in->line;
    /* A body of nothing, however many times, is nothing. */
    if (body.count == 0) {
        count = 0;
    }
    /* And one that would come to more than a source may is refused
     * before it is begun, not when the last byte of it is reached. */
    {
        unsigned long long bytes = rept_body_bytes(&body);

        if (count > 0 && bytes > text_limit / (unsigned long long)count) {
            snprintf(expand_err, sizeof(expand_err),
                     "%s:%u: this .rept expands to more than %llu bytes (--max-expansion-bytes)",
                     in->file != NULL ? in->file : "<input>", first_line - 1, text_limit);
            goto out;
        }
    }
    for (iter = 0; iter < count; ++iter) {
        size_t i;
        for (i = 0; i < body.count; ++i) {
            in->line = first_line + (unsigned)i;
            if (write_split_asm_line(out, body.items[i]) != 0) {
                in->line = end_line;
                goto out;
            }
        }
    }
    in->line = end_line;
    rc = 0;

out:
    free(line);
    strvec_free(&body);
    return rc;
}

static int expand_rept_file(as_text_t *in, as_text_t *out) {
    char *line = NULL;
    size_t cap = 0;
    int rc = -1;

    if (in == NULL || out == NULL) {
        return -1;
    }
    out->origin = in;

    while (text_getline(&line, &cap, in) >= 0) {
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
        text_puts(line, out);
    }
    if (out->failed) {
        goto out;
    }
    rc = 0;

out:
    /* Where it went wrong, if what says so does not say where. */
    if (rc != 0 && expand_err[0] != '\0' && in->file != NULL &&
        strncmp(expand_err, in->file, strlen(in->file)) != 0) {
        char said[sizeof(expand_err)];

        snprintf(said, sizeof(said), "%s", expand_err);
        snprintf(expand_err, sizeof(expand_err), "%s:%u: %.180s", in->file, in->line, said);
    }
    free(line);
    return rc;
}

typedef struct {
    char *name;
    strvec_t params;
    strvec_t defaults;
    /* What each parameter was declared as, after its colon: "", "req"
     * for one that must be given, "vararg" for one that takes the rest. */
    strvec_t kinds;
    strvec_t body;
    /* Where it was defined, to be named if it is defined again. */
    char *def_file;
    unsigned def_line;
} gas_macro_t;

struct macro_cond;

typedef struct {
    gas_macro_t *items;
    size_t count;
    size_t cap;
    /* How many expansions have been begun: what \@ is in the next. */
    unsigned long serial;
    /* How deep an expansion may go (--max-macro-depth). */
    unsigned long max_depth;
    /* The conditionals of what has been written, and the text. */
    struct macro_cond *conds;
    /* A macro being defined: the lines up to its own .endm are its
     * body, and `def_nesting` counts the .macro lines among them. */
    int defining;
    unsigned def_nesting;
    gas_macro_t def;
    /* Where the line being expanded is from. */
    const char *file;
    unsigned line;
} gas_macro_vec_t;

static void gas_macro_free(gas_macro_t *m) {
    if (m == NULL) {
        return;
    }
    free(m->name);
    free(m->def_file);
    strvec_free(&m->params);
    strvec_free(&m->defaults);
    strvec_free(&m->kinds);
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
    gas_macro_free(&v->def);
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
    as_scan_t scan;
    int depth = 0;

    if (s == NULL || out == NULL) {
        return -1;
    }
    memset(&scan, 0, sizeof(scan));
    start = s;
    for (i = 0;; ++i) {
        char ch = s[i];
        int sep = 0;
        size_t more;

        if (ch != '\0' && as_scan_step(&scan, s, i, &more) == AS_SCAN_QUOTED) {
            /* part of a string, whatever it is */
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
    as_scan_t scan;
    int depth = 0;

    if (s == NULL || out == NULL) {
        return -1;
    }
    memset(&scan, 0, sizeof(scan));
    start = s;
    for (i = 0;; ++i) {
        char ch = s[i];
        size_t more;

        if (ch != '\0' && as_scan_step(&scan, s, i, &more) == AS_SCAN_QUOTED) {
            /* part of a string, whatever it is */
        } else if (ch == '(' || ch == '[' || ch == '{') {
            depth++;
        } else if ((ch == ')' || ch == ']' || ch == '}') && depth > 0) {
            depth--;
        } else if (depth == 0 && ch == '#' && as_scan_dialect() == AS_DIALECT_X86) {
            /* GAS treats '#' as a line comment on x86. Stop here so
             * trailing comments do not become spurious positional macro
             * arguments and overwrite earlier named values. */
            return push_macro_arg_slice(out, start, s + i);
        } else if (depth == 0 && isspace((unsigned char)ch)) {
            const char *next = s + i + 1;
            while (isspace((unsigned char)*next)) {
                next++;
            }
            if (*next == '#' && as_scan_dialect() == AS_DIALECT_X86) {
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
        char *kind = "";
        size_t k;
        if (eq != NULL) {
            *eq = '\0';
            def = trim_in_place(eq + 1);
        }
        colon = strchr(spec, ':');
        if (colon != NULL) {
            *colon = '\0';
            kind = trim_in_place(colon + 1);
        }
        spec = trim_in_place(spec);
        /* A parameter named twice is refused, as GNU as refuses it. */
        for (k = 0; spec != NULL && k < m->params.count; ++k) {
            if (strcmp(m->params.items[k], spec) == 0) {
                snprintf(expand_err, sizeof(expand_err), "macro %s: the parameter %s is named twice", m->name, spec);
                free(tmp);
                strvec_free(&specs);
                gas_macro_free(m);
                return -1;
            }
        }
        if (spec != NULL && *spec != '\0') {
            if (strvec_push(&m->params, spec) != 0 || strvec_push(&m->defaults, def) != 0 ||
                strvec_push(&m->kinds, kind) != 0) {
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

static int expand_gas_macro_statement(as_text_t *out, gas_macro_vec_t *macros, const char *line, unsigned depth);
static int macro_cond_active(struct macro_cond *mc, const as_text_t *written);
static size_t macro_cond_depth(struct macro_cond *mc, const as_text_t *written);

/* What expand_gas_macro_statement returns for an .exitm: neither done
 * nor failed, but the end of the expansion it is in. */
#define MACRO_EXIT 2
/* The parameters of one macro that are known to have been given. */
#define MACRO_PARAMS_TRACKED 64

static int expand_split_statement(as_text_t *out, gas_macro_vec_t *macros, const char *line, unsigned depth) {
    size_t i;
    const char *start = line;
    as_scan_t scan;
    int split = 0;

    memset(&scan, 0, sizeof(scan));
    for (i = 0; line != NULL && line[i] != '\0'; ++i) {
        char ch = line[i];
        size_t more;
        int kind = as_scan_step(&scan, line, i, &more);

        /* The rest is comment, and a `;` in it is no statement's end. */
        if (kind == AS_SCAN_COMMENT_TO_EOL) {
            break;
        }
        if (kind == AS_SCAN_TEXT && ch == ';') {
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

static int expand_gas_macro_statement(as_text_t *out, gas_macro_vec_t *macros, const char *line, unsigned depth) {
    const char *p;
    const char *name_start;
    char name[128];
    size_t n = 0;
    gas_macro_t *m;
    char **values = NULL;
    strvec_t args = {0};
    size_t i;
    int rc = -1;
    const char *directive = line_start_directive(line);
    unsigned char given[MACRO_PARAMS_TRACKED] = {0};

    /* The body of a macro being defined, to its own .endm.  Its lines
     * are kept as they are written, not expanded and not split. */
    if (macros->defining) {
        if (line_starts_with_directive(directive, ".endm")) {
            if (macros->def_nesting == 0) {
                macros->defining = 0;
                if (gas_macro_vec_push(macros, &macros->def) != 0) {
                    gas_macro_free(&macros->def);
                    return -1;
                }
                memset(&macros->def, 0, sizeof(macros->def));
                return 0;
            }
            macros->def_nesting--;
        } else if (line_starts_with_directive(directive, ".macro")) {
            macros->def_nesting++;
        }
        return strvec_push(&macros->def.body, line) != 0 ? -1 : 0;
    }
    if (depth > macros->max_depth) {
        snprintf(expand_err, sizeof(expand_err),
                 "macros are expanded more than %lu deep (--max-macro-depth): one calls itself without end, or the "
                 "limit is too low",
                 macros->max_depth);
        return -1;
    }
    /* In a branch of a conditional that is not taken nothing is defined,
     * removed or expanded: the line goes on as it is, to be left out
     * with the branch. */
    if (macros->conds != NULL &&
        (line_starts_with_directive(directive, ".macro") || line_starts_with_directive(directive, ".purgem") ||
         line_starts_with_directive(directive, ".exitm")) &&
        !macro_cond_active(macros->conds, out)) {
        return write_split_asm_line(out, line);
    }
    if (line_starts_with_directive(directive, ".macro")) {
        gas_macro_t *old;

        if (parse_gas_macro_header(directive + 6, &macros->def) != 0) {
            if (expand_err[0] == '\0') {
                snprintf(expand_err, sizeof(expand_err), ".macro: no name, or parameters that cannot be read");
            }
            return -1;
        }
        old = find_gas_macro(macros, macros->def.name);
        if (old != NULL) {
            snprintf(expand_err, sizeof(expand_err), "macro %s is defined already, at %s:%u", old->name,
                     old->def_file != NULL ? old->def_file : "<input>", old->def_line);
            gas_macro_free(&macros->def);
            return -1;
        }
        macros->def.def_file = xstrdup(macros->file != NULL ? macros->file : "<input>");
        macros->def.def_line = macros->line;
        macros->defining = 1;
        macros->def_nesting = 0;
        return 0;
    }
    if (line_starts_with_directive(directive, ".purgem")) {
        char gone[128];
        size_t k = 0;
        const char *q = directive + 7;
        gas_macro_t *old;

        while (isspace((unsigned char)*q)) {
            q++;
        }
        while (*q != '\0' && !isspace((unsigned char)*q) && k + 1 < sizeof(gone)) {
            gone[k++] = *q++;
        }
        gone[k] = '\0';
        old = find_gas_macro(macros, gone);
        /* One that is not defined is nothing to remove, and GNU as says
         * nothing of it. */
        if (old != NULL) {
            gas_macro_free(old);
            *old = macros->items[--macros->count];
        }
        return 0;
    }
    if (line_starts_with_directive(directive, ".exitm")) {
        /* For the expansion this is a line of to end; outside one it is
         * a directive of no meaning, and is passed on to be refused. */
        return depth > 0 ? MACRO_EXIT : write_split_asm_line(out, line);
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
                for (; label_start < q; ++label_start) {
                    text_putc((unsigned char)*label_start, out);
                }
                text_puts(":\n", out);
                if (out->failed) {
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
    /* A call in a branch not taken is not expanded: it is how a macro
     * that calls itself comes to an end. */
    if (macros->conds != NULL && !macro_cond_active(macros->conds, out)) {
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
                        if (k < MACRO_PARAMS_TRACKED) {
                            given[k] = 1;
                        }
                        break;
                    }
                }
            } else if (pos < m->params.count) {
                /* The last parameter, if it is :vararg, takes this
                 * argument and all that follow, with their commas. */
                if (pos + 1 == m->params.count && pos < m->kinds.count &&
                    strcmp(m->kinds.items[pos], "vararg") == 0) {
                    size_t total = 1;
                    size_t k;
                    char *rest;

                    for (k = i; k < args.count; ++k) {
                        total += strlen(args.items[k]) + 2;
                    }
                    rest = (char *)malloc(total);
                    if (rest == NULL) {
                        goto out;
                    }
                    rest[0] = '\0';
                    for (k = i; k < args.count; ++k) {
                        size_t at = strlen(rest);

                        snprintf(rest + at, total - at, "%s%s", k == i ? "" : ", ", trim_in_place(args.items[k]));
                    }
                    free(values[pos]);
                    values[pos] = rest;
                    given[pos] = 1;
                    break;
                }
                if (macro_arg_assign(&values[pos], trim_in_place(args.items[i])) != 0) {
                    goto out;
                }
                if (pos < MACRO_PARAMS_TRACKED) {
                    given[pos] = 1;
                }
                pos++;
            }
        }
    }
    for (i = 0; i < m->params.count && i < m->kinds.count; ++i) {
        if (strcmp(m->kinds.items[i], "req") == 0 && (i >= MACRO_PARAMS_TRACKED || !given[i]) &&
            values[i][0] == '\0') {
            snprintf(expand_err, sizeof(expand_err), "macro %s is called without %s, which it requires", m->name,
                     m->params.items[i]);
            goto out;
        }
    }
    {
        /* \@ is how many expansions were begun before this one. */
        unsigned long macro_serial = macros->serial++;
        size_t entry_depth = macros->conds != NULL ? macro_cond_depth(macros->conds, out) : 0;

        for (i = 0; i < m->body.count; ++i) {
            char *expanded = expand_gas_macro_line_text(m->body.items[i], m, values, macro_serial);
            /* The lines written are of this macro's expansion, and of
             * the one that called it again when it is done. */
            const char *outer = out->macro;
            int step;

            if (expanded == NULL) {
                goto out;
            }
            out->macro = m->name;
            step = expand_gas_macro_statement(out, macros, expanded, depth + 1);
            out->macro = outer;
            free(expanded);
            if (step == MACRO_EXIT) {
                /* .exitm: the rest of the body is not expanded, and the
                 * conditionals it opened are closed for it. */
                size_t now = macros->conds != NULL ? macro_cond_depth(macros->conds, out) : entry_depth;

                for (; now > entry_depth; --now) {
                    text_puts(".endif\n", out);
                }
                break;
            }
            if (step != 0) {
                goto out;
            }
        }
        if (macros->defining && depth == 0) {
            snprintf(expand_err, sizeof(expand_err), "macro %s: the .macro it defines, %s, has no .endm", m->name,
                     macros->def.name != NULL ? macros->def.name : "?");
            goto out;
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

static int expand_rept_block_with_macros(as_text_t *in, as_text_t *out, gas_macro_vec_t *macros, const char *header) {
    char *line = NULL;
    size_t cap = 0;
    strvec_t body = {0};
    long count;
    long iter;
    int nested = 0;
    int rc = -1;
    int closed = 0;
    unsigned first_line = in->line + 1;
    unsigned end_line;

    if (eval_rept_count(header, &count) != 0) {
        snprintf(expand_err, sizeof(expand_err), "%s:%u: .rept: the count is not a number that is known here",
                 in->file != NULL ? in->file : "<input>", in->line);
        return -1;
    }
    while (text_getline(&line, &cap, in) >= 0) {
        const char *d = line_start_directive(line);
        if (line_starts_with_directive(d, ".rept")) {
            nested++;
        } else if (line_starts_with_directive(d, ".endr")) {
            if (nested == 0) {
                closed = 1;
                break;
            }
            nested--;
        }
        if (strvec_push(&body, line) != 0) {
            goto out;
        }
    }
    if (!closed) {
        snprintf(expand_err, sizeof(expand_err), "%s:%u: the .rept here has no .endr",
                 in->file != NULL ? in->file : "<input>", first_line - 1);
        goto out;
    }
    end_line = in->line;
    /* A body of nothing, however many times, is nothing. */
    if (body.count == 0) {
        count = 0;
    }
    /* And one that would come to more than a source may is refused
     * before it is begun, not when the last byte of it is reached. */
    {
        unsigned long long bytes = rept_body_bytes(&body);

        if (count > 0 && bytes > text_limit / (unsigned long long)count) {
            snprintf(expand_err, sizeof(expand_err),
                     "%s:%u: this .rept expands to more than %llu bytes (--max-expansion-bytes)",
                     in->file != NULL ? in->file : "<input>", first_line - 1, text_limit);
            goto out;
        }
    }
    for (iter = 0; iter < count; ++iter) {
        size_t i;
        for (i = 0; i < body.count; ++i) {
            in->line = first_line + (unsigned)i;
            if (expand_gas_macro_statement(out, macros, body.items[i], 0) != 0) {
                in->line = end_line;
                goto out;
            }
        }
    }
    in->line = end_line;
    rc = 0;

out:
    free(line);
    strvec_free(&body);
    return rc;
}

/*
 * Whether what has been written so far leaves the source in a branch of
 * a conditional that is taken.  The macros are expanded before the
 * conditionals are taken out, so a `.macro` in a branch not taken was
 * defined all the same, and could be called.  The conditionals of the
 * text written are followed here as they will be followed there.
 */
struct macro_cond;
static struct macro_cond *macro_cond_new(const as_ctx_t *ctx);
static int macro_cond_active(struct macro_cond *mc, const as_text_t *written);
static void macro_cond_free(struct macro_cond *mc);

/*
 * The file an .include names: as written, which is to say in the
 * current directory, if that opens; or else under each -I directory in
 * the order given.  That is the order GNU as looks in.  (The -I
 * directories were looked in first.)  The caller frees.
 */
static char *find_include_file(const as_ctx_t *ctx, const char *name) {
    size_t i;

    if (access(name, R_OK) == 0) {
        return xstrdup(name);
    }
    if (name[0] != '/' && ctx != NULL) {
        for (i = 0; i < ctx->as_opts.count; ++i) {
            const char *opt = ctx->as_opts.items[i];
            size_t n;
            char *path;

            if (strncmp(opt, "-I", 2) != 0 || opt[2] == '\0') {
                continue;
            }
            n = strlen(opt + 2) + strlen(name) + 2;
            path = (char *)malloc(n);
            if (path == NULL) {
                return NULL;
            }
            snprintf(path, n, "%s/%s", opt + 2, name);
            if (access(path, R_OK) == 0) {
                return path;
            }
            free(path);
        }
    }
    errno = ENOENT;
    return NULL;
}

/*
 * Expand one text into `out`: the source, or a file it includes, which
 * is read here and expanded in its place -- with the macros defined so
 * far, defining macros for what follows, and under the conditionals that
 * stand.  (.include was left for the tokenizer, after all of that: a
 * macro of an included file was not defined, its conditionals were not
 * followed, and its lines were numbered apart from the including
 * file's, so a `1b` could not reach back out of it.)  `chain` is the
 * files being read, outermost first.
 */
static int expand_source_lines(const as_ctx_t *ctx, gas_macro_vec_t *macros, as_text_t *in, as_text_t *out,
                               strvec_t *chain) {
    struct macro_cond *conds = macros->conds;
    const as_text_t *outer = out->origin;
    char *line = NULL;
    size_t cap = 0;
    int rc = -1;

    out->origin = in;
    while (text_getline(&line, &cap, in) >= 0) {
        const char *d = line_start_directive(line);

        macros->file = in->file;
        macros->line = in->line;
        /* The lines of a macro being defined are its body, whatever
         * they are. */
        if (macros->defining) {
            if (expand_gas_macro_statement(out, macros, line, 0) != 0) {
                goto out;
            }
            continue;
        }
        /* `lab: .include "f"`: the label is a line of its own, and the
         * directive the next. */
        {
            const char *p = d;

            for (;;) {
                const char *q = p;

                while (isalnum((unsigned char)*q) || *q == '_' || *q == '.' || *q == '$') {
                    q++;
                }
                if (q == p || *q != ':') {
                    break;
                }
                p = q + 1;
                while (isspace((unsigned char)*p)) {
                    p++;
                }
            }
            if (p != d && line_starts_with_directive(p, ".include") && macro_cond_active(conds, out)) {
                for (; d < p; ++d) {
                    text_putc((unsigned char)*d, out);
                }
                text_putc('\n', out);
            }
        }
        if (line_starts_with_directive(d, ".include") && macro_cond_active(conds, out)) {
            unsigned long limit = ctx != NULL && ctx->max_include_depth > 0 ? (unsigned long)ctx->max_include_depth
                                                                           : AS_INCLUDE_DEPTH_DEFAULT;
            const char *q = d + 8;
            const char *end;
            char *name;
            char *path;
            as_text_t inc;
            int inner;

            while (isspace((unsigned char)*q)) {
                q++;
            }
            end = *q == '"' ? strchr(q + 1, '"') : NULL;
            if (end == NULL) {
                snprintf(expand_err, sizeof(expand_err), ".include: a file name in double quotes is wanted");
                goto out;
            }
            name = (char *)malloc((size_t)(end - q));
            if (name == NULL) {
                goto out;
            }
            memcpy(name, q + 1, (size_t)(end - q) - 1);
            name[end - q - 1] = '\0';
            if (chain->count >= limit) {
                size_t k;
                size_t at;

                snprintf(expand_err, sizeof(expand_err), "files are included more than %lu deep (--max-include-depth):",
                         limit);
                for (k = 0; k < chain->count && k < 4; ++k) {
                    at = strlen(expand_err);
                    snprintf(expand_err + at, sizeof(expand_err) - at, " %.40s,", chain->items[k]);
                }
                at = strlen(expand_err);
                snprintf(expand_err + at, sizeof(expand_err) - at, " ... %.40s", name);
                free(name);
                goto out;
            }
            path = find_include_file(ctx, name);
            if (path == NULL || text_read_file(&inc, path) != 0) {
                snprintf(expand_err, sizeof(expand_err), ".include: cannot open %.150s: %s", name, strerror(errno));
                free(path);
                free(name);
                goto out;
            }
            free(name);
            if (strvec_push(chain, path) != 0) {
                free(path);
                text_free(&inc);
                goto out;
            }
            free(path);
            inner = expand_source_lines(ctx, macros, &inc, out, chain);
            free(chain->items[--chain->count]);
            text_free(&inc);
            out->origin = in;
            if (inner != 0) {
                rc = inner;
                free(line);
                out->origin = outer;
                return rc;
            }
            continue;
        }
        /* A .macro in a branch not taken is not defined: passed over,
         * to its own .endm. */
        if (line_starts_with_directive(d, ".macro") && !macro_cond_active(conds, out)) {
            unsigned nesting = 0;

            while (text_getline(&line, &cap, in) >= 0) {
                const char *b = line_start_directive(line);

                if (line_starts_with_directive(b, ".macro")) {
                    nesting++;
                } else if (line_starts_with_directive(b, ".endm") && nesting-- == 0) {
                    break;
                }
            }
            continue;
        }
        if (line_starts_with_directive(d, ".rept")) {
            if (expand_rept_block_with_macros(in, out, macros, d + 5) != 0) {
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
        if (expand_gas_macro_statement(out, macros, line, 0) != 0) {
            goto out;
        }
    }

    /* A macro is defined within one file: its .endm is not looked for
     * in the file that includes this one. */
    if (macros->defining) {
        macros->line = macros->def.def_line;
        snprintf(expand_err, sizeof(expand_err), "the .macro %s here has no .endm",
                 macros->def.name != NULL ? macros->def.name : "");
        goto out;
    }
    if (out->failed) {
        goto out;
    }
    rc = 0;

out:
    /* Where it went wrong, for whoever reports it. */
    if (rc != 0 && expand_err[0] != '\0' &&
        (in->file == NULL || strncmp(expand_err, in->file, strlen(in->file)) != 0)) {
        char said[sizeof(expand_err)];

        snprintf(said, sizeof(said), "%s", expand_err);
        snprintf(expand_err, sizeof(expand_err), "%s:%u: %.180s", in->file != NULL ? in->file : "<input>",
                 macros->defining ? macros->line : in->line, said);
    }
    free(line);
    out->origin = outer;
    return rc;
}

static int expand_gas_source_controls(const as_ctx_t *ctx, as_text_t *in, as_text_t *out) {
    gas_macro_vec_t macros = {0};
    strvec_t chain = {0};
    int rc;

    if (in == NULL || out == NULL) {
        return -1;
    }
    macros.conds = macro_cond_new(ctx);
    if (macros.conds == NULL) {
        return -1;
    }
    macros.max_depth = ctx != NULL && ctx->max_macro_depth > 0 ? (unsigned long)ctx->max_macro_depth
                                                                : AS_MACRO_DEPTH_DEFAULT;
    expand_err[0] = '\0';
    rc = expand_source_lines(ctx, &macros, in, out, &chain);
    out->origin = in;
    {
        struct macro_cond *conds = macros.conds;

        gas_macro_vec_free(&macros);
        macro_cond_free(conds);
    }
    strvec_free(&chain);
    return rc;
}

typedef struct {
    int parent_active;
    int branch_taken;
    int active;
    int else_seen;
} gas_cond_frame_t;

#define DEFSYM_OPT "--defsym="

/*
 * A --defsym's NAME=VALUE, taken apart: where the name is, how long, and
 * the value.  No value is 0, as GNU as has it.  -1 if there is no `=`,
 * no name, or a value that is not a number.
 */
static int defsym_split(const char *spec, size_t *name_len, long long *value) {
    const char *eq = spec != NULL ? strchr(spec, '=') : NULL;

    if (eq == NULL || eq == spec) {
        return -1;
    }
    *name_len = (size_t)(eq - spec);
    *value = 0;
    if (eq[1] == '\0') {
        return 0;
    }
    return as_expr_eval_string(eq + 1, NULL, NULL, value) == AS_EXPR_EVAL_OK ? 0 : -1;
}

/*
 * The value --defsym gave a name, for the expressions of conditionals:
 * an as_expr_lookup_fn, whose cookie is the driver's context.  The last
 * of two definitions of a name is the one that stands.
 */
static int defsym_lookup(void *cookie, const char *name, long long *value_out) {
    const as_ctx_t *ctx = (const as_ctx_t *)cookie;
    size_t i;

    if (ctx == NULL || name == NULL) {
        return 0;
    }
    for (i = ctx->as_opts.count; i > 0; --i) {
        const char *opt = ctx->as_opts.items[i - 1];
        size_t name_len;
        long long value;

        if (strncmp(opt, DEFSYM_OPT, sizeof(DEFSYM_OPT) - 1) != 0) {
            continue;
        }
        opt += sizeof(DEFSYM_OPT) - 1;
        if (defsym_split(opt, &name_len, &value) == 0 && strlen(name) == name_len &&
            strncmp(opt, name, name_len) == 0) {
            if (value_out != NULL) {
                *value_out = value;
            }
            return 1;
        }
    }
    return 0;
}

/*
 * The --defsym symbols, as a source would define them: a `.set` for
 * each, in a text to be read before the source.  It is a text of its
 * own, and not lines put before the source's, so that no line of the
 * source has its number moved.  It is empty if there are none.
 */
static int write_defsym_text(const as_ctx_t *ctx, as_text_t *out) {
    size_t i;

    memset(out, 0, sizeof(*out));
    for (i = 0; i < ctx->as_opts.count; ++i) {
        const char *opt = ctx->as_opts.items[i];
        size_t name_len;
        long long value;
        char number[32];

        if (strncmp(opt, DEFSYM_OPT, sizeof(DEFSYM_OPT) - 1) != 0) {
            continue;
        }
        opt += sizeof(DEFSYM_OPT) - 1;
        if (defsym_split(opt, &name_len, &value) != 0) {
            continue;
        }
        snprintf(number, sizeof(number), ", %lld\n", value);
        text_put_raw(out, ".set ", 5);
        text_put_raw(out, opt, name_len);
        text_put_raw(out, number, strlen(number));
    }
    if (out->failed) {
        return -1;
    }
    return 0;
}

/*
 * What the conditionals know of the source's symbols.  They are decided
 * while the source is still text, a line at a time, so this is kept as
 * the lines go by: a name is here from the line that defines it, with a
 * value if `.set`, `.equ`, `.equiv` or `=` gave it one that is a number,
 * and without if it is a label or its value is not known this early.
 */
typedef struct {
    char *name;
    int has_value;
    long long value;
} cond_sym_t;

typedef struct {
    const as_ctx_t *ctx;
    cond_sym_t *items;
    size_t count;
    size_t cap;
    /* Where each name is in `items`, less one, by the hash of the name;
     * 0 for an empty slot.  (Every label of the source is a name here,
     * and each was looked for among all those before it.) */
    size_t *slots;
    size_t slot_cap;
} cond_syms_t;

static void cond_syms_free(cond_syms_t *syms) {
    size_t i;

    for (i = 0; i < syms->count; ++i) {
        free(syms->items[i].name);
    }
    free(syms->items);
    free(syms->slots);
    syms->items = NULL;
    syms->slots = NULL;
    syms->count = 0;
    syms->cap = 0;
    syms->slot_cap = 0;
}

static size_t cond_sym_hash(const char *name, size_t name_len) {
    size_t h = 2166136261u;
    size_t i;

    for (i = 0; i < name_len; ++i) {
        h = (h ^ (unsigned char)name[i]) * 16777619u;
    }
    return h;
}

/* Room in the index for one name more, at no more than half full. */
static int cond_syms_index_grow(cond_syms_t *syms) {
    size_t ncap;
    size_t *next;
    size_t i;

    if ((syms->count + 1) * 2 <= syms->slot_cap) {
        return 0;
    }
    ncap = syms->slot_cap == 0 ? 64 : syms->slot_cap * 2;
    next = (size_t *)calloc(ncap, sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    for (i = 0; i < syms->count; ++i) {
        size_t at = cond_sym_hash(syms->items[i].name, strlen(syms->items[i].name)) & (ncap - 1);

        while (next[at] != 0) {
            at = (at + 1) & (ncap - 1);
        }
        next[at] = i + 1;
    }
    free(syms->slots);
    syms->slots = next;
    syms->slot_cap = ncap;
    return 0;
}

static cond_sym_t *cond_sym_find(const cond_syms_t *syms, const char *name, size_t name_len) {
    size_t at;

    if (syms->slot_cap == 0) {
        return NULL;
    }
    for (at = cond_sym_hash(name, name_len) & (syms->slot_cap - 1); syms->slots[at] != 0;
         at = (at + 1) & (syms->slot_cap - 1)) {
        cond_sym_t *sym = &syms->items[syms->slots[at] - 1];

        if (strncmp(sym->name, name, name_len) == 0 && sym->name[name_len] == '\0') {
            return sym;
        }
    }
    return NULL;
}

/* Is the name defined, here or by --defsym, with a value or without? */
static int cond_sym_defined(const cond_syms_t *syms, const char *name) {
    return cond_sym_find(syms, name, strlen(name)) != NULL || defsym_lookup((void *)syms->ctx, name, NULL);
}

/*
 * A name's value, for the expressions of conditionals: an
 * as_expr_lookup_fn, whose cookie is the cond_syms_t.  The source's own
 * definition stands before --defsym's, which it may have replaced.
 */
static int cond_sym_lookup(void *cookie, const char *name, long long *value_out) {
    const cond_syms_t *syms = (const cond_syms_t *)cookie;
    const cond_sym_t *sym;

    if (syms == NULL || name == NULL) {
        return 0;
    }
    sym = cond_sym_find(syms, name, strlen(name));
    if (sym != NULL) {
        if (sym->has_value && value_out != NULL) {
            *value_out = sym->value;
        }
        return sym->has_value;
    }
    return defsym_lookup((void *)syms->ctx, name, value_out);
}

static int cond_sym_define(cond_syms_t *syms, const char *name, size_t name_len, int has_value, long long value) {
    cond_sym_t *sym = cond_sym_find(syms, name, name_len);

    if (sym == NULL) {
        if (syms->count == syms->cap) {
            size_t ncap = syms->cap == 0 ? 32 : syms->cap * 2;
            cond_sym_t *next = (cond_sym_t *)realloc(syms->items, ncap * sizeof(*next));

            if (next == NULL) {
                return -1;
            }
            syms->items = next;
            syms->cap = ncap;
        }
        sym = &syms->items[syms->count];
        sym->name = (char *)malloc(name_len + 1);
        if (sym->name == NULL) {
            return -1;
        }
        memcpy(sym->name, name, name_len);
        sym->name[name_len] = '\0';
        if (cond_syms_index_grow(syms) != 0) {
            free(sym->name);
            return -1;
        }
        {
            size_t at = cond_sym_hash(name, name_len) & (syms->slot_cap - 1);

            while (syms->slots[at] != 0) {
                at = (at + 1) & (syms->slot_cap - 1);
            }
            syms->slots[at] = syms->count + 1;
        }
        syms->count++;
    }
    sym->has_value = has_value;
    sym->value = value;
    return 0;
}

static int is_symbol_char(int c) {
    return isalnum((unsigned char)c) || c == '_' || c == '.' || c == '$';
}

/*
 * A line that is being assembled: note the symbols it defines.  Its
 * labels, and then `.set NAME, EXPR` (or .equ, or .equiv) or
 * `NAME = EXPR`.  An expression that is not a number yet -- it names a
 * label, say -- leaves the name defined and without a value.
 */
static int cond_syms_note_line(cond_syms_t *syms, const char *line) {
    const char *p = line;
    const char *name;
    const char *expr = NULL;
    size_t name_len;
    char *copy;
    char *hash;
    long long value = 0;
    int has_value;
    int rc;

    for (;;) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        name = p;
        while (is_symbol_char(*p)) {
            p++;
        }
        name_len = (size_t)(p - name);
        if (name_len == 0) {
            return 0;
        }
        if (*p != ':') {
            break;
        }
        if (cond_sym_define(syms, name, name_len, 0, 0) != 0) {
            return -1;
        }
        p++;
    }

    if ((name_len == 4 && (strncmp(name, ".set", 4) == 0 || strncmp(name, ".equ", 4) == 0)) ||
        (name_len == 6 && strncmp(name, ".equiv", 6) == 0)) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        name = p;
        while (is_symbol_char(*p)) {
            p++;
        }
        name_len = (size_t)(p - name);
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (name_len == 0 || *p != ',') {
            return 0;
        }
        expr = p + 1;
    } else {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (name[0] == '.' || p[0] != '=' || p[1] == '=') {
            return 0;
        }
        expr = p + 1;
    }

    copy = xstrdup(expr);
    if (copy == NULL) {
        return -1;
    }
    hash = as_scan_dialect() == AS_DIALECT_X86 ? strchr(copy, '#') : NULL;
    if (hash != NULL) {
        *hash = '\0';
    }
    has_value = as_expr_eval_string(trim_in_place(copy), cond_sym_lookup, syms, &value) == AS_EXPR_EVAL_OK;
    free(copy);
    rc = cond_sym_define(syms, name, name_len, has_value, value);
    return rc;
}


/* Whether the expression of an .if is true: 1 or 0, or -1 if it is not
 * a number. */
static int eval_gas_cond_expr(cond_syms_t *syms, const char *expr) {
    long long ln = 0;

    if (expr == NULL) {
        return 0;
    }
    /* An expression of numbers, and of the names that have been given
     * numbers by now, is as_expr.c's, whole: `.if 1+1 == 2`,
     * `.if (3 > 2) && 1`, `.if DEBUG`, `.if K*2 > 3`. */
    if (as_expr_eval_string(expr, cond_sym_lookup, syms, &ln) == AS_EXPR_EVAL_OK) {
        return ln != 0;
    }
    /*
     * A name in it has no number: a label, or a symbol not defined.  It
     * was then compared as text, side against side, or counted as true
     * for being there at all -- `.if undefined_symbol` assembled its
     * branch.  GNU as refuses it, and text is what .ifc is for.
     */
    return -1;
}

/*
 * The state of the conditionals of a source as it is read: which are
 * open and whether what stands now is assembled.  It is kept by what
 * takes the conditionals out (filter_gas_conditionals), and by what
 * expands the macros before that, which has to know whether a `.macro`
 * it meets is in a branch not taken.
 */
#define COND_DEPTH_MAX 1024

typedef struct {
    gas_cond_frame_t stack[COND_DEPTH_MAX];
    /* Where each open conditional began, and what it was, to be named
     * if it is never closed. */
    unsigned start_line[COND_DEPTH_MAX];
    char start_name[COND_DEPTH_MAX][8];
    size_t depth;
    int active;
    cond_syms_t known;
    char err[160];
} cond_state_t;

static void cond_state_init(cond_state_t *cs, const as_ctx_t *ctx) {
    memset(cs, 0, sizeof(*cs));
    cs->active = 1;
    cs->known.ctx = ctx;
}

/*
 * Take one line.  1 if it is a directive of conditional assembly, which
 * is then done and is no part of what is assembled; 0 if it is any other
 * line, which is assembled if cs->active; -1 for a conditional that is
 * wrong, with why in cs->err.  `at` is the line's number.
 */
static int cond_step(cond_state_t *cs, const char *line, unsigned at) {
    gas_cond_frame_t *stack = cs->stack;
    cond_syms_t *known_syms = &cs->known;
    size_t depth = cs->depth;
    size_t depth_before = cs->depth;
    int active = cs->active;
    int consumed = 0;
    int once = 0;
    int rc = -1;

    cs->err[0] = '\0';
    while (!once++) {
        const char *d = line_start_directive(line);

        if (line_starts_with_directive(d, ".ifdef") || line_starts_with_directive(d, ".ifndef")) {
            gas_cond_frame_t *f;
            int is_ifndef = line_starts_with_directive(d, ".ifndef");
            char *tmp = xstrdup(d + (is_ifndef ? 7 : 6));
            int defined;
            if (tmp == NULL || depth >= COND_DEPTH_MAX) {
                free(tmp);
                goto out;
            }
            defined = cond_sym_defined(known_syms, trim_in_place(tmp));
            free(tmp);
            f = &stack[depth++];
            f->parent_active = active;
            f->branch_taken = f->parent_active && (is_ifndef ? !defined : defined);
            f->active = f->branch_taken;
            f->else_seen = 0;
            active = f->active;
            consumed = 1;
            continue;
        }
        if (line_starts_with_directive(d, ".ifb") || line_starts_with_directive(d, ".ifnb")) {
            gas_cond_frame_t *f;
            int is_ifnb = line_starts_with_directive(d, ".ifnb");
            const char *arg = d + (is_ifnb ? 5 : 4);
            char *tmp = xstrdup(arg);
            char *trimmed;
            int nonblank;
            if (tmp == NULL || depth >= COND_DEPTH_MAX) {
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
            consumed = 1;
            continue;
        }
        if (line_starts_with_directive(d, ".ifc") || line_starts_with_directive(d, ".ifnc") ||
            line_starts_with_directive(d, ".ifeqs") || line_starts_with_directive(d, ".ifnes")) {
            /* Two texts compared: .ifc a, b and .ifnc; and .ifeqs "a",
             * "b" and .ifnes, which are the same of two strings. */
            int is_strings = line_starts_with_directive(d, ".ifeqs") || line_starts_with_directive(d, ".ifnes");
            int is_negated = line_starts_with_directive(d, ".ifnc") || line_starts_with_directive(d, ".ifnes");
            gas_cond_frame_t *f;
            const char *args = d + (is_strings ? 6 : (is_negated ? 5 : 4));
            char *tmp = xstrdup(args);
            char *trimmed;
            char *comma;
            int eq = 0;
            if (tmp == NULL || depth >= COND_DEPTH_MAX) {
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
            consumed = 1;
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
            if (depth >= COND_DEPTH_MAX) {
                goto out;
            }
            /* What is not an expression of numbers counts as 0. */
            if (as_expr_eval_string(args, cond_sym_lookup, known_syms, &v) != AS_EXPR_EVAL_OK) {
                v = 0;
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
            consumed = 1;
            continue;
        }
        if (line_starts_with_directive(d, ".if")) {
            gas_cond_frame_t *f;
            if (depth >= COND_DEPTH_MAX) {
                goto out;
            }
            f = &stack[depth++];
            f->parent_active = active;
            f->branch_taken = 0;
            /* Asked only where it decides something: in a branch not
             * taken a name need have no value. */
            if (f->parent_active) {
                int truth = eval_gas_cond_expr(known_syms, d + 3);

                if (truth < 0) {
                    snprintf(cs->err, sizeof(cs->err), ".if: the expression is not a number");
                    goto out;
                }
                f->branch_taken = truth;
            }
            f->active = f->branch_taken;
            f->else_seen = 0;
            active = f->active;
            consumed = 1;
            continue;
        }
        if (line_starts_with_directive(d, ".elseif")) {
            gas_cond_frame_t *f;
            if (depth == 0) {
                snprintf(cs->err, sizeof(cs->err), ".elseif without an .if before it");
                goto out;
            }
            f = &stack[depth - 1];
            if (f->else_seen) {
                snprintf(cs->err, sizeof(cs->err), ".elseif after the .else of the %s on line %u",
                         cs->start_name[depth - 1], cs->start_line[depth - 1]);
                goto out;
            }
            if (f->branch_taken || !f->parent_active) {
                f->active = 0;
            } else {
                int truth = eval_gas_cond_expr(known_syms, d + 7);

                if (truth < 0) {
                    snprintf(cs->err, sizeof(cs->err), ".elseif: the expression is not a number");
                    goto out;
                }
                f->active = truth;
                if (f->active) {
                    f->branch_taken = 1;
                }
            }
            active = f->active;
            consumed = 1;
            continue;
        }
        if (line_starts_with_directive(d, ".else")) {
            gas_cond_frame_t *f;
            if (depth == 0) {
                snprintf(cs->err, sizeof(cs->err), ".else without an .if before it");
                goto out;
            }
            f = &stack[depth - 1];
            if (f->else_seen) {
                snprintf(cs->err, sizeof(cs->err), "a second .else for the %s on line %u",
                         cs->start_name[depth - 1], cs->start_line[depth - 1]);
                goto out;
            }
            f->active = f->parent_active && !f->branch_taken && !f->else_seen;
            f->branch_taken = f->branch_taken || f->active;
            f->else_seen = 1;
            active = f->active;
            consumed = 1;
            continue;
        }
        if (line_starts_with_directive(d, ".endif")) {
            if (depth == 0) {
                snprintf(cs->err, sizeof(cs->err), ".endif without an .if before it");
                goto out;
            }
            depth--;
            active = depth == 0 ? 1 : stack[depth - 1].active;
            consumed = 1;
            continue;
        }
        if (active && cond_syms_note_line(known_syms, line) != 0) {
            snprintf(cs->err, sizeof(cs->err), "out of memory");
            goto out;
        }
    }
    /* A conditional that this line opened: where, and what it was. */
    if (depth > depth_before) {
        const char *d = line_start_directive(line);
        size_t n = 0;

        while (d[n] != '\0' && !isspace((unsigned char)d[n]) && n + 1 < sizeof(cs->start_name[0])) {
            cs->start_name[depth - 1][n] = d[n];
            n++;
        }
        cs->start_name[depth - 1][n] = '\0';
        cs->start_line[depth - 1] = at;
    }
    rc = consumed;

out:
    if (rc < 0 && cs->err[0] == '\0') {
        snprintf(cs->err, sizeof(cs->err), "a conditional that cannot be read, or too many inside one another");
    }
    cs->depth = depth;
    cs->active = active;
    return rc;
}

struct macro_cond {
    cond_state_t cs;
    size_t seen; /* how much of the text has been followed */
};

static struct macro_cond *macro_cond_new(const as_ctx_t *ctx) {
    struct macro_cond *mc = (struct macro_cond *)malloc(sizeof(*mc));

    if (mc != NULL) {
        cond_state_init(&mc->cs, ctx);
        mc->seen = 0;
    }
    return mc;
}

static void macro_cond_free(struct macro_cond *mc) {
    if (mc != NULL) {
        cond_syms_free(&mc->cs.known);
        free(mc);
    }
}

static int macro_cond_active(struct macro_cond *mc, const as_text_t *written) {
    while (mc->seen < written->len) {
        const char *start = written->data + mc->seen;
        size_t n = 0;
        char *line;

        while (mc->seen + n < written->len && start[n] != '\n') {
            n++;
        }
        mc->seen += n + (mc->seen + n < written->len ? 1 : 0);
        if (n == 0 || start[0] == AS_LEX_MARK) {
            continue;
        }
        line = (char *)malloc(n + 1);
        if (line == NULL) {
            break;
        }
        memcpy(line, start, n);
        line[n] = '\0';
        /* What is wrong with a conditional is said when they are taken
         * out; here it is only followed as far as it can be. */
        (void)cond_step(&mc->cs, line, 0);
        free(line);
    }
    return mc->cs.active;
}

/* How many conditionals what has been written leaves open. */
static size_t macro_cond_depth(struct macro_cond *mc, const as_text_t *written) {
    (void)macro_cond_active(mc, written);
    return mc->cs.depth;
}

static int filter_gas_conditionals(const as_ctx_t *ctx, as_text_t *in, as_text_t *out) {
    cond_state_t *cs;
    char *line = NULL;
    size_t cap = 0;
    int rc = -1;

    if (in == NULL || out == NULL) {
        return -1;
    }
    cs = (cond_state_t *)malloc(sizeof(*cs));
    if (cs == NULL) {
        as_diag(AS_E_INTERNAL, "out of memory");
        return -1;
    }
    cond_state_init(cs, ctx);
    out->origin = in;

    while (text_getline(&line, &cap, in) >= 0) {
        int step = cond_step(cs, line, in->line);

        if (step < 0) {
            as_diag(AS_E_BACKEND, "%s:%u: %s", in->file != NULL ? in->file : "<input>", in->line, cs->err);
            goto out;
        }
        if (step == 0 && cs->active) {
            text_puts(line, out);
            /* A last line without its newline is a line all the same. */
            if (out->mid_line) {
                text_putc('\n', out);
            }
        } else {
            text_putc('\n', out);
        }
    }
    if (cs->depth != 0) {
        as_diag(AS_E_BACKEND, "%s:%u: the %s here has no .endif", in->file != NULL ? in->file : "<input>",
                cs->start_line[cs->depth - 1], cs->start_name[cs->depth - 1]);
        goto out;
    }
    if (out->failed) {
        as_diag(AS_E_INTERNAL, "out of memory");
        goto out;
    }
    rc = 0;

out:
    free(line);
    cond_syms_free(&cs->known);
    free(cs);
    return rc;
}

static void listing_begin(const char *file, as_elf_cfg_t *ecfg);

/* The emitter's way of saying what is wrong with one instruction. */
static void report_emit_error(void *cookie, const char *message) {
    (void)cookie;
    as_diag(AS_E_BACKEND, "%s", message);
}

static int run_native_backend(const as_ctx_t *ctx) {
    as_token_vec_t toks;
    char **thumb_funcs = NULL;
    as_parse_result_t parsed;
    as_symtab_t syms;
    as_section_state_t secs;
    as_data_program_t data;
    as_lexer_cfg_t lcfg;
    as_parser_cfg_t pcfg;
    as_elf_cfg_t ecfg;
    const char **include_dirs = NULL;
    size_t include_dir_count = 0;
    char errbuf[8192];
    int source_in_error = 0;
    as_text_t text_source;
    as_text_t text_expanded;
    as_text_t text_rept;
    as_text_t text_cond;
    as_text_t text_defs;
    as_text_t *source = &text_source;
    as_elf_warnings_t warnings;
    int rc = -1;

    memset(&text_source, 0, sizeof(text_source));
    memset(&text_expanded, 0, sizeof(text_expanded));
    memset(&text_rept, 0, sizeof(text_rept));
    memset(&text_cond, 0, sizeof(text_cond));
    memset(&text_defs, 0, sizeof(text_defs));
    memset(&warnings, 0, sizeof(warnings));
    warnings.suppress = !ctx->warn_enabled;
    memset(&lcfg, 0, sizeof(lcfg));
    memset(&pcfg, 0, sizeof(pcfg));
    memset(&ecfg, 0, sizeof(ecfg));
    as_token_vec_init(&toks);
    as_parse_result_init(&parsed);
    as_symtab_init(&syms);
    as_section_state_init(&secs);
    as_data_program_init(&data);

    /* The source: gathered already, the output of the C preprocessor, or
     * the file as it stands. */
    if (ctx->gathered != NULL) {
        source = ctx->gathered;
    } else if (source_needs_cpp(ctx->in_path)) {
        if (run_cpp_capture(ctx, ctx->in_path, &text_source.data, &text_source.len) != 0 ||
            text_set_file(&text_source.file, ctx->in_path) != 0) {
            as_diag(AS_E_BACKEND, "preprocessor stage failed");
            goto out;
        }
        text_source.cap = text_source.len + 1;
    } else if (text_read_file(&text_source, ctx->in_path) != 0) {
        as_diag(AS_E_USAGE, "%s: %s", ctx->in_path, strerror(errno));
        goto out;
    }

    /*
     * A NUL in the source ends, for everything that reads a line as a
     * string, the line it is in -- and ended the source: what followed
     * it was not assembled, and the exit status was 0.  It is no
     * character of an assembly source, and is refused.
     */
    if (source->data != NULL && memchr(source->data, '\0', source->len) != NULL) {
        const char *nul = (const char *)memchr(source->data, '\0', source->len);
        unsigned long at = 1;
        const char *p;

        for (p = source->data; p < nul; ++p) {
            if (*p == '\n') {
                at++;
            }
        }
        as_diag(AS_E_USAGE, "%s:%lu: a NUL byte in the source", ctx->in_path, at);
        goto out;
    }

    if (expand_gas_source_controls(ctx, source, &text_expanded) != 0) {
        as_diag(AS_E_BACKEND, "%s", expand_err[0] != '\0' ? expand_err : "macro/control expansion stage failed");
        goto out;
    }
    expand_err[0] = '\0';
    if (expand_rept_file(&text_expanded, &text_rept) != 0) {
        as_diag(AS_E_BACKEND, "%s", expand_err[0] != '\0' ? expand_err : "repeat expansion stage failed");
        goto out;
    }
    /* It says what is wrong itself. */
    if (filter_gas_conditionals(ctx, &text_rept, &text_cond) != 0) {
        goto out;
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
    pcfg.arch = ctx->target != 0 ? AS_PARSER_ARCH_ARM : AS_PARSER_ARCH_X86;
    ecfg.target = ctx->target;

    ecfg.machine = ctx->mode == AS_MODE_64 ? EM_X86_64 : EM_386;
    ecfg.is_64 = ctx->mode == AS_MODE_64 ? 1u : 0u;
    secs.elf64 = ctx->mode == AS_MODE_64;
    ecfg.x86_code_bits = ctx->mode == AS_MODE_64 ? 64u : 32u;
    ecfg.use_rela = ctx->mode == AS_MODE_64 ? 1u : 0u;
    ecfg.x86_64_isa_level = (unsigned)x64_isa_level_from_march(ctx->march);
    ecfg.intel_syntax = (unsigned)(ctx->syntax_intel ? 1 : 0);
    ecfg.warnings = &warnings;

    /* For whoever is debugging the expansion: what the tokenizer is
     * given, a mark shown as `#@`.  (AS_KEEP_TEMPS kept the files.) */
    if (getenv("AS_DUMP_EXPANDED") != NULL) {
        size_t k;

        for (k = 0; k < text_cond.len; ++k) {
            if (text_cond.data[k] == AS_LEX_MARK) {
                fputs("#@", stderr);
            } else {
                fputc(text_cond.data[k], stderr);
            }
        }
    }
    if (write_defsym_text(ctx, &text_defs) != 0) {
        as_diag(AS_E_INTERNAL, "failed to write the --defsym definitions");
        goto out;
    }
    {
        AS_PHASE_BEGIN();
        /* The --defsym symbols first: their tokens go before the source's. */
        if (text_defs.len != 0 &&
            as_lex_text("--defsym", text_defs.data, text_defs.len, 0, &lcfg, &toks, errbuf, sizeof(errbuf)) != 0) {
            as_diag(AS_E_BACKEND, "%s", errbuf);
            goto out;
        }
        if (as_lex_text(ctx->in_path, text_cond.data != NULL ? text_cond.data : "", text_cond.len, 1, &lcfg,
                        &toks, errbuf, sizeof(errbuf)) != 0) {
            as_diag(AS_E_BACKEND, "%s", errbuf);
            goto out;
        }
        AS_PHASE_END(ctx, "lex");
    }
    {
        AS_PHASE_BEGIN();
        /* Statements that cannot be read are each reported, a line of
         * the buffer each, and the rest of the source is gone through
         * for what else is wrong with it; nothing is written. */
        if (as_parse_tokens(&toks, &pcfg, &parsed, errbuf, sizeof(errbuf)) != 0) {
            char *line = errbuf;

            while (line != NULL && *line != '\0') {
                char *end = strchr(line, '\n');

                if (end != NULL) {
                    *end = '\0';
                }
                as_diag(AS_E_BACKEND, "%s", line);
                line = end != NULL ? end + 1 : NULL;
            }
            source_in_error = 1;
        }
        AS_PHASE_END(ctx, "parse");
    }
    ecfg.report_error = report_emit_error;
    ecfg.line_own_source = (unsigned)(ctx->gen_debug != 0);
    ecfg.source_name = ctx->in_path;
    if (ctx->emit_listing) {
        listing_begin(ctx->in_path, &ecfg);
    }
    ecfg.check_only = (unsigned)source_in_error;
    if (ctx->target != 0 && as_arm_lower(&parsed, ctx->target == EM_AARCH64, ctx->arm_thumb, &thumb_funcs,
                                         &ecfg.thumb_func_count, errbuf, sizeof(errbuf)) != 0) {
        as_diag(AS_E_BACKEND, "%s", errbuf);
        goto out;
    }
    ecfg.thumb_funcs = (const char *const *)thumb_funcs;
    if (as_directives_lower(&parsed, ecfg.is_64 ? 8u : 4u, errbuf, sizeof(errbuf)) != 0) {
        as_diag(AS_E_BACKEND, "%s", errbuf);
        goto out;
    }
    {
        AS_PHASE_BEGIN();
        if (validate_directives(&parsed, errbuf, sizeof(errbuf)) != 0) {
            as_diag(AS_E_BACKEND, "%s", errbuf);
            goto out;
        }
        AS_PHASE_END(ctx, "validate");
    }
    if (run_message_directives(&parsed, &warnings) != 0) {
        goto out;
    }
    if (as_sections_gather_subsections(&parsed, ecfg.x86_code_bits, errbuf, sizeof(errbuf)) != 0) {
        as_diag(AS_E_BACKEND, "%s", errbuf);
        goto out;
    }
    if (as_elf_mark_far_branches(&parsed, &ecfg) != 0) {
        as_diag(AS_E_BACKEND, "out of memory");
        goto out;
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
        /* A data directive that is wrong is one more thing wrong: the
         * instructions are still looked at. */
        if (as_data_build(&parsed, &data, errbuf, sizeof(errbuf)) != 0) {
            as_diag(AS_E_BACKEND, "%s", errbuf);
            source_in_error = 1;
            ecfg.check_only = 1;
        }
        AS_PHASE_END(ctx, "data");
    }
    if (ctx->output == AS_OUTPUT_BINARY && source_in_error) {
        goto out;
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
            /* Nothing in the buffer: each instruction in error has been
             * reported already. */
            if (errbuf[0] != '\0') {
                as_diag(AS_E_BACKEND, "%s", errbuf);
            }
            goto out;
        }
        AS_PHASE_END(ctx, "emit-elf");
    }
    if (source_in_error) {
        goto out;
    }

    /* The object is written by now: with --fatal-warnings a warning
     * takes it away again. */
    if (ctx->fatal_warnings && warnings.count > 0) {
        as_diag(AS_E_BACKEND, "%u warning%s, treating warnings as errors", warnings.count,
                warnings.count == 1 ? "" : "s");
        unlink(ctx->out_path);
        goto out;
    }

    rc = 0;

out:
    /*
     * A run that fails leaves no object: not a new one, and not the one
     * that was there from a run before, which a build would go on to
     * link as if it were this source's.  (A file that is not an ordinary
     * one -- /dev/null -- is left alone.)
     */
    if (rc != 0 && ctx->out_path != NULL) {
        struct stat sb;

        if (lstat(ctx->out_path, &sb) == 0 && S_ISREG(sb.st_mode)) {
            unlink(ctx->out_path);
        }
    }
    as_elf_warnings_free(&warnings);
    text_free(&text_source);
    text_free(&text_expanded);
    text_free(&text_rept);
    text_free(&text_cond);
    text_free(&text_defs);
    free(include_dirs);
    as_data_program_free(&data);
    as_section_state_free(&secs);
    as_symtab_free(&syms);
    as_parse_result_free(&parsed);
    as_token_vec_free(&toks);
    if (thumb_funcs != NULL) {
        size_t k;

        for (k = 0; k < ecfg.thumb_func_count; ++k) {
            free(thumb_funcs[k]);
        }
        free(thumb_funcs);
    }
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
    /* What was written to something that is no file -- /dev/null, a
     * pipe -- is not there to be read back. */
    {
        struct stat sb;

        if (stat(ctx->out_path, &sb) == 0 && !S_ISREG(sb.st_mode)) {
            return 0;
        }
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

    if (ctx->target != 0) {
        if (elf_machine(check) != ctx->target ||
            elf_class(check) != (ctx->target == EM_AARCH64 ? ELFOBJ_CLASS_64 : ELFOBJ_CLASS_32)) {
            as_diag(AS_E_VALIDATE, "output is not an object of the target");
            elf_close(check);
            return -1;
        }
    } else if (ctx->mode == AS_MODE_64) {
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
    puts("Substrate assembler: the machines it assembles for.");
    puts("  i386 (-32, --32, --target=i386):");
    puts("    the integer instructions, x87, MMX and SSE to SSE4.2, AES, SHA, BMI, AVX, AVX2 and AVX-512,");
    puts("    in 32-bit code and, after .code16, in 16-bit; ELF32, relocations with their addends in place");
    puts("    AT&T syntax, or Intel's after .intel_syntax or with -msyntax=intel");
    puts("  x86-64 (-64, --64, --target=x86_64):");
    puts("    the same in 64-bit code; ELF64, relocations with addends");
    puts("    -march=x86-64, x86-64-v2, x86-64-v3 or x86-64-v4 allows no more than that level has");
    puts("  arm (run as arm-as, or --target=arm):");
    puts("    ARM's integer instructions, VFP and NEON, and after .thumb or with -mthumb those of Thumb-2;");
    puts("    ELF32, relocations with their addends in place");
    puts("  aarch64 (run as aarch64-as, or --target=aarch64):");
    puts("    A64's integer and system instructions, floats and vectors; ELF64, relocations with addends");
    puts("  For arm and aarch64: ldr reg, =value and literal pools (.ltorg, .pool); -mcpu, -mfpu and");
    puts("    -march are taken and allow everything.");
    puts("  Output: -O elf, an object, or -O binary, a flat image of the sections (x86 only).");
}

/*
 * What the emitter told of each statement, for the listing: the line of
 * the source file it was written on, where its bytes begin in its
 * section, and the bytes.  Several statements of one line -- two on the
 * line, or a macro's -- are one entry each, in order.
 */
typedef struct {
    unsigned line;
    uint64_t offset;
    unsigned char *bytes;
    size_t len;
} list_entry_t;

static struct {
    const char *file;
    list_entry_t *items;
    size_t count;
    size_t cap;
} listing;

static void listing_note(void *cookie, const char *file, unsigned line, uint64_t offset, const unsigned char *bytes,
                         size_t len) {
    const char *user_file = file;
    unsigned user_line = line;
    list_entry_t *e;

    (void)cookie;
    if (len == 0 || bytes == NULL) {
        return;
    }
    (void)as_source_position(file, line, &user_file, &user_line);
    /* Lines of an included file are not lines of the one listed. */
    if (listing.file == NULL || user_file == NULL || strcmp(user_file, listing.file) != 0) {
        return;
    }
    if (listing.count == listing.cap) {
        size_t cap = listing.cap != 0 ? listing.cap * 2 : 256;
        list_entry_t *items = (list_entry_t *)realloc(listing.items, cap * sizeof(*items));

        if (items == NULL) {
            return;
        }
        listing.items = items;
        listing.cap = cap;
    }
    e = &listing.items[listing.count];
    e->bytes = (unsigned char *)malloc(len);
    if (e->bytes == NULL) {
        return;
    }
    memcpy(e->bytes, bytes, len);
    e->len = len;
    e->line = user_line;
    e->offset = offset;
    listing.count++;
}

static void listing_free(void) {
    size_t i;

    for (i = 0; i < listing.count; ++i) {
        free(listing.items[i].bytes);
    }
    free(listing.items);
    memset(&listing, 0, sizeof(listing));
}

/* A listing of FILE is wanted: the emitter is to tell of its statements. */
static void listing_begin(const char *file, as_elf_cfg_t *ecfg) {
    listing_free();
    listing.file = file;
    ecfg->list_stmt = listing_note;
}

/*
 * One line of the source in the listing, as GNU as lays it out: the
 * line's number, the offset its bytes begin at, four of them, and the
 * text; then the rest of the bytes four to a line, for as many more
 * lines as AS_LISTING_MORE_LINES.  *AT is the first entry not yet
 * listed, and entries are in the order of the lines.
 */
static int listing_line(FILE *out, unsigned long long line_no, const char *text, size_t *at) {
    unsigned char bytes[4 * (AS_LISTING_MORE_LINES + 1)];
    size_t len = 0;
    uint64_t offset = 0;
    int have = 0;
    size_t i;
    char hex[9];

    while (*at < listing.count && listing.items[*at].line < line_no) {
        (*at)++;
    }
    for (; *at < listing.count && listing.items[*at].line == line_no; ++*at) {
        const list_entry_t *e = &listing.items[*at];
        size_t take = e->len < sizeof(bytes) - len ? e->len : sizeof(bytes) - len;

        if (!have) {
            offset = e->offset;
            have = 1;
        }
        memcpy(bytes + len, e->bytes, take);
        len += take;
    }
    if (!have) {
        return fprintf(out, "%4llu              \t%s", line_no, text) < 0 ? -1 : 0;
    }
    for (i = 0; i < len; i += 4) {
        size_t k;
        size_t n = len - i < 4 ? len - i : 4;

        for (k = 0; k < n; ++k) {
            snprintf(hex + k * 2, 3, "%02X", bytes[i + k]);
        }
        if (i == 0) {
            if (fprintf(out, "%4llu %04llx %-8s \t%s", line_no, (unsigned long long)offset, hex, text) < 0) {
                return -1;
            }
        } else if (fprintf(out, "%4llu      %s%s\n", line_no, hex, n == 4 ? " " : "") < 0) {
            return -1;
        }
    }
    return 0;
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
    size_t listed = 0;
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
        /* A last line with no newline is given one: bytes may follow. */
        if (nread == 0 || line[nread - 1] != '\n') {
            char *with = (char *)realloc(line, (size_t)nread + 2);

            if (with != NULL) {
                line = with;
                cap = (size_t)nread + 2;
                line[nread] = '\n';
                line[nread + 1] = '\0';
            }
        }
        if (listing_line(out, line_no, line, &listed) != 0) {
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

/*
 * The sources named, or the standard input if none is, one after another
 * in one text, each behind a mark that says whose lines follow.  A
 * source that does not end its last line has it ended, so that the next
 * begins on a line of its own.
 */
static int gather_sources(const strvec_t *inputs, as_text_t *out) {
    size_t i;
    size_t count = inputs->count == 0 ? 1 : inputs->count;

    memset(out, 0, sizeof(*out));
    for (i = 0; i < count; ++i) {
        const char *name = inputs->count == 0 ? "-" : inputs->items[i];
        int is_stdin = strcmp(name, "-") == 0;
        const char *shown = is_stdin ? AS_STDIN_NAME : name;
        int fd = is_stdin ? STDIN_FILENO : open(name, O_RDONLY);
        char mark[32];

        if (fd < 0) {
            as_diag(AS_E_USAGE, "failed to open input %s: %s", name, strerror(errno));
            return -1;
        }
        snprintf(mark, sizeof(mark), "%c1 ", AS_LEX_MARK);
        text_put_raw(out, mark, strlen(mark));
        text_put_raw(out, shown, strlen(shown));
        text_put_raw(out, "\n", 1);
        for (;;) {
            ssize_t got;

            if (text_reserve(out, 65536) != 0) {
                break;
            }
            got = read(fd, out->data + out->len, 65536);
            if (got < 0 && errno == EINTR) {
                continue;
            }
            if (got <= 0) {
                if (got < 0) {
                    out->failed = 1;
                }
                break;
            }
            out->len += (size_t)got;
            out->data[out->len] = '\0';
        }
        if (!is_stdin) {
            close(fd);
        }
        if (out->len != 0 && out->data[out->len - 1] != '\n') {
            text_put_raw(out, "\n", 1);
        }
        if (out->failed) {
            as_diag(AS_E_INTERNAL, "failed to read %s", is_stdin ? "the standard input" : name);
            return -1;
        }
    }
    return text_set_file(&out->file, inputs->count == 0 ? AS_STDIN_NAME : inputs->items[0]);
}

/*
 * The assembler, given its arguments with each -Wa taken apart: what is
 * in a -Wa is an option like any other, and is read where they are.
 * `inputs` is the caller's, to free.
 */
static int as_run_with(as_ctx_t *ctx, int argc, char **argv, strvec_t *inputs) {
    static as_text_t gathered;
    int i;
    int query_version = 0;
    unsigned long long t_start_us;
    unsigned long long t_end_us;

    ctx->mode = AS_MODE_AUTO;
    ctx->out_path = "a.out";
    ctx->output = AS_OUTPUT_ELF;
    ctx->self_path = argv[0];
    ctx->warn_enabled = 1;
    ctx->max_input_bytes = limit_from_env("AS_MAX_INPUT_BYTES");
    ctx->max_line_bytes = limit_from_env("AS_MAX_LINE_BYTES");
    ctx->max_token_length = limit_from_env("AS_MAX_TOKEN_LENGTH");
    ctx->max_macro_depth = limit_from_env("AS_MAX_MACRO_DEPTH");
    ctx->max_include_depth = limit_from_env("AS_MAX_INCLUDE_DEPTH");
    g_emit_error_codes = getenv("AS_ERROR_CODES") != NULL ? 1 : 0;
    /*
     * Run by the name of an assembler for another machine it is asked
     * for that machine's code, which it cannot write.  It wrote x86: as
     * aarch64-as, `ret` became the byte C3 in an x86-64 object, and the
     * exit status was 0.
     */
    {
        const char *base = argv[0] != NULL ? strrchr(argv[0], '/') : NULL;

        base = base != NULL ? base + 1 : (argv[0] != NULL ? argv[0] : "");
        if (strncmp(base, "aarch64", 7) == 0) {
            ctx->target = EM_AARCH64;
        } else if (strncmp(base, "arm", 3) == 0) {
            ctx->target = EM_ARM;
        } else if (strncmp(base, "thumb", 5) == 0) {
            as_diag(AS_E_USAGE, "run as %s: Thumb is assembled by arm-as, after `.thumb`", base);
            return 1;
        }
    }
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
        if (strcmp(arg, "--help") == 0) {
            print_help(stdout, argv[0] != NULL ? argv[0] : "as");
            return 0;
        }
        if (strcmp(arg, "--target-help") == 0) {
            ctx->target_help = 1;
            continue;
        }
        if (strncmp(arg, "--target=", 9) == 0 || strcmp(arg, "--target") == 0) {
            const char *name = arg[8] == '=' ? arg + 9 : (i + 1 < argc ? argv[++i] : NULL);

            if (name == NULL) {
                as_diag(AS_E_USAGE, "--target requires an argument");
                return 2;
            }
            if (strncmp(name, "aarch64", 7) == 0 || strncmp(name, "arm64", 5) == 0) {
                ctx->target = EM_AARCH64;
            } else if (strncmp(name, "arm", 3) == 0) {
                ctx->target = EM_ARM;
            } else if (strncmp(name, "x86_64", 6) == 0 || strncmp(name, "x86-64", 6) == 0 ||
                       strncmp(name, "amd64", 5) == 0) {
                ctx->target = 0;
                ctx->mode = AS_MODE_64;
            } else if (strcmp(name, "x86") == 0 || (name[0] == 'i' && name[1] != '\0' && strncmp(name + 2, "86", 2) == 0)) {
                ctx->target = 0;
                ctx->mode = AS_MODE_32;
            } else {
                as_diag(AS_E_USAGE, "--target=%s: the targets are i386, x86_64, arm and aarch64", name);
                return 2;
            }
            continue;
        }
        if (strcmp(arg, "--statistics") == 0) {
            ctx->statistics = 1;
            continue;
        }
        if (strcmp(arg, "--from-cc") == 0) {
            ctx->invoked_from_cc = 1;
            continue;
        }
        if (strcmp(arg, "-c") == 0) {
            continue;
        }
        if (strcmp(arg, "-O") == 0) {
            const char *value;
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            value = argv[++i];
            if (strcmp(value, "elf") == 0) {
                ctx->output = AS_OUTPUT_ELF;
            } else if (strcmp(value, "binary") == 0 || strcmp(value, "bin") == 0) {
                ctx->output = AS_OUTPUT_BINARY;
            } else if (isdigit((unsigned char)value[0])) {
                /* A compiler's level of optimisation, handed on: taken,
                 * and nothing to an assembler. */
            } else {
                as_diag(AS_E_USAGE, "unsupported output format '%s' for -O (expected elf|binary)", value);
                return 2;
            }
            continue;
        }
        if (strncmp(arg, "-O", 2) == 0 && arg[2] != '\0') {
            const char *value = arg + 2;
            if (strcmp(value, "elf") == 0) {
                ctx->output = AS_OUTPUT_ELF;
            } else if (strcmp(value, "binary") == 0 || strcmp(value, "bin") == 0) {
                ctx->output = AS_OUTPUT_BINARY;
            } else if (isdigit((unsigned char)value[0])) {
                /* (as above) */
            } else {
                as_diag(AS_E_USAGE, "unsupported output format '%s' for -O (expected elf|binary)", value);
                return 2;
            }
            continue;
        }
        if (strcmp(arg, "--warn") == 0) {
            ctx->warn_enabled = 1;
            continue;
        }
        /* -W is --no-warn, as it is to GNU as. */
        if (strcmp(arg, "-W") == 0 || strcmp(arg, "--no-warn") == 0) {
            ctx->warn_enabled = 0;
            continue;
        }
        if (strcmp(arg, "--fatal-warnings") == 0) {
            ctx->fatal_warnings = 1;
            continue;
        }
        if (strcmp(arg, "-al") == 0) {
            ctx->emit_listing = 1;
            continue;
        }
        if (strncmp(arg, "-al=", 4) == 0) {
            ctx->emit_listing = 1;
            ctx->listing_path = arg + 4;
            continue;
        }
        if (strcmp(arg, "--defsym") == 0 || strncmp(arg, DEFSYM_OPT, sizeof(DEFSYM_OPT) - 1) == 0) {
            const char *spec = arg[8] == '=' ? arg + 9 : (i + 1 < argc ? argv[++i] : NULL);
            size_t name_len;
            long long value;

            if (spec == NULL || defsym_split(spec, &name_len, &value) != 0) {
                as_diag(AS_E_USAGE, "bad --defsym%s%s: the form is --defsym name=value", spec != NULL ? " " : "",
                        spec != NULL ? spec : "");
                return 2;
            }
            if (push_opt_with_value(&ctx->as_opts, DEFSYM_OPT, spec) != 0) {
                return 1;
            }
            continue;
        }
        if (strcmp(arg, "-msyntax") == 0) {
            const char *value;

            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            value = argv[++i];
            if (strcmp(value, "intel") == 0) {
                ctx->syntax_intel = 1;
            } else if (strcmp(value, "att") == 0) {
                ctx->syntax_intel = 0;
            } else {
                as_diag(AS_E_USAGE, "unsupported -msyntax=%s (expected att|intel)", value);
                return 2;
            }
            continue;
        }
        if (strncmp(arg, "-msyntax=", 9) == 0) {
            const char *value = arg + 9;

            if (strcmp(value, "intel") == 0) {
                ctx->syntax_intel = 1;
            } else if (strcmp(value, "att") == 0) {
                ctx->syntax_intel = 0;
            } else {
                as_diag(AS_E_USAGE, "unsupported -msyntax=%s (expected att|intel)", value);
                return 2;
            }
            continue;
        }
        if (strcmp(arg, "--max-input-bytes") == 0 || strcmp(arg, "--max-line-bytes") == 0 ||
            strcmp(arg, "--max-token-length") == 0 || strcmp(arg, "--max-macro-depth") == 0 ||
            strcmp(arg, "--max-include-depth") == 0 || strcmp(arg, "--max-expansion-bytes") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
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
        } else if (strncmp(arg, "--max-expansion-bytes=", 22) == 0) {
            val = arg + 22;
        }
        if (val != NULL) {
            /* A limit is a positive number.  `-1` was read as the
             * largest there is, and 0 as no limit at all. */
            if (!isdigit((unsigned char)val[0]) || parse_u64(val, &parsed) != 0 || parsed == 0) {
                as_diag(AS_E_USAGE, "%s: a limit is a positive number", arg);
                return 2;
            }
            if (strncmp(arg, "--max-expansion-bytes", 21) == 0) {
                text_limit = parsed;
            } else if (strncmp(arg, "--max-input-bytes", 17) == 0) {
                ctx->max_input_bytes = parsed;
            } else if (strncmp(arg, "--max-line-bytes", 16) == 0) {
                ctx->max_line_bytes = parsed;
            } else if (strncmp(arg, "--max-token-length", 18) == 0) {
                ctx->max_token_length = parsed;
            } else if (strncmp(arg, "--max-macro-depth", 17) == 0) {
                ctx->max_macro_depth = parsed;
            } else {
                ctx->max_include_depth = parsed;
            }
            continue;
        }
        if (strcmp(arg, "-o") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            ctx->out_path = argv[++i];
            continue;
        }
        if (strcmp(arg, "-32") == 0 || strcmp(arg, "--32") == 0) {
            ctx->mode = AS_MODE_32;
            continue;
        }
        if (strcmp(arg, "-64") == 0 || strcmp(arg, "--64") == 0) {
            ctx->mode = AS_MODE_64;
            continue;
        }
        if (strcmp(arg, "-g") == 0) {
            if (strvec_push(&ctx->gcc_opts, arg) != 0) {
                return 1;
            }
            ctx->gen_debug = 1;
            continue;
        }
        if (strcmp(arg, "-I") == 0 || strcmp(arg, "-D") == 0 || strcmp(arg, "-march") == 0 ||
            strcmp(arg, "-mtune") == 0) {
            const char *value;

            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            value = argv[++i];
            if (strcmp(arg, "-march") == 0) {
                ctx->march = value;
                if (push_opt_with_value(&ctx->gcc_opts, "-march=", value) != 0) {
                    return 1;
                }
                continue;
            }
            if (strcmp(arg, "-mtune") == 0) {
                if (push_opt_with_value(&ctx->gcc_opts, "-mtune=", value) != 0) {
                    return 1;
                }
                continue;
            }
            if (push_opt_with_value(&ctx->gcc_opts, strcmp(arg, "-I") == 0 ? "-I" : "-D", value) != 0 ||
                (strcmp(arg, "-I") == 0 && push_opt_with_value(&ctx->as_opts, "-I", value) != 0)) {
                return 1;
            }
            continue;
        }
        /* ARM's and AArch64's own: which processor, of which every
         * instruction this assembler has is allowed already, and which
         * of ARM's two instruction sets the source begins in. */
        if (ctx->target != 0 && (strncmp(arg, "-mcpu=", 6) == 0 || strncmp(arg, "-mfpu=", 6) == 0 ||
                                 strncmp(arg, "-mfloat-abi=", 12) == 0)) {
            continue;
        }
        if (ctx->target == EM_ARM && (strcmp(arg, "-mthumb") == 0 || strcmp(arg, "-marm") == 0)) {
            ctx->arm_thumb = arg[2] == 't';
            continue;
        }
        if (strncmp(arg, "-march=", 7) == 0) {
            ctx->march = arg + 7;
            if (strvec_push(&ctx->gcc_opts, arg) != 0) {
                return 1;
            }
            continue;
        }
        if (strncmp(arg, "-mtune=", 7) == 0) {
            if (strvec_push(&ctx->gcc_opts, arg) != 0) {
                return 1;
            }
            continue;
        }
        if (strncmp(arg, "-I", 2) == 0 || strncmp(arg, "-D", 2) == 0) {
            if (strvec_push(&ctx->gcc_opts, arg) != 0 || (strncmp(arg, "-I", 2) == 0 && strvec_push(&ctx->as_opts, arg) != 0)) {
                return 1;
            }
            continue;
        }

        /*
         * What a compiler's driver passes and this assembler has no use
         * for: taken, and nothing done.  --gdwarf-N asks for line
         * numbers in a form the .loc directives of the source do not
         * depend on; --noexecstack is what an object here is already.
         */
        if (strncmp(arg, "--gdwarf", 8) == 0 || strcmp(arg, "--gen-debug") == 0) {
            ctx->gen_debug = 1;
            continue;
        }
        if (strcmp(arg, "--noexecstack") == 0) {
            continue;
        }

        /* Any other option is one there is not. */
        if (arg[0] == '-' && arg[1] != '\0') {
            as_diag(AS_E_USAGE, "unrecognized option '%s'", arg);
            return 1;
        }

        /* A source; `-` alone is the standard input. */
        if (strvec_push(inputs, arg) != 0) {
            return 1;
        }
    }

    /*
     * One file is read where it is.  The standard input -- named by `-`,
     * or by naming nothing -- and more than one source are first copied,
     * in order, to one file: the stages that follow each open their
     * source by name, and some open it twice.
     */
    /*
     * The output may not be one of the sources: it is made before the
     * source is done with, and the source was gone -- `as -o x.s x.s`
     * left an object where the source had been.  Said before anything
     * is read, as GNU as says it.
     */
    if (!query_version && !ctx->target_help && ctx->out_path != NULL) {
        struct stat out_sb;
        size_t k;

        if (stat(ctx->out_path, &out_sb) == 0 && S_ISREG(out_sb.st_mode)) {
            for (k = 0; k < inputs->count; ++k) {
                struct stat in_sb;

                if (strcmp(inputs->items[k], "-") != 0 && stat(inputs->items[k], &in_sb) == 0 &&
                    in_sb.st_dev == out_sb.st_dev && in_sb.st_ino == out_sb.st_ino) {
                    /* (Not AS_E_USAGE's way out, which takes the
                     * output away: it is the source.) */
                    fprintf(stderr, "as: error: the input '%s' and output '%s' files are the same\n", inputs->items[k],
                            ctx->out_path);
                    ctx->out_path = NULL;
                    return 1;
                }
            }
        }
    }

    if (!query_version && !ctx->target_help) {
        if (inputs->count == 1 && strcmp(inputs->items[0], "-") != 0) {
            ctx->in_path = inputs->items[0];
        } else if (inputs->count == 0 && isatty(STDIN_FILENO)) {
            /* Nothing named and nothing piped: said, not waited for. */
            usage(argv[0]);
            return 2;
        } else if (gather_sources(inputs, &gathered) != 0) {
            return 1;
        } else {
            ctx->gathered = &gathered;
            ctx->in_path = inputs->count == 0 ? AS_STDIN_NAME : inputs->items[0];
        }
    }

    if (query_version) {
        /*
         * Its own name, and no one else's: a configure script that reads
         * "GNU assembler" here takes it for one with every feature of
         * that version, and does not try them.
         */
        printf("as (Substrate assembler)\n");
        printf("Assembles for i386 and x86-64, to ELF or to a flat binary, and for ARM and AArch64, to ELF.\n");
        return 0;
    }

    if (ctx->target_help) {
        print_target_help(ctx);
        return 0;
    }

    if (ctx->in_path == NULL) {
        usage(argv[0]);
        return 2;
    }
    if (ctx->target != 0) {
        /* The object's class is the target's, whatever else was said. */
        ctx->mode = ctx->target == EM_AARCH64 ? AS_MODE_64 : AS_MODE_32;
        as_scan_set_dialect(ctx->target == EM_AARCH64 ? AS_DIALECT_A64 : AS_DIALECT_ARM);
        /* (-march= names one of the target's processors, not of x86's.) */
        ctx->march = NULL;
    }
    if (ctx->mode == AS_MODE_AUTO) {
        ctx->mode = infer_mode(ctx);
    }
    if (validate_march(ctx->mode, ctx->march) != 0) {
        if (validate_march(ctx->mode, ctx->march) == -2) {
            as_diag(AS_E_USAGE, "64bit mode not supported on `%.*s'", (int)strcspn(ctx->march, "+"), ctx->march);
        } else {
            as_diag(AS_E_USAGE, "invalid -march= option: `%s'", ctx->march);
        }
        return 2;
    }
    if (ctx->max_include_depth > 0) {
        char lim[32];
        if (snprintf(lim, sizeof(lim), "%llu", ctx->max_include_depth) >= (int)sizeof(lim) ||
            push_opt_with_value(&ctx->gcc_opts, "-fmax-include-depth=", lim) != 0) {
            return 1;
        }
    }
    if (preflight_source_limits(ctx) != 0) {
        return 1;
    }
    if (run_native_backend(ctx) != 0) {
        return 1;
    }
    if (validate_output_file(ctx) != 0) {
        return 1;
    }
    if (ctx->emit_listing) {
        if (ctx->listing_path == NULL) {
            ctx->owned_listing_path = default_listing_path(ctx);
            ctx->listing_path = ctx->owned_listing_path;
        }
        if (emit_listing_file(ctx) != 0) {
            return 1;
        }
    }
    t_end_us = wallclock_us();
    if (ctx->statistics) {
        print_statistics(t_start_us, t_end_us);
    }
    return 0;
}

/*
 * One run of the assembler.  What a run holds -- the options it
 * gathered, the listing's path if it made one, what the emitter told
 * for the listing -- is let go here, by whichever of the forty ways the
 * run ended: each of them had the same two lines before its return, and
 * some had forgotten one.
 */
static int as_run(int argc, char **argv, strvec_t *inputs) {
    as_ctx_t run;
    int rc;

    memset(&run, 0, sizeof(run));
    rc = as_run_with(&run, argc, argv, inputs);
    strvec_free(&run.gcc_opts);
    strvec_free(&run.as_opts);
    free(run.owned_listing_path);
    listing_free();
    return rc;
}

/*
 * Ended by a signal: the one file the assembler may have made and not
 * finished, the output's temporary, is removed, and then the signal
 * does what it does.  Only what a signal handler may call is called.
 */
static void end_by_signal(int sig) {
    const char *pending = elf_write_pending_path();

    if (pending != NULL) {
        (void)unlink(pending);
    }
    (void)signal(sig, SIG_DFL);
    (void)raise(sig);
}

int main(int argc, char **argv) {
    strvec_t args;
    strvec_t inputs;
    int i;
    int rc;

    /* One that is ignored already -- a job run with nohup -- stays so. */
    for (i = 0; i < 3; ++i) {
        static const int signals[] = {SIGINT, SIGTERM, SIGHUP};

        if (signal(signals[i], end_by_signal) == SIG_IGN) {
            (void)signal(signals[i], SIG_IGN);
        }
    }

    /* `-Wa,a,b` and `-Wa a,b` are the arguments a and b. */
    memset(&args, 0, sizeof(args));
    for (i = 0; i < argc; ++i) {
        const char *list = NULL;
        int failed;

        if (i > 0 && strcmp(argv[i], "-Wa") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                strvec_free(&args);
                return 2;
            }
            list = argv[++i];
        } else if (i > 0 && strncmp(argv[i], "-Wa,", 4) == 0) {
            list = argv[i] + 4;
        }
        failed = list != NULL ? strvec_push_csv(&args, list) : strvec_push(&args, argv[i]);
        if (failed != 0) {
            as_diag(AS_E_USAGE, list != NULL ? "invalid -Wa argument" : "out of memory");
            strvec_free(&args);
            return 2;
        }
    }

    memset(&inputs, 0, sizeof(inputs));
    rc = as_run((int)args.count, args.items, &inputs);
    strvec_free(&inputs);
    strvec_free(&args);
    return rc;
}
