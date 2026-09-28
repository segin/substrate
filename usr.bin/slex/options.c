/*
 * options.c - command-line options.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "options.h"

/* Table sizes start at the POSIX minimums. */
struct lex_options opt = {
    .to_stdout = false,
    .no_stats = true,
    .verbose = false,
    .use_pointer = false,
    .use_array = false,
    .positions = 2500,
    .states = 500,
    .transitions = 2000,
    .tree_nodes = 1000,
    .classes = 1000,
    .output_size = 3000
};

/*
 * POSIX leaves the statistics summary off unless -v is given; -n turns it
 * off again.  The last of -n and -v wins.
 */
void parse_options(int argc, char **argv) {
    int c;

    while ((c = getopt(argc, argv, "tnv")) != -1) {
        switch (c) {
        case 't':
            opt.to_stdout = true;
            break;
        case 'n':
            opt.no_stats = true;
            opt.verbose = false;
            break;
        case 'v':
            opt.verbose = true;
            opt.no_stats = false;
            break;
        default:
            fprintf(stderr, "usage: slex [-t] [-n] [-v] [file ...]\n");
            exit(1);
        }
    }
}
