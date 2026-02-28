#include <stdio.h>
#include <stdlib.h>
#include <locale.h>

#include "ls.h"
#include "ls_colors.h"
#include "ls_opts.h"
#include "ls_traverse.h"

#ifndef TEST
int main(int argc, char **argv) {
    ls_config_t config;
    char **file_operands = NULL;
    int file_count = 0;
    int rc;

    setvbuf(stdout, NULL, _IOFBF, 64 * 1024);
#ifdef NATIVE_BUILD
    (void)setlocale(LC_ALL, "");
#endif
    ls_colors_init();

    rc = ls_parse_opts(argc, argv, &config, &file_operands, &file_count);
    if (rc < 0) {
        ls_print_usage(argv[0]);
        free(file_operands);
        return LS_EXIT_SERIOUS;
    }
    if (rc > 0) {
        free(file_operands);
        return LS_EXIT_OK;
    }

    rc = ls_run(&config, file_operands, file_count);
    free(file_operands);
    return rc;
}
#endif
