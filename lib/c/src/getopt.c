/*
 * getopt - POSIX-like getopt and GNU-like getopt_long implementation
 */

#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = '?';

static char *nextchar = NULL;

static int _getopt_internal(int argc, char *const argv[], const char *optstring,
                            const struct option *longopts, int *longindex, int long_only) {
    char c;
    char *cp;

    if (optind >= argc || !argv[optind])
        return -1;

    if (nextchar == NULL || *nextchar == '\0') {
        if (argv[optind][0] != '-' || argv[optind][1] == '\0')
            return -1;

        if (strcmp(argv[optind], "--") == 0) {
            optind++;
            return -1;
        }

        /* Check for long opt */
        if (longopts != NULL && (argv[optind][1] == '-' || long_only)) {
            char *current_opt = argv[optind] + (argv[optind][1] == '-' ? 2 : 1);
            int match = -1;
            int ambig = 0;
            char *eq = strchr(current_opt, '=');
            size_t currlen = eq ? (size_t)(eq - current_opt) : strlen(current_opt);

            for (int i = 0; longopts[i].name; i++) {
                if (strncmp(longopts[i].name, current_opt, currlen) == 0) {
                    if (strlen(longopts[i].name) == currlen) {
                        match = i;
                        break;
                    } else {
                        if (match == -1) match = i;
                        else ambig = 1;
                    }
                }
            }

            if (match != -1 && !ambig) {
                if (longindex) *longindex = match;
                
                if (longopts[match].has_arg != no_argument) {
                    if (eq) optarg = eq + 1;
                    else if (longopts[match].has_arg == required_argument) {
                        if (optind + 1 < argc) optarg = argv[++optind];
                        else {
                            if (opterr && optstring[0] != ':')
                                fprintf(stderr, "%s: option '--%s' requires an argument\n", argv[0], longopts[match].name);
                            optind++;
                            return optstring[0] == ':' ? ':' : '?';
                        }
                    } else {
                        optarg = NULL;
                    }
                } else {
                    if (eq) {
                        if (opterr)
                            fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n", argv[0], longopts[match].name);
                        optind++;
                        return '?';
                    }
                    optarg = NULL;
                }

                optind++;
                if (longopts[match].flag != NULL) {
                    *longopts[match].flag = longopts[match].val;
                    return 0;
                }
                return longopts[match].val;
            }

            if (ambig) {
                if (opterr) fprintf(stderr, "%s: option '%s' is ambiguous\n", argv[0], argv[optind]);
                optind++;
                return '?';
            }

            /* If it's a long_only call but parsing failed, and it starts with single '-', fall back to short option processing */
            if (argv[optind][1] == '-') {
                if (opterr) fprintf(stderr, "%s: unrecognized option '--%s'\n", argv[0], current_opt);
                optind++;
                return '?';
            }
        }

        nextchar = argv[optind] + 1;
    }

    c = *nextchar++;
    cp = strchr(optstring, c);

    if (!cp || c == ':') {
        if (opterr && c != ':' && optstring[0] != ':')
            fprintf(stderr, "%s: illegal option -- %c\n", argv[0], c);
        optopt = c;
        if (!nextchar || *nextchar == '\0') {
            optind++;
            nextchar = NULL;
        }
        return '?';
    }

    if (cp[1] == ':') {
        if (cp[2] == ':') {
            /* Optional argument */
            if (*nextchar != '\0') {
                optarg = nextchar;
                nextchar = NULL;
                optind++;
            } else {
                optarg = NULL;
                if (!nextchar || *nextchar == '\0') {
                    optind++;
                    nextchar = NULL;
                }
            }
        } else {
            /* Required argument */
            if (*nextchar != '\0') {
                optarg = nextchar;
                optind++;
            } else if (optind + 1 < argc) {
                optarg = argv[++optind];
                optind++;
            } else {
                if (opterr && optstring[0] != ':')
                    fprintf(stderr, "%s: option requires an argument -- %c\n", argv[0], c);
                optopt = c;
                if (optstring[0] == ':')
                    return ':';
                return '?';
            }
            nextchar = NULL;
        }
    } else {
        if (*nextchar == '\0') {
            optind++;
            nextchar = NULL;
        }
        optarg = NULL;
    }

    return c;
}

int getopt(int argc, char *const argv[], const char *optstring) {
    return _getopt_internal(argc, argv, optstring, NULL, NULL, 0);
}

int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex) {
    return _getopt_internal(argc, argv, optstring, longopts, longindex, 0);
}

int getopt_long_only(int argc, char *const argv[], const char *optstring,
                     const struct option *longopts, int *longindex) {
    return _getopt_internal(argc, argv, optstring, longopts, longindex, 1);
}
