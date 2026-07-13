/*
 * test, [ - evaluate a conditional expression (POSIX).
 *
 * The previous stub understood only -f/-d/-e and returned false (1) for
 * every other expression — so `test "$a" = "$b"`, `test 1 -eq 1`,
 * `test -n "$x"`, and every string test silently evaluated false,
 * quietly breaking any script that used them.
 *
 * This is a full POSIX test: file-type/permission unary operators,
 * string operators (-n/-z/=/!=), integer comparisons
 * (-eq/-ne/-lt/-le/-gt/-ge), and the -a/-o/!/( ) combinators, resolved
 * with the standard argc-driven shortcuts for 0..4 arguments and a
 * recursive-descent parser beyond that.
 *
 * Exit status: 0 true, 1 false, 2 usage/syntax error.
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* Parser cursor over the argument vector. */
static char **args;
static int    nargs;
static int    pos;
static int    syntax_err;

static const char *prog = "test";

static void
err2(const char *msg)
{
    fprintf(stderr, "%s: %s\n", prog, msg);
    syntax_err = 1;
}

static char *
peek(void)
{
    return (pos < nargs) ? args[pos] : NULL;
}

static char *
advance(void)
{
    return (pos < nargs) ? args[pos++] : NULL;
}

/* Parse a signed integer operand; sets syntax_err on a malformed value. */
static long
to_int(const char *s)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE) {
        char buf[128];
        snprintf(buf, sizeof buf, "%s: integer expression expected", s);
        err2(buf);
        return 0;
    }
    return v;
}

static int stat_mode(const char *path, mode_t *m, int follow)
{
    struct stat st;
    int r = follow ? stat(path, &st) : lstat(path, &st);
    if (r != 0)
        return -1;
    *m = st.st_mode;
    return 0;
}

/* Evaluate a unary operator `-x arg`.  Returns 1 (true) / 0 (false). */
static int
unary_op(char op, const char *arg)
{
    struct stat st;
    mode_t m;

    switch (op) {
    case 'n': return arg[0] != '\0';
    case 'z': return arg[0] == '\0';
    case 'e': return access(arg, F_OK) == 0;
    case 'r': return access(arg, R_OK) == 0;
    case 'w': return access(arg, W_OK) == 0;
    case 'x': return access(arg, X_OK) == 0;
    case 'f': return stat_mode(arg, &m, 1) == 0 && S_ISREG(m);
    case 'd': return stat_mode(arg, &m, 1) == 0 && S_ISDIR(m);
    case 'c': return stat_mode(arg, &m, 1) == 0 && S_ISCHR(m);
    case 'b': return stat_mode(arg, &m, 1) == 0 && S_ISBLK(m);
    case 'p': return stat_mode(arg, &m, 1) == 0 && S_ISFIFO(m);
    case 'S': return stat_mode(arg, &m, 1) == 0 && S_ISSOCK(m);
    case 'L': /* -L and -h: symlink (no-follow) */
    case 'h': return stat_mode(arg, &m, 0) == 0 && S_ISLNK(m);
    case 's': return stat(arg, &st) == 0 && st.st_size > 0;
    case 'g': return stat_mode(arg, &m, 1) == 0 && (m & S_ISGID);
    case 'u': return stat_mode(arg, &m, 1) == 0 && (m & S_ISUID);
    case 'k': return stat_mode(arg, &m, 1) == 0 && (m & S_ISVTX);
    case 't': {
        long fd = to_int(arg);
        return !syntax_err && isatty((int)fd);
    }
    default:
        err2("unknown unary operator");
        return 0;
    }
}

/* Is `s` one of the recognised unary file/string operators? */
static int
is_unary(const char *s)
{
    if (s == NULL || s[0] != '-' || s[1] == '\0' || s[2] != '\0')
        return 0;
    return strchr("nzerwxfdcbpSLhsgukt", s[1]) != NULL;
}

/* Is `s` a binary operator token? */
static int
is_binary(const char *s)
{
    static const char *ops[] = {
        "=", "!=", "-eq", "-ne", "-lt", "-le", "-gt", "-ge",
        "-nt", "-ot", "-ef", NULL
    };
    for (int i = 0; ops[i]; i++)
        if (strcmp(s, ops[i]) == 0)
            return 1;
    return 0;
}

static int
binary_op(const char *l, const char *op, const char *r)
{
    if (strcmp(op, "=") == 0)  return strcmp(l, r) == 0;
    if (strcmp(op, "!=") == 0) return strcmp(l, r) != 0;

    /* integer comparisons */
    long a = to_int(l), b = to_int(r);
    if (syntax_err)
        return 0;
    if (strcmp(op, "-eq") == 0) return a == b;
    if (strcmp(op, "-ne") == 0) return a != b;
    if (strcmp(op, "-lt") == 0) return a <  b;
    if (strcmp(op, "-le") == 0) return a <= b;
    if (strcmp(op, "-gt") == 0) return a >  b;
    if (strcmp(op, "-ge") == 0) return a >= b;

    err2("unknown binary operator");
    return 0;
}

static int or_expr(void);

/* primary := ( expr ) | ! primary | -op arg | arg op arg | arg */
static int
primary(void)
{
    char *t = peek();
    if (t == NULL) {
        err2("argument expected");
        return 0;
    }

    if (strcmp(t, "(") == 0) {
        advance();
        int v = or_expr();
        char *close = advance();
        if (close == NULL || strcmp(close, ")") != 0)
            err2("')' expected");
        return v;
    }

    if (strcmp(t, "!") == 0) {
        advance();
        return !primary();
    }

    /* Look ahead: a binary operator two tokens along wins over treating
     * the leading token as a unary operator (POSIX: `test -f = -f` is a
     * string comparison of "-f" and "-f"). */
    if (pos + 1 < nargs && is_binary(args[pos + 1])) {
        char *l  = advance();
        char *op = advance();
        char *r  = advance();
        if (r == NULL) {
            err2("argument expected");
            return 0;
        }
        return binary_op(l, op, r);
    }

    if (is_unary(t) && pos + 1 < nargs) {
        advance();               /* the operator */
        char *arg = advance();
        return unary_op(t[1], arg);
    }

    /* Bare string: true iff non-empty. */
    advance();
    return t[0] != '\0';
}

/* and_expr := primary ( -a primary )* */
static int
and_expr(void)
{
    int v = primary();
    while (!syntax_err) {
        char *t = peek();
        if (t == NULL || strcmp(t, "-a") != 0)
            break;
        advance();
        int rhs = primary();
        v = v && rhs;
    }
    return v;
}

/* or_expr := and_expr ( -o and_expr )* */
static int
or_expr(void)
{
    int v = and_expr();
    while (!syntax_err) {
        char *t = peek();
        if (t == NULL || strcmp(t, "-o") != 0)
            break;
        advance();
        int rhs = and_expr();
        v = v || rhs;
    }
    return v;
}

int
main(int argc, char *argv[])
{
    if (argc > 0 && argv[0] != NULL) {
        const char *base = strrchr(argv[0], '/');
        prog = base ? base + 1 : argv[0];
    }

    /* Invoked as `[`: require a trailing `]` and drop it. */
    if (strcmp(prog, "[") == 0) {
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            fprintf(stderr, "[: missing ']'\n");
            return 2;
        }
        argc--;                  /* hide the ']' */
    }

    args  = argv + 1;
    nargs = argc - 1;
    pos   = 0;
    syntax_err = 0;

    /* POSIX argc-driven shortcuts (also give the historically-correct
     * result for operands that look like operators). */
    if (nargs == 0)
        return 1;                                     /* no expr: false */

    int result = or_expr();

    if (syntax_err)
        return 2;
    if (pos != nargs) {
        fprintf(stderr, "%s: extra argument '%s'\n", prog, args[pos]);
        return 2;
    }
    return result ? 0 : 1;
}
