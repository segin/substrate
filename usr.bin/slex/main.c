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
#include "rules.h"
#include "symtab.h"

/*
 * The -v summary.  POSIX puts it on standard output, or on standard error
 * when -t has claimed standard output for the scanner.
 */
static void print_statistics(const struct dfa *d) {
    FILE *f = opt.to_stdout ? stderr : stdout;
    int rules = 0;

    for (struct rule *r = get_rules(); r; r = r->next)
        rules++;
    fprintf(f, "slex: %d rules, %d start conditions, %d DFA states\n",
            rules, get_num_start_conditions(), d->num_states);
}

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

    if (opt.verbose)
        print_statistics(d);

    dfa_free(d);
    return 0;
}
