#ifndef LS_PRINT_H
#define LS_PRINT_H

#include <stdbool.h>

#include "ls.h"

void ls_print_list(const char *label, file_info_t *files, size_t count,
                   const ls_config_t *config, bool show_total_blocks);

#endif
