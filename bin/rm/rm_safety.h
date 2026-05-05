#ifndef RM_SAFETY_H
#define RM_SAFETY_H

#include "rm.h"

#include <stdio.h>
#include <sys/stat.h>

bool rm_operand_is_dot_or_dotdot(const char *path);
char *rm_normalize_path(const char *path);
int rm_split_path(const char *path, char **parent_out, char **name_out,
    char **display_out, bool *had_trailing_slash);
const char *rm_file_type_name(mode_t mode);
FILE *rm_open_prompt_stream(void);
int rm_prompt_string(FILE *input, const char *question);
int rm_prompt_removal(FILE *input, bool write_protected,
    const char *type_name, const char *path);
bool rm_target_is_write_protected(const struct stat *target_st,
    const struct stat *parent_st);

#endif