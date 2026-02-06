#ifndef LS_PRINT_H
#define LS_PRINT_H

#include "ls.h"

void ls_print_list(file_info_t *files, int count, const ls_config_t *config);
void ls_print_entry(file_info_t *f, const ls_config_t *config);
void ls_print_newline(const ls_config_t *config);

#endif // LS_PRINT_H
