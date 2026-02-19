#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "options.h"

struct lex_options opt = {
    .to_stdout = false,
    .no_stats = true,
    .verbose = false,
    .use_pointer = false, // Implementation defined default: let's say array? Flex defaults to ?
    .use_array = false,   // If neither, we pick one later.
    .positions = 2500,    // POSIX min defaults
    .states = 500,
    .transitions = 2000,
    .tree_nodes = 1000,
    .classes = 1000,
    .output_size = 3000
};

void parse_options(int argc, char **argv) {
    int c;
    
    // Reset defaults if needed, though initialization covers it.
    // If -v is set, we turn on stats. If -n is set, we turn off stats.
    // POSIX says: "-n" suppresses summary. "-v" writes output.
    // "If neither -t, -v, nor -n is specified, existing implementations... may conform... using -n implied."
    
    // We start with no_stats = true. If -v is seen, no_stats = false. If -n is seen, no_stats = true.
    
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
            case '?':
                fprintf(stderr, "usage: lex [-t] [-n] [-v] [file ...]\n");
                exit(1);
            default:
                break;
        }
    }
}
