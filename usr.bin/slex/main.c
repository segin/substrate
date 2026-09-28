#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "options.h"
#include "parser.h"
#include "dfa.h"
#include "codegen.h"

/* External access to definition code buffer */
extern char *get_def_code(void);

int main(int argc, char **argv) {
    parse_options(argc, argv);
    
    /* Parse input file(s) */
    init_parser(argc - optind, &argv[optind]);
    parse_input();

    /* Build DFA from NFA patterns */
    struct dfa *d = nfa_to_dfa();
    if (!d) {
        fprintf(stderr, "Error: failed to build DFA\n");
        return 1;
    }

    /* Generate scanner */
    generate_scanner(d, get_def_code(), get_sub_code(), opt.to_stdout);

    if (opt.verbose) {
        printf("Statistics: %d DFA states\n", d->num_states);
    }

    dfa_free(d);
    return 0;
}

