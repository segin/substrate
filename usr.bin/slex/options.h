#ifndef SLEX_OPTIONS_H
#define SLEX_OPTIONS_H

#include <stdbool.h>

struct lex_options {
    /* Command line */
    bool to_stdout;     /* -t: write the scanner to standard output */
    bool no_stats;      /* -n: suppress the statistics summary (default) */
    bool verbose;       /* -v: write the statistics summary */

    /*
     * yytext declaration from the definitions section: %array makes it a
     * char array, %pointer (the default when neither is given) a char *.
     */
    bool use_pointer;
    bool use_array;

    /*
     * Table-size declarations (%p %n %a %e %k %o).  They are accepted for
     * POSIX compatibility and recorded, but the tables are sized
     * dynamically, so they have no effect.
     */
    int positions;      /* %p */
    int states;         /* %n */
    int transitions;    /* %a */
    int tree_nodes;     /* %e */
    int classes;        /* %k */
    int output_size;    /* %o */
};

extern struct lex_options opt;

/* Parse -t, -n and -v; exits with a usage message on anything else. */
void parse_options(int argc, char **argv);

#endif
