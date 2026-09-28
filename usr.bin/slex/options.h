#ifndef LEX_OPTIONS_H
#define LEX_OPTIONS_H

#include <stdbool.h>

struct lex_options {
    bool to_stdout;     // -t
    bool no_stats;      // -n
    bool verbose;       // -v
    
    /* Configuration from Definitions Section */
    bool use_pointer;   // %pointer (default true? or implementation defined. POSIX says impl defined. Flex uses pointer?)
    bool use_array;     // %array

    /* Table Sizes */
    int positions;      // %p
    int states;         // %n
    int transitions;    // %a
    int tree_nodes;     // %e
    int classes;        // %k
    int output_size;    // %o
};

extern struct lex_options opt;

void parse_options(int argc, char **argv);

#endif
