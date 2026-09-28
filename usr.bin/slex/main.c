/*
 * slex - Substrate's lex: compile a lex specification into a C scanner.
 *
 * parser.c reads the three sections into the symbol table (symtab.c) and
 * the rule list (rules.c, patterns compiled to an NFA by regex.c); dfa.c
 * turns the NFA into a minimized DFA; codegen.c writes lex.yy.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "codegen.h"
#include "dfa.h"
#include "options.h"
#include "parser.h"

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

