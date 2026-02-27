#ifndef LS_OPTS_H
#define LS_OPTS_H

#include "ls.h"

int ls_parse_opts(int argc, char **argv, ls_config_t *config, char ***files, int *file_count);
void ls_print_usage(const char *prog);

#endif
