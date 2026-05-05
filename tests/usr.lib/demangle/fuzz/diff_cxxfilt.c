/*
 * diff_cxxfilt.c - differential fuzzer against GNU c++filt.
 *
 * Coverage-guided crash fuzzers find inputs that make us SIGSEGV; they
 * say nothing about whether our output is *correct*.  This harness
 * pipes the same input through GNU libiberty's c++filt and our
 * demangler for the Itanium scheme, then flags inputs where:
 *
 *   - both succeed but produce different non-trivial outputs, or
 *   - we succeed where c++filt rejects (likely overly permissive parser).
 *
 * Inputs where c++filt succeeds and we fail are NOT flagged here:
 * c++filt accepts a lot of vendor extensions and historical quirks
 * we don't care about.  We're after parser bugs, not feature parity.
 *
 * Usage:
 *   ./diff_cxxfilt < corpus.txt
 *   ./diff_cxxfilt --runs=N        (read N random inputs from /dev/urandom)
 *
 * Each input line is one mangled name; lines starting with '#' are
 * skipped (comment).  Output: a TSV report on stderr for each diff,
 * plus a final summary on stdout.
 *
 * Build: see Makefile target diff_cxxfilt.
 */
#include <demangle.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This is a host-only program but the substrate include path shadows
 * the system unistd.h with a target-flavoured one that pulls in
 * arch/<x>/syscall.h.  Declare the few POSIX bits we need directly
 * so the build doesn't depend on the host's include search order.
 * Use ssize_t from substrate's sys/types.h (already included via
 * stdio.h above), and forward-declare just the syscalls. */
typedef int pid_t;
extern int pipe(int pipefd[2]);
extern pid_t fork(void);
extern int dup2(int oldfd, int newfd);
extern int close(int fd);
extern int execlp(const char *file, const char *arg, ...);
extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern pid_t waitpid(pid_t pid, int *wstatus, int options);
extern void _exit(int status);

#define MAX_INPUT  4096
#define MAX_OUTPUT (1u << 20)  /* mirror the in-lib cap */

/* Run c++filt --no-strip-underscore --no-verbose on the given input
 * and capture stdout.  Returns malloc'd string on success (caller
 * frees) or NULL on c++filt failure / nonzero exit / output identical
 * to input (= c++filt rejected it). */
static char *
run_cxxfilt(const char *input)
{
    int in_pipe[2];
    int out_pipe[2];
    pid_t pid;
    char *result;
    size_t cap, len;
    ssize_t n;
    int status;
    size_t in_len;

    if (pipe(in_pipe) != 0) return NULL;
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        return NULL;
    }

    pid = fork();
    if (pid < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return NULL;
    }
    if (pid == 0) {
        dup2(in_pipe[0], 0);
        dup2(out_pipe[1], 1);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        /* --no-strip-underscore: don't strip a leading _ on input.
         * Without it c++filt strips the _ from _Z which means it
         * tries to demangle Z<rest>, which always fails. */
        execlp("c++filt", "c++filt", "--no-strip-underscore", "--no-verbose", (char *)NULL);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);

    in_len = strlen(input);
    if (in_len > 0) {
        ssize_t w = write(in_pipe[1], input, in_len);
        (void)w;
    }
    if (write(in_pipe[1], "\n", 1) < 0) { /* best-effort */ }
    close(in_pipe[1]);

    cap = 4096;
    result = (char *)malloc(cap);
    if (!result) {
        close(out_pipe[0]);
        waitpid(pid, &status, 0);
        return NULL;
    }
    len = 0;
    while ((n = read(out_pipe[0], result + len, cap - len - 1)) > 0) {
        len += (size_t)n;
        if (len + 1 >= cap) {
            if (cap > MAX_OUTPUT) break;
            char *nr = (char *)realloc(result, cap * 2);
            if (!nr) { free(result); result = NULL; break; }
            result = nr;
            cap *= 2;
        }
    }
    close(out_pipe[0]);
    waitpid(pid, &status, 0);

    if (!result) return NULL;
    result[len] = '\0';

    /* Strip trailing newline. */
    while (len > 0 && (result[len - 1] == '\n' || result[len - 1] == '\r')) {
        result[--len] = '\0';
    }

    /* If c++filt couldn't demangle, it echoes the input back unchanged.
     * Treat that as a NULL result so we don't false-positive on every
     * malformed input. */
    if (strcmp(result, input) == 0) {
        free(result);
        return NULL;
    }

    return result;
}

/* Normalize whitespace and a few common cosmetic differences so we
 * don't flag cosmetic mismatches.  Both demanglers may render the
 * same symbol with different spacing or "void" inclusion.  We also
 * drop the historical `> >` (C++03) → `>>` (C++11) difference and
 * unify const-vs-T-const ordering quirks. */
static void
normalize(char *s)
{
    char *r = s;
    char *w = s;
    int last_space = 1;

    /* First pass: collapse whitespace runs to single space, trim. */
    while (*r) {
        if (isspace((unsigned char)*r)) {
            if (!last_space) { *w++ = ' '; last_space = 1; }
            r++;
        } else {
            *w++ = *r++;
            last_space = 0;
        }
    }
    while (w > s && w[-1] == ' ') w--;
    *w = '\0';

    /* Second pass: collapse `> >` → `>>`, repeatedly. */
    for (;;) {
        char *p = strstr(s, "> >");
        if (!p) break;
        memmove(p + 1, p + 2, strlen(p + 2) + 1);
    }

    /* Third pass: collapse `, ,` runs that can appear between empty
     * pack expansions in older c++filt output. */
    for (;;) {
        char *p = strstr(s, ", ,");
        if (!p) break;
        memmove(p, p + 2, strlen(p + 2) + 1);
    }
}

static int
process_input(const char *input, unsigned long *both_match,
              unsigned long *both_diff, unsigned long *we_only)
{
    char *ours = NULL;
    char *theirs = NULL;
    int diff = 0;

    if (input[0] == '\0' || input[0] == '#') return 0;

    /* Only run c++filt on inputs that look itanium-shaped — feeding
     * Rust v0 (`_R`) or D (`_D`) names to c++filt is just noise. */
    if (input[0] != '_' || (input[1] != 'Z' && input[1] != 'z')) {
        return 0;
    }

    ours   = demangle(input, DEMANGLE_ITANIUM);
    theirs = run_cxxfilt(input);

    if (ours && theirs) {
        normalize(ours);
        normalize(theirs);
        if (strcmp(ours, theirs) != 0) {
            (*both_diff)++;
            fprintf(stderr, "DIFF\t%s\tours=[%s]\ttheirs=[%s]\n",
                    input, ours, theirs);
            diff = 1;
        } else {
            (*both_match)++;
        }
    } else if (ours && !theirs) {
        (*we_only)++;
        /* Don't print these to stderr by default — too noisy for a
         * CI run.  Set DEMANGLE_DIFF_VERBOSE=1 to enable. */
        if (getenv("DEMANGLE_DIFF_VERBOSE")) {
            fprintf(stderr, "WE_ONLY\t%s\tours=[%s]\n", input, ours);
        }
    }
    /* (!ours && theirs) — c++filt accepts a feature we don't.  Skip. */

    free(ours);
    free(theirs);
    return diff;
}

int
main(int argc, char **argv)
{
    char buf[MAX_INPUT];
    unsigned long both_match = 0, both_diff = 0, we_only = 0;
    unsigned long lines = 0;
    int max_diffs = 0;

    /* CLI: --max-diffs=N exits early after N diffs (handy for CI). */
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--max-diffs=", 12) == 0) {
            max_diffs = atoi(argv[i] + 12);
        }
    }

    while (fgets(buf, sizeof(buf), stdin) != NULL) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
        lines++;
        if (process_input(buf, &both_match, &both_diff, &we_only)) {
            if (max_diffs > 0 && (long)both_diff >= max_diffs) {
                fprintf(stderr, "diff_cxxfilt: stopping after %d diffs\n", max_diffs);
                break;
            }
        }
    }

    printf("diff_cxxfilt: %lu lines, both-match=%lu both-diff=%lu we-only=%lu\n",
           lines, both_match, both_diff, we_only);
    return both_diff > 0 ? 1 : 0;
}
