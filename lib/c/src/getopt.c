/*
 * getopt - POSIX-like getopt implementation
 *
 * Based on public domain implementations.
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>

char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = '?';

static char *nextchar = NULL;

int getopt(int argc, char *const argv[], const char *optstring) {
    char c;
    char *cp;

    if (optind >= argc || !argv[optind])
        return -1;

    if (argv[optind][0] != '-' || argv[optind][1] == '\0')
        return -1;

    if (strcmp(argv[optind], "--") == 0) {
        optind++;
        return -1;
    }

    if (!nextchar || *nextchar == '\0') {
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
                if (nextchar == NULL) { /* End of this argv element */
                   /* Optional arg must be attached, so none here */
                }
                /* If we are at end of argv[optind], increment optind */
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
