#ifndef SLEX_PARSER_H
#define SLEX_PARSER_H

/* Read the lex source from the argc files in argv ("-" or none: stdin). */
void init_parser(int argc, char **argv);

/* Parse all three sections, filling the symbol table and rule list. */
void parse_input(void);

/* C code from the definitions section, or NULL if there was none. */
char *get_def_code(void);

/* The user-subroutines section, or NULL if there was none. */
char *get_sub_code(void);

#endif
