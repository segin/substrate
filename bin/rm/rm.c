/*
 * bin/rm/rm.c — dispatcher for POSIX.1-2024 + BSD + GNU rm(1).
 *
 * Heavy lifting lives in rm_opts.c (option parsing), rm_safety.c
 * (path-safety checks, "is this /, ., .." refusal), rm_walk.c
 * (recursive openat/unlinkat traversal), and rm_scrub.c (BSD -P
 * 3-pass overwrite).  This file only wires them together.
 */
#include "rm.h"
#include "rm_opts.h"
#include "rm_safety.h"
#include "rm_walk.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_interrupted = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_interrupted = 1;
}

static const char *progname_from_argv0(const char *argv0)
{
    const char *s;
    if (argv0 == NULL || *argv0 == '\0') return "rm";
    s = strrchr(argv0, '/');
    return s ? s + 1 : argv0;
}

static void usage(const struct rm_options *opts, FILE *out)
{
    fprintf(out,
        "usage: %s [-dfiIPrRvx] [--interactive[=WHEN]]\n"
        "       %s [--one-file-system] [--preserve-root[=all]]\n"
        "       %s [--no-preserve-root] FILE...\n",
        opts->progname, opts->progname, opts->progname);
}

static int rm_confirm_once(const struct rm_options *opts, int argc, int first)
{
    int file_count = argc - first;
    bool need = (opts->recursive && file_count > 0) || file_count > 3;
    int ch, first_ch = EOF;

    if (!need) return 0;

    fprintf(stderr,
        "%s: remove %d argument%s%s? ",
        opts->progname,
        file_count,
        file_count == 1 ? "" : "s",
        opts->recursive ? " recursively" : "");
    fflush(stderr);

    while ((ch = getchar()) != EOF && ch != '\n') {
        if (first_ch == EOF) first_ch = ch;
    }
    if (first_ch == 'y' || first_ch == 'Y') return 0;
    return -1;
}

int main(int argc, char *argv[])
{
    struct rm_options opts;
    struct rm_walk_state state;
    struct sigaction sa;
    const char *err = NULL;
    int rc = 0;

    rm_options_init(&opts, progname_from_argv0(argv[0]));
    if (rm_parse_options(&opts, argc, argv, &err) != 0) {
        if (err != NULL) {
            fprintf(stderr, "%s: %s\n", opts.progname, err);
        }
        usage(&opts, stderr);
        return 1;
    }

    if (opts.show_help) {
        usage(&opts, stdout);
        return 0;
    }
    if (opts.show_version) {
        printf("%s\n", RM_VERSION);
        return 0;
    }

    if (opts.operand_start >= argc) {
        if (opts.force) {
            return 0;       /* POSIX: rm -f with no operands is a no-op */
        }
        fprintf(stderr, "%s: missing operand\n", opts.progname);
        usage(&opts, stderr);
        return 1;
    }

    /* -I prompt: once, before processing any operand. */
    if (opts.prompt_mode == RM_PROMPT_ONCE) {
        if (rm_confirm_once(&opts, argc, opts.operand_start) != 0) {
            return 0;       /* user said no — exit 0 silently */
        }
    }

    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    state.opts = &opts;
    state.prompt_input = stdin;
    state.interrupted = &g_interrupted;

    for (int i = opts.operand_start; i < argc; i++) {
        if (g_interrupted) break;

        if (rm_operand_is_dot_or_dotdot(argv[i])) {
            fprintf(stderr,
                "%s: refusing to remove '.' or '..' directory: skipping '%s'\n",
                opts.progname, argv[i]);
            rc = 1;
            continue;
        }
        if (rm_remove_operand(&state, argv[i]) == RM_WALK_FAILED) {
            rc = 1;
        }
    }

    return rc;
}
