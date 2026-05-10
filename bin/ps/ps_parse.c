/*
 * ps_parse.c — option parsing for ps(1).
 *
 * BSD ps notoriously accepts both the POSIX `-aux` form and the
 * dashless BSD-cluster form `aux`.  We honor that by detecting a
 * dashless argv[1] of the recognized cluster letters and synthesizing
 * a `-` in front before handing the vector to getopt_long(3).
 *
 * Long options layered on top:
 *   --all, --user, --long, --bitness, --environment,
 *   --help, --version
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <pwd.h>

#include "ps_impl.h"

/* Parse a comma-separated PID list ("123,456,789") into out[].
 * Returns the count written, or -1 on syntax error / overflow. */
static int parse_pid_list(const char *s, int *out, size_t cap) {
    size_t n = 0;
    while (*s) {
        char *end = NULL;
        long v = strtol(s, &end, 10);
        if (end == s || v < 0) return -1;
        if (n >= cap) return -1;
        out[n++] = (int)v;
        s = end;
        if (*s == ',') s++;
        else if (*s != '\0') return -1;
    }
    return (int)n;
}

/* Parse a comma-separated user list ("root,1000,jdoe") into uids.
 * Numeric tokens used directly; named tokens looked up via getpwnam.
 * Unknown names are skipped silently (procps behavior). */
static int parse_user_list(const char *s, int *out, size_t cap) {
    size_t n = 0;
    char buf[64];
    while (*s) {
        size_t i = 0;
        while (*s && *s != ',' && i + 1 < sizeof(buf)) buf[i++] = *s++;
        buf[i] = '\0';
        if (*s == ',') s++;
        if (i == 0) continue;

        /* Numeric? */
        char *end = NULL;
        long v = strtol(buf, &end, 10);
        if (end != buf && *end == '\0' && v >= 0) {
            if (n >= cap) return -1;
            out[n++] = (int)v;
            continue;
        }
        /* Named lookup. */
        struct passwd *pw = getpwnam(buf);
        if (pw) {
            if (n >= cap) return -1;
            out[n++] = (int)pw->pw_uid;
        }
        /* Unknown name → skip (don't fail; user may have given a
         * mix of valid and non-existent accounts). */
    }
    return (int)n;
}

/* The BSD cluster letters we accept dashless.  Keep in sync with
 * the optstring below and the man page. */
static int looks_like_bsd_cluster(const char *s) {
    if (!s || s[0] == '\0' || s[0] == '-') return 0;
    for (size_t i = 0; s[i]; i++) {
        if (!strchr("auxleb", s[i])) return 0;
    }
    return 1;
}

int ps_parse_options(int argc, char **argv, ps_options_t *opts, const char **error) {
    memset(opts, 0, sizeof(*opts));
    *error = NULL;

    /* Detect BSD-cluster form (`ps aux`) and rewrite argv[1] in-place
     * to `-aux` so getopt_long can parse it uniformly.  We grow a
     * tiny static scratch buffer per the maximum expected cluster
     * length (a..z = 26 chars + leading dash + NUL). */
    static char bsd_scratch[32];
    if (argc >= 2 && looks_like_bsd_cluster(argv[1])) {
        size_t n = strlen(argv[1]);
        if (n + 2 <= sizeof(bsd_scratch)) {
            bsd_scratch[0] = '-';
            memcpy(bsd_scratch + 1, argv[1], n);
            bsd_scratch[n + 1] = '\0';
            argv[1] = bsd_scratch;
        }
    }

    /* Long options.  Use distinct val codes for long-only flags so
     * the short-option switch can route them; reuse the short letter
     * for short/long alias pairs. */
    enum { OPT_HELP = 0x100, OPT_VERSION, OPT_NO_HEADERS };
    static const struct option longs[] = {
        {"all",         no_argument,       NULL, 'a'},
        {"user",        no_argument,       NULL, 'u'},
        {"long",        no_argument,       NULL, 'l'},
        {"bitness",     no_argument,       NULL, 'b'},
        {"environment", no_argument,       NULL, 'e'},
        {"pid",         required_argument, NULL, 'p'},
        {"User",        required_argument, NULL, 'U'},
        {"no-headers",  no_argument,       NULL, OPT_NO_HEADERS},
        {"help",        no_argument,       NULL, OPT_HELP},
        {"version",     no_argument,       NULL, OPT_VERSION},
        {NULL, 0, NULL, 0},
    };

    /* Reset getopt state so test harnesses that call us repeatedly
     * start each parse from scratch.  Substrate libc and BSD libc
     * use `optreset = 1; optind = 1`; glibc instead resets when
     * optind is set to 0.  Branch on the build target since the
     * Substrate getopt.h is force-included via -I before the host
     * one in NATIVE_BUILD mode, hiding __GLIBC__ from the
     * preprocessor here. */
    extern int optind, opterr;
#if defined(NATIVE_BUILD) || defined(HOST_TEST)
    optind = 0;
#else
    extern int optreset;
    optreset = 1;
    optind = 1;
#endif
    opterr = 0; /* we emit our own diagnostics through `*error` */

    int c;
    while ((c = getopt_long(argc, argv, "auxlebp:U:", longs, NULL)) != -1) {
        switch (c) {
        case 'a': opts->flag_a = true; break;
        case 'u': opts->flag_u = true; break;
        case 'x': opts->flag_x = true; break;
        case 'l': opts->flag_l = true; break;
        case 'e': opts->flag_e = true; break;
        case 'b': opts->flag_b = true; break;
        case 'p': {
            int n = parse_pid_list(optarg, opts->pid_filter, PS_FILTER_MAX);
            if (n < 0) { *error = "bad -p PID list"; return -1; }
            opts->pid_filter_n = (size_t)n;
            break;
        }
        case 'U': {
            int n = parse_user_list(optarg, opts->uid_filter, PS_FILTER_MAX);
            if (n < 0) { *error = "bad -U user list"; return -1; }
            opts->uid_filter_n = (size_t)n;
            break;
        }
        case OPT_NO_HEADERS:
            opts->flag_no_headers = true;
            break;
        case OPT_HELP:
            *error = NULL;
            fprintf(stdout,
                "usage: ps [-auxleb] [-p PID,PID,...] [-U USER,USER,...]\n"
                "          [--no-headers] [--help] [--version]\n");
            exit(0);
        case OPT_VERSION:
            fprintf(stdout, "ps (Substrate) 1.0\n");
            exit(0);
        case '?':
        default:
            *error = "unknown option";
            return -1;
        }
    }

    /* Trailing non-option arguments aren't accepted yet (no -p PID
     * support).  Fail loudly rather than silently ignore them. */
    if (optind < argc) {
        *error = "unexpected non-option argument";
        return -1;
    }

    return 0;
}
