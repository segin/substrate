#include <stdio.h>
#include <stdlib.h>
#include "ls.h"
#include "ls_opts.h"
#include "ls_traverse.h"

#ifndef TEST
int main(int argc, char **argv) {
    ls_config_t config;
    char **file_operands = NULL;
    int file_count = 0;

    if (ls_parse_opts(argc, argv, &config, &file_operands, &file_count) != 0) {
        ls_print_usage(argv[0]);
        free(file_operands);
        return 1;
    }

    if (file_count == 0) {
        ls_list_dir(".", &config);
    } else {
        for (int i = 0; i < file_count; i++) {
            ls_list_dir(file_operands[i], &config);
        }
    }

    free(file_operands);
    return 0;
}
#endif
