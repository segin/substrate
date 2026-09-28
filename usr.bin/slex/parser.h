#ifndef LEX_PARSER_H
#define LEX_PARSER_H

#include <stdio.h>

/* Abstract input stream */
struct input_stream {
    int argc;
    char **argv;
    int current_arg;
    FILE *current_fp;
    int line_number;
};

void init_parser(int argc, char **argv);
void parse_input(void);
char *get_def_code(void);
char *get_sub_code(void);

#endif
